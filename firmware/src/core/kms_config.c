#include "kms_config.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------ small string helpers
static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}
static void lower(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }
static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}
static void copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

// ------------------------------------------------------------------ keys
typedef struct { const char *name; uint8_t usage; } keyname_t;
static const keyname_t KEYS[] = {
    {"UP", 0x52}, {"DOWN", 0x51}, {"LEFT", 0x50}, {"RIGHT", 0x4F}, {"HOME", 0x4A}, {"END", 0x4D},
    {"PGUP", 0x4B}, {"PGDN", 0x4E}, {"INS", 0x49}, {"DEL", 0x4C}, {"ESC", 0x29}, {"TAB", 0x2B},
    {"SPACE", 0x2C}, {"ENTER", 0x28}, {"BACKSPACE", 0x2A}, {"PAUSE", 0x48}, {"SCROLLLOCK", 0x47},
    {"NUMLOCK", 0x53}, {"CAPSLOCK", 0x39}, {"PRINTSCREEN", 0x46},
};
static const char *MODNAMES[8] = {"CTRL", "ALT", "WIN", "SHIFT", "RCTRL", "RALT", "RWIN", "RSHIFT"};

static uint8_t key_usage(const char *k) {
    size_t n = strlen(k);
    if (n == 1 && k[0] >= 'A' && k[0] <= 'Z') return (uint8_t)(0x04 + k[0] - 'A');
    if (n == 1 && k[0] >= '1' && k[0] <= '9') return (uint8_t)(0x1E + k[0] - '1');
    if (n == 1 && k[0] == '0') return 0x27;
    if (k[0] == 'F' && n >= 2 && n <= 3 && isdigit((unsigned char)k[1])) {
        int f = atoi(k + 1);
        if (n == 3 && !isdigit((unsigned char)k[2])) return 0;
        if (f >= 1 && f <= 12) return (uint8_t)(0x3A + f - 1);
        if (f >= 13 && f <= 24) return (uint8_t)(0x68 + f - 13);
        return 0;
    }
    if (n == 3 && k[0] == 'K' && k[1] == 'P' && isdigit((unsigned char)k[2]))
        return k[2] == '0' ? 0x62 : (uint8_t)(0x59 + k[2] - '1');
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (!strcmp(KEYS[i].name, k)) return KEYS[i].usage;
    return 0;
}

static size_t key_name(uint8_t u, char *out, size_t cap) {
    if (u >= 0x04 && u <= 0x1D) return (size_t)snprintf(out, cap, "%c", 'A' + u - 0x04);
    if (u >= 0x1E && u <= 0x26) return (size_t)snprintf(out, cap, "%c", '1' + u - 0x1E);
    if (u == 0x27) return (size_t)snprintf(out, cap, "0");
    if (u >= 0x3A && u <= 0x45) return (size_t)snprintf(out, cap, "F%d", u - 0x3A + 1);
    if (u >= 0x68 && u <= 0x73) return (size_t)snprintf(out, cap, "F%d", u - 0x68 + 13);
    if (u >= 0x59 && u <= 0x61) return (size_t)snprintf(out, cap, "KP%d", u - 0x59 + 1);
    if (u == 0x62) return (size_t)snprintf(out, cap, "KP0");
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (KEYS[i].usage == u) return (size_t)snprintf(out, cap, "%s", KEYS[i].name);
    return (size_t)snprintf(out, cap, "?");
}

bool kms_parse_hotkey(const char *s, kms_hotkey_t *hk) {
    char buf[64];
    size_t j = 0;
    for (; *s && j < sizeof buf - 1; s++)
        if (!isspace((unsigned char)*s)) buf[j++] = (char)toupper((unsigned char)*s);
    buf[j] = 0;
    if (!j || !strcmp(buf, "NONE")) { hk->names = 0; hk->key = 0; return true; }
    uint8_t names = 0, key = 0;
    char *tok = buf, *last = NULL;
    // the last non-empty part is the key, the rest are modifiers
    char *parts[12]; int np = 0;
    for (char *p = buf;; p++) {
        if (*p == '+' || *p == 0) {
            int end = *p == 0;
            *p = 0;
            if (*tok && np < 12) parts[np++] = tok;
            tok = p + 1;
            if (end) break;
        }
    }
    if (!np) return false;
    last = parts[np - 1];
    key = key_usage(last);
    if (!key) return false;
    for (int i = 0; i < np - 1; i++) {
        int m = -1;
        for (int k = 0; k < 8; k++) if (!strcmp(parts[i], MODNAMES[k])) m = k;
        if (m < 0) return false;
        names |= (uint8_t)(1u << m);
    }
    hk->names = names; hk->key = key;
    return true;
}

