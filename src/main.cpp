#include <Arduino.h>
#include <string.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

// USB HID interface
#include "usb_hid.h"
#include "debug.h"
#include "eaf.h"

// ---------------------------------------------------------------------------
// FreeRTOS task queue for inter-task communication
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Task: blink the on-board LED (heartbeat indicator)
// ---------------------------------------------------------------------------
static void taskBlink(void *pvParameters)
{
    (void)pvParameters;
    pinMode(LED_BUILTIN, OUTPUT);

    for (;;)
    {
        // Check if host mounted the HID device
        bool is_mounted = usb_hid_is_mounted();

        // Blink pattern: fast if mounted, slow otherwise
        if (is_mounted)
        {
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(400));
        }
        else
        {
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(500));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// ---------------------------------------------------------------------------
// Task: periodically send heartbeat data via HID (Report ID 1 - Input)
// ---------------------------------------------------------------------------
static void taskHeartbeat(void *pvParameters)
{
    (void)pvParameters;

    // Wait until the USB host enumerates us
    while (!usb_hid_is_mounted())
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// ---------------------------------------------------------------------------
// setup() – runs once on core 0 inside the FreeRTOS idle task context
// ---------------------------------------------------------------------------
void setup()
{
    // Initialize UART0 debug output (115200 baud)
    DBG_INIT();
    DBG_PRINTLN("\n========================================");
    DBG_PRINTF("picoEAF - version " __DATE__ " " __TIME__ "\n\r");
    DBG_PRINTLN("========================================\n");

    // Initialize USB HID device (handles all USB setup)
    DBG_PRINTLN("Initializing USB HID device...");
    usb_hid_init();


    // Spawn FreeRTOS tasks
    xTaskCreate(taskBlink,     "Blink",     256,  NULL, 1, NULL);
    xTaskCreate(taskHeartbeat, "Heartbeat", 512,  NULL, 1, NULL);

    DBG_PRINT("Device ready - waiting for USB host...\n");

    // Note: do NOT call vTaskStartScheduler() – the earlephilhower core
    // starts the FreeRTOS SMP scheduler automatically before setup() runs.


    EAF_Init();
}

// ---------------------------------------------------------------------------
// loop() – runs continuously on core 0 as a regular FreeRTOS task
// ---------------------------------------------------------------------------
void loop()
{
    // Yield to other tasks; add application logic here as needed.
    vTaskDelay(pdMS_TO_TICKS(100));
}
