// MCU-A: the microSD card. Settings file, the drive for the active PC (directly for PC 1, over the link for
// PC 2), and safe writes by the box itself (the drive is taken away from the PC while the box writes).
#include "storage.h"
#include <stdio.h>
#include <string.h>
#include "fatfs/ff.h"
#include "link_io.h"
#include "pc_port.h"
#include "pico/time.h"
#include "sd_spi.h"

#define CFG_FILE "KMSWITCH.TXT"
#define HOLD_MS 1500          // the PC sees "no medium" this long before the box writes
#define RELOAD_AFTER_MS 2000  // re-read the settings this long after the PC's last write

static FATFS fs;
static int owner = 0;              // PC that sees the drive (the active PC)
static bool box_busy;              // the box is about to write / writing: no PC sees the drive
static bool exclusive;             // firmware update: the box uses the card, no PC sees the drive
static uint32_t busy_since;
static char *pending_text;         // file content waiting to be written
static size_t pending_len;
static uint32_t pc_write_ms;       // last write by a PC
static bool reload_flag;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// ------------------------------------------------------------------ settings file
static bool mount(void) {
    if (!sd_ready() && !sd_init()) return false;
    return f_mount(&fs, "", 1) == FR_OK;
}

size_t storage_read_config(char *buf, size_t cap) {
    if (!mount()) return 0;
    FIL f;
    UINT n = 0;
    if (f_open(&f, CFG_FILE, FA_READ) == FR_OK) {
        f_read(&f, buf, (UINT)cap - 1, &n);
        f_close(&f);
    }
    buf[n] = 0;
    f_unmount("");
    return n;
}

static bool write_now(const char *text, size_t len) {
    if (!mount()) return false;
    FIL f;
    UINT n = 0;
    bool ok = false;
    // temporary file + rename: a power cut never leaves half a settings file
    if (f_open(&f, CFG_FILE ".NEW", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        ok = f_write(&f, text, (UINT)len, &n) == FR_OK && n == len;
        ok = f_close(&f) == FR_OK && ok;
        if (ok) {
            f_unlink(CFG_FILE);
            ok = f_rename(CFG_FILE ".NEW", CFG_FILE) == FR_OK;
        }
    }
    f_unmount("");
    return ok;
}

bool storage_write_config(const char *text, size_t len) {
    static char buf[STORAGE_CFG_MAX];
    if (len >= sizeof buf) return false;
    memcpy(buf, text, len);
    pending_text = buf;
    pending_len = len;
    if (!box_busy) {
        box_busy = true;
        busy_since = now_ms();
        port_media_changed();
    }
    return true;
}

bool storage_config_reload_due(void) {
    if (reload_flag) { reload_flag = false; return true; }
    return false;
}

void storage_task(void) {
    uint32_t t = now_ms();
    if (box_busy && pending_text && t - busy_since >= HOLD_MS) {
        write_now(pending_text, pending_len);
        pending_text = NULL;
        box_busy = false;
        port_media_changed();
        uint8_t m = 1;   // tell MCU-B too: unit attention on PC 2
        link_send(LM_MSC_RESP, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, m}, 5);
    }
    if (pc_write_ms && t - pc_write_ms >= RELOAD_AFTER_MS) {
        pc_write_ms = 0;
        reload_flag = true;
    }
    static uint32_t det_check;
    if (t - det_check > 500) {   // card inserted / removed
        det_check = t;
        bool present = sd_present();
        if (present && !sd_ready()) { if (sd_init()) { port_media_changed(); reload_flag = true; } }
        else if (!present && sd_ready()) { sd_init(); port_media_changed(); }
    }
}

void storage_init(void) { sd_init(); }

void storage_set_owner(int pc) {
    if (owner == pc) return;
    owner = pc;
    port_media_changed();
}

// ------------------------------------------------------------------ the drive for PC 1 (local)
static bool d_ready(void) { return owner == 0 && !box_busy && !exclusive && sd_ready(); }
static uint32_t d_sectors(void) { return sd_sectors(); }
static int32_t d_read(uint32_t lba, uint32_t off, void *buf, uint32_t n) {
    (void)off;
    return sd_read(lba, buf, n / 512) ? (int32_t)n : -1;
}
static int32_t d_write(uint32_t lba, uint32_t off, const uint8_t *buf, uint32_t n) {
    (void)off;
    pc_write_ms = now_ms();
    return sd_write(lba, buf, n / 512) ? (int32_t)n : -1;
}
static const port_disk_t local_disk = {d_ready, d_sectors, d_read, d_write, NULL};
const port_disk_t *storage_local_disk(void) { return &local_disk; }

