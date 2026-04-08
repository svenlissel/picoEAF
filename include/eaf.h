/**
 * @file eaf.h
 * @brief EAF HID Device Header
 * @author 
 * @date 2026-04-04
 */

#ifndef __EAF_H
#define __EAF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported defines ----------------------------------------------------------*/
#define EAF_HID_REPORT_SIZE     16
#define EAF_MAX_POSITION        100000
#define EAF_MIN_POSITION        0

/* EAF Protocol Magic Bytes */
#define EAF_MAGIC_1             0x7E
#define EAF_MAGIC_2             0x5A
#define EAF_REPORT_ID           0x03
#define EAF_RESPONSE_MARKER     0x01

/* EAF Command IDs (discovered from USB capture) */
#define EAF_CMD_SET_POSITION    0x01  // Set absolute position
#define EAF_CMD_GET_POSITION    0x03  // Get current position + status
#define EAF_CMD_GET_INFO        0x04  // Get device info (FW version + name)
#define EAF_CMD_GET_SERIAL      0x0C  // Get 8-byte serial number
#define EAF_CMD_CUSTOM_NAME     0x0D  // Get custom Name
#define EAF_CMD_GET_MAX_POS     0x10  // Get max position (uint32_t)
#define EAF_CMD_GET_TEMP        0x12  // Get temperature (uint16_t, in 0.01°C)
#define EAF_CMD_MOVE_REL        0x13  // Move relative (int32_t)
#define EAF_CMD_UNKNOWN_1F      0x1F  // Unknown (returns 0x01)
#define EAF_CMD_STOP            0x20  // Stop motor
#define EAF_CMD_RESET_POS       0x21  // Reset position to zero

/* EAF Firmware Version */
#define EAF_FW_MAJOR            3
#define EAF_FW_MINOR            8
#define EAF_FW_PATCH            1


#define EAF_DEVICE_NAME "EEAFN"
#define EAF_CUSTOM_NAME "picoEAF"


/* Exported types ------------------------------------------------------------*/
typedef struct __attribute__((packed)) {
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    char device_name[9];  // 4 characters + null terminator
} EAF_DeviceInfo;

#if 0
26bit
   00 dc 00 00 0e 35 00 7d 78 f3 fa 00
24bit
   00 dc 09 00 0e 35 00 7d 78 f3 27 c0
moving
   00 dc 09 00 17 1e 00 7e 40 f3 27 c0

#endif

/* max step (position) is a 24bit value slittet into 4 bits high + 16 bits low */

typedef struct __attribute__((packed)) {
    uint8_t status;          // Byte 0: 0x00=idle, 0x01=moving
    uint8_t backlash;          // Byte 1: Motor backlash (0xD3=211 observed)
    uint8_t max_high;            // Byte 2: 0x00=idle, 0x01=moving
    uint8_t position[3];       // Bytes 3-5: Current position (24-bit Big-Endian!)
    uint8_t reserved;          // Byte 6: Reserved (0x00)
    uint16_t temperature;      // Bytes 7-8: Temperature (Big-Endian, (leading3, temp in 0.01°C / 10 = °C)
    uint8_t flags;             // Byte 9: Flags (0xF0 base, bit0=beep, bit1=reverse)
    uint16_t max_low;          // Bytes 10-11: max_steps[15:0] (16-bit Big-Endian!)
} EAF_PositionResponse;  // Total: 12 bytes payload


//stop command: 
//DE 00 00 0D 05 02 00 00 F2 EA 60 00 before
//DE 00 00 00 00 00 00 00 F2 EA 60 00 

typedef struct __attribute__((packed)) {
    uint8_t backlash;           // Byte 0: Motor backlash (0xDC=220 observed)
    uint8_t max_high;           // Byte 1: High nibble=?, Low nibble=max_steps[19:16] (0x09 for 599999)
    uint8_t position[3];        // Bytes 2-4: Target position (24-bit Big-Endian!)
    uint8_t overwrite_currPosition;          // Byte 5: is 0x00: do not overwrite current position, 0x01: overwrite
    uint8_t reserved4;          // Byte 6: Reserved (0x00)
    uint8_t reserved5;          // Byte 7: Reserved (0x00)
    uint8_t flags;              // Byte 8: Flags (0xF0 base, bit0=beep, bit1=reverse)
    uint16_t max_low;           // Bytes 9-10: max_steps[15:0] (16-bit Big-Endian!)
    uint8_t reserved11;         // Byte 11: Reserved
} EAF_SetSettingsRequest;  // Total: 12 bytes payload

typedef struct {
    uint32_t current_position;  // 32-bit position (little-endian)
    uint32_t target_position;
    uint32_t max_position;
    uint8_t backlash;
    int16_t temperature;        // Temperature in 0.01°C units
    uint8_t status;             // 0x00=idle, 0x01=moving
    uint8_t beep_enabled;       // 0=off, 1=on
    uint8_t reverse_enabled;    // 0=off, 1=on
    uint8_t serial_number[8];   // 8-byte serial number
    uint8_t custom_name[8];
} EAF_HandleTypeDef;

/* warning, never change this values! */
typedef struct __attribute__((packed)) {
    uint8_t version; 
    uint32_t current_position;  // 32-bit position (little-endian)
    uint32_t max_position;
    uint8_t backlash;
    uint8_t beep_enabled;       // 0=off, 1=on
    uint8_t reverse_enabled;    // 0=off, 1=on
    uint8_t serial_number[8];   // 8-byte serial number
    uint8_t custom_name[8];
    uint8_t reserved[4];
} EAF_StorageTypeDef;

#ifdef __cplusplus
static_assert(sizeof(EAF_StorageTypeDef) == 32, "EAF_StorageTypeDef size must be 32 bytes");
#else
_Static_assert(sizeof(EAF_StorageTypeDef) == 32, "EAF_StorageTypeDef size must be 32 bytes");
#endif

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/
/* Exported function prototypes ---------------------------------------------*/

/**
 * @brief Initialize EAF HID Device
 * @retval None
 */
void EAF_Init(void);

/**
 * @brief HID Report Received Callback
 * @param report: Pointer to the received report
 * @param len: Report length
 * @retval None
 */
void EAF_HID_ReportReceived(uint8_t* report, uint8_t len);


uint8_t EAF_parse_report(uint8_t report_id, uint8_t report_type, uint8_t const* data, uint8_t len, uint8_t* answereBuf);

/**
 * @brief Persist current EAF settings/state to storage
 * @retval 1 on success, 0 on error
 */
uint8_t EAF_SaveSettings(bool withPosition);

/**
 * @brief Update current position (call this in main loop)
 * @retval None
 */
void EAF_UpdatePosition(void);

bool EAF_isMoving(void);

void EAF_SetTemperatureCentiDeg(int16_t temperature_centi_deg);

#ifdef __cplusplus
}
#endif

#endif /* __EAF_PROXY_H */