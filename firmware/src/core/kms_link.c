#include "kms_link.h"
#include <string.h>

uint16_t kms_crc16(const uint8_t *d, size_t n) {   // CRC-16/CCITT-FALSE
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*d++ << 8);
        for (int i = 0; i < 8; i++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

size_t kms_link_encode(uint8_t type, const uint8_t *payload, size_t n, uint8_t *out, size_t cap) {
    uint8_t raw[LINK_MAX_PAYLOAD + 3];
    if (n > LINK_MAX_PAYLOAD) return 0;
    raw[0] = type;
    if (n) memcpy(raw + 1, payload, n);
    uint16_t crc = kms_crc16(raw, n + 1);
    raw[n + 1] = (uint8_t)(crc >> 8);
    raw[n + 2] = (uint8_t)crc;
    size_t len = n + 3;
    // COBS
    if (cap < len + len / 254 + 3) return 0;
    size_t o = 1, code_at = 0;
    uint8_t code = 1;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == 0) {
            out[code_at] = code;
            code_at = o++;
            code = 1;
        } else {
            out[o++] = raw[i];
            if (++code == 0xFF) {
                out[code_at] = code;
                code_at = o++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    out[o++] = 0;
    return o;
}

bool kms_link_rx(kms_link_rx_t *rx, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len) {
    if (byte != 0) {
        if (rx->n < sizeof rx->buf) rx->buf[rx->n++] = byte;
        else rx->overflow = true;
        return false;
    }
    // end of frame: decode COBS in place
    size_t n = rx->n;
    bool ovf = rx->overflow;
    rx->n = 0;
    rx->overflow = false;
    if (!n) return false;
    if (ovf) { rx->bad++; return false; }
    uint8_t *b = rx->buf;
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t code = b[i];
        if (!code || i + code > n) { rx->bad++; return false; }
        for (uint8_t k = 1; k < code; k++) b[o++] = b[i + k];
        i += code;
        if (code < 0xFF && i < n) b[o++] = 0;
    }
    if (o < 3) { rx->bad++; return false; }
    uint16_t crc = (uint16_t)(b[o - 2] << 8 | b[o - 1]);
    if (kms_crc16(b, o - 2) != crc) { rx->bad++; return false; }
    *type = b[0];
    *payload = b + 1;
    *len = o - 3;
    return true;
}