// ------------------------------------------------------------------ the drive for PC 2 (requests from MCU-B)
void storage_link_request(const uint8_t *p, size_t len) {
    // u32 tag, u8 op, u32 lba, u16 count, data
    if (len < 11) return;
    uint8_t out[5 + 512];
    memcpy(out, p, 4);
    uint8_t op = p[4];
    uint32_t lba = (uint32_t)p[5] | (uint32_t)p[6] << 8 | (uint32_t)p[7] << 16 | (uint32_t)p[8] << 24;
    bool ready = owner == 1 && !box_busy && !exclusive && sd_ready();
    int8_t st = -1;
    size_t n = 5;
    if (!ready) {
        st = -2;   // not ready
    } else if (op == 0) {
        if (sd_read(lba, out + 5, 1)) { st = 0; n = 5 + 512; }
    } else if (op == 1 && len >= 11 + 512) {
        pc_write_ms = now_ms();
        if (sd_write(lba, p + 11, 1)) st = 0;
    } else if (op == 2) {
        uint32_t s = sd_sectors();
        memcpy(out + 5, &s, 4);
        n = 9;
        st = 0;
    }
    out[4] = (uint8_t)st;
    link_send(LM_MSC_RESP, out, n);
}

// ------------------------------------------------------------------ files used by firmware updates
static FIL wf;
static bool wf_open;
static FIL rf;
static char open_path[64];
static bool ro;
void storage_exclusive(bool on) {
    if (on == exclusive) return;
    exclusive = on;
    if (on) {
        mount();
    } else {
        if (ro) { f_close(&rf); ro = false; }
        if (wf_open) { f_close(&wf); wf_open = false; }
        f_unmount("");
    }
    port_media_changed();
    link_send(LM_MSC_RESP, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 1}, 5);
}


bool storage_file_create(const char *path) {
    if (!exclusive && !mount()) return false;
    f_mkdir("/.kmswitch");
    f_mkdir("/.kmswitch/fw");
    wf_open = f_open(&wf, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
    return wf_open;
}
bool storage_file_append(const void *d, size_t n) {
    UINT w = 0;
    return wf_open && f_write(&wf, d, (UINT)n, &w) == FR_OK && w == n;
}
bool storage_file_close(void) {
    if (!wf_open) return false;
    wf_open = false;
    return f_close(&wf) == FR_OK;
}

bool storage_file_read(const char *path, uint32_t off, void *buf, size_t n, size_t *got) {
    if (!ro || strcmp(open_path, path)) {
        if (ro) f_close(&rf);
        ro = f_open(&rf, path, FA_READ) == FR_OK;
        if (!ro) return false;
        snprintf(open_path, sizeof open_path, "%s", path);
    }
    UINT r = 0;
    if (f_lseek(&rf, off) != FR_OK || f_read(&rf, buf, (UINT)n, &r) != FR_OK) return false;
    *got = r;
    return true;
}

uint32_t storage_file_size(const char *path) {
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK ? (uint32_t)fi.fsize : 0;
}

bool storage_file_rename(const char *from, const char *to) {
    f_unlink(to);
    return f_rename(from, to) == FR_OK;
}
bool storage_file_delete(const char *path) { return f_unlink(path) == FR_OK; }

// an update file the user copied to the root of the drive (way 2): its name, or false
bool storage_find_update(char *name, size_t cap) {
    if (!mount()) return false;
    DIR d;
    FILINFO fi;
    bool found = false;
    if (f_findfirst(&d, &fi, "/", "*.kmsfw") == FR_OK && fi.fname[0]) {
        snprintf(name, cap, "/%s", fi.fname);
        found = true;
    }
    f_closedir(&d);
    if (!exclusive) f_unmount("");
    return found;
}

void storage_log(const char *line) {
    if (!exclusive && !mount()) return;
    FIL f;
    if (f_open(&f, "KMSWITCH.LOG", FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
        UINT w;
        f_write(&f, line, (UINT)strlen(line), &w);
        f_write(&f, "\r\n", 2, &w);
        f_close(&f);
    }
    if (!exclusive) f_unmount("");
}
