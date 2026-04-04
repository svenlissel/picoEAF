#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Debug output via UART0 (Serial on GPIO 0/1)
// Thread-safe for FreeRTOS (Serial.print is generally safe)
// ---------------------------------------------------------------------------

#define DEBUG_BAUDRATE 115200

// Enable/disable debug output
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
  #define DBG_INIT()          Serial1.begin(DEBUG_BAUDRATE);
  #define DBG_PRINTLN(x)      Serial1.println(x)
  #define DBG_PRINT(x)        Serial1.print(x)
  #define DBG_PRINTF(fmt,...) Serial1.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_INIT()          do {} while (0)
  #define DBG_PRINTLN(x)      do {} while (0)
  #define DBG_PRINT(x)        do {} while (0)
  #define DBG_PRINTF(fmt,...) do {} while (0)
#endif

// Helper: print task name and tick count
static inline void dbg_task_header(const char *task_name)
{
    #if DEBUG_ENABLED
    Serial.printf("[%8lu] [%s] ", (unsigned long)xTaskGetTickCount(), task_name);
    #endif
}
