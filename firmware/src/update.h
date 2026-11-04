#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "net/http.h"

#define FLASH_SECTOR_SIZE_KMS 4096

bool update_stream(int pc, size_t total, size_t offset, const uint8_t *data, size_t n, bool last, http_resp_t *out);
void update_check_drive(void);       // way 2: a *.kmsfw file on the drive
bool update_waiting_confirm(void);
bool update_busy(void);              // installing: no input, no other work
void update_confirm(void);           // Win+F8 on the box's keyboard
void update_cancel(void);            // Esc, or the page's Cancel button
void update_task(void);
void update_b_status(int slot);
void update_b_ack(const uint8_t *p, size_t n);
void update_sync_b(const char *fw_b);
