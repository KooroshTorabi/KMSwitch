#include "kms_typing.h"
#include <string.h>

#define SH 0x02   // left shift

// one character -> modifier + key for a keyboard layout. Covers what the box types:
// lower-case letters, digits, space and  : / . ; - ?
bool kms_char_key(char ch, const char *layout, uint8_t *mods, uint8_t *key) {
    bool de = !strcmp(layout, "de") || !strcmp(layout, "at"), ch_ = !strcmp(layout, "ch");
    bool fr = !strcmp(layout, "fr"), it = !strcmp(layout, "it"), es = !strcmp(layout, "es");
    bool qwertz = de || ch_;
    *mods = 0;
    if (ch >= 'a' && ch <= 'z') {
        char c = ch;
        if (qwertz && (c == 'y' || c == 'z')) c = c == 'y' ? 'z' : 'y';
        if (fr) {
            if (c == 'a' || c == 'q') c = c == 'a' ? 'q' : 'a';
            else if (c == 'z' || c == 'w') c = c == 'z' ? 'w' : 'z';
            else if (c == 'm') { *key = 0x33; return true; }
        }
        *key = (uint8_t)(0x04 + c - 'a');
        return true;
    }
    if (ch >= '0' && ch <= '9') {
        *key = ch == '0' ? 0x27 : (uint8_t)(0x1E + ch - '1');
        if (fr) *mods = SH;
        return true;
    }
    switch (ch) {
    case ' ': *key = 0x2C; return true;
    case '\n': *key = 0x28; return true;
    case '.':
        if (fr) { *key = 0x36; *mods = SH; } else *key = 0x37;
        return true;
    case ':':
        if (fr) *key = 0x37;
        else if (qwertz || it || es) { *key = 0x37; *mods = SH; }
        else { *key = 0x33; *mods = SH; }
        return true;
    case ';':
        if (fr) *key = 0x36;
        else if (qwertz || it || es) { *key = 0x36; *mods = SH; }
        else *key = 0x33;
        return true;
    case '/':
        if (fr) { *key = 0x37; *mods = SH; }
        else if (qwertz || it || es) { *key = 0x24; *mods = SH; }
        else *key = 0x38;
        return true;
    case '-':
        if (fr) *key = 0x23;
        else if (qwertz || it || es) *key = 0x38;
        else *key = 0x2D;
        return true;
    default:
        return false;
    }
}

size_t kms_type_string(const char *s, const char *layout, kms_keystroke_t *out, size_t cap) {
    size_t n = 0;
    for (; *s && n < cap; s++) {
        uint8_t m, k;
        if (kms_char_key(*s, layout, &m, &k)) out[n++] = (kms_keystroke_t){m, k, 0};
    }
    return n;
}

size_t kms_open_url(kms_os_t os, const char *layout, const char *url, kms_keystroke_t *out, size_t cap) {
    size_t n = 0;
#define KEY(m, k, w) do { if (n < cap) out[n++] = (kms_keystroke_t){(m), (k), (w)}; } while (0)
    char cmd[96];
    switch (os) {
    case OS_MAC:
        KEY(0x08, 0x2C, 400);                           // Cmd+Space (Spotlight)
        n += kms_type_string("terminal", layout, out + n, cap - n);
        KEY(0, 0x28, 1200);                             // Enter, wait for Terminal
        strcpy(cmd, "open ");
        strncat(cmd, url, sizeof cmd - 12);
        strcat(cmd, "; exit");
        n += kms_type_string(cmd, layout, out + n, cap - n);
        KEY(0, 0x28, 0);
        break;
    case OS_LINUX:
        KEY(0x05, 0x17, 1200);                          // Ctrl+Alt+T, wait for the terminal
        strcpy(cmd, "xdg-open ");
        strncat(cmd, url, sizeof cmd - 16);
        strcat(cmd, "; exit");
        n += kms_type_string(cmd, layout, out + n, cap - n);
        KEY(0, 0x28, 0);
        break;
    default: {                                          // Windows: Win+R, the address, Enter
        uint8_t m, k;
        kms_char_key('r', layout, &m, &k);
        KEY(0x08, k, 500);
        n += kms_type_string(url, layout, out + n, cap - n);
        KEY(0, 0x28, 0);
        break;
    }
    }
#undef KEY
    return n;
}
