#pragma once

#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_DEBUG 0
#define CFG_TUD_ENABLED 1
#define BOARD_TUD_RHPORT 0
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

#ifdef KMS_ROLE_A
// USB host for the keyboard / mouse / dongle / hub on the USB-A port, through PIO-USB
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#define BOARD_TUH_RHPORT 1
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 5
#define CFG_TUH_HID 12
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#endif

#define CFG_TUD_HID 2                 // keyboard, mouse
#define CFG_TUD_HID_EP_BUFSIZE 16
#define CFG_TUD_MSC 1                 // the shared microSD drive
#define CFG_TUD_MSC_EP_BUFSIZE 512
#define CFG_TUD_CDC 0
#define CFG_TUD_VENDOR 0
