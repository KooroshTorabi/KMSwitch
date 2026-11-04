// MCU-A: the settings page protocol (docs/web-api.md) and the automatic screen measurement.
// Requests come from PC 1's network (lwIP on MCU-A) or from PC 2's (forwarded by MCU-B over the link).
#include "web_api.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "core/kms_typing.h"
#include "led.h"
#include "pico/time.h"

extern const uint8_t page_gz[];
extern const size_t page_gz_len;

#define SESSION_MS 8000

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// ------------------------------------------------------------------ tiny JSON readers (flat, trusted shapes)
static const char *jkey(const char *s, const char *key) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}
static bool jnum(const char *s, const char *key, double *v) {
    const char *p = jkey(s, key);
    if (!p) return false;
    char *e;
    *v = strtod(p, &e);
    return e != p;
}
static bool jtrue(const char *s, const char *key) {
    const char *p = jkey(s, key);
    return p && !strncmp(p, "true", 4);
}

// ------------------------------------------------------------------ measurement
static struct {
    int state;             // 0 idle, 1 opening the page, 2 session, 3 closing
    int pc, return_to;
    int queue[2], nq;
    uint32_t deadline;
    const char *last;      // "done" / "failed" of the PC just measured
    uint32_t auto_at[2];   // automatic measurement scheduled for a newly connected PC
} M;

static void m_next(void);

static void m_finish_pc(bool ok) {
    if (!ok) led_set(LED_FLASH_TWICE, LED_RED);
    M.last = ok ? "done" : "failed";
    m_next();
}

static void m_next(void) {
    while (M.nq) {
        int pc = M.queue[0];
        M.queue[0] = M.queue[1];
        M.nq--;
        if (!app_pc_connected(pc)) continue;
        M.pc = pc;
        app_switch_to(pc);
        app_hold_input(true);
        app_measuring_leds(true);
        app_type_url(pc, "http://192.168.7.1/measure");
        M.state = 1;
        M.deadline = now_ms() + SESSION_MS + 6000;   // typing and opening the browser take a few seconds
        return;
    }
    if (M.state) {
        M.state = 0;
        app_measuring_leds(false);
        app_hold_input(false);
        app_switch_to(M.return_to);
    }
}

void web_measure(bool pc1, bool pc2) {
    const kms_config_t *c = app_cfg();
    if (M.state || !c->usb_network) return;
    M.nq = 0;
    M.return_to = app_active();
    // the active PC last, so the box ends where the user is
    int first = !M.return_to;
    if ((first == 0 ? pc1 : pc2)) M.queue[M.nq++] = first;
    if ((first == 0 ? pc2 : pc1)) M.queue[M.nq++] = !first;
    m_next();
}

void web_measure_cancel(void) {
    if (!M.state) return;
    M.nq = 0;
    M.last = "failed";
    m_next();
}

bool web_measuring(void) { return M.state != 0; }

void web_pc_connected(int pc) {
    const kms_config_t *c = app_cfg();
    if (c->auto_measure && c->usb_network && c->pc[pc].nmon == 0) M.auto_at[pc] = now_ms() + 6000;   // DHCP first
}

void web_task(void) {
    uint32_t t = now_ms();
    for (int pc = 0; pc < 2; pc++)
        if (M.auto_at[pc] && (int32_t)(t - M.auto_at[pc]) >= 0) {
            M.auto_at[pc] = 0;
            if (!M.state && app_pc_connected(pc) && app_cfg()->pc[pc].nmon == 0) web_measure(pc == 0, pc == 1);
        }
    if (M.state && (int32_t)(t - M.deadline) >= 0) {
        if (M.state == 3) m_next();
        else m_finish_pc(false);
    }
}

// ------------------------------------------------------------------ status / config
static char jbuf[2048];
static char tbuf[7000];

static void json_escape(char *out, size_t cap, const char *s) {
    size_t n = 0;
    for (; *s && n + 7 < cap; s++) {
        if (*s == '"' || *s == '\\') { out[n++] = '\\'; out[n++] = *s; }
        else if ((unsigned char)*s < 0x20) n += (size_t)snprintf(out + n, cap - n, "\\u%04x", *s);
        else out[n++] = *s;
    }
    out[n] = 0;
}

