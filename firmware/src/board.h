// Pins and build variants. Built four times: KMS_BOARD 'A' or 'B', KMS_ROLE_A (MCU-A, PC 1) or not (MCU-B, PC 2).
#pragma once

#ifndef KMS_BOARD
#define KMS_BOARD 'A'
#endif
#ifndef KMS_VERSION
#define KMS_VERSION "dev"
#endif

// USB host (MCU-A only): PIO-USB, D+ on GP2, D- on GP3
#define PIN_USBH_DP 2
// UART1 through the isolator
#define PIN_UART_TX 4
#define PIN_UART_RX 5
#define LINK_UART uart1
// microSD on SPI1 (MCU-A only)
#define PIN_SD_SCK 14
#define PIN_SD_MOSI 15
#define PIN_SD_MISO 28
#define PIN_SD_CS 29
#define PIN_SD_DET 26          // card detect switch to GND (variant B card socket; variant A: same pin, same socket)
#define SD_SPI spi1
// external RGB LED header (WS2812 / SK6812) on MCU-A
#define PIN_RGB_EXT 6
// variant B: VBUS of PC 1 through a 10k / 20k divider (MCU-A)
#define PIN_VBUS_SENSE 24

// status LED
#if KMS_BOARD == 'A'
#define PIN_LED_WS2812 16      // RP2040-Zero on-board WS2812
#else
#define PIN_LED_R 16           // variant B: discrete red / green / blue LEDs, active high
#define PIN_LED_G 17
#define PIN_LED_B 18
#endif

#define USB_VID 0x1209         // pid.codes open-source VID
#define USB_PID 0x4B4D         // "KM" (to be registered at pid.codes)
