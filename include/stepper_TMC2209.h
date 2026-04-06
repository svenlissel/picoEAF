/**
 ******************************************************************************
 * @file    stepper_TMC2209.h
 * @brief   Driver for TMC2209 stepper motor driver IC (STEP/DIR interface)
 ******************************************************************************
 */

#ifndef __STEPPER_TMC2209_H
#define __STEPPER_TMC2209_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Microstep divisors
#define TMC2209_MICROSTEP_1      1U
#define TMC2209_MICROSTEP_2      2U
#define TMC2209_MICROSTEP_4      4U
#define TMC2209_MICROSTEP_8      8U
#define TMC2209_MICROSTEP_16     16U
#define TMC2209_MICROSTEP_32     32U
#define TMC2209_MICROSTEP_64     64U
#define TMC2209_MICROSTEP_128    128U
#define TMC2209_MICROSTEP_256    256U

// TMC2209 UART register addresses
#define TMC2209_REG_GCONF        0x00U
#define TMC2209_REG_GSTAT        0x01U
#define TMC2209_REG_IHOLD_IRUN   0x10U
#define TMC2209_REG_TPOWERDOWN   0x11U
#define TMC2209_REG_TSTEP        0x12U
#define TMC2209_REG_TCOOLTHRS    0x14U
#define TMC2209_REG_CHOPCONF     0x6CU
#define TMC2209_REG_PWMCONF      0x70U
#define TMC2209_REG_SGTHRS       0x40U

// GCONF bits
#define TMC2209_GCONF_PDN_DISABLE   (1U << 6)
#define TMC2209_GCONF_MSTEP_REG     (1U << 7)
#define TMC2209_GCONF_SPREADCYCLE   (1U << 2)

// Default currents (0..31)
#define TMC2209_IRUN_DEFAULT        20U
#define TMC2209_IHOLD_DEFAULT       8U
#define TMC2209_IHOLDDELAY_DEFAULT  6U

// Minimal status type for cross-platform compatibility
typedef enum {
    STEPPER_OK = 0,
    STEPPER_ERROR = 1
} Stepper_Status_t;

typedef enum {
    STEPPER_DIR_CW = 1,
    STEPPER_DIR_CCW = -1
} Stepper_Direction_t;

typedef enum {
    STEPPER_STATE_IDLE = 0,
    STEPPER_STATE_RUNNING,
    STEPPER_STATE_ERROR
} Stepper_State_t;

// NOTE:
// - Pins are Arduino GPIO numbers.
// - Optional pins can be set to -1.
// - serial can be NULL when UART register access is not used.
typedef struct {
    uint8_t step_pin;
    uint8_t dir_pin;
    uint8_t en_pin;

    int8_t ms1_pin;
    int8_t ms2_pin;

    uint16_t full_steps_per_rev;
    uint16_t microstep_divider;
    uint16_t rpm;
    bool hold_when_idle;

    void *serial;
    uint8_t uart_address;
} Stepper_Config_t;

typedef struct {
    Stepper_Config_t config;

    Stepper_State_t state;
    Stepper_Direction_t direction;

    int32_t current_position;
    int32_t target_position;

    uint32_t step_period_ticks;
    uint32_t tick_counter;
} Stepper_Handle_t;

Stepper_Status_t Stepper_Init(Stepper_Handle_t *handle, Stepper_Config_t *config);
void Stepper_SetSpeed(Stepper_Handle_t *handle, uint16_t rpm);
void Stepper_MoveSteps(Stepper_Handle_t *handle, int32_t steps);
void Stepper_MoveTo(Stepper_Handle_t *handle, int32_t position);
bool Stepper_Process(Stepper_Handle_t *handle);
void Stepper_Stop(Stepper_Handle_t *handle);
void Stepper_Release(Stepper_Handle_t *handle);
int32_t Stepper_GetPosition(Stepper_Handle_t *handle);
void Stepper_ResetPosition(Stepper_Handle_t *handle);
bool Stepper_IsMoving(Stepper_Handle_t *handle);
void Stepper_Beep(Stepper_Handle_t *handle, uint16_t duration_ms, uint16_t frequency_hz);

Stepper_Status_t TMC2209_WriteReg(Stepper_Handle_t *handle, uint8_t reg, uint32_t data);
Stepper_Status_t TMC2209_ReadReg(Stepper_Handle_t *handle, uint8_t reg, uint32_t *data);
Stepper_Status_t TMC2209_SetCurrent(Stepper_Handle_t *handle, uint8_t irun, uint8_t ihold);
Stepper_Status_t TMC2209_SetMicrostepUART(Stepper_Handle_t *handle, uint16_t microstep_div);

#ifdef __cplusplus
}
#endif

#endif
