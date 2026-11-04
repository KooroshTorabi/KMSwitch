#include "../src/core/kms_typing.h"
#include <stdio.h>
#include <string.h>
int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void) {
    uint8_t m, k;
    CHECK(kms_char_key(':', "us", &m, &k) && m == 2 && k == 0x33);
    CHECK(kms_char_key('/', "de", &m, &k) && m == 2 && k == 0x24);
    CHECK(kms_char_key('y', "de", &m, &k) && m == 0 && k == 0x1D);
    CHECK(kms_char_key('1', "fr", &m, &k) && m == 2 && k == 0x1E);
    CHECK(kms_char_key('a', "fr", &m, &k) && k == 0x14);
    CHECK(kms_char_key('m', "fr", &m, &k) && k == 0x33);
    CHECK(kms_char_key('.', "fr", &m, &k) && m == 2 && k == 0x36);
    CHECK(kms_char_key('-', "ch", &m, &k) && m == 0 && k == 0x38);
    kms_keystroke_t s[160];
    size_t n = kms_open_url(OS_WINDOWS, "us", "http://192.168.7.1", s, 160);
    CHECK(n == 1 + 18 + 1 && s[0].mods == 0x08 && s[0].key == 0x15 && s[n - 1].key == 0x28);
    n = kms_open_url(OS_LINUX, "de", "http://192.168.7.1", s, 160);
    CHECK(n == 1 + strlen("xdg-open http://192.168.7.1; exit") + 1 && s[0].mods == 0x05);
    n = kms_open_url(OS_MAC, "us", "http://192.168.7.1/measure", s, 160);
    CHECK(s[0].mods == 0x08 && s[0].key == 0x2C && n == 1 + 8 + 1 + strlen("open http://192.168.7.1/measure; exit") + 1);
    printf("typing: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
