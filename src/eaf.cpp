/**
 * @file eaf.c
 * @brief EAF HID Device Implementation
 * @author 
 * @date 2025-11-25
 */

#include "eaf.h"
#include <string.h>
#include "debug.h"
#include "storage.h"
#ifdef EAF_USE_EXTERNAL_STEPPER
#include "stepper_TMC2209.h"
#endif


/* Private variables */
static EAF_HandleTypeDef heaf = {0};
static uint8_t hid_responseBuffer[16];
static constexpr uint8_t EAF_STORAGE_VERSION = 1;


/* Optional stepper backend.
 * If you have a real stepper module, compile with -DEAF_USE_EXTERNAL_STEPPER
 * and provide these symbols from your platform code.
 */
#ifdef EAF_USE_EXTERNAL_STEPPER
extern Stepper_Handle_t stepper_motor;
#else
typedef struct {
    int32_t position;
    uint8_t moving;
} Stepper_Handle_t;

static Stepper_Handle_t stepper_motor = {0};

static void Stepper_MoveSteps(Stepper_Handle_t* motor, int32_t delta_steps)
{
    motor->position += delta_steps;
    motor->moving = 0;
}

static int32_t Stepper_GetPosition(Stepper_Handle_t* motor)
{
    return motor->position;
}

static uint8_t Stepper_IsMoving(Stepper_Handle_t* motor)
{
    return motor->moving;
}

static void Stepper_Beep(Stepper_Handle_t* motor, uint16_t duration_ms, uint16_t freq_hz)
{
    (void)motor;
    (void)duration_ms;
    (void)freq_hz;
}
#endif

/* Private function prototypes */
static void EAF_ProcessCommand(uint8_t type, uint8_t cmd, const uint8_t* params, uint8_t* response);

uint8_t EAF_SaveSettings(bool withPosition)
{
    EAF_StorageTypeDef persisted = {0};
    uint8_t raw[STORAGE_DATA_LEN] = {0};

    if(true == withPosition)
    {
        persisted.current_position = heaf.current_position;
    }
    else
    {
        // Load persisted settings/state from storage if valid.
        uint8_t raw[STORAGE_DATA_LEN] = {0};
        if (storage_load(raw)) 
        {
            EAF_StorageTypeDef eepromPersisted = {0};
            memcpy(&eepromPersisted, raw, sizeof(EAF_StorageTypeDef));
            persisted.current_position = eepromPersisted.current_position;
        }
    }

    persisted.version = EAF_STORAGE_VERSION;
    persisted.max_position = heaf.max_position;
    persisted.backlash = heaf.backlash;
    persisted.beep_enabled = heaf.beep_enabled;
    persisted.reverse_enabled = heaf.reverse_enabled;
    memcpy(persisted.serial_number, heaf.serial_number, sizeof(persisted.serial_number));
    memcpy(persisted.custom_name, heaf.custom_name, sizeof(persisted.custom_name));

    memcpy(raw, &persisted, sizeof(persisted));
    if (!storage_save(raw)) {
        DBG_PRINTLN("EAF storage unchanged");
        return 0;
    }
    else
    {
        DBG_PRINTLN("EAF storage saved");
    }

    return 1;
}

/**
 * @brief Initialize EAF HID Device
 */
void EAF_Init(void)
{
    /* default values */
    heaf.current_position = 0;
    heaf.target_position = heaf.current_position ;
    heaf.max_position = 60000;
    heaf.backlash = 222;
    heaf.beep_enabled = 0;
    heaf.reverse_enabled = 0;
    heaf.temperature = 2500;
    uint8_t serial[8] = {0x61, 0x00, 0x20, 0x67, 0xEA, 0x3C, 0x66, 0x26};
    memcpy(heaf.serial_number, serial, 8);
    memcpy(heaf.custom_name, EAF_CUSTOM_NAME, sizeof(EAF_CUSTOM_NAME));


    // Load persisted settings/state from storage if valid.
    uint8_t raw[STORAGE_DATA_LEN] = {0};
    if (storage_init() && storage_load(raw)) {
        EAF_StorageTypeDef persisted = {0};
        memcpy(&persisted, raw, sizeof(EAF_StorageTypeDef));

        if (persisted.version == EAF_STORAGE_VERSION) {
            heaf.current_position = persisted.current_position;
            heaf.target_position = persisted.current_position;
            heaf.max_position = persisted.max_position;
            heaf.backlash = persisted.backlash;
            heaf.beep_enabled = persisted.beep_enabled;
            heaf.reverse_enabled = persisted.reverse_enabled;
            memcpy(heaf.serial_number, persisted.serial_number, sizeof(heaf.serial_number));
            memcpy(heaf.custom_name, persisted.custom_name, sizeof(heaf.custom_name));
            DBG_PRINTLN("EAF storage loaded");
        } else {
            EAF_SaveSettings(true);
            DBG_PRINTLN("EAF storage initialized");
        }
    }
}

