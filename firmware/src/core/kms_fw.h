// Firmware update pieces shared by the bootloader, the firmware and the host tools:
// SHA-256, the boot state record, and the signed update bundle (.kmsfw).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------ flash layout (offsets from the start of flash)
#define FW_FLASH_SIZE (2u * 1024 * 1024)
#define FW_BOOTLOADER_OFF 0x000000u     // boot2 (256 B) + bootloader, up to 32 KB
#define FW_STATE_OFF 0x008000u          // boot state, copy 1
#define FW_STATE2_OFF 0x009000u         // boot state, copy 2
#define FW_SLOT0_OFF 0x00A000u
#define FW_SLOT1_OFF 0x104000u
#define FW_SLOT_SIZE 0x0FA000u          // 1000 KB
#define FW_VECTOR_OFF 0x100u            // the vector table follows the (unused) boot2 copy of the image

// ------------------------------------------------------------------ SHA-256
typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} kms_sha256_t;
void kms_sha256_init(kms_sha256_t *s);
void kms_sha256_update(kms_sha256_t *s, const void *data, size_t n);
void kms_sha256_final(kms_sha256_t *s, uint8_t out[32]);
uint32_t kms_crc32(const void *data, size_t n);

// ------------------------------------------------------------------ boot state
#define FW_STATE_MAGIC 0x53424D4Bu   // "KMBS"
#define FW_NO_SLOT 0xFF
typedef struct {
    uint32_t magic, seq;
    uint8_t active;        // slot that is known good
    uint8_t pending;       // slot to try, FW_NO_SLOT = none
    uint8_t tries;         // starts left for the pending slot
    uint8_t reserved;
    uint32_t len[2];       // image length per slot (0 = unknown: no hash check)
    uint8_t sha[2][32];
    uint32_t crc;          // CRC-32 of everything above
} fw_state_t;

bool fw_state_valid(const fw_state_t *s);
void fw_state_seal(fw_state_t *s);   // sets crc
// the newest valid record of the two copies (NULL if none)
const fw_state_t *fw_state_pick(const fw_state_t *a, const fw_state_t *b);

// what the bootloader starts. Updates *s (decremented tries / dropped pending) and returns true if it must be written.
bool fw_boot_decide(fw_state_t *s, uint8_t *slot);

// ------------------------------------------------------------------ update bundle
#define FW_BUNDLE_MAGIC "KMSFW1\0\0"
#define FW_HEADER_SIZE 4096u
#define FW_SIG_OFF 4032u
#define FW_MAX_IMAGES 4
typedef struct {
    uint8_t role;          // 0 = MCU-A (pc1), 1 = MCU-B (pc2)
    uint8_t slot;          // linked for slot 0 or 1
    uint16_t reserved;
    uint32_t offset, length;
    uint8_t sha[32];
} fw_image_t;
typedef struct {
    char magic[8];
    char board;            // 'A' / 'B'
    char pad[3];
    uint16_t version[3];
    uint16_t pad2;
    uint32_t flags;        // bit 0: allow downgrade
    uint32_t count;
    fw_image_t image[FW_MAX_IMAGES];
} fw_header_t;

typedef enum { FWE_OK = 0, FWE_FORMAT, FWE_BOARD, FWE_SIGNATURE, FWE_HASH, FWE_DOWNGRADE } fw_err_t;
const char *fw_err_name(fw_err_t e);

// check magic, board, signature (Ed25519 over the first FW_SIG_OFF bytes, which hold every image's SHA-256)
// and the version. `have` = installed version (NULL = development build: any version is accepted).
fw_err_t fw_check_header(const uint8_t hdr[FW_HEADER_SIZE], char board, const uint8_t pubkey[32],
                         const uint16_t *have, fw_header_t *out);
const fw_image_t *fw_find_image(const fw_header_t *h, int role, int slot);
bool fw_parse_version(const char *s, uint16_t v[3]);
