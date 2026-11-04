// Minimal HID report descriptor parser: finds the mouse fields (buttons, X, Y, wheel, pan).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t bit;      // bit offset in the report (after the report ID byte)
    uint8_t size;      // bits
    bool is_signed;
    bool present;
} kms_field_t;

typedef struct {
    bool valid;
    uint8_t report_id;     // 0 = no report IDs
    bool absolute;         // X/Y absolute (tablet) instead of relative
    kms_field_t buttons;   // size = number of buttons (1 bit each)
    kms_field_t x, y, wheel, pan;
    int32_t x_max, y_max;  // logical maximum (absolute devices)
} kms_mouse_desc_t;

typedef struct {
    uint8_t buttons;
    int32_t x, y, wheel, pan;
} kms_mouse_in_t;

bool kms_hid_parse_mouse(const uint8_t *desc, size_t len, kms_mouse_desc_t *m);
// extract one input report (with the report ID byte if the device uses IDs). false if it is not the mouse report.
bool kms_hid_read_mouse(const kms_mouse_desc_t *m, const uint8_t *rep, size_t len, kms_mouse_in_t *out);
