#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc_port.h"

#define STORAGE_CFG_MAX 6144

void storage_init(void);
void storage_task(void);
size_t storage_read_config(char *buf, size_t cap);         // 0 = no card / no file
bool storage_write_config(const char *text, size_t len);   // queued; written safely within ~1.5 s
bool storage_config_reload_due(void);                      // a PC changed files on the drive
void storage_set_owner(int pc);                            // which PC sees the drive
const port_disk_t *storage_local_disk(void);
void storage_link_request(const uint8_t *p, size_t len);   // LM_MSC_REQ from MCU-B

// firmware update support (MCU-A): the box takes the card for itself while it handles an update file
void storage_exclusive(bool on);
bool storage_file_create(const char *path);
bool storage_file_append(const void *d, size_t n);
bool storage_file_close(void);
bool storage_file_read(const char *path, uint32_t off, void *buf, size_t n, size_t *got);
uint32_t storage_file_size(const char *path);
bool storage_file_rename(const char *from, const char *to);
bool storage_file_delete(const char *path);
bool storage_find_update(char *name, size_t cap);
void storage_log(const char *line);
