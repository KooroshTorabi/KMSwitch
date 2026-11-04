#include "../src/core/kms_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void) {
    uint8_t out[1400], pl[LINK_MAX_PAYLOAD];
    kms_link_rx_t rx; memset(&rx, 0, sizeof rx);
    srand(1);
    for (int it = 0; it < 2000; it++) {
        size_t n = it < 4 ? (size_t[]){0, 1, 253, LINK_MAX_PAYLOAD}[it] : (size_t)(rand() % (LINK_MAX_PAYLOAD + 1));
        for (size_t i = 0; i < n; i++) pl[i] = (uint8_t)(rand() % 4 == 0 ? 0 : rand());
        size_t k = kms_link_encode((uint8_t)(it & 0x7F), pl, n, out, sizeof out);
        CHECK(k > 0 && out[k - 1] == 0);
        for (size_t i = 0; i + 1 < k; i++) if (!out[i]) { CHECK(0); break; }
        if (it % 7 == 3) out[k / 2] ^= 0x40;   // corrupt some frames
        int got = 0; uint8_t t; const uint8_t *p; size_t len;
        for (size_t i = 0; i < k; i++) if (kms_link_rx(&rx, out[i], &t, &p, &len)) {
            got = 1; CHECK(t == (it & 0x7F) && len == n && !memcmp(p, pl, n));
        }
        if (it % 7 == 3) CHECK(!got || out[k / 2] == 0x40); else CHECK(got);
    }
    printf("link: %s (bad frames %u)\n", fails ? "FAILED" : "ok", rx.bad);
    return fails != 0;
}
