// The firmware's side of the A/B slots: which slot runs, confirm a new version, mark an installed one pending,
// write flash safely (the other core is paused, interrupts are off).
#include "bootctl.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"

static bool victim;          // core 1 runs code from flash and must be paused around flash writes
static bool confirmed;

void bootctl_core1_is_victim(bool v) { victim = v; }

int bootctl_running_slot(void) {
    uintptr_t a = (uintptr_t)&bootctl_running_slot;
    return a >= XIP_BASE + FW_SLOT1_OFF ? 1 : 0;
}

static const fw_state_t *copy(int i) {
    return (const fw_state_t *)(XIP_BASE + (i ? FW_STATE2_OFF : FW_STATE_OFF));
}

bool bootctl_read(fw_state_t *out) {
    const fw_state_t *s = fw_state_pick(copy(0), copy(1));
    if (s) { *out = *s; return true; }
    memset(out, 0, sizeof *out);   // no record yet (fresh BOOTSEL flash): the running slot is the good one
    out->magic = FW_STATE_MAGIC;
    out->active = (uint8_t)bootctl_running_slot();
    out->pending = FW_NO_SLOT;
    return false;
}

typedef struct { uint32_t off; const uint8_t *data; size_t n; } op_t;
static void do_op(void *p) {
    op_t *o = p;
    flash_range_erase(o->off, (o->n + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));
    flash_range_program(o->off, o->data, (o->n + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1));
}

void bootctl_flash_write(uint32_t off, const uint8_t *data, size_t n) {
    op_t o = {off, data, n};
    if (victim) multicore_lockout_start_blocking();
    uint32_t irq = save_and_disable_interrupts();
    do_op(&o);
    restore_interrupts(irq);
    if (victim) multicore_lockout_end_blocking();
}

static bool write_state(fw_state_t *s) {
    static uint8_t buf[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    // overwrite the older copy so the newest stays intact until the new one is complete
    const fw_state_t *a = copy(0), *b = copy(1);
    const fw_state_t *cur = fw_state_pick(a, b);
    s->magic = FW_STATE_MAGIC;
    s->seq = cur ? cur->seq + 1 : 1;
    fw_state_seal(s);
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, s, sizeof *s);
    uint32_t off = cur == a ? FW_STATE2_OFF : FW_STATE_OFF;
    bootctl_flash_write(off, buf, sizeof buf);
    return fw_state_valid((const fw_state_t *)(XIP_BASE + off));
}

void bootctl_confirm(void) {
    if (confirmed) return;
    confirmed = true;
    fw_state_t s;
    bootctl_read(&s);
    int run = bootctl_running_slot();
    if (s.pending == run) {   // the new version started and is healthy: make it the good one
        s.active = (uint8_t)run;
        s.pending = FW_NO_SLOT;
        s.tries = 0;
        write_state(&s);
    }
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);   // the bootloader armed it for the trial start
}

bool bootctl_trial(void) {
    fw_state_t s;
    bootctl_read(&s);
    return s.pending == bootctl_running_slot();
}

bool bootctl_set_pending(int slot, uint32_t len, const uint8_t sha[32]) {
    fw_state_t s;
    bootctl_read(&s);
    s.active = (uint8_t)bootctl_running_slot();
    s.pending = (uint8_t)slot;
    s.tries = 1;
    s.len[slot] = len;
    memcpy(s.sha[slot], sha, 32);
    return write_state(&s);
}

bool bootctl_verify(int slot, uint32_t len, const uint8_t sha[32]) {
    kms_sha256_t h;
    uint8_t d[32];
    kms_sha256_init(&h);
    kms_sha256_update(&h, (const void *)(XIP_BASE + (slot ? FW_SLOT1_OFF : FW_SLOT0_OFF)), len);
    kms_sha256_final(&h, d);
    return !memcmp(d, sha, 32);
}

uint32_t bootctl_slot_offset(int slot) { return slot ? FW_SLOT1_OFF : FW_SLOT0_OFF; }
