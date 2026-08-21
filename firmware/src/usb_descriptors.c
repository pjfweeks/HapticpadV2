/* usb_descriptors.c - USB identity and interface layout. */

#include <string.h>

#include "board.h"
#include "hid.h"

#include "pico/unique_id.h"
#include "tusb.h"

/* TinyUSB's example VID/PID pair, which is explicitly reserved for testing
 * and development.  Swap both for your own allocation before selling one. */
#define USB_VID 0xCAFE
#define USB_PID 0x4010

#define USB_BCD 0x0200

/* --- device descriptor -------------------------------------------------- */

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    /* Composite device: the class is declared per-interface, and the IAD in
     * the configuration descriptor groups the two CDC interfaces together. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0200,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

/* --- HID report descriptor ---------------------------------------------- */

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HP_REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE   (HID_REPORT_ID(HP_REPORT_ID_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(HP_REPORT_ID_CONSUMER)),
};

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

/* --- configuration descriptor ------------------------------------------- */

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#define EPNUM_HID_IN     0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    /* 1 ms polling: macro steps and wheel ticks reach the host as fast as the
     * host is willing to ask for them. */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID_IN,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* --- string descriptors -------------------------------------------------- */

static char s_serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *const string_desc[] = {
    (const char[]){ 0x09, 0x04 },   /* 0: English (US) */
    "HapticPad",                    /* 1: manufacturer */
    HP_FW_NAME " " HP_FW_VERSION,   /* 2: product      */
    s_serial,                       /* 3: serial       */
    "HapticPad Config",             /* 4: CDC          */
    "HapticPad Controls",           /* 5: HID          */
};

static uint16_t s_desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    if (index == 3 && s_serial[0] == '\0') {
        /* Derived from the flash chip's unique ID so two pads on the same
         * machine are always distinguishable. */
        pico_get_unique_board_id_string(s_serial, sizeof s_serial);
    }

    size_t chars;
    if (index == 0) {
        memcpy(&s_desc_str[1], string_desc[0], 2);
        chars = 1;
    } else {
        if (index >= sizeof string_desc / sizeof string_desc[0]) return NULL;

        const char *str = string_desc[index];
        chars = strlen(str);
        if (chars > sizeof s_desc_str / sizeof s_desc_str[0] - 1) {
            chars = sizeof s_desc_str / sizeof s_desc_str[0] - 1;
        }
        for (size_t i = 0; i < chars; i++) s_desc_str[i + 1] = (uint16_t)str[i];
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chars + 2));
    return s_desc_str;
}
