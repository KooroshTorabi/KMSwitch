// The USB device port towards this MCU's own PC: keyboard, mouse and drive. Same code on MCU-A and MCU-B.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void port_init(void);
void port_task(void);                         // call often: sends pending reports
bool port_mounted(void);
bool port_suspended(void);
uint8_t port_kbd_leds(void);                  // Num / Caps / Scroll lock from this PC

void port_kbd(const uint8_t rep[8]);          // boot keyboard report: mods, reserved, 6 keys
void port_mouse_abs(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel, int8_t pan);
void port_mouse_rel(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan);
void port_release_all(void);
bool port_idle(void);                         // nothing pending (all reports sent)

// drive backend (MCU-A: the microSD card; MCU-B: requests to MCU-A over the link)
typedef struct {
    bool (*ready)(void);                      // medium present for this PC
    uint32_t (*sectors)(void);
    // return bytes done (multiple of 512), 0 = busy (call again later), -1 = error
    int32_t (*read)(uint32_t lba, uint32_t offset, void *buf, uint32_t n);
    int32_t (*write)(uint32_t lba, uint32_t offset, const uint8_t *buf, uint32_t n);
    void (*flush)(void);
} port_disk_t;
void port_set_disk(const port_disk_t *d);
void port_media_changed(void);                // the PC must re-read the drive (unit attention)
