// KMSWITCH.TXT: parse, defaults, write. Same rules as config/KMSWITCH.HTM (see docs/configuration.md).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KMS_MAX_MON 6
#define KMS_MAX_PROBLEMS 16
#define KMS_EXTRA_MAX 1024

typedef struct {
    int32_t x, y, w, h;   // desktop pixels, PC's own origin (primary monitor at 0,0)
    float scale;
} kms_monitor_t;

typedef enum { ACCEL_OFF, ACCEL_LOW, ACCEL_MEDIUM, ACCEL_HIGH } kms_accel_t;
typedef enum { OS_WINDOWS, OS_MAC, OS_LINUX } kms_os_t;
typedef enum { START_PC1, START_PC2, START_LAST } kms_start_t;

typedef enum {
    HK_SWITCH, HK_RELATIVE, HK_SPEED_UP, HK_SPEED_DOWN, HK_SPEED_RESET, HK_OPEN_CONFIG, HK_MEASURE, HK_COUNT
} kms_hk_id_t;

// modifier names in canonical order: CTRL ALT WIN SHIFT (either side) RCTRL RALT RWIN RSHIFT (right only)
enum { HKM_CTRL = 1, HKM_ALT = 2, HKM_WIN = 4, HKM_SHIFT = 8, HKM_RCTRL = 16, HKM_RALT = 32, HKM_RWIN = 64, HKM_RSHIFT = 128 };
typedef struct {
    uint8_t names;  // HKM_* bits
    uint8_t key;    // HID keyboard usage, 0 = hotkey disabled ("none")
} kms_hotkey_t;

typedef struct {
    char name[25];
    float speed_x, speed_y;
    kms_accel_t accel;
    kms_os_t os;
    char kbd_layout[3];
    kms_monitor_t mon[KMS_MAX_MON];
    int nmon;           // 0 = not measured
    int aspect_w, aspect_h;
} kms_pc_cfg_t;

typedef struct {
    bool has_arrangement;
    int32_t arr_x, arr_y;
    bool edge_switch;
    int edge_delay_ms;
    bool edge_corner_block;
    kms_start_t start_on;
    bool usb_network, auto_measure, config_ui;
    kms_hotkey_t hk[HK_COUNT];
    kms_pc_cfg_t pc[2];
    // unknown keys, kept verbatim: "section\0key = value\0" records
    char extra[KMS_EXTRA_MAX];
    size_t extra_len;
} kms_config_t;

typedef struct {
    int n;
    char key[KMS_MAX_PROBLEMS][40];
} kms_problems_t;

void kms_config_defaults(kms_config_t *c);
// Parse text (not necessarily NUL terminated). Missing / wrong values keep their default.
void kms_config_parse(kms_config_t *c, const char *text, size_t len, kms_problems_t *p);
// Write the canonical file (same text as the settings page writes). Returns length, or 0 if it did not fit.
size_t kms_config_write(const kms_config_t *c, char *out, size_t cap);

// helpers shared with the web API and the layout engine
int kms_parse_monitors(const char *s, kms_monitor_t *out, int max);
size_t kms_format_monitors(const kms_monitor_t *m, int n, char *out, size_t cap);
bool kms_parse_hotkey(const char *s, kms_hotkey_t *hk);   // false = invalid
size_t kms_format_hotkey(const kms_hotkey_t *hk, char *out, size_t cap);
const char *kms_hk_name(kms_hk_id_t id);
