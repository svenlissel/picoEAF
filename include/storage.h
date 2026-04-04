#pragma once

#include <stdint.h>

#define STORAGE_DATA_LEN 32

// Initialize EEPROM emulation area.
// Must be called once before load/save.
bool storage_init(void);

// Load STORAGE_DATA_LEN bytes from EEPROM emulation into out_data.
// Returns false if out_data is null.
bool storage_load(uint8_t out_data[STORAGE_DATA_LEN]);

// Save STORAGE_DATA_LEN bytes to EEPROM emulation.
// Returns true only when bytes changed and commit was performed successfully.
// Returns false when nothing changed or on error.
bool storage_save(const uint8_t in_data[STORAGE_DATA_LEN]);
