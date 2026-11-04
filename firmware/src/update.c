// MCU-A: firmware updates (docs/firmware-update.md). A bundle arrives over the settings page (from either PC)
// or as a file on the drive; it is stored on the microSD card, checked (board, signature, hashes, version),
// confirmed with Win+F8 on the box's own keyboard, then MCU-B's part goes over the link and MCU-A's own part
// into its inactive flash slot. The bootloader starts the new version once and falls back if it does not confirm.
#include "update.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "bootctl.h"
#include "hardware/watchdog.h"
#include "led.h"
#include "link_io.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "storage.h"
#include "web_api.h"

#define INCOMING "/.kmswitch/fw/incoming.kmsfw"
#define CURRENT "/.kmswitch/fw/current.kmsfw"
#define CONFIRM_MS 60000

extern const uint8_t kms_release_pubkey[32];

typedef enum { U_IDLE, U_RECEIVING, U_VERIFYING, U_CONFIRM, U_INSTALL_B, U_INSTALL_A, U_REBOOTING, U_DONE, U_FAILED } ustate_t;
static const char *SNAME[] = {"idle", "receiving", "verifying", "confirm", "installing", "installing", "rebooting", "done", "failed"};

static struct {
    ustate_t st;
    fw_header_t hdr;
    char version[20];
    char message[64];
    char file[64];             // the checked bundle on the card
    bool from_drive;           // way 2: delete / rename the user's file afterwards
    char drive_name[64];
    uint32_t deadline, b_off, b_sent_ms;
    const fw_image_t *b_img, *a_img;
    int progress;
    int b_slot;                // MCU-B's running slot (from its status)
    bool b_only;               // only re-sync MCU-B (versions differ after a power cut)
} U;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void set_leds(void) {
    uint8_t m[4] = {LED_BLINK_SLOW, LED_ORANGE};
    led_set(LED_BLINK_SLOW, LED_ORANGE);
    link_send(LM_LED, m, 4);
}

static void fail(const char *why) {
    snprintf(U.message, sizeof U.message, "%s", why);
    U.st = U_FAILED;
    U.deadline = now_ms() + 10000;
    storage_exclusive(false);
    app_hold_input(false);
    app_measuring_leds(false);   // back to the normal LED colours
    led_set(LED_FLASH_TWICE, LED_RED);
    if (U.from_drive) {
        char to[80], line[160];
        snprintf(to, sizeof to, "%s.rejected", U.drive_name);
        storage_exclusive(true);
        storage_file_rename(U.drive_name, to);
        snprintf(line, sizeof line, "update %s rejected: %s", U.drive_name, why);
        storage_log(line);
        storage_exclusive(false);
    }
}

static bool hash_file(const char *path, const fw_image_t *im) {
    static uint8_t buf[4096];
    kms_sha256_t h;
    uint8_t d[32];
    kms_sha256_init(&h);
    for (uint32_t o = 0; o < im->length;) {
        size_t n = im->length - o < sizeof buf ? im->length - o : sizeof buf, got = 0;
        if (!storage_file_read(path, im->offset + o, buf, n, &got) || got != n) return false;
        kms_sha256_update(&h, buf, n);
        o += n;
        watchdog_update();
    }
    kms_sha256_final(&h, d);
    return !memcmp(d, im->sha, 32);
}

// check a bundle file on the card. Returns NULL if fine, else the error name.
static const char *check(const char *path) {
    static uint8_t hdr[FW_HEADER_SIZE];
    size_t got = 0;
    if (!storage_file_read(path, 0, hdr, sizeof hdr, &got) || got != sizeof hdr) return "format";
    uint16_t have[3];
    bool dev = !fw_parse_version(KMS_VERSION, have);
    fw_err_t e = fw_check_header(hdr, KMS_BOARD, kms_release_pubkey, dev ? NULL : have, &U.hdr);
    if (e) return fw_err_name(e);
    // every image this box needs: MCU-A and MCU-B, linked for each slot
    for (int role = 0; role < 2; role++)
        for (int slot = 0; slot < 2; slot++) {
            const fw_image_t *im = fw_find_image(&U.hdr, role, slot);
            if (!im) return "format";
            if (im->offset + im->length > storage_file_size(path)) return "format";
            if (!hash_file(path, im)) return "hash";
        }
    snprintf(U.version, sizeof U.version, "%u.%u.%u", U.hdr.version[0], U.hdr.version[1], U.hdr.version[2]);
    return NULL;
}

// ------------------------------------------------------------------ receiving (settings page, either PC)
static uint32_t rx_total, rx_got;