size_t kms_format_hotkey(const kms_hotkey_t *hk, char *out, size_t cap) {
    if (!hk->key) return (size_t)snprintf(out, cap, "none");
    size_t n = 0;
    out[0] = 0;
    for (int k = 0; k < 8; k++)
        if (hk->names & (1u << k)) n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s+", MODNAMES[k]);
    if (n < cap) n += key_name(hk->key, out + n, cap - n);
    return n;
}

static const char *HK_KEYS[HK_COUNT] = {"switch", "relative_mode", "speed_up", "speed_down", "speed_reset",
                                        "open_config", "measure"};
static const char *HK_DEFAULT[HK_COUNT] = {"WIN+F12", "WIN+F11", "WIN+SHIFT+UP", "WIN+SHIFT+DOWN",
                                           "WIN+SHIFT+HOME", "WIN+F10", "WIN+F9"};
const char *kms_hk_name(kms_hk_id_t id) { return HK_KEYS[id]; }

// ------------------------------------------------------------------ monitors
static int parse_int(const char **p, int32_t *v) {
    const char *s = *p;
    int neg = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    if (!isdigit((unsigned char)*s)) return 0;
    long x = 0;
    while (isdigit((unsigned char)*s)) { x = x * 10 + (*s++ - '0'); if (x > 1000000) return 0; }
    *v = (int32_t)(neg ? -x : x);
    *p = s;
    return 1;
}

int kms_parse_monitors(const char *s, kms_monitor_t *out, int max) {
    int n = 0;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == '\t') s++;
        if (!*s) break;
        kms_monitor_t m = {0, 0, 0, 0, 1.0f};
        const char *p = s;
        // WxH+X+Y[@scale]
        if (!isdigit((unsigned char)*p) || !parse_int(&p, &m.w) || (*p != 'x' && *p != 'X')) goto skip;
        p++;
        if (!isdigit((unsigned char)*p) || !parse_int(&p, &m.h)) goto skip;
        if ((*p != '+' && *p != '-') || !parse_int(&p, &m.x)) goto skip;
        if ((*p != '+' && *p != '-') || !parse_int(&p, &m.y)) goto skip;
        if (*p == '@') {
            p++;
            char *e;
            double sc = strtod(p, &e);
            if (e == p || sc <= 0) goto skip;
            m.scale = (float)sc;
            p = e;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != ',') goto skip;
        if (m.w > 0 && m.h > 0 && n < max) out[n++] = m;
    skip:
        while (*s && *s != ',') s++;
    }
    return n;
}

static size_t fmt_num(double v, char *out, size_t cap) {
    // like JavaScript String(+v.toFixed(2)): 1 -> "1", 1.5 -> "1.5", 1.25 -> "1.25"
    long c = lround(v * 100.0);
    long ip = labs(c) / 100, fp = labs(c) % 100;
    const char *sg = c < 0 ? "-" : "";
    if (fp == 0) return (size_t)snprintf(out, cap, "%s%ld", sg, ip);
    if (fp % 10 == 0) return (size_t)snprintf(out, cap, "%s%ld.%ld", sg, ip, fp / 10);
    return (size_t)snprintf(out, cap, "%s%ld.%02ld", sg, ip, fp);
}

size_t kms_format_monitors(const kms_monitor_t *m, int n, char *out, size_t cap) {
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        char sc[16];
        fmt_num(m[i].scale, sc, sizeof sc);
        k += (size_t)snprintf(out + k, k < cap ? cap - k : 0, "%s%ldx%ld%c%ld%c%ld@%s", i ? ", " : "",
                              (long)m[i].w, (long)m[i].h, m[i].x < 0 ? '-' : '+', labs((long)m[i].x),
                              m[i].y < 0 ? '-' : '+', labs((long)m[i].y), sc);
    }
    return k;
}

