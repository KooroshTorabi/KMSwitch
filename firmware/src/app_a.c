// MCU-A: USB host for the box's keyboard / mouse, the router, the settings and the microSD card.
// Core 1 runs the PIO-USB host stack; core 0 runs everything else.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "core/kms_config.h"
#include "core/kms_hidparse.h"
#include "core/kms_hotkey.h"
#include "core/kms_layout.h"
#include "core/kms_typing.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "led.h"
#include "link_io.h"
#include "pc_port.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pio_usb.h"
#include "sd_spi.h"
#include "storage.h"
#include "tusb.h"

// ================================================================== core 1: USB host
enum { EV_KBD = 1, EV_MOUSE, EV_GONE };
typedef struct {
    uint8_t kind, dev, idx, absolute;
    kms_kbd_report_t kbd;
    kms_mouse_in_t ms;
    int32_t xmax, ymax;
} host_ev_t;

static queue_t evq;
static volatile uint8_t want_kbd_leds;
static struct hid_itf {
    uint8_t proto;
    bool mounted, use_desc;
    kms_mouse_desc_t desc;
} itf[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];

void tuh_hid_mount_cb(uint8_t dev, uint8_t idx, uint8_t const *desc, uint16_t len) {
    if (dev > CFG_TUH_DEVICE_MAX || idx >= CFG_TUH_HID) return;
    struct hid_itf *h = &itf[dev][idx];
    memset(h, 0, sizeof *h);
    h->mounted = true;
    h->proto = tuh_hid_interface_protocol(dev, idx);
    if (h->proto != HID_ITF_PROTOCOL_KEYBOARD && kms_hid_parse_mouse(desc, len, &h->desc)) {
        h->use_desc = true;
        if (h->proto == HID_ITF_PROTOCOL_MOUSE) {
            // report protocol: wheel, tilt and full resolution instead of the 3-byte boot format
            tuh_hid_set_protocol(dev, idx, HID_PROTOCOL_REPORT);
            return;   // receiving starts in tuh_hid_set_protocol_complete_cb
        }
    }
    tuh_hid_receive_report(dev, idx);
}

void tuh_hid_set_protocol_complete_cb(uint8_t dev, uint8_t idx, uint8_t protocol) {
    if (dev <= CFG_TUH_DEVICE_MAX && idx < CFG_TUH_HID && protocol != HID_PROTOCOL_REPORT)
        itf[dev][idx].use_desc = false;   // the device stayed in boot protocol
    tuh_hid_receive_report(dev, idx);
}

void tuh_hid_umount_cb(uint8_t dev, uint8_t idx) {
    if (dev > CFG_TUH_DEVICE_MAX || idx >= CFG_TUH_HID) return;
    itf[dev][idx].mounted = false;
    host_ev_t e = {.kind = EV_GONE, .dev = dev, .idx = idx};
    queue_try_add(&evq, &e);
}

void tuh_hid_report_received_cb(uint8_t dev, uint8_t idx, uint8_t const *rep, uint16_t len) {
    if (dev <= CFG_TUH_DEVICE_MAX && idx < CFG_TUH_HID) {
        struct hid_itf *h = &itf[dev][idx];
        host_ev_t e = {.dev = dev, .idx = idx};
        if (h->proto == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
            e.kind = EV_KBD;
            memcpy(&e.kbd, rep, 8);
        } else if (h->use_desc && kms_hid_read_mouse(&h->desc, rep, len, &e.ms)) {
            e.kind = EV_MOUSE;
            e.absolute = h->desc.absolute;
            e.xmax = h->desc.x_max;
            e.ymax = h->desc.y_max;
        } else if (h->proto == HID_ITF_PROTOCOL_MOUSE && !h->use_desc && len >= 3) {
            e.kind = EV_MOUSE;   // boot mouse: buttons, x, y, [wheel]
            e.ms.buttons = rep[0];
            e.ms.x = (int8_t)rep[1];
            e.ms.y = (int8_t)rep[2];
            e.ms.wheel = len >= 4 ? (int8_t)rep[3] : 0;
        }
        if (e.kind && !queue_try_add(&evq, &e) && e.kind == EV_MOUSE) {
            // queue full: drop motion, never a button change (the next report carries the buttons again)
        }
    }
    tuh_hid_receive_report(dev, idx);
}

