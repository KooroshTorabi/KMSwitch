// Hotkey detection on boot-protocol keyboard reports. Fired hotkeys are swallowed: the PC never sees their key.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "kms_config.h"

typedef struct {
    uint8_t mods;
    uint8_t reserved;
    uint8_t keys[6];
} kms_kbd_report_t;

typedef struct {
    kms_kbd_report_t prev;
    uint8_t swallowed[6];   // keys that fired a hotkey and are hidden until released
} kms_hk_state_t;

// true if the modifier state `mods` (HID bits: 0 LCtrl 1 LShift 2 LAlt 3 LGui 4 RCtrl 5 RShift 6 RAlt 7 RGui)
// matches the hotkey's modifier names exactly (no extra modifier groups held)
bool kms_hk_mods_match(uint8_t names, uint8_t mods);
bool kms_report_has(const kms_kbd_report_t *r, uint8_t key);

// Process one report. Returns a bitmask of hotkeys (1 << index into hk[]) that fired on this report;
// `out` is the report to forward to the PC (fired and still-held hotkey keys removed).
uint32_t kms_hk_process(kms_hk_state_t *st, const kms_hotkey_t *hk, int nhk,
                        const kms_kbd_report_t *in, kms_kbd_report_t *out);