// ------------------------------------------------------------------ defaults
static void pc_defaults(kms_pc_cfg_t *p, int idx) {
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "PC %d", idx + 1);
    p->speed_x = p->speed_y = 1.0f;
    p->accel = ACCEL_MEDIUM;
    p->os = OS_WINDOWS;
    strcpy(p->kbd_layout, "us");
    p->aspect_w = 16; p->aspect_h = 9;
}

void kms_config_defaults(kms_config_t *c) {
    memset(c, 0, sizeof *c);
    c->edge_switch = true;
    c->edge_delay_ms = 0;
    c->edge_corner_block = true;
    c->start_on = START_PC1;
    c->usb_network = c->auto_measure = c->config_ui = true;
    for (int i = 0; i < HK_COUNT; i++) kms_parse_hotkey(HK_DEFAULT[i], &c->hk[i]);
    pc_defaults(&c->pc[0], 0);
    pc_defaults(&c->pc[1], 1);
}

// ------------------------------------------------------------------ parse
static int parse_bool(const char *v, bool *out) {
    if (ieq(v, "on") || ieq(v, "yes") || ieq(v, "true") || ieq(v, "1")) { *out = true; return 1; }
    if (ieq(v, "off") || ieq(v, "no") || ieq(v, "false") || ieq(v, "0")) { *out = false; return 1; }
    return 0;
}
static int parse_float(const char *v, float *out) {
    char *e;
    double d = strtod(v, &e);
    if (e == v) return 0;
    *out = (float)d;
    return 1;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float round2(float v) { return (float)(lround(v * 100.0) / 100.0); }

static void problem(kms_problems_t *p, const char *sec, const char *key) {
    if (!p || p->n >= KMS_MAX_PROBLEMS) return;
    snprintf(p->key[p->n++], sizeof p->key[0], "%s%s%s", sec, key[0] ? "." : "", key);
}

static void add_extra(kms_config_t *c, const char *sec, const char *key, const char *val) {
    size_t need = strlen(sec) + 1 + strlen(key) + 3 + strlen(val) + 1;
    if (c->extra_len + need > KMS_EXTRA_MAX) return;
    char *d = c->extra + c->extra_len;
    c->extra_len += (size_t)sprintf(d, "%s", sec) + 1;
    c->extra_len += (size_t)sprintf(c->extra + c->extra_len, "%s = %s", key, val) + 1;
}

static int valid_aspect(const char *v, int *w, int *h) {
    // \d{1,2}:\d{1,2}, neither zero
    const char *p = v;
    int a = 0, b = 0, na = 0, nb = 0;
    while (isdigit((unsigned char)*p)) { a = a * 10 + (*p++ - '0'); na++; }
    if (*p++ != ':') return 0;
    while (isdigit((unsigned char)*p)) { b = b * 10 + (*p++ - '0'); nb++; }
    if (*p || na < 1 || na > 2 || nb < 1 || nb > 2 || !a || !b) return 0;
    *w = a; *h = b;
    return 1;
}

static const char *LAYOUTS[] = {"us", "de", "at", "ch", "fr", "uk", "it", "es"};

static void set_key(kms_config_t *c, const char *sec, char *key, char *val, kms_problems_t *p) {
    lower(key);
    int pci = ieq(sec, "pc1") ? 0 : ieq(sec, "pc2") ? 1 : -1;
    if (ieq(sec, "general")) {
        bool b;
        if (!strcmp(key, "arrangement")) {
            if (!*val) { c->has_arrangement = false; return; }
            char *comma = strchr(val, ',');
            if (comma) {
                *comma = 0;
                char a[24], bb[24];
                copy(a, sizeof a, val); copy(bb, sizeof bb, comma + 1);
                trim(a); trim(bb);
                const char *pa = a, *pb = bb;
                int32_t x, y;
                if (*pa != '+' && parse_int(&pa, &x) && !*pa && *pb != '+' && parse_int(&pb, &y) && !*pb) {
                    c->has_arrangement = true; c->arr_x = x; c->arr_y = y;
                    return;
                }
            }
            problem(p, sec, key);
        } else if (!strcmp(key, "edge_switch")) { if (parse_bool(val, &b)) c->edge_switch = b; else problem(p, sec, key); }
        else if (!strcmp(key, "edge_corner_block")) { if (parse_bool(val, &b)) c->edge_corner_block = b; else problem(p, sec, key); }
        else if (!strcmp(key, "usb_network")) { if (parse_bool(val, &b)) c->usb_network = b; else problem(p, sec, key); }
        else if (!strcmp(key, "auto_measure")) { if (parse_bool(val, &b)) c->auto_measure = b; else problem(p, sec, key); }
        else if (!strcmp(key, "config_ui")) { if (parse_bool(val, &b)) c->config_ui = b; else problem(p, sec, key); }
        else if (!strcmp(key, "edge_delay_ms")) {
            float f;
            if (parse_float(val, &f)) c->edge_delay_ms = (int)lroundf(clampf(f, 0, 1000)); else problem(p, sec, key);
        } else if (!strcmp(key, "start_on")) {
            if (!*val) problem(p, sec, key);
            else if (ieq(val, "pc1")) c->start_on = START_PC1;
            else if (ieq(val, "pc2")) c->start_on = START_PC2;
            else if (ieq(val, "last")) c->start_on = START_LAST;
            else c->start_on = START_PC1;   // the page keeps unknown text; the box treats it as pc1
        } else add_extra(c, sec, key, val);
    } else if (ieq(sec, "hotkeys")) {
        for (int i = 0; i < HK_COUNT; i++)
            if (!strcmp(key, HK_KEYS[i])) {
                kms_hotkey_t hk;
                if (kms_parse_hotkey(val, &hk)) c->hk[i] = hk; else problem(p, sec, key);
                return;
            }
        add_extra(c, sec, key, val);
    } else if (pci >= 0) {
        kms_pc_cfg_t *pc = &c->pc[pci];
        float f;
        if (!strcmp(key, "name")) { if (*val) copy(pc->name, sizeof pc->name, val); else problem(p, sec, key); }
        else if (!strcmp(key, "speed_x")) { if (parse_float(val, &f)) pc->speed_x = round2(clampf(f, 0.25f, 4.0f)); else problem(p, sec, key); }
        else if (!strcmp(key, "speed_y")) { if (parse_float(val, &f)) pc->speed_y = round2(clampf(f, 0.25f, 4.0f)); else problem(p, sec, key); }
        else if (!strcmp(key, "accel")) {
            if (!*val) problem(p, sec, key);
            else pc->accel = ieq(val, "off") ? ACCEL_OFF : ieq(val, "low") ? ACCEL_LOW : ieq(val, "high") ? ACCEL_HIGH : ACCEL_MEDIUM;
        } else if (!strcmp(key, "os")) {
            if (!*val) problem(p, sec, key);
            else pc->os = ieq(val, "mac") ? OS_MAC : ieq(val, "linux") ? OS_LINUX : OS_WINDOWS;
        } else if (!strcmp(key, "kbd_layout")) {
            if (!*val) { problem(p, sec, key); return; }
            lower(val);
            strcpy(pc->kbd_layout, "us");
            for (size_t i = 0; i < sizeof LAYOUTS / sizeof LAYOUTS[0]; i++)
                if (!strcmp(val, LAYOUTS[i])) strcpy(pc->kbd_layout, LAYOUTS[i]);
        } else if (!strcmp(key, "monitors")) {
            if (!*val) { pc->nmon = 0; return; }
            kms_monitor_t m[KMS_MAX_MON];
            int n = kms_parse_monitors(val, m, KMS_MAX_MON);
            if (!n) { problem(p, sec, key); return; }
            memcpy(pc->mon, m, sizeof m);
            pc->nmon = n;
            for (int i = 0; i < n; i++) pc->mon[i].scale = round2(pc->mon[i].scale);
        } else if (!strcmp(key, "aspect")) {
            int w, h;
            if (valid_aspect(val, &w, &h)) { pc->aspect_w = w; pc->aspect_h = h; } else problem(p, sec, key);
        } else add_extra(c, sec, key, val);
    } else {
        add_extra(c, sec, key, val);
    }
}

void kms_config_parse(kms_config_t *c, const char *text, size_t len, kms_problems_t *p) {
    kms_config_defaults(c);
    if (p) p->n = 0;
    char sec[24] = "";
    int have_sec = 0, lineno = 0;
    size_t i = 0;
    char line[256];
    while (i < len) {
        size_t j = 0;
        while (i < len && text[i] != '\n') {
            if (j < sizeof line - 1) line[j++] = text[i];
            i++;
        }
        i++;
        line[j] = 0;
        lineno++;
        // comments: whole line starting with # or ;, or " #..." / " ;..." after whitespace
        if (line[0] == '#' || line[0] == ';') continue;
        for (size_t k = 1; line[k]; k++)
            if ((line[k] == '#' || line[k] == ';') && isspace((unsigned char)line[k - 1])) { line[k] = 0; break; }
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            char *e = strchr(line, ']');
            if (e) {
                *e = 0;
                copy(sec, sizeof sec, line + 1);
                trim(sec);
                lower(sec);
                have_sec = 1;
                continue;
            }
        }
        char *eq = strchr(line, '=');
        if (!eq || !have_sec) {
            char l[16];
            snprintf(l, sizeof l, "line %d", lineno);
            problem(p, l, "");
            continue;
        }
        *eq = 0;
        char key[32], val[200];
        copy(key, sizeof key, line); trim(key);
        copy(val, sizeof val, eq + 1); trim(val);
        set_key(c, sec, key, val, p);
    }
}

