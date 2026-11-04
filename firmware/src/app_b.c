// MCU-B: the USB device towards PC 2. Everything it sends comes from MCU-A over the isolated UART;
// the drive is the microSD card on MCU-A, reached block by block over the same link.
#include <string.h>
#include "board.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "led.h"
#include "link_io.h"
#include "pc_port.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// ------------------------------------------------------------------ drive over the link
static bool owner;                 // PC 2 sees the drive
static uint32_t capacity;          // sectors, 0 = unknown
static uint32_t tag;
static struct {
    bool busy, done;
    uint8_t op;
    uint32_t lba, tag, sent;
    int8_t status;
    uint8_t data[512];
} req;

static void request(uint8_t op, uint32_t lba, const uint8_t *data) {
    uint8_t m[11 + 512];
    req.busy = true;
    req.done = false;
    req.op = op;
    req.lba = lba;
    req.tag = ++tag;
    req.sent = now_ms();
    memcpy(m, &req.tag, 4);
    m[4] = op;
    memcpy(m + 5, &lba, 4);
    m[9] = 1; m[10] = 0;
    size_t n = 11;
    if (data) { memcpy(m + 11, data, 512); n += 512; }
    link_send(LM_MSC_REQ, m, n);
}

static void on_msc_resp(const uint8_t *p, size_t n) {
    if (n < 5) return;
    uint32_t t;
    memcpy(&t, p, 4);
    if (t == 0xFFFFFFFFu) { port_media_changed(); return; }   // MCU-A wrote to the card
    if (!req.busy || t != req.tag) return;
    req.status = (int8_t)p[4];
    if (req.op == 0 && req.status == 0 && n >= 5 + 512) memcpy(req.data, p + 5, 512);
    if (req.op == 2 && req.status == 0 && n >= 9) memcpy(&capacity, p + 5, 4);
    req.done = true;
}

// common pattern for the TinyUSB callbacks: start a request, report "busy" until the answer is there
static int32_t finish(uint8_t op, uint32_t lba) {
    if (!req.busy || req.op != op || req.lba != lba) return -2;   // not ours
    if (!req.done) {
        if (now_ms() - req.sent > 1500) { req.busy = false; return -1; }
        return 0;
    }
    req.busy = false;
    return req.status == 0 ? 1 : -1;
}

static bool d_ready(void) { return owner && capacity && link_alive(); }
static uint32_t d_sectors(void) { return capacity; }
static int32_t d_read(uint32_t lba, uint32_t off, void *buf, uint32_t n) {
    (void)off;
    int32_t r = finish(0, lba);
    if (r == -2) {
        if (req.busy && !req.done && now_ms() - req.sent < 1500) return 0;
        request(0, lba, NULL);
        return 0;
    }
    if (r <= 0) return r;
    memcpy(buf, req.data, 512);
    return n < 512 ? (int32_t)n : 512;
}
static int32_t d_write(uint32_t lba, uint32_t off, const uint8_t *buf, uint32_t n) {
    (void)off;
    int32_t r = finish(1, lba);
    if (r == -2) {
        if (req.busy && !req.done && now_ms() - req.sent < 1500) return 0;
        request(1, lba, buf);
        return 0;
    }
    if (r <= 0) return r;
    return n < 512 ? (int32_t)n : 512;
}
static const port_disk_t link_disk = {d_ready, d_sectors, d_read, d_write, NULL};

// ------------------------------------------------------------------ main
static void send_status(void) {
    uint8_t m[14] = {0};
    m[0] = (uint8_t)((port_mounted() ? 1 : 0) | (port_suspended() ? 2 : 0));
    m[1] = port_kbd_leds();
    strncpy((char *)m + 2, KMS_VERSION, 12);
    link_send(LM_STATUS, m, sizeof m);
}

int main(void) {
    set_sys_clock_khz(120000, true);
    led_init();
    led_set(LED_BLINK_SLOW, LED_IDLE);   // until MCU-A talks to us
    link_init();
    port_init();
    port_set_disk(&link_disk);
    tud_init(BOARD_TUD_RHPORT);
    uint32_t t_status = 0, t_cap = 0;
    uint8_t last_flags = 0xFF;
    bool was_alive = false;
    while (true) {
        tud_task();
        port_task();
        uint8_t type;
        const uint8_t *p;
        size_t n;
        while (link_poll(&type, &p, &n)) {
            switch (type) {
            case LM_KBD: if (n >= 8) port_kbd(p); break;
            case LM_MOUSE_ABS:
                if (n >= 7) port_mouse_abs(p[0], (uint16_t)(p[1] | p[2] << 8), (uint16_t)(p[3] | p[4] << 8), (int8_t)p[5], (int8_t)p[6]);
                break;
            case LM_MOUSE_REL:
                if (n >= 7) port_mouse_rel(p[0], (int16_t)(p[1] | p[2] << 8), (int16_t)(p[3] | p[4] << 8), (int8_t)p[5], (int8_t)p[6]);
                break;
            case LM_RELEASE_ALL: port_release_all(); break;
            case LM_LED: if (n >= 4) led_set(p[0], p[1], p[2], p[3]); break;
            case LM_SETTINGS:
                if (n >= 1) {
                    bool o = p[0] & 2;
                    if (o != owner) { owner = o; port_media_changed(); }
                    if (o && !capacity) request(2, 0, NULL);
                }
                break;
            case LM_MSC_RESP: on_msc_resp(p, n); break;
            case LM_REBOOT:
                if (n >= 1 && p[0] == 1) reset_usb_boot(0, 0);
                watchdog_reboot(0, 0, 10);
                break;
            case LM_PING: link_send(LM_PONG, p, n); break;
            default: break;
            }
        }
        uint32_t t = now_ms();
        bool alive = link_alive();
        if (was_alive && !alive) {   // MCU-A gone (PC 1 side unpowered): nothing to send, show it
            port_release_all();
            led_set(LED_BLINK_SLOW, LED_IDLE);
            capacity = 0;
        }
        was_alive = alive;
        uint8_t flags = (uint8_t)((port_mounted() ? 1 : 0) | (port_suspended() ? 2 : 0));
        if (t - t_status > 200 || flags != last_flags) {
            t_status = t;
            last_flags = flags;
            send_status();
        }
        // capacity request may be lost if MCU-A was busy: retry
        if (owner && !capacity && !req.busy && t - t_cap > 1000) { t_cap = t; request(2, 0, NULL); }
        if (req.busy && req.op == 2 && req.done) req.busy = false;
        if (req.busy && !req.done && t - req.sent > 1500) req.busy = false;
        led_task();
    }
}
