#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/kms_fw.h"

void bootctl_core1_is_victim(bool v);   // MCU-A: core 1 called multicore_lockout_victim_init()
int bootctl_running_slot(void);
bool bootctl_read(fw_state_t *out);
bool bootctl_trial(void);               // this start is the first try of a new version
void bootctl_confirm(void);             // healthy: keep this version (once per start)
bool bootctl_set_pending(int slot, uint32_t len, const uint8_t sha[32]);
bool bootctl_verify(int slot, uint32_t len, const uint8_t sha[32]);
void bootctl_flash_write(uint32_t off, const uint8_t *data, size_t n);   // erase + program, n rounded up
uint32_t bootctl_slot_offset(int slot);