// ------------------------------------------------------------------ write
typedef struct { char *out; size_t cap, n; int full; } wbuf_t;
static void put(wbuf_t *w, const char *s) {
    size_t k = strlen(s);
    if (w->n + k + 1 > w->cap) { w->full = 1; return; }
    memcpy(w->out + w->n, s, k);
    w->n += k;
    w->out[w->n] = 0;
}
static void line(wbuf_t *w, const char *kv, const char *comment) {
    char buf[480];
    if (comment) snprintf(buf, sizeof buf, "%-25s # %s\n", kv, comment);
    else snprintf(buf, sizeof buf, "%s\n", kv);
    put(w, buf);
}
static const char *b2s(bool b) { return b ? "on" : "off"; }
static void speed(char *out, float v) {
    long c = lround(v * 100.0);
    if (c % 100 == 0) sprintf(out, "%ld.0", c / 100);
    else fmt_num(v, out, 16);
}

static void extras_for(wbuf_t *w, const kms_config_t *c, const char *sec, int *any) {
    for (size_t i = 0; i < c->extra_len;) {
        const char *s = c->extra + i;
        const char *kv = s + strlen(s) + 1;
        if (!strcmp(s, sec)) {
            if (!*any) { put(w, "# kept from your file (not used by this page):\n"); *any = 1; }
            put(w, kv); put(w, "\n");
        }
        i = (size_t)(kv - c->extra) + strlen(kv) + 1;
    }
}

