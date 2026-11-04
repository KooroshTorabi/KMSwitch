#include "kms_fw.h"
#include <stdlib.h>
#include <string.h>
#ifndef KMS_FW_NO_SIGNATURE
#include "../crypto/monocypher-ed25519.h"
#endif

// ------------------------------------------------------------------ SHA-256 (FIPS 180-4)
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(kms_sha256_t *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

void kms_sha256_init(kms_sha256_t *s) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->len = 0;
    s->n = 0;
}

void kms_sha256_update(kms_sha256_t *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->len += n;
    while (n) {
        size_t k = 64 - s->n;
        if (k > n) k = n;
        memcpy(s->buf + s->n, p, k);
        s->n += k; p += k; n -= k;
        if (s->n == 64) { block(s, s->buf); s->n = 0; }
    }
}

void kms_sha256_final(kms_sha256_t *s, uint8_t out[32]) {
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    kms_sha256_update(s, &pad, 1);
    pad = 0;
    while (s->n != 56) kms_sha256_update(s, &pad, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    kms_sha256_update(s, l, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24); out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8); out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

uint32_t kms_crc32(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

// ------------------------------------------------------------------ boot state
bool fw_state_valid(const fw_state_t *s) {
    return s->magic == FW_STATE_MAGIC && s->crc == kms_crc32(s, offsetof(fw_state_t, crc)) && s->active < 2 &&
           (s->pending < 2 || s->pending == FW_NO_SLOT);
}
void fw_state_seal(fw_state_t *s) { s->crc = kms_crc32(s, offsetof(fw_state_t, crc)); }

const fw_state_t *fw_state_pick(const fw_state_t *a, const fw_state_t *b) {
    bool va = fw_state_valid(a), vb = fw_state_valid(b);
    if (va && vb) return (int32_t)(a->seq - b->seq) >= 0 ? a : b;
    return va ? a : vb ? b : NULL;
}

bool fw_boot_decide(fw_state_t *s, uint8_t *slot) {
    if (s->pending == FW_NO_SLOT) { *slot = s->active; return false; }
    if (s->tries > 0) {
        s->tries--;
        *slot = s->pending;
    } else {   // the new firmware never confirmed itself: back to the known good one
        s->pending = FW_NO_SLOT;
        *slot = s->active;
    }
    s->seq++;
    fw_state_seal(s);
    return true;
}

// ------------------------------------------------------------------ bundle
const char *fw_err_name(fw_err_t e) {
    static const char *n[] = {"ok", "format", "board", "signature", "hash", "downgrade"};
    return n[e];
}

bool fw_parse_version(const char *s, uint16_t v[3]) {
    for (int i = 0; i < 3; i++) {
        char *e;
        long x = strtol(s, &e, 10);
        if (e == s || x < 0 || x > 65535) return false;
        v[i] = (uint16_t)x;
        s = e;
        if (i < 2) { if (*s != '.') return false; s++; }
    }
    return *s == 0;
}

fw_err_t fw_check_header(const uint8_t hdr[FW_HEADER_SIZE], char board, const uint8_t pubkey[32],
                         const uint16_t *have, fw_header_t *out) {
    memcpy(out, hdr, sizeof *out);
    if (memcmp(out->magic, FW_BUNDLE_MAGIC, 8) || out->count == 0 || out->count > FW_MAX_IMAGES) return FWE_FORMAT;
    for (uint32_t i = 0; i < out->count; i++) {
        const fw_image_t *im = &out->image[i];
        if (im->role > 1 || im->slot > 1 || im->offset < FW_HEADER_SIZE || im->length == 0 || im->length > FW_SLOT_SIZE)
            return FWE_FORMAT;
    }
    if (out->board != board) return FWE_BOARD;
#ifndef KMS_FW_NO_SIGNATURE
    if (crypto_ed25519_check(hdr + FW_SIG_OFF, pubkey, hdr, FW_SIG_OFF)) return FWE_SIGNATURE;
#else
    (void)pubkey;
#endif
    if (have && !(out->flags & 1)) {
        for (int i = 0; i < 3; i++) {
            if (out->version[i] > have[i]) break;
            if (out->version[i] < have[i]) return FWE_DOWNGRADE;
        }
    }
    return FWE_OK;
}

const fw_image_t *fw_find_image(const fw_header_t *h, int role, int slot) {
    for (uint32_t i = 0; i < h->count && i < FW_MAX_IMAGES; i++)
        if (h->image[i].role == role && h->image[i].slot == slot) return &h->image[i];
    return NULL;
}
