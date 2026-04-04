#include "usb_hid.h"
#include "usb_descriptors.h"
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "debug.h"
#include "eaf.h"

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
static uint8_t hid_get_report_data[16] = {0};
static uint16_t hid_get_report_len = 0;

// ---------------------------------------------------------------------------
// Adafruit TinyUSB HID callbacks
// ---------------------------------------------------------------------------

// HID Get Report Callback (Host <- Device)
static uint16_t usb_hid_get_report_cb(uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t* buffer, uint16_t reqlen)
{
    (void)report_id;
    (void)report_type;

    if (buffer == NULL || reqlen == 0) {
        return 0;
    }

    uint16_t out_len = (reqlen < hid_get_report_len) ? reqlen : hid_get_report_len;
    if (out_len > 0) {
        memcpy(buffer, hid_get_report_data, out_len);
    }
    return out_len;
}

// HID Set Report Callback (Host -> Device data arrival)
static void usb_hid_set_report_cb(uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t const* buffer, uint16_t buflen)
{
    uint8_t anwereSize;
    uint8_t answereBuf[16];
    DBG_PRINTF("HID RX packet: report_id=%u, report_type=%u, buflen=%u\r\n",
                report_id,
                report_type,
                (unsigned)buflen);
    
#if 0
    // Build full EAF packet expected by EAF_HID_ReportReceived().
    // rx_msg.data is raw TinyUSB payload without report ID.
    memset(eaf_packet, 0, sizeof(eaf_packet));
    eaf_packet[0] = rx_msg.report_id;
    uint16_t payload_len = (rx_msg.buflen < 15u) ? rx_msg.buflen : 15u;
    memcpy(&eaf_packet[1], rx_msg.data, payload_len);
    // Process EAF command in place (fills response packet)
    EAF_HID_ReportReceived(eaf_packet, sizeof(eaf_packet));
#endif

    anwereSize = EAF_parse_report(report_id, report_type, buffer, buflen, answereBuf);

    // prepare response payload (bytes 1..15). Report ID is provided separately.
    usb_hid_set_report_answere(answereBuf, anwereSize);
}
bool usb_hid_set_report_answere(const uint8_t *data, uint16_t len)
{
    if (data == NULL) {
        return false;
    }

    hid_get_report_len = (len < sizeof(hid_get_report_data)) ? len : sizeof(hid_get_report_data);
    memcpy(hid_get_report_data, data, hid_get_report_len);
    return true;
}

bool usb_hid_send_report(uint8_t report_id, const uint8_t *data, uint16_t len)
{
    if (TinyUSBDevice.mounted()) {
        DBG_PRINTF("usb_hid_send_report: report_id=%u buflen=%u ",
            (unsigned)report_id,
            (unsigned)len);
        for (uint16_t i = 0; i < len; i++) {
            DBG_PRINTF("%02X ", data[i]);
        }
        DBG_PRINT("\r\n");

        // Keep last sent payload available for GET_REPORT requests.
        usb_hid_set_report_answere(data, len);

        usb_hid.sendReport(report_id, data, len);
        
        return true;
    }
    else
    {
        return false;
    }
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
    hid_rx_queue = xQueueCreate(4, sizeof(usb_hid_rx_message_t));
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

QueueHandle_t usb_hid_get_rx_queue(void)
{
    return hid_rx_queue;
}



bool usb_hid_is_mounted(void)
{
    return TinyUSBDevice.mounted();
}
