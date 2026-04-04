#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// USB Device Descriptor Configuration (ZWO EAF)
// ---------------------------------------------------------------------------

// ZWO Vendor ID and Product ID
#define USBD_VID                0x3C3
#define USBD_PID                0x1F10

// Device strings (USB Language ID: English US 0x0409)
#define USBD_MANUFACTURER_STR   "ZWO"
#define USBD_PRODUCT_STR        "ZWO Device"
#define USBD_CONFIG_STR         "EAF Config"
#define USBD_INTERFACE_STR      "EAF Interface"

// String descriptor indices
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CONFIG,
    STRID_INTERFACE
};