bool update_stream(int pc, size_t total, size_t offset, const uint8_t *data, size_t n, bool last, http_resp_t *out) {
    (void)pc;
    if (offset == 0) {
        if (U.st != U_IDLE && U.st != U_DONE && U.st != U_FAILED) { http_json(out, 400, "{\"error\":\"busy\"}"); return false; }
        if (total > 2 * 1024 * 1024 || total < FW_HEADER_SIZE) { http_json(out, 400, "{\"error\":\"format\"}"); return false; }
        memset(&U, 0, sizeof U);
        U.st = U_RECEIVING;
        storage_exclusive(true);
        if (!storage_file_create(INCOMING)) { fail("no card"); http_json(out, 400, "{\"error\":\"format\"}"); return false; }
        rx_total = (uint32_t)total;
        rx_got = 0;
    }
    if (U.st != U_RECEIVING) { http_json(out, 400, "{\"error\":\"busy\"}"); return false; }
    if (!storage_file_append(data, n)) { storage_file_close(); fail("card write"); http_json(out, 400, "{\"error\":\"format\"}"); return false; }
    rx_got += (uint32_t)n;
    U.progress = (int)((uint64_t)rx_got * 100 / rx_total);
    if (!last) return true;
    storage_file_close();
    U.st = U_VERIFYING;
    snprintf(U.file, sizeof U.file, INCOMING);
    const char *err = check(U.file);
    if (err) {
        static char e[48];
        snprintf(e, sizeof e, "{\"error\":\"%s\"}", err);
        fail(err);
        http_json(out, 400, e);
        return false;
    }
    U.st = U_CONFIRM;
    U.deadline = now_ms() + CONFIRM_MS;
    snprintf(U.message, sizeof U.message, "Press Win+F8 on the keyboard connected to kmswitch");
    set_leds();
    static char ok[48];
    snprintf(ok, sizeof ok, "{\"version\":\"%s\"}", U.version);
    http_json(out, 202, ok);
    return true;
}

// way 2: an update file on the drive
void update_check_drive(void) {
    if (U.st != U_IDLE && U.st != U_DONE && U.st != U_FAILED) return;
    char name[64];
    if (!storage_find_update(name, sizeof name)) return;
    memset(&U, 0, sizeof U);
    U.from_drive = true;
    snprintf(U.drive_name, sizeof U.drive_name, "%s", name);
    snprintf(U.file, sizeof U.file, "%s", name);
    U.st = U_VERIFYING;
    storage_exclusive(true);
    const char *err = check(U.file);
    if (err) { fail(err); return; }
    U.st = U_CONFIRM;
    U.deadline = now_ms() + CONFIRM_MS;
    snprintf(U.message, sizeof U.message, "Press Win+F8 on the keyboard connected to kmswitch");
    set_leds();
}

bool update_waiting_confirm(void) { return U.st == U_CONFIRM; }
bool update_busy(void) { return U.st >= U_INSTALL_B && U.st <= U_REBOOTING; }

void update_cancel(void) {
    if (U.st != U_CONFIRM) return;
    U.st = U_IDLE;
    storage_exclusive(false);
    app_measuring_leds(false);
}

static void start_b(void) {
    U.b_img = fw_find_image(&U.hdr, 1, !U.b_slot);
    U.b_off = 0;
    U.b_sent_ms = 0;
    U.st = U_INSTALL_B;
}

void update_confirm(void) {
    if (U.st != U_CONFIRM) return;
    app_hold_input(true);
    U.progress = 0;
    snprintf(U.message, sizeof U.message, "Installing MCU-B");
    if (!app_pc_connected(1) && !link_alive()) {
        // MCU-B is not powered (PC 2 off): install MCU-A now; MCU-B is brought in line when it appears
        U.st = U_INSTALL_A;
        return;
    }
    start_b();
}

// MCU-B reports its running slot in its status
void update_b_status(int slot) { U.b_slot = slot; }

// MCU-B acknowledges chunks: u32 offset (or 0xFFFFFFFF + u8 status for the final check)
void update_b_ack(const uint8_t *p, size_t n) {
    if (U.st != U_INSTALL_B || n < 4) return;
    uint32_t off;
    memcpy(&off, p, 4);
    if (off == 0xFFFFFFFFu) {
        if (n < 5 || p[4] != 0) { fail("MCU-B did not accept its part"); return; }
        U.progress = 50;
        if (U.b_only) { U.st = U_DONE; U.deadline = now_ms() + 10000; storage_exclusive(false); app_hold_input(false); app_measuring_leds(false); return; }
        U.st = U_INSTALL_A;
        snprintf(U.message, sizeof U.message, "Installing MCU-A");
        return;
    }
    if (off == U.b_off) {
        U.b_off += 512;
        U.b_sent_ms = 0;
        U.progress = (int)((uint64_t)U.b_off * 50 / U.b_img->length);
    }
}