static void status_json(int you) {
    const kms_config_t *c = app_cfg();
    char mon[2][200], fwb[16];
    for (int i = 0; i < 2; i++) kms_format_monitors(c->pc[i].mon, c->pc[i].nmon, mon[i], sizeof mon[i]);
    json_escape(fwb, sizeof fwb, app_fw_b());
    char meas[96] = "null";
    if (M.state) snprintf(meas, sizeof meas, "{\"pc\":\"pc%d\",\"state\":\"%s\"}", M.pc + 1,
                          M.state == 1 ? "opening" : M.state == 2 ? "probing" : "done");
    snprintf(jbuf, sizeof jbuf,
             "{\"fw\":\"%s\",\"fw_b\":\"%s\",\"board\":\"%c\",\"you\":\"pc%d\",\"active\":\"pc%d\","
             "\"pcs\":{\"pc1\":{\"connected\":%s,\"measured\":%s,\"monitors\":\"%s\"},"
             "\"pc2\":{\"connected\":%s,\"measured\":%s,\"monitors\":\"%s\"}},\"measuring\":%s,\"update\":%s}",
             KMS_VERSION, fwb, KMS_BOARD, you + 1, app_active() + 1,
             app_pc_connected(0) ? "true" : "false", c->pc[0].nmon ? "\"yes\"" : "null", mon[0],
             app_pc_connected(1) ? "true" : "false", c->pc[1].nmon ? "\"yes\"" : "null", mon[1], meas,
             web_update_json());
}

static bool put_config(const uint8_t *body, size_t len, http_resp_t *out) {
    static kms_config_t n;
    kms_problems_t p;
    kms_config_parse(&n, (const char *)body, len, &p);
    const kms_config_t *c = app_cfg();
    // a page opened before a measurement finished must not erase it
    for (int i = 0; i < 2; i++)
        if (!n.pc[i].nmon && c->pc[i].nmon) {
            memcpy(n.pc[i].mon, c->pc[i].mon, sizeof n.pc[i].mon);
            n.pc[i].nmon = c->pc[i].nmon;
        }
    app_set_config(&n);
    size_t k = (size_t)snprintf(jbuf, sizeof jbuf, "{\"ok\":true,\"problems\":[");
    for (int i = 0; i < p.n && k < sizeof jbuf - 48; i++)
        k += (size_t)snprintf(jbuf + k, sizeof jbuf - k, "%s\"%s\"", i ? "," : "", p.key[i]);
    snprintf(jbuf + k, sizeof jbuf - k, "]}");
    http_json(out, 200, jbuf);
    return true;
}

