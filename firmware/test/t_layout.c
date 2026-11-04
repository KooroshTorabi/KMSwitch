#include "../src/core/kms_layout.h"
#include "../src/core/kms_hotkey.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void cfg(kms_config_t *c, const char *txt) { kms_problems_t p; kms_config_parse(c, txt, strlen(txt), &p); }
int main(void) {
    static kms_config_t c; kms_layout_t L; kms_pointer_t p;
    kms_move_opts_t o = {true, 0, true, false};
    cfg(&c, "");
    kms_layout_build(&L, &c);
    CHECK(L.off_x == 1920 && L.off_y == 0 && L.bbox[0].h == 1080);
    kms_pointer_init(&p, &L, 0);
    CHECK(p.x == 960 && p.y == 540);
    CHECK(kms_pointer_move(&p, &L, 2000, 0, 10, &o) && p.pc == 1 && p.x == 0 && p.y == 540);
    CHECK(kms_pointer_move(&p, &L, -5, 3, 20, &o) && p.pc == 0 && p.x > 1919 && p.y == 540);   // back, x first
    uint16_t ax, ay;
    p.x = 0; p.y = 0; kms_pointer_abs(&p, &L, &ax, &ay); CHECK(ax == 8 && ay == 15);
    p.x = 1919.99f; p.y = 1079.99f; kms_pointer_abs(&p, &L, &ax, &ay); CHECK(ax == 32759 && ay == 32752);
    // corner block and delay
    kms_pointer_init(&p, &L, 0); p.y = 10;
    CHECK(!kms_pointer_move(&p, &L, 2000, 0, 30, &o) && p.pc == 0);
    o.edge_delay_ms = 150; p.y = 500;
    CHECK(!kms_pointer_move(&p, &L, 5, 0, 1000, &o));
    CHECK(!kms_pointer_move(&p, &L, 5, 0, 1100, &o));
    CHECK(kms_pointer_move(&p, &L, 5, 0, 1150, &o) && p.pc == 1);
    o.edge_delay_ms = 0;
    o.relative = true; kms_pointer_init(&p, &L, 0);
    CHECK(!kms_pointer_move(&p, &L, 5000, 0, 10, &o)); o.relative = false;
    // tall PC 1 next to short PC 2: only the touching part switches
    cfg(&c, "[general]\narrangement = 1440,0\n[pc1]\nmonitors = 1440x2560+0+0@1\n[pc2]\nmonitors = 1920x1080+0+0\n");
    kms_layout_build(&L, &c);
    CHECK(!L.arrangement_moved && L.off_x == 1440);
    kms_pointer_init(&p, &L, 0); p.y = 2000;
    CHECK(!kms_pointer_move(&p, &L, 3000, 0, 10, &o) && p.pc == 0 && p.x > 1439 && p.x < 1440);
    p.y = 500;
    CHECK(kms_pointer_move(&p, &L, 3, 0, 10, &o) && p.pc == 1 && p.y == 500);
    // two monitors on PC 1, the right one 300 px higher; empty corners cannot be entered
    cfg(&c, "[pc1]\nmonitors = 2560x1600+0+0@1.5, 3440x1440+2560-300@1\n");
    kms_layout_build(&L, &c);
    CHECK(L.bbox[0].x == 0 && L.bbox[0].y == -300 && L.bbox[0].w == 6000 && L.bbox[0].h == 1900);
    CHECK(L.off_x == 6000 && L.off_y == -300);
    kms_pointer_init(&p, &L, 0);
    CHECK(p.x == 1280 && p.y == 800);
    p.y = 100; CHECK(!kms_pointer_move(&p, &L, 2000, 0, 10, &o) && p.x == 3280);          // into monitor 2
    CHECK(!kms_pointer_move(&p, &L, 0, -1000, 10, &o) && p.y == -300);                    // stops at its top
    p.x = 1000; p.y = 1500;
    CHECK(!kms_pointer_move(&p, &L, 3000, 0, 10, &o) && p.x > 2559 && p.x < 2560);       // no monitor beside
    p.x = 4000; p.y = -100;
    CHECK(kms_pointer_move(&p, &L, 5000, 0, 10, &o) && p.pc == 1 && p.x == 0 && p.y == 200);   // PC 2 starts at -300
    // an overlapping arrangement is snapped to the nearest valid one
    cfg(&c, "[general]\narrangement = 1000,100\n");
    kms_layout_build(&L, &c);
    CHECK(L.arrangement_moved && kms_layout_contacts(&L, L.off_x, L.off_y) > 0);
    // motion scaling
    float px, py;
    kms_scale_motion(10, -4, 8, 1.0f, 2.0f, ACCEL_OFF, &px, &py); CHECK(px == 10 && py == -8);
    kms_scale_motion(10, 0, 1, 1.0f, 1.0f, ACCEL_HIGH, &px, &py); CHECK(px > 40 && px <= 50);
    // hotkeys
    kms_hotkey_t hk[2]; kms_parse_hotkey("WIN+F12", &hk[0]); kms_parse_hotkey("RCTRL+SCROLLLOCK", &hk[1]);
    CHECK(kms_hk_mods_match(HKM_WIN, 0x08) && kms_hk_mods_match(HKM_WIN, 0x80) && !kms_hk_mods_match(HKM_WIN, 0x09));
    CHECK(kms_hk_mods_match(HKM_RCTRL, 0x10) && !kms_hk_mods_match(HKM_RCTRL, 0x01));
    kms_hk_state_t st = {0}; kms_kbd_report_t in = {0x08, 0, {0}}, out;
    CHECK(kms_hk_process(&st, hk, 2, &in, &out) == 0 && out.mods == 0x08);
    in.keys[0] = 0x45;   // F12
    CHECK(kms_hk_process(&st, hk, 2, &in, &out) == 1 && out.keys[0] == 0);
    in.keys[1] = 0x04;
    CHECK(kms_hk_process(&st, hk, 2, &in, &out) == 0 && out.keys[0] == 0x04 && out.keys[1] == 0);
    in.keys[0] = 0; in.keys[1] = 0; in.mods = 0x10; in.keys[2] = 0x47;
    CHECK(kms_hk_process(&st, hk, 2, &in, &out) == 2);
    printf("layout+hotkey: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