static void send_b_chunk(void) {
    uint8_t m[4 + 512];
    if (U.b_sent_ms && now_ms() - U.b_sent_ms < 1000) return;   // waiting for the ack (resent after 1 s)
    if (U.b_off >= U.b_img->length) {
        uint32_t done = 0xFFFFFFFFu;
        memcpy(m, &done, 4);
        memcpy(m + 4, &U.b_img->length, 4);
        memcpy(m + 8, U.b_img->sha, 32);
        link_send(LM_FW_CHUNK, m, 40);
        U.b_sent_ms = now_ms();
        return;
    }
    size_t n = U.b_img->length - U.b_off < 512 ? U.b_img->length - U.b_off : 512, got = 0;
    memcpy(m, &U.b_off, 4);
    memset(m + 4, 0xFF, 512);
    if (!storage_file_read(U.file, U.b_img->offset + U.b_off, m + 4, n, &got) || got != n) { fail("card read"); return; }
    link_send(LM_FW_CHUNK, m, 4 + 512);
    U.b_sent_ms = now_ms();
}

static void install_a(void) {
    static uint8_t buf[FLASH_SECTOR_SIZE_KMS];
    int slot = !bootctl_running_slot();
    const fw_image_t *im = fw_find_image(&U.hdr, 0, slot);
    U.st = U_REBOOTING;
    // keep a copy of the bundle: MCU-B can be brought in line later from it
    if (!U.from_drive) storage_file_rename(U.file, CURRENT);
    else storage_file_rename(U.file, CURRENT);
    snprintf(U.file, sizeof U.file, CURRENT);
    // the USB host on core 1 runs from flash: stop it, the keyboard is not needed any more
    multicore_reset_core1();
    bootctl_core1_is_victim(false);
    uint32_t base = bootctl_slot_offset(slot);
    for (uint32_t o = 0; o < im->length; o += sizeof buf) {
        size_t n = im->length - o < sizeof buf ? im->length - o : sizeof buf, got = 0;
        memset(buf, 0xFF, sizeof buf);
        if (!storage_file_read(U.file, im->offset + o, buf, n, &got) || got != n) { fail("card read"); return; }
        bootctl_flash_write(base + o, buf, sizeof buf);
        watchdog_update();
    }
    if (!bootctl_verify(slot, im->length, im->sha)) { fail("flash verify"); watchdog_reboot(0, 0, 100); return; }
    bootctl_set_pending(slot, im->length, im->sha);
    storage_exclusive(false);
    watchdog_reboot(0, 0, 50);
    while (true) tight_loop_contents();
}

void update_task(void) {
    switch (U.st) {
    case U_CONFIRM:
        if ((int32_t)(now_ms() - U.deadline) >= 0) { fail("not confirmed within 60 s"); }
        break;
    case U_INSTALL_B:
        if (!link_alive()) { fail("MCU-B not reachable"); break; }
        send_b_chunk();
        break;
    case U_INSTALL_A:
        install_a();
        break;
    case U_DONE:
    case U_FAILED:
        if ((int32_t)(now_ms() - U.deadline) >= 0) U.st = U_IDLE;
        break;
    default:
        break;
    }
}

// MCU-B runs another version (for example power was lost between the two parts): re-send its part from the
// last installed bundle on the card, without asking again (that bundle was already confirmed).
void update_sync_b(const char *fw_b) {
    if (U.st != U_IDLE || !strcmp(fw_b, KMS_VERSION) || !strcmp(KMS_VERSION, "dev")) return;
    static uint32_t tried;
    if (tried && now_ms() - tried < 600000) return;
    tried = now_ms() | 1;
    storage_exclusive(true);
    snprintf(U.file, sizeof U.file, CURRENT);
    if (check(U.file) || strcmp(U.version, KMS_VERSION)) { storage_exclusive(false); U.st = U_IDLE; return; }
    U.b_only = true;
    snprintf(U.message, sizeof U.message, "Bringing MCU-B to %s", KMS_VERSION);
    app_hold_input(true);
    set_leds();
    start_b();
}

const char *web_update_json(void) {
    static char j[200];
    if (U.st == U_IDLE) return "null";
    snprintf(j, sizeof j, "{\"version\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"message\":\"%s\"}", U.version,
             SNAME[U.st], U.progress, U.message);
    return j;
}

bool web_firmware(int pc, const char *path, const http_req_t *r, http_resp_t *out) {
    (void)pc; (void)r;
    if (!strcmp(path, "/api/firmware/cancel")) {
        update_cancel();
        http_json(out, 200, "{\"ok\":true}");
        return true;
    }
    // a small body arrived here instead of the stream: not a bundle
    http_json(out, 400, "{\"error\":\"format\"}");
    return true;
}