size_t kms_config_write(const kms_config_t *c, char *out, size_t cap) {
    wbuf_t w = {out, cap, 0, 0};
    char kv[300], v[220];
    if (cap) out[0] = 0;
    put(&w, "# kmswitch settings. Every line is optional: a missing or wrong value uses the default.\n"
            "# Full description: docs/configuration.md.  Settings page: http://192.168.7.1 or KMSWITCH.HTM on this drive.\n");
    int any;
    put(&w, "\n[general]\n");
    if (c->has_arrangement) snprintf(v, sizeof v, "%ld,%ld", (long)c->arr_x, (long)c->arr_y); else v[0] = 0;
    snprintf(kv, sizeof kv, "arrangement = %s", v);
    line(&w, kv, "X,Y: where PC 2's desktop sits, in PC 1's pixels (drag on the settings page); empty = right of PC 1");
    snprintf(kv, sizeof kv, "edge_switch = %s", b2s(c->edge_switch));
    line(&w, kv, "switch by pushing the pointer past the screen edge");
    snprintf(kv, sizeof kv, "edge_delay_ms = %d", c->edge_delay_ms);
    line(&w, kv, "0-1000: stay at the edge this long before switching");
    snprintf(kv, sizeof kv, "edge_corner_block = %s", b2s(c->edge_corner_block));
    line(&w, kv, "no switch while the pointer is in a screen corner");
    snprintf(kv, sizeof kv, "start_on = %s", c->start_on == START_PC2 ? "pc2" : c->start_on == START_LAST ? "last" : "pc1");
    line(&w, kv, "pc1 | pc2 | last");
    snprintf(kv, sizeof kv, "usb_network = %s", b2s(c->usb_network));
    line(&w, kv, "settings page at http://192.168.7.1 on both PCs");
    snprintf(kv, sizeof kv, "auto_measure = %s", b2s(c->auto_measure));
    line(&w, kv, "measure a newly connected PC by itself");
    snprintf(kv, sizeof kv, "config_ui = %s", b2s(c->config_ui));
    line(&w, kv, "allow the open_config and measure hotkeys to type into the PC");
    any = 0; extras_for(&w, c, "general", &any);

    put(&w, "\n[hotkeys]                 # modifiers CTRL ALT WIN SHIFT (+ RCTRL RALT RWIN RSHIFT) + one key, or none\n");
    for (int i = 0; i < HK_COUNT; i++) {
        kms_format_hotkey(&c->hk[i], v, sizeof v);
        snprintf(kv, sizeof kv, "%s = %s", HK_KEYS[i], v);
        line(&w, kv, i == HK_RELATIVE ? "relative mouse for games; pauses edge switching"
                     : i == HK_MEASURE ? "measure the screens of both PCs (LEDs blink)" : NULL);
    }
    any = 0; extras_for(&w, c, "hotkeys", &any);

    for (int k = 0; k < 2; k++) {
        const kms_pc_cfg_t *p = &c->pc[k];
        const int cm = k == 0;   // comments only in [pc1], like the page
        put(&w, k ? "\n[pc2]\n" : "\n[pc1]\n");
        snprintf(kv, sizeof kv, "name = %s", p->name); line(&w, kv, NULL);
        speed(v, p->speed_x); snprintf(kv, sizeof kv, "speed_x = %s", v); line(&w, kv, cm ? "0.25-4.0" : NULL);
        speed(v, p->speed_y); snprintf(kv, sizeof kv, "speed_y = %s", v); line(&w, kv, cm ? "0.25-4.0" : NULL);
        static const char *AC[] = {"off", "low", "medium", "high"};
        snprintf(kv, sizeof kv, "accel = %s", AC[p->accel]); line(&w, kv, cm ? "off | low | medium | high" : NULL);
        static const char *OS[] = {"windows", "mac", "linux"};
        snprintf(kv, sizeof kv, "os = %s", OS[p->os]); line(&w, kv, cm ? "windows | mac | linux (for typed commands)" : NULL);
        snprintf(kv, sizeof kv, "kbd_layout = %s", p->kbd_layout);
        line(&w, kv, cm ? "us de at ch fr uk it es (for typed commands)" : NULL);
        kms_format_monitors(p->mon, p->nmon, v, sizeof v);
        snprintf(kv, sizeof kv, "monitors = %s", v);
        line(&w, kv, cm ? "written by the box when it measures: WxH+X+Y@scale, ..." : NULL);
        snprintf(kv, sizeof kv, "aspect = %d:%d", p->aspect_w, p->aspect_h);
        line(&w, kv, cm ? "used only while monitors is empty: 16:9 16:10 21:9 32:9 4:3 5:4 9:16 or W:H" : NULL);
        any = 0; extras_for(&w, c, k ? "pc2" : "pc1", &any);
    }
    // sections the box does not know
    const char *known[] = {"general", "hotkeys", "pc1", "pc2"};
    char done[8][24]; int nd = 0;
    for (size_t i = 0; i < c->extra_len;) {
        const char *s = c->extra + i;
        const char *kvx = s + strlen(s) + 1;
        int isk = 0;
        for (int k = 0; k < 4; k++) if (!strcmp(s, known[k])) isk = 1;
        for (int k = 0; k < nd; k++) if (!strcmp(s, done[k])) isk = 1;
        if (!isk && nd < 8) {
            copy(done[nd++], 24, s);
            put(&w, "\n["); put(&w, s); put(&w, "]\n");
            any = 0; extras_for(&w, c, s, &any);
        }
        i = (size_t)(kvx - c->extra) + strlen(kvx) + 1;
    }
    return w.full ? 0 : w.n;
}
