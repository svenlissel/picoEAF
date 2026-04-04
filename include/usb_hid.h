#pragma once

#include <stdint.h>
#include <FreeRTOS.h>
#include <queue.h>

// ---------------------------------------------------------------------------
// USB HID Interface
// Handles all TinyUSB HID device functionality and callbacks
// ---------------------------------------------------------------------------

// Initialize USB HID device
// Must be called during setup() before creating tasks
void usb_hid_init(void);

// Check if USB device is mounted by the host
bool usb_hid_is_mounted(void);
