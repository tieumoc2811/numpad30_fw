#include <string.h>
#include "pico/unique_id.h"
#include "tusb.h"
#include "usb_descriptors.h"

// VID 0xCAFE là VID thử nghiệm của TinyUSB, chỉ dùng cá nhân.
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCAFE,
    .idProduct          = 0x4030,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
};

uint8_t const desc_hid_cfg[] = { TUD_HID_REPORT_DESC_GENERIC_INOUT(32) };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_MIDI_DESC_LEN)
#define EPNUM_HID        0x81

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                       EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 5),
    // Giao diện 1: HID thô để app cấu hình
    TUD_HID_INOUT_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_cfg),
                             0x02, 0x82, 32, 5),
    // Giao diện 2-3: USB-MIDI
    TUD_MIDI_DESCRIPTOR(2, 0, 0x03, 0x83, 64),
};

static char const *string_desc[] = {
    NULL,
    "Numpad30",   // 1: manufacturer
    "Numpad30",   // 2: product
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return instance == 0 ? desc_hid_report : desc_hid_cfg;
}

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t n;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

    if (index == 0) {
        desc_str[1] = 0x0409;
        n = 1;
    } else {
        const char *s;
        if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof serial);
            s = serial;
        } else if (index < 3) {
            s = string_desc[index];
        } else {
            return NULL;
        }
        n = (uint8_t)strlen(s);
        if (n > 31) n = 31;
        for (uint8_t i = 0; i < n; i++) desc_str[1 + i] = s[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
