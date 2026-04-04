#include "storage.h"

#include <Arduino.h>
#include <EEPROM.h>

namespace {
constexpr int kStorageSize = 64;
constexpr int kDataLen = STORAGE_DATA_LEN;
constexpr int kBaseAddr = 0;
}  // namespace

bool storage_init(void)
{
    EEPROM.begin(kStorageSize);
    return true;
}

bool storage_load(uint8_t out_data[STORAGE_DATA_LEN])
{
    if (out_data == nullptr) {
        return false;
    }

    for (int i = 0; i < kDataLen; ++i) {
        out_data[i] = EEPROM.read(kBaseAddr + i);
    }
    return true;
}

bool storage_save(const uint8_t in_data[STORAGE_DATA_LEN])
{
    if (in_data == nullptr) {
        return false;
    }

    bool changed = false;
    for (int i = 0; i < kDataLen; ++i) {
        uint8_t old_value = EEPROM.read(kBaseAddr + i);
        if (old_value != in_data[i]) {
            EEPROM.write(kBaseAddr + i, in_data[i]);
            changed = true;
        }
    }

    if (!changed) {
        // No flash write happened.
        return false;
    }

    return EEPROM.commit();
}
