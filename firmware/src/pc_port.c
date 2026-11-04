#include "pc_port.h"
#include <string.h>
#include "tusb.h"
#include "usb_desc.h"

// keyboard reports are queued, not merged: a short press (or the mask key around a hotkey) must reach the PC
#define KQ 16
static uint8_t kbd[8], kq[KQ][8], kbd_leds;
static uint8_t kq_head, kq_tail;
#define kbd_pending (kq_head != kq_tail)
static struct {
    bool rel_pending, abs_pending;
    uint8_t buttons;
    int32_t dx, dy;
    uint16_t x, y;
    int32_t wheel, pan;
} ms;
static const port_disk_t *disk;
static bool media_changed;

void port_init(void) { memset(&ms, 0, sizeof ms); }
bool port_mounted(void) { return tud_mounted(); }
bool port_suspended(void) { return tud_suspended(); }
uint8_t port_kbd_leds(void) { return kbd_leds; }
void port_set_disk(const port_disk_t *d) { disk = d; }
void port_media_changed(void) { media_changed = true; }

static void wake(void) {
    if (tud_suspended()) tud_remote_wakeup();
}

void port_kbd(const uint8_t rep[8]) {
    if (!memcmp(kbd, rep, 8)) return;
    memcpy(kbd, rep, 8);
    uint8_t n = (uint8_t)((kq_head + 1) % KQ);
    if (n == kq_tail) {   // full: replace the newest entry, the final state still arrives
        memcpy(kq[(kq_head + KQ - 1) % KQ], rep, 8);
    } else {
        memcpy(kq[kq_head], rep, 8);
        kq_head = n;
    }
    if (rep[0] || rep[2]) wake();
}

void port_mouse_abs(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel, int8_t pan) {
    if (ms.rel_pending) return;   // keep the order: a pending relative report goes first
    if (ms.abs_pending && ms.buttons != buttons) {
        // a button change must not be merged away; flush what we have first
        port_task();
    }
    ms.abs_pending = true;
    ms.buttons = buttons;
    ms.x = x; ms.y = y;
    ms.wheel += wheel; ms.pan += pan;
    if (buttons) wake();
}

void port_mouse_rel(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan) {
    if (ms.rel_pending && ms.buttons != buttons) port_task();
    ms.rel_pending = true;
    ms.abs_pending = false;
    ms.buttons = buttons;
    ms.dx += dx; ms.dy += dy;
    ms.wheel += wheel; ms.pan += pan;
    if (buttons) wake();
}

void port_release_all(void) {
    static const uint8_t zero[8] = {0};
    port_kbd(zero);
    if (ms.buttons || ms.rel_pending || ms.abs_pending) {
        ms.buttons = 0;
        ms.rel_pending = true;
    }
}

bool port_idle(void) { return !kbd_pending && !ms.rel_pending && !ms.abs_pending; }

static int8_t take8(int32_t *v) {
    int32_t t = *v < -127 ? -127 : *v > 127 ? 127 : *v;
    *v -= t;
    return (int8_t)t;
}
static int16_t take16(int32_t *v) {
    int32_t t = *v < -32767 ? -32767 : *v > 32767 ? 32767 : *v;
    *v -= t;
    return (int16_t)t;
}

void port_task(void) {
    if (!tud_mounted() || tud_suspended()) return;
    if (kbd_pending && tud_hid_n_ready(ITF_KBD)) {
        tud_hid_n_report(ITF_KBD, 0, kq[kq_tail], 8);   // the same 8 bytes in boot and report protocol
        kq_tail = (uint8_t)((kq_tail + 1) % KQ);
    }
    if ((ms.rel_pending || ms.abs_pending) && tud_hid_n_ready(ITF_MOUSE)) {
        if (tud_hid_n_get_protocol(ITF_MOUSE) == HID_PROTOCOL_BOOT) {
            // BIOS / boot protocol: buttons, dx, dy, wheel; absolute positions cannot be expressed
            uint8_t r[4] = {(uint8_t)(ms.buttons & 7), 0, 0, 0};
            if (ms.rel_pending) {
                r[1] = (uint8_t)take8(&ms.dx);
                r[2] = (uint8_t)take8(&ms.dy);
            }
            r[3] = (uint8_t)take8(&ms.wheel);
            ms.pan = 0;
            tud_hid_n_report(ITF_MOUSE, 0, r, 3);
            ms.rel_pending = ms.dx || ms.dy || ms.wheel;
            ms.abs_pending = false;
        } else if (ms.rel_pending) {
            uint8_t r[7];
            int16_t dx = take16(&ms.dx), dy = take16(&ms.dy);
            r[0] = ms.buttons;
            r[1] = (uint8_t)dx; r[2] = (uint8_t)((uint16_t)dx >> 8);
            r[3] = (uint8_t)dy; r[4] = (uint8_t)((uint16_t)dy >> 8);
            r[5] = (uint8_t)take8(&ms.wheel);
            r[6] = (uint8_t)take8(&ms.pan);
            tud_hid_n_report(ITF_MOUSE, RID_MOUSE_REL, r, 7);
            ms.rel_pending = ms.dx || ms.dy || ms.wheel || ms.pan;
        } else {
            uint8_t r[7] = {ms.buttons, (uint8_t)ms.x, (uint8_t)(ms.x >> 8), (uint8_t)ms.y, (uint8_t)(ms.y >> 8)};
            r[5] = (uint8_t)take8(&ms.wheel);
            r[6] = (uint8_t)take8(&ms.pan);
            tud_hid_n_report(ITF_MOUSE, RID_MOUSE_ABS, r, 7);
            ms.abs_pending = ms.wheel || ms.pan;
        }
    }
}

// ------------------------------------------------------------------ TinyUSB HID callbacks
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t id, hid_report_type_t type, uint8_t *buf, uint16_t len) {
    (void)id; (void)type;
    if (itf == ITF_KBD && len >= 8) {
        memcpy(buf, kbd, 8);
        return 8;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t id, hid_report_type_t type, uint8_t const *buf, uint16_t len) {
    (void)id;
    if (itf == ITF_KBD && type == HID_REPORT_TYPE_OUTPUT && len >= 1) kbd_leds = buf[len - 1];
}

// ------------------------------------------------------------------ TinyUSB MSC callbacks
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "kmswitch", 8);
    memcpy(product_id, "shared drive    ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (media_changed) {
        media_changed = false;
        tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);   // medium may have changed
        return false;
    }
    if (!disk || !disk->ready()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);        // medium not present
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = disk && disk->ready() ? disk->sectors() : 0;
    *block_size = 512;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buf, uint32_t n) {
    (void)lun;
    if (!disk || !disk->ready()) return -1;
    return disk->read(lba, offset, buf, n);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t n) {
    (void)lun;
    if (!disk || !disk->ready()) return -1;
    return disk->write(lba, offset, buf, n);
}

void tud_msc_write10_complete_cb(uint8_t lun) {
    (void)lun;
    if (disk && disk->flush) disk->flush();
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buf, uint16_t n) {
    (void)buf; (void)n;
    switch (scsi_cmd[0]) {
    case 0x1E:   // prevent / allow medium removal
        return 0;
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