/**
 * @brief HID Report Received Callback
 * @param report: Pointer to the received report (16 bytes)
 * @param len: Report length
 * 
 * Request Format: [03] [7E] [5A] [type] [CMD] [PARAMS...]
 * 
 * NOTE: The 'report' buffer is the USB device's Report_buf, so we can write
 * the response directly into it for GET_REPORT to retrieve.
 */
uint8_t EAF_parse_report(uint8_t report_id, uint8_t report_type, uint8_t const* data, uint8_t len, uint8_t* answereBuf)
{
    /* print data */
    #if 0
    DBG_PRINTF("\r\n[EAF] RX Report (%d bytes): ", len);
    for (uint8_t i = 0; i < len && i < 16; i++) {
        DBG_PRINTF("%02X ", report[i]);
    }
    DBG_PRINTF("\r\n");
    #endif

    /* check report ID and Magic codes */
    if (len < 4) {
        DBG_PRINTF("[EAF] ERROR: Packet too short\r\n");
        return 0;
    }

    uint8_t type = data[2];
    uint8_t command = data[3];
    EAF_ProcessCommand(type, command, &data[4], answereBuf);

    #if 0
    DBG_PRINTF("[EAF] TX Response: ");
    for (uint8_t i = 0; i < 16; i++) {
        DBG_PRINTF("%02X ", report[i]);
    }
    DBG_PRINTF("\r\n");
    #endif

    return 16;
}

/**
 * @brief Process EAF Commands
 * @param cmd: Command byte
 * @param params: Parameter bytes (11 bytes)
 * @param response: Response buffer to fill (16 bytes)
 * 
 */
