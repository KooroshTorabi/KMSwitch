#pragma once
#include <stdbool.h>
#include <stdint.h>

bool sd_present(void);           // card detect switch
bool sd_init(void);              // (re)initialise the card; false if absent or not answering
bool sd_ready(void);
uint32_t sd_sectors(void);       // 512-byte sectors
bool sd_read(uint32_t lba, uint8_t *buf, uint32_t count);
bool sd_write(uint32_t lba, const uint8_t *buf, uint32_t count);
bool sd_sync(void);
