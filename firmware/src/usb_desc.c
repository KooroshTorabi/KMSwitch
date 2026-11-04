// USB device descriptors, the same on MCU-A (towards PC 1) and MCU-B (towards PC 2).
// Interfaces: 0 boot keyboard, 1 mouse (boot; report ID 1 relative, 2 absolute), 2 mass storage (microSD).
#include <string.h>
#include "tusb.h"
#include "board.h"
#include "usb_desc.h"
#include "pico/unique_id.h"
#include "pico/time.h"

uint8_t const desc_hid_kbd[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

uint8_t const desc_hid_mouse[] = {
    // report 1: relative mouse (games, BIOS-like behaviour)
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RID_MOUSE_REL,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
    // report 2: absolute pointer, 0..32767 over the PC's whole desktop
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RID_MOUSE_ABS,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
};

static bool net_on = true;

static tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,           // interface association descriptors (network function)
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 2,
};

uint8_t const *tud_descriptor_device_cb(void) {
    desc_device.bNumConfigurations = net_on ? 2 : 1;
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
    return itf == ITF_KBD ? desc_hid_kbd : desc_hid_mouse;
}

#define EP_KBD 0x81
#define EP_MOUSE 0x82
#define EP_MSC_OUT 0x03
#define EP_MSC_IN 0x83
#define EP_NET_NOTIF 0x84
#define EP_NET_OUT 0x05
#define EP_NET_IN 0x85
#define BASE_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN)
#define BASE_ITFS                                                                                              \
    TUD_HID_DESCRIPTOR(ITF_KBD, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_kbd), EP_KBD, 8, 1),            \
    TUD_HID_DESCRIPTOR(ITF_MOUSE, 5, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_mouse), EP_MOUSE, 16, 1),        \
    TUD_MSC_DESCRIPTOR(ITF_MSC, 6, EP_MSC_OUT, EP_MSC_IN, 64)

// keyboard first (interface 0) for old BIOSes; configuration 1 has RNDIS (Windows), configuration 2 CDC-ECM
// (macOS, Linux); without the network a single configuration
static uint8_t const desc_rndis[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, BASE_LEN + TUD_RNDIS_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    BASE_ITFS,
    TUD_RNDIS_DESCRIPTOR(ITF_NET, 7, EP_NET_NOTIF, 8, EP_NET_OUT, EP_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE),
};
static uint8_t const desc_ecm[] = {
    TUD_CONFIG_DESCRIPTOR(2, ITF_COUNT, 0, BASE_LEN + TUD_CDC_ECM_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    BASE_ITFS,
    TUD_CDC_ECM_DESCRIPTOR(ITF_NET, 7, 8, EP_NET_NOTIF, 64, EP_NET_OUT, EP_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE,
                           CFG_TUD_NET_MTU),
};
static uint8_t const desc_plain[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NET, 0, BASE_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    BASE_ITFS,
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    if (!net_on) return index == 0 ? desc_plain : NULL;
    return index == 0 ? desc_rndis : index == 1 ? desc_ecm : NULL;
}

bool usb_network_enabled(void) { return net_on; }

void usb_set_network(bool on) {
    if (on == net_on) return;
    net_on = on;
    if (tud_inited()) {   // the PC must enumerate the box again
        tud_disconnect();
        sleep_ms(300);
        tud_connect();
    }
}

static char const *strings[] = {
    NULL, "kmswitch", "kmswitch keyboard/mouse switch", NULL /* serial */, "kmswitch keyboard", "kmswitch mouse",
    "kmswitch drive", "kmswitch settings network", NULL /* MAC */,
};

static uint16_t desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t n;
    if (index == 0) {
        desc_str[1] = 0x0409;
        n = 1;
    } else {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 3];
        const char *s;
        if (index == 8) {   // CDC-ECM MAC address string
            static const char hex[] = "0123456789ABCDEF";
            for (int i = 0; i < 6; i++) {
                serial[2 * i] = hex[tud_network_mac_address[i] >> 4];
                serial[2 * i + 1] = hex[tud_network_mac_address[i] & 15];
            }
            serial[12] = 0;
            s = serial;
        } else if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof serial - 2);
            size_t k = strlen(serial);
#ifdef KMS_ROLE_A
            serial[k] = 'A';
#else
            serial[k] = 'B';
#endif
            serial[k + 1] = 0;
            s = serial;
        } else if (index < sizeof strings / sizeof strings[0]) {
            s = strings[index];
        } else {
            return NULL;
        }
        n = strlen(s);
        if (n > 32) n = 32;
        for (size_t i = 0; i < n; i++) desc_str[1 + i] = (uint8_t)s[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