static void core1_main(void) {
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = PIN_USBH_DP;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    tuh_init(BOARD_TUH_RHPORT);
    uint8_t sent_leds = 0xFF;
    while (true) {
        tuh_task();
        uint8_t w = want_kbd_leds;
        if (w != sent_leds) {
            sent_leds = w;
            static uint8_t buf;
            buf = w;
            for (uint8_t d = 1; d <= CFG_TUH_DEVICE_MAX; d++)
                for (uint8_t i = 0; i < CFG_TUH_HID; i++)
                    if (itf[d][i].mounted && itf[d][i].proto == HID_ITF_PROTOCOL_KEYBOARD)
                        tuh_hid_set_report(d, i, 0, HID_REPORT_TYPE_OUTPUT, &buf, 1);
        }
    }
}

// ================================================================== core 0: router
static kms_config_t cfg;
static kms_layout_t L;
static kms_pointer_t ptr;
static int active;
static bool relative;
static float last_x[2], last_y[2];
static bool have_last[2];
static kms_hk_state_t hks;
static uint8_t mask_mods;             // modifiers held while switching: hidden from the new PC until released
static uint8_t mouse_buttons;
static uint32_t last_mouse_ms;
static uint32_t save_at;              // write KMSWITCH.TXT at this time (speed hotkeys), 0 = nothing to save
static uint32_t bootsel_since;        // Ctrl+Alt+Shift+F8 held since
static char cfg_text[STORAGE_CFG_MAX];

static struct { bool connected, suspended; uint8_t leds; char fw[13]; } pcb;   // MCU-B / PC 2 state

static struct { uint8_t dev, idx; bool used; kms_kbd_report_t r; } kslot[4];
static kms_kbd_report_t merged;

// hotkeys: the 7 configurable ones, then the reserved ones
enum { HK_RES_BOOTSEL = HK_COUNT, HK_RES_CONFIRM, HK_TOTAL };
static kms_hotkey_t hk[HK_TOTAL];

// typing (open the settings page)
static kms_keystroke_t typing[160];
static size_t typing_n, typing_i;
static uint32_t typing_next;
static bool typing_down;
static int typing_pc;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static bool pc_connected(int pc) { return pc == 0 ? port_mounted() : pcb.connected && link_alive(); }

static void send_kbd(int pc, const kms_kbd_report_t *r) {
    if (pc == 0) port_kbd((const uint8_t *)r);
    else link_send(LM_KBD, r, 8);
}
static void send_abs(int pc, uint8_t b, uint16_t x, uint16_t y, int8_t w, int8_t p) {
    if (pc == 0) {
        port_mouse_abs(b, x, y, w, p);
    } else {
        uint8_t m[7] = {b, (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)(y >> 8), (uint8_t)w, (uint8_t)p};
        link_send(LM_MOUSE_ABS, m, 7);
    }
}
static void send_rel(int pc, uint8_t b, int16_t dx, int16_t dy, int8_t w, int8_t p) {
    if (pc == 0) {
        port_mouse_rel(b, dx, dy, w, p);
    } else {
        uint8_t m[7] = {b, (uint8_t)dx, (uint8_t)((uint16_t)dx >> 8), (uint8_t)dy, (uint8_t)((uint16_t)dy >> 8),
                        (uint8_t)w, (uint8_t)p};
        link_send(LM_MOUSE_REL, m, 7);
    }
}
static void release_all(int pc) {
    if (pc == 0) port_release_all();
    else link_send(LM_RELEASE_ALL, NULL, 0);
}
static void send_pointer(uint8_t wheel, uint8_t pan) {
    uint16_t ax, ay;
    kms_pointer_abs(&ptr, &L, &ax, &ay);
    send_abs(active, mouse_buttons, ax, ay, (int8_t)wheel, (int8_t)pan);
}

// ------------------------------------------------------------------ LEDs and MCU-B settings
static void update_leds(void) {
    // MCU-A's LED shows PC 1, MCU-B's LED shows PC 2
    if (active == 0) led_set(LED_STEADY, LED_ACTIVE); else led_set(LED_STEADY, LED_IDLE);
    uint8_t m[4] = {LED_STEADY, 0, 0, 0};
    if (active == 1) { m[1] = 0; m[2] = 90; m[3] = 255; } else { m[3] = 24; }
    link_send(LM_LED, m, 4);
}
static void send_settings(void) {
    uint8_t f = (uint8_t)((cfg.usb_network ? 1 : 0) | (active == 1 ? 2 : 0));
    link_send(LM_SETTINGS, &f, 1);
}

