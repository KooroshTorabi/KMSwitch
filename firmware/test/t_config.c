#include "../src/core/kms_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); fseek(f, 0, 2); *n = ftell(f); rewind(f); char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) exit(1); b[*n] = 0; fclose(f); return b; }
int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void) {
    static kms_config_t c; kms_problems_t p; size_t n; char *t = slurp("../../config/KMSWITCH.TXT", &n);
    kms_config_parse(&c, t, n, &p);
    CHECK(p.n == 0);
    static char out[8192]; size_t k = kms_config_write(&c, out, sizeof out);
    CHECK(k == n && !memcmp(out, t, n));
    if (k != n || memcmp(out, t, n)) { FILE *f = fopen("/tmp/out.txt", "w"); fwrite(out, 1, k, f); fclose(f); }
    const char *ex = "[general]\narrangement = 2560,-120\nedge_delay_ms = 150\nfoo = bar\n[hotkeys]\nswitch = scrolllock\n"
        "speed_up = ctrl + alt + pgup\nmeasure = WIN+F99\n[pc1]\nname = Office laptop\nspeed_x = 0.9 # c\nspeed_y = 9\n"
        "monitors = 2560x1600+0+0@1.5, 3440x1440+2560-300@1\n[pc2]\nkbd_layout = DE\naspect = 21:9\n[extra]\nk = v\n";
    kms_config_parse(&c, ex, strlen(ex), &p);
    CHECK(c.has_arrangement && c.arr_x == 2560 && c.arr_y == -120 && c.edge_delay_ms == 150);
    CHECK(c.hk[HK_SWITCH].key == 0x47 && c.hk[HK_SWITCH].names == 0);
    CHECK(c.hk[HK_SPEED_UP].key == 0x4B && c.hk[HK_SPEED_UP].names == (HKM_CTRL | HKM_ALT));
    CHECK(p.n == 1 && !strcmp(p.key[0], "hotkeys.measure"));
    CHECK(c.pc[0].nmon == 2 && c.pc[0].mon[1].x == 2560 && c.pc[0].mon[1].y == -300 && c.pc[0].speed_y == 4.0f);
    CHECK(!strcmp(c.pc[1].kbd_layout, "de") && c.pc[1].aspect_w == 21);
    k = kms_config_write(&c, out, sizeof out);
    CHECK(strstr(out, "monitors = 2560x1600+0+0@1.5, 3440x1440+2560-300@1"));
    CHECK(strstr(out, "speed_up = CTRL+ALT+PGUP"));
    CHECK(strstr(out, "speed_x = 0.9  ") && strstr(out, "speed_y = 4.0"));
    CHECK(strstr(out, "# kept from your file (not used by this page):\nfoo = bar\n"));
    CHECK(strstr(out, "\n[extra]\n# kept from your file (not used by this page):\nk = v\n"));
    // round trip of the written file is stable
    static kms_config_t c2; static char out2[8192];
    kms_config_parse(&c2, out, k, &p); size_t k2 = kms_config_write(&c2, out2, sizeof out2);
    CHECK(k2 == k && !memcmp(out, out2, k));
    free(t);
    printf("config: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
