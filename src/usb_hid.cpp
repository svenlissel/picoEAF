#include "usb_hid.h"
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "debug.h"
#include "eaf.h"


// Vendor ID and Product ID
#define USBD_VID                0x3C3
#define USBD_PID                0x1F10

// Device strings (USB Language ID: English US 0x0409)
#define USBD_MANUFACTURER_STR   "ZWO"
#define USBD_PRODUCT_STR        "ZWO Device"
#define USBD_CONFIG_STR         "EAF Config"
#define USBD_INTERFACE_STR      "EAF Interface"
// ---------------------------------------------------------------------------
// Custom HID Report Descriptor (ZWO EAF - 68 bytes)
// Vendor-defined device with 4 report IDs (1-4), 15 bytes each
// ---------------------------------------------------------------------------
const uint8_t HID_REPORT_DESCRIPTOR[] = {
    // Usage Page (Vendor Defined 0xFF00)
    0x06, 0x00, 0xFF,
    // Usage (0x01)
    0x09, 0x01,
    // Collection (Application)
    0xA1, 0x01,

    // Report ID 1 - Input (15 bytes)
    0x85, 0x01,        // Report ID (1)
    0x95, 0x0F,        // Report Count (15 bytes)
    0x75, 0x08,        // Report Size (8 bits)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x15, 0x00,        // Logical Minimum (0)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0x81, 0x02,        // Input (Data,Var,Abs)

    // Report ID 2 - Input (15 bytes)
    0x85, 0x02,        // Report ID (2)
    0x95, 0x0F,        // Report Count (15 bytes)
    0x75, 0x08,        // Report Size (8 bits)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x15, 0x00,        // Logical Minimum (0)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0x81, 0x02,        // Input (Data,Var,Abs)

    // Report ID 3 - Output (15 bytes)
    0x85, 0x03,        // Report ID (3)
    0x95, 0x0F,        // Report Count (15 bytes)
    0x75, 0x08,        // Report Size (8 bits)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x15, 0x00,        // Logical Minimum (0)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0x91, 0x02,        // Output (Data,Var,Abs)

    // Report ID 4 - Output (15 bytes)
    0x85, 0x04,        // Report ID (4)
    0x95, 0x0F,        // Report Count (15 bytes)
    0x75, 0x08,        // Report Size (8 bits)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x15, 0x00,        // Logical Minimum (0)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0x91, 0x02,        // Output (Data,Var,Abs)

    // End Collection
    0xC0
};

// ---------------------------------------------------------------------------
// Static USB HID instance and buffers
// ZWO EAF format: 16 bytes per report (1 byte report ID + 15 bytes data)
// ---------------------------------------------------------------------------
static Adafruit_USBD_HID usb_hid;
static QueueHandle_t hid_rx_queue = NULL;
static uint8_t usb_hid_get_report_buffer[20] = {0};
static uint16_t usb_hid_get_report_len = 0;

// ---------------------------------------------------------------------------
// Adafruit TinyUSB HID callbacks
// ---------------------------------------------------------------------------

// HID Get Report Callback (Host <- Device), is called after every set command, 
// data prepared from set command before
static uint16_t usb_hid_get_report_cb(uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t* buffer, uint16_t reqlen)
{
    (void)report_id;
    (void)report_type;

    if (buffer == NULL || reqlen == 0) {
        return 0;
    }

    uint16_t out_len = (reqlen < usb_hid_get_report_len) ? reqlen : usb_hid_get_report_len;
    if (out_len > 0) {
        memcpy(buffer, usb_hid_get_report_buffer, out_len);
    }
    return out_len;
}

// HID Set Report Callback (Host -> Device data arrival)
static void usb_hid_set_report_cb(uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t const* buffer, uint16_t buflen)
{
    //DBG_PRINTF("HID RX report_id=%u, report_type=%u, buflen=%u\r\n", report_id, report_type,(unsigned)buflen);
    
    usb_hid_get_report_len = EAF_parse_report(report_id, report_type, buffer, buflen, usb_hid_get_report_buffer);
}

// ---------------------------------------------------------------------------
// Public API Implementation
// ---------------------------------------------------------------------------
void usb_hid_init(void)
{
    // Start the USB stack
    TinyUSBDevice.begin(0);
    // Set USB Device ID (VID/PID) - must be before setReportDescriptor()
    TinyUSBDevice.setID(0x3C3, 0x1F10);
    //TinyUSBDevice.setLanguageDescriptor(uint16_t language_id);
    TinyUSBDevice.setManufacturerDescriptor(USBD_MANUFACTURER_STR);
    TinyUSBDevice.setProductDescriptor(USBD_PRODUCT_STR);
    //TinyUSBDevice.setSerialDescriptor(const char *s);

    // Configure the USB HID device with our custom report descriptor
    usb_hid.setPollInterval(2);   // Poll every 2 ms
    usb_hid.setReportDescriptor(HID_REPORT_DESCRIPTOR, sizeof(HID_REPORT_DESCRIPTOR));
    usb_hid.setStringDescriptor(USBD_INTERFACE_STR);
    usb_hid.setReportCallback(usb_hid_get_report_cb, usb_hid_set_report_cb);
    usb_hid.begin();

    // If already enumerated, additional class driverr begin() e.g msc, hid won't take effect until re-enumeration
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }

    // Wait for enumeration
    delay(100);
}

bool usb_hid_is_mounted(void)
{
    return TinyUSBDevice.mounted();
}