// ------------------------------------------------------------------ switching
static void mask_key(int pc) {
    // Win or Alt pressed and released alone opens the Start menu / the menu bar: tap Ctrl in between
    if (merged.mods & 0xCC) {
        kms_kbd_report_t r = {0};
        r.mods = (uint8_t)(merged.mods | 0x01);
        send_kbd(pc, &r);
        r.mods = merged.mods;
        send_kbd(pc, &r);
    }
}

static void became_active(int from) {
    release_all(from);
    mask_mods = merged.mods;
    storage_set_owner(active);
    send_settings();
    update_leds();
    watchdog_hw->scratch[0] = 0x4B4D0000u | (uint32_t)active;   // for start_on = last (survives a reset)
    want_kbd_leds = active == 0 ? port_kbd_leds() : pcb.leds;
}

static void switch_to(int to) {
    if (to == active) return;
    if (!pc_connected(to)) {
        led_set(LED_FLASH_TWICE, LED_RED);
        return;
    }
    int from = active;
    mask_key(from);
    last_x[from] = ptr.x; last_y[from] = ptr.y; have_last[from] = true;
    active = to;
    if (have_last[to]) { ptr.pc = to; ptr.x = last_x[to]; ptr.y = last_y[to]; kms_pointer_clamp(&ptr, &L); }
    else kms_pointer_init(&ptr, &L, to);
    became_active(from);
    if (!relative) send_pointer(0, 0);
}

// ------------------------------------------------------------------ settings
static void apply_config(void) {
    kms_layout_build(&L, &cfg);
    for (int i = 0; i < HK_COUNT; i++) hk[i] = cfg.hk[i];
    kms_parse_hotkey("CTRL+ALT+SHIFT+F8", &hk[HK_RES_BOOTSEL]);
    kms_parse_hotkey("WIN+F8", &hk[HK_RES_CONFIRM]);
    ptr.pc = active;
    kms_pointer_clamp(&ptr, &L);
    send_settings();
}

static void load_config(void) {
    size_t n = storage_read_config(cfg_text, sizeof cfg_text);
    kms_problems_t p;
    kms_config_parse(&cfg, cfg_text, n, &p);
    if (!n && sd_ready()) {
        // no settings file yet: write the default one so the user finds it on the drive
        size_t k = kms_config_write(&cfg, cfg_text, sizeof cfg_text);
        if (k) storage_write_config(cfg_text, k);
    }
    apply_config();
}

static void save_config_later(void) { save_at = now_ms() + 2000; }

static void save_config_now(void) {
    size_t k = kms_config_write(&cfg, cfg_text, sizeof cfg_text);
    if (k) storage_write_config(cfg_text, k);
}

static void change_speed(float f) {
    kms_pc_cfg_t *p = &cfg.pc[active];
    float sx = f ? p->speed_x * f : 1.0f, sy = f ? p->speed_y * f : 1.0f;
    sx = sx < 0.25f ? 0.25f : sx > 4.0f ? 4.0f : sx;
    sy = sy < 0.25f ? 0.25f : sy > 4.0f ? 4.0f : sy;
    p->speed_x = (float)((int)(sx * 100 + 0.5f)) / 100.0f;
    p->speed_y = (float)((int)(sy * 100 + 0.5f)) / 100.0f;
    save_config_later();
}

// ------------------------------------------------------------------ typing into the active PC
static void start_typing(const char *url) {
    const kms_pc_cfg_t *p = &cfg.pc[active];
    typing_n = kms_open_url(p->os, p->kbd_layout, url, typing, sizeof typing / sizeof typing[0]);
    typing_i = 0;
    typing_down = false;
    typing_pc = active;
    typing_next = now_ms() + 300;   // let the user release the hotkey first
}