static void EAF_ProcessCommand(uint8_t type, uint8_t cmd, uint8_t const* params, uint8_t* response)
{
    uint8_t* pResponseParam =  &response[3];
    memset(pResponseParam, 0, 12);      // Clear param bytes, maximum 12 bytes!
    
    /* Initialize response header (report ID and Response marker (0x01) included by stack before) */
    response[0] = EAF_MAGIC_1;          // Magic: 0x7E ~
    response[1] = EAF_MAGIC_2;          // Magic: 0x5A Z
    response[2] = cmd;                  // Echo command

    if(2==type) /* get parameter commands */
    {
    switch (cmd) {
        case EAF_CMD_GET_POSITION: {
            //DBG_PRINTF("[EAF] -> GET_POSITION: %lu, Status: 0x%02X\r\n", 
             //           heaf.current_position, heaf.status);
            
            // Response: 12 bytes payload (16 total with 4-byte header 01 7E 5A 03)
            // Position: 24-bit Big-Endian at bytes 3-5
            // Temperature: Big-Endian uint16 at bytes 7-8 (encode as 30000 + temp*10)
            // Max steps: Big-Endian uint16 at bytes 10-11 (0xEA60 = 60000)
            
            // Temperature: heaf.temperature is in 0.01°C, convert to encoding format
            // Formula: 30000 + (temp_in_0.01C / 10) = 30000 + temp_in_0.1C
            uint16_t temp_encoded = 30000 + (heaf.temperature);
            uint16_t temp_be = ((temp_encoded & 0xFF) << 8) | ((temp_encoded >> 8) & 0xFF);
            
            // Flags: bit0=beep, bit1=reverse, base=0xF0
            uint8_t flags = 0xF0 | (heaf.beep_enabled & 0x01) | ((heaf.reverse_enabled & 0x01) << 1);
            
            EAF_PositionResponse pos_resp = {0};
            pos_resp.status = heaf.status;       // Status: 0x00=idle, 0x01=moving
            pos_resp.backlash = heaf.backlash;   // Backlash value (211 = 0xD3)
            pos_resp.max_high = static_cast<uint8_t>((heaf.max_position >> 16) & 0x0F);
            pos_resp.position[0] = static_cast<uint8_t>((heaf.current_position >> 16) & 0xFF);
            pos_resp.position[1] = static_cast<uint8_t>((heaf.current_position >> 8) & 0xFF);
            pos_resp.position[2] = static_cast<uint8_t>(heaf.current_position & 0xFF);
            pos_resp.reserved = 0x00;
            pos_resp.temperature = temp_be;      // Big-Endian! Temperature encoded
            pos_resp.flags = flags;              // Bit0=beep, Bit1=reverse
            pos_resp.max_low = static_cast<uint16_t>(((heaf.max_position & 0xFFu) << 8) |
                                                     ((heaf.max_position >> 8) & 0xFFu));
            //memcpy(pResponseParam, &pos_resp, sizeof(EAF_PositionResponse));
            memcpy(pResponseParam, &pos_resp, 12);
            break;
        }
            
        case EAF_CMD_GET_INFO: { //0x04
                const EAF_DeviceInfo device_info = {
                    .fw_major = EAF_FW_MAJOR,
                    .fw_minor = EAF_FW_MINOR,
                    .fw_patch = EAF_FW_PATCH,
                    .device_name = EAF_DEVICE_NAME
                };
            DBG_PRINTF("[EAF] -> GET_INFO type%d cmd%d: FW %d.%d.%d, Name: %s\r\n",
                        type, cmd, device_info.fw_major, device_info.fw_minor, device_info.fw_patch, device_info.device_name);
            memcpy(pResponseParam, &device_info, sizeof(EAF_DeviceInfo));
            break;
        }
            
        case EAF_CMD_GET_SERIAL: //0x0C
            DBG_PRINTF("[EAF] -> GET_SERIAL type%d cmd%d: ", type, cmd);
            for (uint8_t i = 0; i < 8; i++) {
                DBG_PRINTF("%02X", heaf.serial_number[i]);
            }
            DBG_PRINTF("\r\n");

            memcpy(pResponseParam, heaf.serial_number, 8);
            break;

        case EAF_CMD_CUSTOM_NAME:
            DBG_PRINTF("[EAF] -> GET_CUSTOM_NAME type%d cmd%d, Custom: %s\r\n", type, cmd, heaf.custom_name);
            memcpy(pResponseParam, heaf.custom_name, sizeof(heaf.custom_name));
            break;
            
        case EAF_CMD_UNKNOWN_1F:
            DBG_PRINTF("[EAF] UNKNOWN_1F type%d cmd%d: Returning 0x01\r\n", type, cmd);
            DBG_PRINTF("[EAF] RX Report: ");
            for (uint8_t i = 0; i < 12; i++) {
                DBG_PRINTF("%02X ", params[i]);
            }
            DBG_PRINTF("\r\n");
            // Response: 0x01
            pResponseParam[0] = 0x01;
            break;

        default:
            DBG_PRINTF("[EAF] WARNING: Unknown get type%d, cmd %d\r\n", type, cmd);
            DBG_PRINTF("[EAF] RX Report: ");
            for (uint8_t i = 0; i < 12; i++) {
                DBG_PRINTF("%02X ", params[i]);
            }
            DBG_PRINTF("\r\n");
            // Unknown command - return response header only
            break;
    }


    }
    else if(3==type) /* set parameter commands */
    {
        // Parse SET_SETTINGS request (12 bytes)
        EAF_SetSettingsRequest* pSettings = (EAF_SetSettingsRequest*)params;
        // Extract position (24-bit Big-Endian!)
        uint32_t targetPosition = (pSettings->position[0] << 16) | (pSettings->position[1] << 8) | pSettings->position[2];
        // Extract flags
        uint8_t beep = pSettings->flags & 0x01;
        uint8_t reverse = (pSettings->flags >> 1) & 0x01;
        // Extract max_steps (Split-Field: low nibble of byte 1 [bits 19:16] + bytes 9-10 [bits 15:0])
        uint32_t max_high = pSettings->max_high & 0x0F;  // Low nibble only
        uint16_t max_low_be = pSettings->max_low;  // Big-Endian!
        uint16_t max_low = ((max_low_be & 0xFF) << 8) | ((max_low_be >> 8) & 0xFF);  // Convert to LE
        uint32_t max_steps = (max_high << 16) | max_low;
        
        DBG_PRINTF("[EAF] <- SET_SETTINGS type%d, cmd %d: pos=%lu, backlash=%u, beep=%u, reverse=%u, max=%lu\r\n", 
                    type, cmd, targetPosition, pSettings->backlash, beep, reverse, max_steps);
        switch (cmd) {
            case 0x01: { /* set target position and start motor */
                // Apply settings
                //heaf.backlash = pSettings->backlash;
                //heaf.beep_enabled = beep;
                heaf.reverse_enabled = reverse;
                //heaf.max_position = max_steps;
                heaf.target_position = targetPosition;
                heaf.status = 0x01;  // start motor
                
                // Calculate steps to move (target - current)
                int32_t delta_steps = (int32_t)targetPosition - (int32_t)heaf.current_position;
                if (heaf.reverse_enabled) {
                    delta_steps = -delta_steps;
                }
                
                // Move stepper motor
                Stepper_MoveSteps(&stepper_motor, delta_steps);

                break;
            }

            case 0x00:  /* set current position and params and do not turn motor */
                if(heaf.status == 0x00)
                {
                    // Apply settings only when motor is not moving
                    heaf.backlash = pSettings->backlash;
                    heaf.beep_enabled = beep;
                    heaf.reverse_enabled = reverse;
                    heaf.max_position = max_steps;

                    if(pSettings->overwrite_currPosition) /* own flag for overwriting current position */
                    {
                        heaf.current_position = targetPosition;
                    }

                    EAF_SaveSettings(false);
                }
                else 
                {
                    heaf.status = 0x00;  // stop motor only
                    Stepper_Stop(&stepper_motor);
                }
                break;
                
            default:
                DBG_PRINTF("[EAF] WARNING: Unknown set Parameter type%d, cmd %d\r\n", type, cmd);
                DBG_PRINTF("[EAF] RX Report: ");
                for (uint8_t i = 0; i < 12; i++) {
                    DBG_PRINTF("%02X ", params[i]);
                }
                DBG_PRINTF("\r\n");
                // Unknown command - return response header only
                break;
        }

    }
    else if(13==type) /* set custom name */
    {
        /* cmc already contains first character */
        heaf.custom_name[0] = cmd;
        memcpy(&heaf.custom_name[1], params, 7);
        DBG_PRINTF("[EAF] <- SET_CUSTOM_NAME type%d: %s\r\n",type, heaf.custom_name);
        EAF_SaveSettings(false);
    }
    else
    {
        DBG_PRINTF("[EAF] WARNING: Unknowntype command type%d, cmd %d\r\n", type, cmd);
        DBG_PRINTF("[EAF] RX Report: ");
        for (uint8_t i = 0; i < 12; i++) {
            DBG_PRINTF("%02X ", params[i]);
        }
        DBG_PRINTF("\r\n");
    }


}

/**
 * @brief Update current position (call this in main loop)
 */
void EAF_UpdatePosition(void)
{
    // Get actual position from stepper motor
    static int32_t last_stepper_pos = 0;
    int32_t current_stepper_pos = Stepper_GetPosition(&stepper_motor);
    
    // Update EAF position based on stepper movement
    int32_t stepper_delta = current_stepper_pos - last_stepper_pos;
    if (heaf.reverse_enabled) {
        stepper_delta = -stepper_delta;
    }
    heaf.current_position += stepper_delta;
    last_stepper_pos = current_stepper_pos;
    
    // Update status based on motor state
    if (Stepper_IsMoving(&stepper_motor)) {
        heaf.status = 0x01;  // Moving
    } else {
        if (heaf.status == 0x01) {
            // Just stopped - optional completion beep
            if (heaf.beep_enabled) {
//                Stepper_Beep(&stepper_motor, 50, 2000);
            }
        }
        heaf.status = 0x00;  // Idle
    }
}

bool EAF_isMoving(void)
{
    return heaf.status = 0x01;  // Moving
}
