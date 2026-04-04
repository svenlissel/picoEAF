#pragma once

#include <stdint.h>
#include <FreeRTOS.h>
#include <queue.h>

// ---------------------------------------------------------------------------
// USB HID Interface
// Handles all TinyUSB HID device functionality and callbacks
// ---------------------------------------------------------------------------

typedef struct {
	uint8_t report_id;
	uint8_t report_type;
	uint16_t buflen;
	uint8_t data[16];  // Raw TinyUSB callback payload bytes (unmodified)
} usb_hid_rx_message_t;

// Initialize USB HID device
// Must be called during setup() before creating tasks
void usb_hid_init(void);

// Send a HID report with the specified report ID (1-4 for ZWO EAF)
// Data is up to 15 bytes (report ID + data = 16 bytes total)
// Returns true if successful, false if device not mounted or invalid report ID
bool usb_hid_send_report(uint8_t report_id, const uint8_t *data, uint16_t len);

// Get the HID RX queue handle (used by receive task)
// Signaled whenever the host sends data to the device
QueueHandle_t usb_hid_get_rx_queue(void);

// Get the HID RX buffer (where received data is stored - 16 bytes)
// Buffer format: [byte0=report_id][bytes1-15=data]
uint8_t *usb_hid_get_rx_buffer(void);

// Check if USB device is mounted by the host
bool usb_hid_is_mounted(void);

bool usb_hid_set_report_answere(const uint8_t *data, uint16_t len);
