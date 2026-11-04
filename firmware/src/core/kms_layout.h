// Pointer engine: both PCs' monitors in one pixel space, edge switching, absolute output.
// Coordinates are desktop pixels. Each PC keeps its own origin (its primary monitor at 0,0); PC 2 sits at
// `arrangement` in PC 1's pixels. See docs/configuration.md "Arranging the two PCs".
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "kms_config.h"

#define KMS_MIN_TOUCH 32        // px of shared edge needed for a valid arrangement
#define KMS_CORNER_PX 24        // edge_corner_block: no switch this close to a monitor corner
#define KMS_ABS_MAX 32767

typedef struct { int32_t x, y, w, h; } kms_rect_t;

typedef struct {
    kms_rect_t mon[2][KMS_MAX_MON];   // local coordinates
    int nmon[2];
    kms_rect_t bbox[2];               // local bounding box (= the desktop the OS maps 0..32767 onto)
    int32_t off_x, off_y;             // PC 2 origin in PC 1 pixels (resolved arrangement)
    bool arrangement_moved;           // the stored arrangement was invalid and was snapped
} kms_layout_t;

typedef struct {
    int pc;                 // 0 / 1: active PC
    float x, y;             // local pixels of the active PC
    uint32_t push_since;    // ms timestamp when pushing against a crossing edge began, 0 = not pushing
    int push_dir;           // 1 right, 2 left, 3 down, 4 up (while pushing)
} kms_pointer_t;

typedef struct {
    bool edge_switch;
    int edge_delay_ms;
    bool corner_block;
    bool relative;          // relative mode: no edge switching
} kms_move_opts_t;

// monitors of a PC from its config (measured ones, or one screen from `aspect`)
int kms_pc_monitors(const kms_pc_cfg_t *pc, kms_rect_t *out);
void kms_layout_build(kms_layout_t *L, const kms_config_t *c);
// snap a wished PC 2 offset to the nearest valid one (same rules as the settings page)
void kms_layout_snap(const kms_layout_t *L, int32_t want_x, int32_t want_y, int32_t *ox, int32_t *oy);
int kms_layout_contacts(const kms_layout_t *L, int32_t ox, int32_t oy);   // number of touching edge pieces

void kms_pointer_init(kms_pointer_t *p, const kms_layout_t *L, int pc);   // centre of the PC's primary monitor
void kms_pointer_clamp(kms_pointer_t *p, const kms_layout_t *L);          // after a layout change
// Move by (dx, dy) pixels. Returns true if the pointer switched to the other PC (p->pc changed).
bool kms_pointer_move(kms_pointer_t *p, const kms_layout_t *L, float dx, float dy, uint32_t now_ms,
                      const kms_move_opts_t *o);
void kms_pointer_abs(const kms_pointer_t *p, const kms_layout_t *L, uint16_t *ax, uint16_t *ay);

// mouse counts -> pixels: speed and acceleration. dt_ms = time since the previous report.
void kms_scale_motion(int32_t cx, int32_t cy, uint32_t dt_ms, float speed_x, float speed_y, kms_accel_t accel,
                      float *px, float *py);
