#include "kms_hotkey.h"
#include <string.h>

bool kms_report_has(const kms_kbd_report_t *r, uint8_t key) {
    for (int i = 0; i < 6; i++) if (r->keys[i] == key) return true;
    return false;
}

bool kms_hk_mods_match(uint8_t names, uint8_t mods) {
    // groups in HID bit order: ctrl (bit 0/4), shift (1/5), alt (2/6), gui (3/7)
    static const uint8_t any_name[4] = {HKM_CTRL, HKM_SHIFT, HKM_ALT, HKM_WIN};
    static const uint8_t right_name[4] = {HKM_RCTRL, HKM_RSHIFT, HKM_RALT, HKM_RWIN};
    for (int g = 0; g < 4; g++) {
        bool l = mods & (1u << g), r = mods & (1u << (g + 4));
        bool want_any = names & any_name[g], want_r = names & right_name[g];
        if (want_r && !r) return false;
        if (want_any && !(l || r)) return false;
        if (!want_any && !want_r && (l || r)) return false;
        if (want_r && !want_any && l) return false;   // RCTRL means the right key only
    }
    return true;
}

uint32_t kms_hk_process(kms_hk_state_t *st, const kms_hotkey_t *hk, int nhk,
                        const kms_kbd_report_t *in, kms_kbd_report_t *out) {
    uint32_t fired = 0;
    // forget swallowed keys that were released
    for (int i = 0; i < 6; i++)
        if (st->swallowed[i] && !kms_report_has(in, st->swallowed[i])) st->swallowed[i] = 0;
    // newly pressed keys
    for (int i = 0; i < 6; i++) {
        uint8_t k = in->keys[i];
        if (k < 4 || kms_report_has(&st->prev, k)) continue;
        for (int h = 0; h < nhk; h++) {
            if (!hk[h].key || hk[h].key != k || !kms_hk_mods_match(hk[h].names, in->mods)) continue;
            fired |= 1u << h;
            for (int s = 0; s < 6; s++)
                if (!st->swallowed[s]) { st->swallowed[s] = k; break; }
        }
    }
    *out = *in;
    int j = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t k = in->keys[i];
        bool hide = false;
        for (int s = 0; s < 6; s++) if (k && st->swallowed[s] == k) hide = true;
        if (!hide) out->keys[j++] = k;
    }
    while (j < 6) out->keys[j++] = 0;
    st->prev = *in;
    return fired;
}
