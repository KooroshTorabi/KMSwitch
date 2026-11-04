#pragma once
#include <stdbool.h>
#include "net/http.h"

// pc: 0 = request from PC 1 (MCU-A's network), 1 = from PC 2 (forwarded by MCU-B)
bool web_handle(int pc, const http_req_t *r, http_resp_t *out);
void web_task(void);
void web_measure(bool pc1, bool pc2);
void web_measure_cancel(void);
bool web_measuring(void);
void web_pc_connected(int pc);   // a PC enumerated the box: maybe measure it

// firmware update hooks (update.c)
const char *web_update_json(void);
bool web_firmware(int pc, const char *path, const http_req_t *r, http_resp_t *out);
