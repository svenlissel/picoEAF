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
#include "stepper_TMC2209.h"

// ---------------------------------------------------------------------------
// Stepper configuration (Pico GPIO numbers)
// Adjust these pins to your board wiring.
// ---------------------------------------------------------------------------
static constexpr uint8_t STEPPER_PIN_EN   = 8;
static constexpr int8_t  STEPPER_PIN_MS1  = 9;
static constexpr int8_t  STEPPER_PIN_MS2  = 10;
static constexpr uint8_t STEPPER_PIN_STEP = 14;
static constexpr uint8_t STEPPER_PIN_DIR  = 15;

Stepper_Handle_t stepper_motor;

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
static void taskPositionSaver(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(60*1000));
        if(false == EAF_isMoving())
        {
            EAF_SaveSettings(true);
        }
    }
}

// ---------------------------------------------------------------------------
// Task: stepper timing engine (1 ms tick)
// ---------------------------------------------------------------------------
static void taskStepper(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        Stepper_Process(&stepper_motor);
        EAF_UpdatePosition();
        vTaskDelay(pdMS_TO_TICKS(1));
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

    // Configure and initialize TMC2209 stepper backend for EAF.
    Stepper_Config_t stepper_cfg = {
        .step_pin = STEPPER_PIN_STEP,
        .dir_pin = STEPPER_PIN_DIR,
        .en_pin = STEPPER_PIN_EN,
        .ms1_pin = STEPPER_PIN_MS1,
        .ms2_pin = STEPPER_PIN_MS2,
        .full_steps_per_rev = 200,
        .microstep_divider = TMC2209_MICROSTEP_8,
        .rpm = 10,
        .hold_when_idle = false,
        .serial = nullptr,
        .uart_address = 0,
    };

    if (Stepper_Init(&stepper_motor, &stepper_cfg) != STEPPER_OK)
    {
        DBG_PRINTLN("Stepper init failed");
    }
    else
    {
        DBG_PRINTLN("Stepper init OK");
    }


    // Spawn FreeRTOS tasks
    xTaskCreate(taskBlink,     "Blink",     256,  NULL, 1, NULL);
    xTaskCreate(taskPositionSaver, "PositionSaver", 512,  NULL, 1, NULL);
    xTaskCreate(taskStepper,   "Stepper",   512,  NULL, 2, NULL);

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
