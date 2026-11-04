// MCU-A <-> MCU-B link over the isolated UART. Frames: COBS(type, payload..., crc16) followed by 0x00.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINK_BAUD 2000000
#define LINK_MAX_PAYLOAD 600

typedef enum {
    // A -> B
    LM_KBD = 1,          // 8-byte boot keyboard report for PC 2
    LM_MOUSE_ABS,        // buttons u8, x u16, y u16, wheel i8, pan i8
    LM_MOUSE_REL,        // buttons u8, dx i16, dy i16, wheel i8, pan i8
    LM_RELEASE_ALL,      // no payload: all keys and buttons up
    LM_LED,              // u8 pattern, u8 r, u8 g, u8 b
    LM_SETTINGS,         // u8 flags: bit0 usb_network
    LM_HTTP_RESP,        // u16 id, u8 flags (bit0 last), data
    LM_MSC_RESP,         // u32 tag, i8 status, data (read) / nothing (write)
    LM_FW_CHUNK,         // u32 offset, data; offset 0xFFFFFFFF = done (u32 length, 32-byte sha256)
    LM_REBOOT,           // u8 mode: 0 normal, 1 BOOTSEL
    // B -> A
    LM_STATUS = 64,      // u8 flags (bit0 PC 2 mounted, bit1 suspended), u8 kbd leds, char fw[12]
    LM_HTTP_REQ,         // u16 id, u8 flags (bit0 last), data (the raw request)
    LM_MSC_REQ,          // u32 tag, u8 op (0 read, 1 write, 2 capacity), u32 lba, u16 count, data (write)
    LM_FW_ACK,           // u32 offset or status
    // both ways
    LM_PING = 120,       // u32 time
    LM_PONG,
} link_msg_t;

uint16_t kms_crc16(const uint8_t *d, size_t n);

// encode one frame into out (cap >= n + n/254 + 6). Returns the frame length including the 0x00 delimiter.
size_t kms_link_encode(uint8_t type, const uint8_t *payload, size_t n, uint8_t *out, size_t cap);

typedef struct {
    uint8_t buf[LINK_MAX_PAYLOAD + 8];
    size_t n;
    bool overflow;
    uint32_t bad;   // frames dropped (CRC / framing)
} kms_link_rx_t;

// feed one received byte. Returns true when a whole valid frame is ready: *type, *payload, *len.
bool kms_link_rx(kms_link_rx_t *rx, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len);
