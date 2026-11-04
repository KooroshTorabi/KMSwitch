// Typing short commands into a PC (open the settings page) for its keyboard layout.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kms_config.h"

typedef struct {
    uint8_t mods, key;
    uint16_t wait_ms;   // pause after this keystroke
} kms_keystroke_t;

bool kms_char_key(char ch, const char *layout, uint8_t *mods, uint8_t *key);
size_t kms_type_string(const char *s, const char *layout, kms_keystroke_t *out, size_t cap);
// the whole sequence that opens `url` in the default browser on that OS
size_t kms_open_url(kms_os_t os, const char *layout, const char *url, kms_keystroke_t *out, size_t cap);