static void typing_task(void) {
    if (typing_i >= typing_n || (int32_t)(now_ms() - typing_next) < 0) return;
    if (merged.mods && !typing_down && typing_i == 0) { typing_next = now_ms() + 50; return; }
    kms_kbd_report_t r = {0};
    const kms_keystroke_t *k = &typing[typing_i];
    if (!typing_down) {
        r.mods = k->mods;
        r.keys[0] = k->key;
        send_kbd(typing_pc, &r);
        typing_down = true;
        typing_next = now_ms() + 12;
    } else {
        send_kbd(typing_pc, &r);
        typing_down = false;
        typing_next = now_ms() + 12 + k->wait_ms;
        typing_i++;
    }
}

// ------------------------------------------------------------------ input
static void on_hotkeys(uint32_t fired) {
    if (fired & (1u << HK_SWITCH)) switch_to(!active);
    if (fired & (1u << HK_RELATIVE)) {
        relative = !relative;
        led_set(LED_FLASH_TWICE, relative ? 255 : 0, relative ? 160 : 90, relative ? 0 : 255);
        if (!relative) send_pointer(0, 0);
    }
    if (fired & (1u << HK_SPEED_UP)) change_speed(1.1f);
    if (fired & (1u << HK_SPEED_DOWN)) change_speed(1.0f / 1.1f);
    if (fired & (1u << HK_SPEED_RESET)) change_speed(0);
    if ((fired & (1u << HK_OPEN_CONFIG)) && cfg.config_ui && cfg.usb_network) start_typing("http://192.168.7.1");
    if (fired & (1u << HK_MEASURE)) led_set(LED_FLASH_TWICE, LED_RED);   // measurement needs the USB network (next phase)
    if (fired & (1u << HK_RES_BOOTSEL)) bootsel_since = now_ms() | 1;
}

static void on_kbd(const host_ev_t *e) {
    int s = -1, free_s = -1;
    for (int i = 0; i < 4; i++) {
        if (kslot[i].used && kslot[i].dev == e->dev && kslot[i].idx == e->idx) s = i;
        if (!kslot[i].used && free_s < 0) free_s = i;
    }
    if (s < 0) s = free_s;
    if (s < 0) return;
    kslot[s].used = true; kslot[s].dev = e->dev; kslot[s].idx = e->idx; kslot[s].r = e->kbd;
    // merge all keyboards (a dongle may bring several)
    memset(&merged, 0, sizeof merged);
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!kslot[i].used) continue;
        merged.mods |= kslot[i].r.mods;
        for (int k = 0; k < 6 && n < 6; k++)
            if (kslot[i].r.keys[k] > 3 && !kms_report_has(&merged, kslot[i].r.keys[k])) merged.keys[n++] = kslot[i].r.keys[k];
    }
    kms_kbd_report_t out;
    uint32_t fired = kms_hk_process(&hks, hk, HK_TOTAL, &merged, &out);
    mask_mods &= merged.mods;
    out.mods &= (uint8_t)~mask_mods;
    if (fired) mask_key(active);
    if (typing_i < typing_n) {
        if (kms_report_has(&merged, 0x29)) typing_n = 0;   // Esc stops typing
    } else {
        send_kbd(active, &out);
    }
    if (fired) on_hotkeys(fired);
}

static void on_mouse(const host_ev_t *e) {
    uint32_t t = now_ms(), dt = t - last_mouse_ms;
    last_mouse_ms = t;
    bool buttons_changed = e->ms.buttons != mouse_buttons;
    mouse_buttons = e->ms.buttons;
    int8_t wheel = (int8_t)(e->ms.wheel < -127 ? -127 : e->ms.wheel > 127 ? 127 : e->ms.wheel);
    int8_t pan = (int8_t)(e->ms.pan < -127 ? -127 : e->ms.pan > 127 ? 127 : e->ms.pan);
    if (e->absolute) {   // a tablet: map straight onto the active PC
        uint16_t ax = (uint16_t)(e->xmax > 0 ? (int64_t)e->ms.x * 32767 / e->xmax : 0);
        uint16_t ay = (uint16_t)(e->ymax > 0 ? (int64_t)e->ms.y * 32767 / e->ymax : 0);
        send_abs(active, mouse_buttons, ax, ay, wheel, pan);
        return;
    }
    if (relative) {
        send_rel(active, mouse_buttons, (int16_t)e->ms.x, (int16_t)e->ms.y, wheel, pan);
        return;
    }
    float px, py;
    const kms_pc_cfg_t *p = &cfg.pc[active];
    kms_scale_motion(e->ms.x, e->ms.y, dt, p->speed_x, p->speed_y, p->accel, &px, &py);
    kms_move_opts_t o = {cfg.edge_switch && !mouse_buttons && pc_connected(!active) && typing_i >= typing_n,
                         cfg.edge_delay_ms, cfg.edge_corner_block, relative};
    int from = active;
    if (kms_pointer_move(&ptr, &L, px, py, t, &o)) {
        mask_key(from);
        last_x[from] = ptr.x; last_y[from] = ptr.y;   // (overwritten below; keeps the crossing point)
        active = ptr.pc;
        became_active(from);
    }
    if (px || py || wheel || pan || buttons_changed) send_pointer((uint8_t)wheel, (uint8_t)pan);
}

