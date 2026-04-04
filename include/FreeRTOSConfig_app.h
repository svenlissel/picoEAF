#pragma once

// ---------------------------------------------------------------------------
// FreeRTOS application-level configuration overrides
//
// The earlephilhower RP2040 Arduino core ships a default FreeRTOSConfig.h.
// Place any project-specific overrides HERE so they take effect without
// modifying the core's source tree.
//
// Common values to customise:
//   configTICK_RATE_HZ          - default 1000 (1 ms tick)
//   configMAX_PRIORITIES        - default 32
//   configMINIMAL_STACK_SIZE    - default 256 words
//   configTOTAL_HEAP_SIZE       - default 128 KB
//   configUSE_TIMERS            - software timer support
//   configUSE_TRACE_FACILITY    - enable task-trace helpers
// ---------------------------------------------------------------------------

// Example: increase total heap to 192 KB
// #define configTOTAL_HEAP_SIZE  (192 * 1024)

// Example: enable FreeRTOS runtime stats
// #define configGENERATE_RUN_TIME_STATS   1
// #define configUSE_TRACE_FACILITY        1
// #define configUSE_STATS_FORMATTING_FUNCTIONS 1
