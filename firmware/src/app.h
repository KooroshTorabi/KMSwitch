// MCU-A application services used by the web API (web_api.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core/kms_config.h"

const kms_config_t *app_cfg(void);
int app_active(void);
bool app_pc_connected(int pc);
const char *app_fw_b(void);
void app_switch_to(int pc);
void app_hold_input(bool hold);                      // measurement: the user's keyboard / mouse are held back
void app_send_abs(int pc, uint16_t x, uint16_t y);
void app_key(int pc, uint8_t mods, uint8_t key);     // press and release one combination
void app_type_url(int pc, const char *url);
bool app_typing(void);
void app_set_config(const kms_config_t *c);          // apply and save
void app_measuring_leds(bool on);
