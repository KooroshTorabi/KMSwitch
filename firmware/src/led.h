#pragma once
#include <stdint.h>

enum { LED_STEADY = 0, LED_BLINK_FAST, LED_BLINK_SLOW, LED_FLASH_TWICE };

// colours used by both MCUs
#define LED_ACTIVE 0, 90, 255      // this PC has keyboard and mouse
#define LED_IDLE 0, 0, 24          // connected, the other PC is active
#define LED_ORANGE 255, 90, 0      // firmware update: waiting for Win+F8 / installing
#define LED_WHITE 200, 200, 200    // measuring screens
#define LED_RED 255, 0, 0

void led_init(void);
void led_set(uint8_t pattern, uint8_t r, uint8_t g, uint8_t b);
void led_task(void);
