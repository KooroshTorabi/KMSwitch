#include "../src/core/kms_fw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void hex(const uint8_t *d, char *o) { for (int i = 0; i < 32; i++) sprintf(o + 2 * i, "%02x", d[i]); }
static uint8_t *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, 2); *n = (size_t)ftell(f); rewind(f); uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b; }
int main(int argc, char **argv) {
    kms_sha256_t s; uint8_t d[32]; char h[65];
    kms_sha256_init(&s); kms_sha256_update(&s, "abc", 3); kms_sha256_final(&s, d); hex(d, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    kms_sha256_init(&s); kms_sha256_final(&s, d); hex(d, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    static char A[1000]; memset(A, 'a', sizeof A);
    kms_sha256_init(&s); for (int i = 0; i < 1000; i++) kms_sha256_update(&s, A, 1000);
    kms_sha256_final(&s, d); hex(d, h);
    CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(kms_crc32("123456789", 9) == 0xCBF43926u);
    // boot decisions
    fw_state_t st = {FW_STATE_MAGIC, 7, 0, 1, 1, 0, {0, 0}, {{0}}, 0}; fw_state_seal(&st);
    CHECK(fw_state_valid(&st));
    uint8_t slot;
    CHECK(fw_boot_decide(&st, &slot) && slot == 1 && st.tries == 0 && st.seq == 8 && fw_state_valid(&st));
    CHECK(fw_boot_decide(&st, &slot) && slot == 0 && st.pending == FW_NO_SLOT);   // never confirmed: roll back
    CHECK(!fw_boot_decide(&st, &slot) && slot == 0);
    fw_state_t old = st; old.seq = 3; fw_state_seal(&old);
    CHECK(fw_state_pick(&old, &st) == &st);
    old.crc ^= 1; CHECK(fw_state_pick(&old, &st) == &st);
    uint16_t v[3]; CHECK(fw_parse_version("1.20.3", v) && v[1] == 20 && !fw_parse_version("1.2", v));
    // bundle made by tools/kmsfw.py with a test key
    if (argc > 2) {
        size_t n, kn; uint8_t *b = slurp(argv[1], &n), *pk = slurp(argv[2], &kn);
        fw_header_t hd; uint16_t have[3] = {1, 0, 0}, newer[3] = {1, 3, 0};
        CHECK(b && pk && kn == 32 && n > FW_HEADER_SIZE);
        CHECK(fw_check_header(b, 'B', pk, have, &hd) == FWE_OK && hd.count == 2 && hd.version[1] == 2);
        CHECK(fw_check_header(b, 'A', pk, have, &hd) == FWE_BOARD);
        CHECK(fw_check_header(b, 'B', pk, newer, &hd) == FWE_DOWNGRADE);
        CHECK(fw_check_header(b, 'B', pk, NULL, &hd) == FWE_OK);
        const fw_image_t *im = fw_find_image(&hd, 1, 1);
        CHECK(im && im->offset + im->length <= n);
        kms_sha256_init(&s); kms_sha256_update(&s, b + im->offset, im->length); kms_sha256_final(&s, d);
        CHECK(!memcmp(d, im->sha, 32));
        b[100] ^= 1; CHECK(fw_check_header(b, 'B', pk, have, &hd) == FWE_SIGNATURE);
        free(b); free(pk);
    }
    printf("fw: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
