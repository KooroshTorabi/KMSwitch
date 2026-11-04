// kmswitch bootloader (first 32 KB of flash, after boot2). Picks the slot to start from the boot state,
// counts tries of a new version, falls back to the known good one, checks the image hash, and jumps.
#include <string.h>
#include "core/kms_fw.h"
#include "hardware/flash.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

static const fw_state_t *copy(int i) { return (const fw_state_t *)(XIP_BASE + (i ? FW_STATE2_OFF : FW_STATE_OFF)); }

static void write_state(fw_state_t *s, const fw_state_t *cur) {
    static uint8_t buf[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    uint32_t off = cur == copy(0) ? FW_STATE2_OFF : FW_STATE_OFF;
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, s, sizeof *s);
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, buf, sizeof buf);
    restore_interrupts(irq);
}

static bool image_ok(int slot, const fw_state_t *s) {
    uint32_t base = XIP_BASE + (slot ? FW_SLOT1_OFF : FW_SLOT0_OFF);
    const uint32_t *vt = (const uint32_t *)(base + FW_VECTOR_OFF);
    if (vt[0] < 0x20000000u || vt[0] > 0x20042000u) return false;          // initial stack pointer in RAM
    if (vt[1] < base || vt[1] >= base + FW_SLOT_SIZE) return false;       // reset handler inside the slot
    if (s && s->len[slot]) {                                               // installed by an update: check its hash
        kms_sha256_t h;
        uint8_t d[32];
        kms_sha256_init(&h);
        kms_sha256_update(&h, (const void *)base, s->len[slot]);
        kms_sha256_final(&h, d);
        if (memcmp(d, s->sha[slot], 32)) return false;
    }
    return true;
}

static void __attribute__((noreturn)) start(int slot) {
    uint32_t base = XIP_BASE + (slot ? FW_SLOT1_OFF : FW_SLOT0_OFF);
    const uint32_t *vt = (const uint32_t *)(base + FW_VECTOR_OFF);
    __asm volatile("cpsid i");
    systick_hw->csr = 0;
    for (int i = 0; i < 32; i++) {
        *(volatile uint32_t *)(PPB_BASE + 0xE180) = 1u << i;   // NVIC ICER
        *(volatile uint32_t *)(PPB_BASE + 0xE280) = 1u << i;   // NVIC ICPR
    }
    scb_hw->vtor = (uintptr_t)vt;
    __asm volatile("msr msp, %0\n cpsie i\n bx %1" ::"r"(vt[0]), "r"(vt[1]));
    __builtin_unreachable();
}

int main(void) {
    const fw_state_t *cur = fw_state_pick(copy(0), copy(1));
    fw_state_t s;
    uint8_t slot = 0;
    bool trial = false;
    if (cur) {
        s = *cur;
        trial = s.pending != FW_NO_SLOT && s.tries > 0;
        if (fw_boot_decide(&s, &slot)) write_state(&s, cur);
    }
    if (image_ok(slot, cur ? &s : NULL)) {
        // a new version gets 8 s to prove itself (it disables the watchdog once healthy)
        if (trial) watchdog_enable(8000, false);
        start(slot);
    }
    int other = !slot;
    if (image_ok(other, cur ? &s : NULL)) start(other);
    // nothing to start: wait in the USB bootloader for a recovery image
    reset_usb_boot(0, 0);
}
