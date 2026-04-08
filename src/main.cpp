#include <Arduino.h>
#include <string.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <OneWire.h>
#include <DallasTemperature.h>

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
static constexpr uint8_t DS18B20_PIN      = 26;
static constexpr uint8_t DS18B20_PULLUP_PIN = 27;

Stepper_Handle_t stepper_motor;
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DeviceAddress ds18_address = {0};

static void printDeviceAddress(const DeviceAddress address)
{
    for (uint8_t i = 0; i < 8; ++i)
    {
        DBG_PRINTF("%02X", address[i]);
    }
}

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

    const TickType_t kSaveAfterIdleTicks = pdMS_TO_TICKS(60 * 1000);
    const TickType_t kPollTicks = pdMS_TO_TICKS(500);

    TickType_t last_motion_tick = xTaskGetTickCount();
    bool was_moving = EAF_isMoving();
    bool saved_for_current_idle = false;

    for (;;)
    {
        bool moving = EAF_isMoving();
        TickType_t now = xTaskGetTickCount();

        if (moving) {
            last_motion_tick = now;
            saved_for_current_idle = false;
        } else {
            // Detect edge: moving -> idle, start idle timer from stop moment.
            if (was_moving) {
                last_motion_tick = now;
                saved_for_current_idle = false;
            }

            if (!saved_for_current_idle && ((now - last_motion_tick) >= kSaveAfterIdleTicks)) {
                EAF_SaveSettings(true);
                saved_for_current_idle = true;
            }
        }

        was_moving = moving;
        vTaskDelay(kPollTicks);
    }
}

static void taskTemperature(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        ds18b20.requestTemperatures();
        float temp_c = ds18b20.getTempCByIndex(0);

        if (temp_c > -100.0f && temp_c < 150.0f)
        {
            int16_t temp_centi = static_cast<int16_t>(temp_c * 100.0f);
            EAF_SetTemperatureCentiDeg(temp_centi);

            int32_t whole = temp_centi / 100;
            int32_t frac = temp_centi % 100;
            if (frac < 0) {
                frac = -frac;
            }
            DBG_PRINTF("[TEMP] DS18B20: %ld.%02ld C\r\n", (long)whole, (long)frac);
        }
        else
        {
            int32_t raw_centi = static_cast<int32_t>(temp_c * 100.0f);
            uint8_t bus_ok = oneWire.reset();
            DBG_PRINTF("[TEMP] DS18B20 read error: raw=%ld centiC, bus=%s\r\n",
                       (long)raw_centi,
                       bus_ok ? "OK" : "MISSING");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
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

    /* set pullup pin high (easier to solder)*/
    pinMode(DS18B20_PULLUP_PIN, OUTPUT);
    digitalWrite(DS18B20_PULLUP_PIN, HIGH);
    
    ds18b20.begin();
    ds18b20.setResolution(12);
    
    DBG_PRINTF("[TEMP] OneWire devices: %u\r\n", ds18b20.getDeviceCount());
    if (ds18b20.getAddress(ds18_address, 0))
    {
        DBG_PRINT("[TEMP] Sensor[0] ROM: ");
        printDeviceAddress(ds18_address);
        DBG_PRINT("\r\n");
        DBG_PRINTF("[TEMP] Family code: 0x%02X\r\n", ds18_address[0]);
    }
    else
    {
        DBG_PRINTLN("[TEMP] No DS18 address found at index 0");
    }

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
    //xTaskCreate(taskBlink,     "Blink",     256,  NULL, 1, NULL);
    xTaskCreate(taskPositionSaver, "PositionSaver", 512,  NULL, 1, NULL);
    xTaskCreate(taskTemperature, "Temperature", 512, NULL, 1, NULL);
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
