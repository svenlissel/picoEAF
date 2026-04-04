#include "usb_hid.h"
#include "usb_descriptors.h"
#include <Adafruit_TinyUSB.h>
#include <string.h>

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
static uint8_t hid_tx_buffer[16];  // 1 byte Report ID + 15 bytes data
static uint8_t hid_rx_buffer[16];  // 1 byte Report ID + 15 bytes data
static uint8_t hid_rx_report_id = 0;  // Track which report ID was received
static QueueHandle_t hid_rx_queue = NULL;

// ---------------------------------------------------------------------------
// TinyUSB HID Callbacks (weak, can be overridden by user)
// ---------------------------------------------------------------------------

// HID Output Report Callback (Host → Device)
__weak uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t* buffer, uint16_t reqlen)
{
    // Not used for OUT reports; we handle those in set_report_cb
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    return 0;
}

// HID Set Report Callback (Host → Device data arrival)
__weak void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t const* buffer, uint16_t buflen)
{
    // Store the report ID
    hid_rx_report_id = report_id;

    // Store the received data in our RX buffer (max 16 bytes including report ID)
    // Note: buffer is the data without report ID; we prepend it
    hid_rx_buffer[0] = report_id;
    uint16_t len = (buflen < (sizeof(hid_rx_buffer) - 1)) ? buflen : (sizeof(hid_rx_buffer) - 1);
    memcpy(hid_rx_buffer + 1, buffer, len);

    // Signal the HID RX task via queue
    if (hid_rx_queue != NULL)
    {
        uint8_t msg = report_id;  // Pass the report ID itself
        xQueueSendFromISR(hid_rx_queue, &msg, NULL);
    }

    (void)itf;
    (void)report_type;
}

// ---------------------------------------------------------------------------
// USB Device Descriptor (ZWO EAF Configuration)
// Managed by Adafruit TinyUSB Library - we only override HID callbacks
// ---------------------------------------------------------------------------

// Note: Device descriptors (VID/PID/Strings) are set in platformio.ini via:
// -D USB_VID=0x3C3
// -D USB_PID=0x1F10
// String descriptors are configured in usb_descriptors.h

// ---------------------------------------------------------------------------
// Public API Implementation
// ---------------------------------------------------------------------------

void usb_hid_init(void)
{
    // Create RX queue early (before callbacks can fire)
    hid_rx_queue = xQueueCreate(4, sizeof(uint8_t));
    configASSERT(hid_rx_queue != NULL);

    // Start the USB stack
    TinyUSBDevice.begin(0);
    // Set USB Device ID (VID/PID) - must be before setReportDescriptor()
    TinyUSBDevice.setID(0x3C3, USBD_PID);
    //TinyUSBDevice.setLanguageDescriptor(uint16_t language_id);
    TinyUSBDevice.setManufacturerDescriptor(USBD_MANUFACTURER_STR);
    TinyUSBDevice.setProductDescriptor(USBD_PRODUCT_STR);
    //TinyUSBDevice.setSerialDescriptor(const char *s);

    // Configure the USB HID device with our custom report descriptor
    usb_hid.setPollInterval(2);   // Poll every 2 ms
    usb_hid.setReportDescriptor(HID_REPORT_DESCRIPTOR, sizeof(HID_REPORT_DESCRIPTOR));
    usb_hid.setStringDescriptor(USBD_INTERFACE_STR);
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

bool usb_hid_send_report(uint8_t report_id, const uint8_t *data, uint16_t len)
{
    if (!TinyUSBDevice.mounted())
        return false;

    // Validate report ID (1-4 for ZWO EAF)
    if (report_id < 1 || report_id > 4)
        return false;

    // Prepare TX buffer with report ID and data
    hid_tx_buffer[0] = report_id;
    uint16_t copy_len = (len < (sizeof(hid_tx_buffer) - 1)) ? len : (sizeof(hid_tx_buffer) - 1);
    if (data != NULL && copy_len > 0)
    {
        memcpy(hid_tx_buffer + 1, data, copy_len);
    }

    // Pad with zeros if needed
    if (copy_len < (sizeof(hid_tx_buffer) - 1))
    {
        memset(hid_tx_buffer + 1 + copy_len, 0, sizeof(hid_tx_buffer) - 1 - copy_len);
    }

    // Send the HID report (first byte is report ID)
    usb_hid.sendReport(report_id, hid_tx_buffer, sizeof(hid_tx_buffer));
    return true;
}

QueueHandle_t usb_hid_get_rx_queue(void)
{
    return hid_rx_queue;
}

uint8_t *usb_hid_get_rx_buffer(void)
{
    return hid_rx_buffer;
}

uint8_t usb_hid_get_rx_report_id(void)
{
    return hid_rx_report_id;
}

bool usb_hid_is_mounted(void)
{
    return TinyUSBDevice.mounted();
}
