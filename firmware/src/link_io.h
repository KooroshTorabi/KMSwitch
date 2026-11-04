#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/kms_link.h"

void link_init(void);
void link_send(uint8_t type, const void *payload, size_t n);
// next received frame; the payload stays valid until the next call
bool link_poll(uint8_t *type, const uint8_t **payload, size_t *len);
bool link_alive(void);   // a frame arrived in the last 1.5 s (both sides ping every 500 ms)
uint32_t link_errors(void);
