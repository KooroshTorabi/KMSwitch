// MCU-B: the USB device towards PC 2. Everything it sends comes from MCU-A over the isolated UART;
// the drive is the microSD card on MCU-A, reached block by block over the same link.
#include <stdio.h>
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
#include "net/http.h"
#include "net/net.h"
#include "usb_desc.h"
#include "bootctl.h"

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

// ------------------------------------------------------------------ settings page for PC 2
// The page itself is served from MCU-B's flash; API requests go to MCU-A over the link.
extern const uint8_t page_gz[];
extern const size_t page_gz_len;
static struct { int id; bool busy, got_head; uint32_t sent; http_resp_t r; uint8_t buf[HTTP_BODY_MAX]; size_t n; } px;

static bool http_handler(const http_req_t *r, http_resp_t *out) {
    if (strncmp(r->path, "/api/", 5)) {
        *out = (http_resp_t){200, "text/html; charset=utf-8", page_gz, page_gz_len, true, true};
        return true;
    }
    if (!link_alive() || px.busy) {
        http_json(out, 503, "{\"error\":\"busy\"}");
        return true;
    }
    px.busy = true; px.got_head = false; px.id = r->id; px.n = 0; px.sent = now_ms();
    uint8_t m[LINK_MAX_PAYLOAD];
    uint32_t id = (uint32_t)r->id;
    memcpy(m, &id, 4);
    size_t k = 5 + (size_t)snprintf((char *)m + 5, LINK_MAX_PAYLOAD - 5, "%s %s\n", r->method, r->path);
    size_t off = 0;
    do {
        size_t c = r->len - off;
        if (c > LINK_MAX_PAYLOAD - k) c = LINK_MAX_PAYLOAD - k;
        memcpy(m + k, r->body + off, c);
        off += c;
        m[4] = off >= r->len ? 1 : 0;
        link_send(LM_HTTP_REQ, m, k + c);
        k = 5;
    } while (off < r->len);
    return false;   // answered when MCU-A replies
}

static void on_http_resp(const uint8_t *p, size_t n) {
    uint32_t id;
    if (n < 5 || !px.busy) return;
    memcpy(&id, p, 4);
    if ((int)id != px.id) return;
    size_t k = 5;
    if (p[4] & 2) {
        if (n < 8) return;
        static const char *ct[] = {"application/json", "text/plain; charset=utf-8", "text/html; charset=utf-8"};
        px.r.status = p[5] | p[6] << 8;
        px.r.ctype = ct[(p[7] & 0x7F) < 3 ? (p[7] & 0x7F) : 1];
        px.r.gzip = p[7] & 0x80;
        px.got_head = true;
        k = 8;
    }
    size_t c = n - k;
    if (px.n + c > sizeof px.buf) c = sizeof px.buf - px.n;
    memcpy(px.buf + px.n, p + k, c);
    px.n += c;
    if ((p[4] & 1) && px.got_head) {
        px.r.body = px.buf;
        px.r.len = px.n;
        px.r.body_static = false;
        http_respond(px.id, &px.r);
        px.busy = false;
    }
}

// ------------------------------------------------------------------ firmware part from MCU-A
// chunks of 512 bytes, each acknowledged; written sector by sector into the slot that is not running
static struct { uint8_t sec[4096]; uint32_t sec_off; bool dirty, active; } fw;

static void fw_flush(void) {
    if (!fw.dirty) return;
    bootctl_flash_write(bootctl_slot_offset(!bootctl_running_slot()) + fw.sec_off, fw.sec, sizeof fw.sec);
    fw.dirty = false;
}

static void on_fw_chunk(const uint8_t *p, size_t n) {
    uint32_t off;
    if (n < 4) return;
    memcpy(&off, p, 4);
    if (off == 0xFFFFFFFFu) {
        uint8_t ack[5] = {0xFF, 0xFF, 0xFF, 0xFF, 1};
        if (n >= 40) {
            uint32_t len;
            memcpy(&len, p + 4, 4);
            fw_flush();
            int slot = !bootctl_running_slot();
            if (len <= FW_SLOT_SIZE && bootctl_verify(slot, len, p + 8) && bootctl_set_pending(slot, len, p + 8)) ack[4] = 0;
        }
        link_send(LM_FW_ACK, ack, 5);
        if (!ack[4]) {
            sleep_ms(100);
            watchdog_reboot(0, 0, 10);
        }
        fw.active = false;
        return;
    }
    if (n < 4 + 512 || off % 512 || off >= FW_SLOT_SIZE) return;
    if (!fw.active || off == 0) { fw.active = true; fw.dirty = false; fw.sec_off = 0; memset(fw.sec, 0xFF, sizeof fw.sec); }
    uint32_t s = off & ~4095u;
    if (s != fw.sec_off) {   // next sector: write the previous one
        fw_flush();
        fw.sec_off = s;
        memset(fw.sec, 0xFF, sizeof fw.sec);
    }
    memcpy(fw.sec + (off - s), p + 4, 512);
    fw.dirty = true;
    if (off - s == 4096 - 512) fw_flush();   // sector complete
    link_send(LM_FW_ACK, &off, 4);
    led_set(LED_BLINK_FAST, LED_ORANGE);
}

static bool stream_handler(int id, const char *path, size_t total, size_t off, const uint8_t *d, size_t n, bool last,
                           http_resp_t *out) {
    if (strncmp(path, "/api/firmware", 13) || !link_alive()) {
        http_json(out, 400, "{\"error\":\"busy\"}");
        return false;
    }
    if (off == 0) { px.busy = true; px.got_head = false; px.id = id; px.n = 0; }
    px.sent = now_ms();
    // forward to MCU-A in link-sized pieces
    uint8_t m[LINK_MAX_PAYLOAD];
    for (size_t k = 0; k < n || (k == 0 && n == 0);) {
        size_t c = n - k > LINK_MAX_PAYLOAD - 13 ? LINK_MAX_PAYLOAD - 13 : n - k;
        uint32_t i = (uint32_t)id, t = (uint32_t)total, o = (uint32_t)(off + k);
        memcpy(m, &i, 4); memcpy(m + 4, &t, 4); memcpy(m + 8, &o, 4);
        m[12] = last && k + c == n;
        memcpy(m + 13, d + k, c);
        link_send(LM_FW_UPLOAD, m, 13 + c);
        k += c;
        if (!c) break;
    }
    return true;   // the answer comes from MCU-A
}

// ------------------------------------------------------------------ main
static void send_status(void) {
    uint8_t m[14] = {0};
    m[0] = (uint8_t)((port_mounted() ? 1 : 0) | (port_suspended() ? 2 : 0) | (bootctl_running_slot() ? 4 : 0));
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
    net_init();
    http_init(http_handler, stream_handler);
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
                    usb_set_network(p[0] & 1);
                    bool o = p[0] & 2;
                    if (o != owner) { owner = o; port_media_changed(); }
                    if (o && !capacity) request(2, 0, NULL);
                }
                break;
            case LM_MSC_RESP: on_msc_resp(p, n); break;
            case LM_HTTP_RESP: on_http_resp(p, n); break;
            case LM_FW_CHUNK: on_fw_chunk(p, n); break;
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
        if (px.busy && t - px.sent > 15000) {   // MCU-A checks an uploaded bundle for a few seconds
            http_resp_t r;
            http_json(&r, 503, "{\"error\":\"busy\"}");
            http_respond(px.id, &r);
            px.busy = false;
        }
        net_task();
        led_task();
        static bool healthy;
        if (!healthy && (port_mounted() || alive)) { healthy = true; bootctl_confirm(); }
        watchdog_update();
    }
}