// ------------------------------------------------------------------ link from MCU-B
static void link_task(void) {
    uint8_t type;
    const uint8_t *p;
    size_t n;
    while (link_poll(&type, &p, &n)) {
        switch (type) {
        case LM_STATUS:
            if (n >= 2) {
                bool was = pcb.connected;
                pcb.connected = p[0] & 1;
                pcb.suspended = p[0] & 2;
                pcb.leds = p[1];
                if (n >= 14) { memcpy(pcb.fw, p + 2, 12); pcb.fw[12] = 0; }
                if (active == 1) want_kbd_leds = pcb.leds;
                if (!was && pcb.connected) { send_settings(); update_leds(); }
                if (was && !pcb.connected && active == 1 && port_mounted()) switch_to(0);
            }
            break;
        case LM_MSC_REQ: storage_link_request(p, n); break;
        case LM_PING: link_send(LM_PONG, p, n); break;
        default: break;
        }
    }
    static uint32_t ping;
    if (now_ms() - ping > 500) {
        ping = now_ms();
        link_send(LM_PING, &ping, 4);
    }
}

// ------------------------------------------------------------------ main
int main(void) {
    set_sys_clock_khz(120000, true);   // PIO-USB needs a multiple of 12 MHz
    queue_init(&evq, sizeof(host_ev_t), 64);
    led_init();
    led_set(LED_BLINK_SLOW, LED_IDLE);
    link_init();
    storage_init();
    port_init();
    port_set_disk(storage_local_disk());
    tud_init(BOARD_TUD_RHPORT);
    multicore_launch_core1(core1_main);

    kms_config_defaults(&cfg);
    load_config();
    bool was_last = (watchdog_hw->scratch[0] & 0xFFFF0000u) == 0x4B4D0000u;
    active = cfg.start_on == START_PC2 ? 1 : cfg.start_on == START_LAST && was_last ? (int)(watchdog_hw->scratch[0] & 1) : 0;
    kms_pointer_init(&ptr, &L, active);
    storage_set_owner(active);
    update_leds();

    uint32_t t_conn = 0;
    while (true) {
        tud_task();
        port_task();
        host_ev_t e;
        while (queue_try_remove(&evq, &e)) {
            if (e.kind == EV_KBD) on_kbd(&e);
            else if (e.kind == EV_MOUSE) on_mouse(&e);
            else if (e.kind == EV_GONE) {
                for (int i = 0; i < 4; i++)
                    if (kslot[i].used && kslot[i].dev == e.dev && kslot[i].idx == e.idx) kslot[i].used = false;
            }
        }
        link_task();
        storage_task();
        typing_task();
        led_task();
        uint32_t t = now_ms();
        if (storage_config_reload_due()) load_config();
        if (save_at && (int32_t)(t - save_at) >= 0) { save_at = 0; save_config_now(); }
        if (bootsel_since) {
            if (!(merged.mods & 0x07) || !kms_report_has(&merged, 0x41)) bootsel_since = 0;
            else if (t - bootsel_since >= 3000) {
                bootsel_since = 0;
                if (active == 0) reset_usb_boot(0, 0);
                else link_send(LM_REBOOT, (uint8_t[]){1}, 1);
            }
        }
        // if the active PC goes away, move to the other one when it is there
        if (t - t_conn > 250) {
            t_conn = t;
            if (!pc_connected(active) && pc_connected(!active)) switch_to(!active);
            uint8_t l = active == 0 ? port_kbd_leds() : pcb.leds;
            if (l != want_kbd_leds) want_kbd_leds = l;
        }
    }
}
