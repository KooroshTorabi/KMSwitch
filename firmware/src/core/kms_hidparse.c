#include "kms_hidparse.h"
#include <string.h>

#define MAX_USAGES 16

bool kms_hid_parse_mouse(const uint8_t *d, size_t len, kms_mouse_desc_t *m) {
    memset(m, 0, sizeof *m);
    uint32_t page = 0, rsize = 0, rcount = 0;
    int32_t lmin = 0, lmax = 0;
    uint8_t rid = 0;
    uint16_t bitpos[256];                // per report ID
    memset(bitpos, 0, sizeof bitpos);
    uint32_t usages[MAX_USAGES];
    int nus = 0;
    uint32_t umin = 0, umax = 0;
    bool have_range = false, in_mouse = false;
    int depth = 0, mouse_depth = -1;
    kms_mouse_desc_t cur;
    memset(&cur, 0, sizeof cur);
    size_t i = 0;
    while (i < len) {
        uint8_t h = d[i++];
        if (h == 0xFE) {                 // long item
            if (i + 1 >= len) break;
            i += 2 + d[i];
            continue;
        }
        uint8_t sz = h & 3;
        if (sz == 3) sz = 4;
        if (i + sz > len) break;
        uint32_t u = 0;
        for (int k = 0; k < sz; k++) u |= (uint32_t)d[i + k] << (8 * k);
        int32_t s = (int32_t)u;
        if (sz == 1) s = (int8_t)u;
        if (sz == 2) s = (int16_t)u;
        i += sz;
        uint8_t tag = h & 0xFC;
        switch (tag) {
        case 0x04: page = u; break;                               // usage page
        case 0x14: lmin = s; break;                               // logical min
        case 0x24: lmax = (lmin >= 0 && sz < 4) ? (int32_t)u : s; break;   // logical max
        case 0x74: rsize = u; break;                              // report size
        case 0x94: rcount = u; break;                             // report count
        case 0x84: rid = (uint8_t)u; break;                       // report ID
        case 0x08:                                                // usage
            if (nus < MAX_USAGES) usages[nus++] = sz == 4 ? u : (page << 16 | u);
            break;
        case 0x18: umin = sz == 4 ? u : (page << 16 | u); have_range = true; break;
        case 0x28: umax = sz == 4 ? u : (page << 16 | u); break;
        case 0xA0:                                                // collection
            if (nus && (usages[0] == 0x00010002 || usages[0] == 0x00010001) && mouse_depth < 0) {
                in_mouse = true; mouse_depth = depth;
            }
            depth++;
            nus = 0; have_range = false;
            break;
        case 0xC0:                                                // end collection
            depth--;
            if (depth == mouse_depth) { in_mouse = false; mouse_depth = -1; }
            break;
        case 0x80: {                                              // input
            bool constant = u & 1, relative = u & 4, variable = u & 2;
            uint16_t *bp = &bitpos[rid];
            for (uint32_t k = 0; k < rcount; k++) {
                uint32_t usage = 0;
                if (have_range) usage = umin + k <= umax ? umin + k : umax;
                else if (nus) usage = usages[k < (uint32_t)nus ? k : (uint32_t)nus - 1];
                uint16_t bit = (uint16_t)(*bp + k * rsize);
                if (in_mouse && !constant && variable) {
                    kms_field_t f = {bit, (uint8_t)rsize, lmin < 0, true};
                    uint16_t up = usage >> 16, us = usage & 0xFFFF;
                    if (up == 0x09 && !cur.buttons.present) { cur.buttons = f; cur.buttons.size = (uint8_t)rcount; cur.report_id = rid; }
                    else if (up == 0x01 && us == 0x30 && !cur.x.present) { cur.x = f; cur.absolute = !relative; cur.x_max = lmax; cur.report_id = rid; }
                    else if (up == 0x01 && us == 0x31 && !cur.y.present) { cur.y = f; cur.y_max = lmax; }
                    else if (up == 0x01 && us == 0x38 && !cur.wheel.present) cur.wheel = f;
                    else if (up == 0x0C && us == 0x238 && !cur.pan.present) cur.pan = f;
                }
            }
            *bp = (uint16_t)(*bp + rsize * rcount);
            nus = 0; have_range = false;
            break;
        }
        case 0x90: case 0xB0:                                     // output / feature
            nus = 0; have_range = false;
            break;
        default: break;
        }
    }
    cur.valid = cur.x.present && cur.y.present;
    *m = cur;
    return cur.valid;
}

static int32_t get(const uint8_t *r, size_t len, kms_field_t f) {
    if (!f.present || !f.size) return 0;
    uint32_t v = 0;
    for (int k = 0; k < f.size && k < 32; k++) {
        size_t bit = f.bit + k;
        if (bit / 8 >= len) break;
        if (r[bit / 8] & (1u << (bit % 8))) v |= 1u << k;
    }
    if (f.is_signed && f.size < 32 && (v & (1u << (f.size - 1)))) v |= ~0u << f.size;
    return (int32_t)v;
}

bool kms_hid_read_mouse(const kms_mouse_desc_t *m, const uint8_t *rep, size_t len, kms_mouse_in_t *out) {
    if (!m->valid) return false;
    if (m->report_id) {
        if (!len || rep[0] != m->report_id) return false;
        rep++; len--;
    }
    memset(out, 0, sizeof *out);
    uint32_t b = 0;
    for (int k = 0; k < m->buttons.size && k < 8; k++) {
        size_t bit = m->buttons.bit + k;
        if (bit / 8 < len && (rep[bit / 8] & (1u << (bit % 8)))) b |= 1u << k;
    }
    out->buttons = (uint8_t)b;
    out->x = get(rep, len, m->x);
    out->y = get(rep, len, m->y);
    out->wheel = get(rep, len, m->wheel);
    out->pan = get(rep, len, m->pan);
    return true;
}