// monitors from /api/m/result: [{"w":..,"h":..,"x":..,"y":..,"scale":..}, ...]
static int parse_monitors(const char *s, kms_monitor_t *m, int max) {
    const char *p = jkey(s, "monitors");
    if (!p || *p != '[') return 0;
    int n = 0;
    const char *end = strchr(p, ']');
    while (n < max && (p = strchr(p, '{')) && (!end || p < end)) {
        const char *q = strchr(p, '}');
        if (!q) break;
        char obj[160];
        size_t l = (size_t)(q - p + 1) < sizeof obj ? (size_t)(q - p + 1) : sizeof obj - 1;
        memcpy(obj, p, l);
        obj[l] = 0;
        double w, h, x, y, sc = 1;
        if (jnum(obj, "w", &w) && jnum(obj, "h", &h) && jnum(obj, "x", &x) && jnum(obj, "y", &y) && w >= 64 && h >= 64) {
            jnum(obj, "scale", &sc);
            m[n++] = (kms_monitor_t){(int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (float)(sc > 0 ? sc : 1)};
        }
        p = q + 1;
    }
    // primary monitor (the one containing 0,0) first is not required; positions are kept as measured
    return n;
}

static bool measure_api(int pc, const char *path, const char *body, http_resp_t *out) {
    bool mine = M.state && M.pc == pc;
    if (!strcmp(path, "/api/m/hello")) {
        if (!mine) { http_json(out, 409, "{\"error\":\"no measurement for this PC\"}"); return true; }
        M.state = 2;
        M.deadline = now_ms() + SESSION_MS;
        http_json(out, 200, "{\"session\":\"s1\",\"timeout_ms\":8000}");
        return true;
    }
    if (!mine || M.state != 2) { http_json(out, 409, "{\"error\":\"no session\"}"); return true; }
    M.deadline = now_ms() + SESSION_MS;
    if (!strcmp(path, "/api/m/move")) {
        double x, y;
        if (!jnum(body, "x", &x) || !jnum(body, "y", &y)) { http_json(out, 400, "{\"error\":\"format\"}"); return true; }
        app_send_abs(pc, (uint16_t)(x < 0 ? 0 : x > 32767 ? 32767 : x), (uint16_t)(y < 0 ? 0 : y > 32767 ? 32767 : y));
        http_json(out, 200, "{\"ok\":true}");
    } else if (!strcmp(path, "/api/m/key")) {
        // only the move-window-to-next-monitor shortcut (Windows, GNOME, KDE)
        if (!strstr(body, "\"WIN+SHIFT+RIGHT\"")) { http_json(out, 400, "{\"error\":\"not allowed\"}"); return true; }
        app_key(pc, 0x0A, 0x4F);
        http_json(out, 200, "{\"ok\":true}");
    } else if (!strcmp(path, "/api/m/result")) {
        kms_monitor_t m[KMS_MAX_MON];
        int n = parse_monitors(body, m, KMS_MAX_MON);
        if (!n) { http_json(out, 400, "{\"error\":\"format\"}"); return true; }
        static kms_config_t c;
        c = *app_cfg();
        memcpy(c.pc[pc].mon, m, sizeof m);
        c.pc[pc].nmon = n;
        app_set_config(&c);
        http_json(out, 200, "{\"ok\":true}");
    } else if (!strcmp(path, "/api/m/close")) {
        if (jtrue(body, "focused")) {
            const kms_pc_cfg_t *p = &app_cfg()->pc[pc];
            uint8_t mods, key;
            kms_char_key('w', p->kbd_layout, &mods, &key);
            app_key(pc, p->os == OS_MAC ? 0x08 : 0x01, key);   // close the tab
        }
        bool ok = !jkey(body, "error");
        M.state = 3;
        M.last = ok ? "done" : "failed";
        if (!ok) led_set(LED_FLASH_TWICE, LED_RED);
        M.deadline = now_ms() + 500;
        http_json(out, 200, "{\"ok\":true}");
    } else {
        http_json(out, 404, "{\"error\":\"not found\"}");
    }
    return true;
}

// ------------------------------------------------------------------ dispatcher
bool web_handle(int pc, const http_req_t *r, http_resp_t *out) {
    char path[96];
    strncpy(path, r->path, sizeof path - 1);
    path[sizeof path - 1] = 0;
    char *q = strchr(path, '?');
    if (q) *q = 0;
    bool get = !strcmp(r->method, "GET"), post = !strcmp(r->method, "POST"), put = !strcmp(r->method, "PUT");
    // body as a NUL-terminated string for the JSON readers
    static char body[HTTP_BODY_MAX + 1];
    size_t bl = r->len < HTTP_BODY_MAX ? r->len : HTTP_BODY_MAX;
    memcpy(body, r->body, bl);
    body[bl] = 0;

    if (get && (!strcmp(path, "/") || !strcmp(path, "/measure") || !strcmp(path, "/index.html") ||
                !strcmp(path, "/KMSWITCH.HTM"))) {
        *out = (http_resp_t){200, "text/html; charset=utf-8", page_gz, page_gz_len, true, true};
        return true;
    }
    if (get && !strcmp(path, "/api/status")) {
        status_json(pc);
        http_json(out, 200, jbuf);
        return true;
    }
    if (get && !strcmp(path, "/api/config")) {
        size_t k = kms_config_write(app_cfg(), tbuf, sizeof tbuf);
        *out = (http_resp_t){200, "text/plain; charset=utf-8", (const uint8_t *)tbuf, k, false, false};
        return true;
    }
    if (put && !strcmp(path, "/api/config")) return put_config(r->body, r->len, out);
    if (post && !strcmp(path, "/api/measure")) {
        bool p1 = strstr(body, "\"pc1\"") != NULL, p2 = strstr(body, "\"pc2\"") != NULL;
        if (!p1 && !p2) p1 = p2 = true;
        if (M.state) { http_json(out, 409, "{\"error\":\"busy\"}"); return true; }
        web_measure(p1, p2);
        http_json(out, 202, "{\"ok\":true}");
        return true;
    }
    if (post && !strncmp(path, "/api/m/", 7)) return measure_api(pc, path, body, out);
    if (post && !strncmp(path, "/api/firmware", 13)) return web_firmware(pc, path, r, out);
    if (!strncmp(path, "/api/", 5)) {
        http_json(out, get || post || put ? 404 : 405, "{\"error\":\"not found\"}");
        return true;
    }
    // anything else: the page (so a typo in the address still opens it)
    *out = (http_resp_t){200, "text/html; charset=utf-8", page_gz, page_gz_len, true, true};
    return true;
}

// firmware updates arrive in the next part; until then the box refuses them cleanly
__attribute__((weak)) const char *web_update_json(void) { return "null"; }
__attribute__((weak)) bool web_firmware(int pc, const char *path, const http_req_t *r, http_resp_t *out) {
    (void)pc; (void)path; (void)r;
    http_json(out, 400, "{\"error\":\"format\"}");
    return true;
}
