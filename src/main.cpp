#include <Arduino.h>
#include <string.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

// USB HID interface
#include "usb_hid.h"

// Debug output (UART0)
#include "debug.h"

// ---------------------------------------------------------------------------
// FreeRTOS task queue for inter-task communication
// ---------------------------------------------------------------------------
static QueueHandle_t xMsgQueue;

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
    uint32_t tick = 0;
    uint8_t report_data[15];  // 15 bytes data (report ID is added by usb_hid_send_report)

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
// Task: receive and echo HID reports from the host
// Receives from Report IDs 3-4 (Output), echoes back on Report IDs 1-2 (Input)
// ---------------------------------------------------------------------------
static void taskHidRx(void *pvParameters)
{
    (void)pvParameters;
    uint8_t report_id;
    uint8_t echo_data[15];  // 15 bytes data (without report ID)

    // Get the HID RX queue from the USB HID module
    QueueHandle_t rx_queue = usb_hid_get_rx_queue();

    for (;;)
    {
        // Wait for data from the HID RX callback (receives report ID via queue)
        if (xQueueReceive(rx_queue, &report_id, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // Get the received data (includes report ID at byte 0)
            uint8_t *rx_buffer = usb_hid_get_rx_buffer();

            // Echo back on corresponding Input report ID
            // Input: 1-2, Output: 3-4 -> map 3->1, 4->2
            uint8_t tx_report_id = (report_id <= 2) ? report_id : (report_id - 2);

            // Copy the data (skip the report ID byte at position 0)
            memcpy(echo_data, rx_buffer + 1, sizeof(echo_data));

            // Add an echo marker at the end
            echo_data[14] = 0xAA;

            // Send it back with the corresponding input report ID
            if (usb_hid_is_mounted())
            {
                usb_hid_send_report(tx_report_id, echo_data, sizeof(echo_data));
            }

            // Signal that we got data
            xQueueSend(xMsgQueue, &report_id, 0);
        }
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

    // Create the message queue for inter-task signaling
    xMsgQueue = xQueueCreate(10, sizeof(uint8_t));
    configASSERT(xMsgQueue != NULL);

    // Spawn FreeRTOS tasks
    xTaskCreate(taskBlink,     "Blink",     256,  NULL, 1, NULL);
    xTaskCreate(taskHeartbeat, "Heartbeat", 512,  NULL, 1, NULL);
    xTaskCreate(taskHidRx,     "HidRx",     512,  NULL, 2, NULL);

    DBG_PRINT("Device ready - waiting for USB host...\n");

    // Note: do NOT call vTaskStartScheduler() – the earlephilhower core
    // starts the FreeRTOS SMP scheduler automatically before setup() runs.
}

// ---------------------------------------------------------------------------
// loop() – runs continuously on core 0 as a regular FreeRTOS task
// ---------------------------------------------------------------------------
void loop()
{
    // Yield to other tasks; add application logic here as needed.
    vTaskDelay(pdMS_TO_TICKS(100));
}
