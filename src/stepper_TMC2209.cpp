#include "stepper_TMC2209.h"

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>

#define STEP_PERIOD_MIN_TICKS 1U
#define STEP_PULSE_NOPS 10
#define TMC2209_UART_SYNC 0x05U

namespace {

static inline HardwareSerial *toSerial(void *ptr)
{
    return reinterpret_cast<HardwareSerial *>(ptr);
}

static bool uartTransmit(HardwareSerial *serial, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    if (serial == nullptr || buf == nullptr) {
        return false;
    }

    size_t written = serial->write(buf, len);
    serial->flush();
    (void)timeout_ms;
    return written == len;
}

static bool uartReceive(HardwareSerial *serial, uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    if (serial == nullptr || buf == nullptr) {
        return false;
    }

    uint32_t start = millis();
    uint16_t got = 0;
    while (got < len) {
        while (serial->available() > 0 && got < len) {
            int v = serial->read();
            if (v >= 0) {
                buf[got++] = static_cast<uint8_t>(v);
            }
        }

        if ((millis() - start) >= timeout_ms) {
            return false;
        }
    }

    return true;
}

static void TMC2209_EnableMotor(Stepper_Handle_t *handle, bool enable)
{
    digitalWrite(handle->config.en_pin, enable ? LOW : HIGH);
}

static void TMC2209_SetDirection(Stepper_Handle_t *handle, Stepper_Direction_t dir)
{
    digitalWrite(handle->config.dir_pin, (dir == STEPPER_DIR_CW) ? HIGH : LOW);
}

static void TMC2209_DoStepPulse(Stepper_Handle_t *handle)
{
    digitalWrite(handle->config.step_pin, HIGH);
    for (int i = 0; i < STEP_PULSE_NOPS; i++) {
        __asm__ __volatile__("nop");
    }
    digitalWrite(handle->config.step_pin, LOW);
}

static void TMC2209_UpdateStepPeriod(Stepper_Handle_t *handle)
{
    uint32_t denom = static_cast<uint32_t>(handle->config.rpm)
                   * static_cast<uint32_t>(handle->config.full_steps_per_rev)
                   * static_cast<uint32_t>(handle->config.microstep_divider);

    if (denom == 0U) {
        handle->step_period_ticks = UINT32_MAX;
        return;
    }

    uint32_t ticks = 60000UL / denom;
    handle->step_period_ticks = (ticks < STEP_PERIOD_MIN_TICKS) ? STEP_PERIOD_MIN_TICKS : ticks;
}

static uint8_t TMC2209_CalcCRC(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;

    for (uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc >> 7) ^ (byte & 0x01U)) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07U);
            } else {
                crc <<= 1;
            }
            byte >>= 1;
        }
    }

    return crc;
}

}  // namespace

Stepper_Status_t Stepper_Init(Stepper_Handle_t *handle, Stepper_Config_t *config)
{
    if (handle == nullptr || config == nullptr) {
        return STEPPER_ERROR;
    }

    handle->config = *config;
    handle->state = STEPPER_STATE_IDLE;
    handle->direction = STEPPER_DIR_CW;
    handle->current_position = 0;
    handle->target_position = 0;
    handle->logical_position = 0;
    handle->backlash_pending = 0;
    handle->tick_counter = 0;

    TMC2209_UpdateStepPeriod(handle);

    pinMode(config->step_pin, OUTPUT);
    pinMode(config->dir_pin, OUTPUT);
    pinMode(config->en_pin, OUTPUT);

    digitalWrite(config->step_pin, LOW);
    digitalWrite(config->dir_pin, LOW);

    if (config->ms1_pin >= 0 && config->ms2_pin >= 0) {
        pinMode(static_cast<uint8_t>(config->ms1_pin), OUTPUT);
        pinMode(static_cast<uint8_t>(config->ms2_pin), OUTPUT);

        bool ms1 = false;
        bool ms2 = false;
        switch (config->microstep_divider) {
            // TMC2209 standalone STEP/DIR pin modes:
            // MS1/MS2: 00=1/8, 01=1/32, 10=1/64, 11=1/16
            case TMC2209_MICROSTEP_32: ms1 = false; ms2 = true;  break;
            case TMC2209_MICROSTEP_64: ms1 = true;  ms2 = false; break;
            case TMC2209_MICROSTEP_16: ms1 = true;  ms2 = true;  break;
            case TMC2209_MICROSTEP_8:
            default: break;
        }

        digitalWrite(static_cast<uint8_t>(config->ms1_pin), ms1 ? HIGH : LOW);
        digitalWrite(static_cast<uint8_t>(config->ms2_pin), ms2 ? HIGH : LOW);
    }

    TMC2209_EnableMotor(handle, config->hold_when_idle);

    if (toSerial(config->serial) != nullptr) {
        delay(10);
        TMC2209_WriteReg(handle, TMC2209_REG_GCONF, TMC2209_GCONF_PDN_DISABLE | TMC2209_GCONF_MSTEP_REG);
        TMC2209_SetCurrent(handle, TMC2209_IRUN_DEFAULT, TMC2209_IHOLD_DEFAULT);
    }

    return STEPPER_OK;
}

void Stepper_SetSpeed(Stepper_Handle_t *handle, uint16_t rpm)
{
    handle->config.rpm = rpm;
    TMC2209_UpdateStepPeriod(handle);
}

void Stepper_MoveSteps(Stepper_Handle_t *handle, int32_t steps, uint16_t backlash_steps)
{
    if (steps == 0) {
        return;
    }

    Stepper_Direction_t next_direction = (steps > 0) ? STEPPER_DIR_CW : STEPPER_DIR_CCW;
    int32_t effective_steps = steps;

    if (backlash_steps > 0 && next_direction != handle->direction) {
        handle->backlash_pending = backlash_steps;
        effective_steps += (steps > 0) ? (int32_t)backlash_steps : -(int32_t)backlash_steps;
    } else {
        handle->backlash_pending = 0;
    }

    handle->target_position = handle->current_position + effective_steps;
    handle->direction = next_direction;
    TMC2209_SetDirection(handle, handle->direction);

    handle->tick_counter = handle->step_period_ticks;
    handle->state = STEPPER_STATE_RUNNING;
    TMC2209_EnableMotor(handle, true);
}

void Stepper_MoveTo(Stepper_Handle_t *handle, int32_t position)
{
    Stepper_MoveSteps(handle, position - handle->logical_position, 0);
}

bool Stepper_Process(Stepper_Handle_t *handle)
{
    if (handle->state != STEPPER_STATE_RUNNING) {
        return false;
    }

    if (handle->current_position == handle->target_position) {
        handle->state = STEPPER_STATE_IDLE;
        if (!handle->config.hold_when_idle) {
            TMC2209_EnableMotor(handle, false);
        }
        return false;
    }

    handle->tick_counter++;
    if (handle->tick_counter < handle->step_period_ticks) {
        return false;
    }
    handle->tick_counter = 0;

    int8_t dir = (handle->current_position < handle->target_position) ? 1 : -1;
    Stepper_Direction_t needed = (dir > 0) ? STEPPER_DIR_CW : STEPPER_DIR_CCW;
    if (needed != handle->direction) {
        handle->direction = needed;
        TMC2209_SetDirection(handle, handle->direction);
    }

    TMC2209_DoStepPulse(handle);
    handle->current_position += dir;

    if (handle->backlash_pending > 0) {
        handle->backlash_pending--;
    } else {
        handle->logical_position += dir;
    }

    return true;
}

void Stepper_Stop(Stepper_Handle_t *handle)
{
    handle->target_position = handle->current_position;
    handle->state = STEPPER_STATE_IDLE;
    if (!handle->config.hold_when_idle) {
        TMC2209_EnableMotor(handle, false);
    }
}

void Stepper_Release(Stepper_Handle_t *handle)
{
    TMC2209_EnableMotor(handle, false);
}

int32_t Stepper_GetPosition(Stepper_Handle_t *handle)
{
    return handle->logical_position;
}

void Stepper_ResetPosition(Stepper_Handle_t *handle)
{
    handle->current_position = 0;
    handle->target_position = 0;
    handle->logical_position = 0;
    handle->backlash_pending = 0;
}

bool Stepper_IsMoving(Stepper_Handle_t *handle)
{
    return (handle->state == STEPPER_STATE_RUNNING);
}

void Stepper_Beep(Stepper_Handle_t *handle, uint16_t duration_ms, uint16_t frequency_hz)
{
    if (handle == nullptr || duration_ms == 0 || frequency_hz == 0) {
        return;
    }

    uint32_t half_period_us = 500000UL / static_cast<uint32_t>(frequency_hz);
    if (half_period_us == 0) {
        half_period_us = 1;
    }

    uint32_t duration_us = static_cast<uint32_t>(duration_ms) * 1000UL;
    uint32_t toggles = (duration_us + half_period_us - 1U) / half_period_us;
    if (toggles == 0) {
        toggles = 1;
    }

    for (uint32_t i = 0; i < toggles; ++i) {
        TMC2209_EnableMotor(handle, (i & 1U) ? true : false);
        delayMicroseconds(half_period_us);
    }

    digitalWrite(handle->config.step_pin, LOW);

    if (!handle->config.hold_when_idle) {
        TMC2209_EnableMotor(handle, false);
    }
}

Stepper_Status_t TMC2209_WriteReg(Stepper_Handle_t *handle, uint8_t reg, uint32_t data)
{
    HardwareSerial *serial = toSerial(handle->config.serial);
    if (serial == nullptr) {
        return STEPPER_ERROR;
    }

    uint8_t buf[8];
    buf[0] = TMC2209_UART_SYNC;
    buf[1] = handle->config.uart_address;
    buf[2] = static_cast<uint8_t>(reg | 0x80U);
    buf[3] = static_cast<uint8_t>((data >> 24) & 0xFFU);
    buf[4] = static_cast<uint8_t>((data >> 16) & 0xFFU);
    buf[5] = static_cast<uint8_t>((data >> 8) & 0xFFU);
    buf[6] = static_cast<uint8_t>(data & 0xFFU);
    buf[7] = TMC2209_CalcCRC(buf, 7);

    return uartTransmit(serial, buf, 8, 100) ? STEPPER_OK : STEPPER_ERROR;
}

Stepper_Status_t TMC2209_ReadReg(Stepper_Handle_t *handle, uint8_t reg, uint32_t *data)
{
    HardwareSerial *serial = toSerial(handle->config.serial);
    if (serial == nullptr || data == nullptr) {
        return STEPPER_ERROR;
    }

    uint8_t req[4];
    req[0] = TMC2209_UART_SYNC;
    req[1] = handle->config.uart_address;
    req[2] = static_cast<uint8_t>(reg & 0x7FU);
    req[3] = TMC2209_CalcCRC(req, 3);

    if (!uartTransmit(serial, req, 4, 100)) {
        return STEPPER_ERROR;
    }

    uint8_t resp[12];
    if (!uartReceive(serial, resp, 12, 100)) {
        return STEPPER_ERROR;
    }

    uint8_t *r = &resp[4];
    if (TMC2209_CalcCRC(r, 7) != r[7]) {
        return STEPPER_ERROR;
    }

    *data = (static_cast<uint32_t>(r[3]) << 24)
          | (static_cast<uint32_t>(r[4]) << 16)
          | (static_cast<uint32_t>(r[5]) << 8)
          | static_cast<uint32_t>(r[6]);

    return STEPPER_OK;
}

Stepper_Status_t TMC2209_SetCurrent(Stepper_Handle_t *handle, uint8_t irun, uint8_t ihold)
{
    uint32_t reg = (static_cast<uint32_t>(TMC2209_IHOLDDELAY_DEFAULT & 0x0FU) << 16)
                 | (static_cast<uint32_t>(irun & 0x1FU) << 8)
                 | static_cast<uint32_t>(ihold & 0x1FU);

    return TMC2209_WriteReg(handle, TMC2209_REG_IHOLD_IRUN, reg);
}

Stepper_Status_t TMC2209_SetMicrostepUART(Stepper_Handle_t *handle, uint16_t microstep_div)
{
    uint8_t mres;
    switch (microstep_div) {
        case 256: mres = 0; break;
        case 128: mres = 1; break;
        case 64: mres = 2; break;
        case 32: mres = 3; break;
        case 16: mres = 4; break;
        case 8: mres = 5; break;
        case 4: mres = 6; break;
        case 2: mres = 7; break;
        case 1: mres = 8; break;
        default: mres = 5; break;
    }

    uint32_t chopconf = 0x10000053U;
    if (TMC2209_ReadReg(handle, TMC2209_REG_CHOPCONF, &chopconf) != STEPPER_OK) {
        chopconf = 0x10000053U;
    }

    chopconf &= ~(0x0FU << 24);
    chopconf |= (static_cast<uint32_t>(mres) << 24);

    handle->config.microstep_divider = microstep_div;
    TMC2209_UpdateStepPeriod(handle);

    uint32_t gconf;
    if (TMC2209_ReadReg(handle, TMC2209_REG_GCONF, &gconf) == STEPPER_OK) {
        gconf |= TMC2209_GCONF_MSTEP_REG;
        TMC2209_WriteReg(handle, TMC2209_REG_GCONF, gconf);
    }

    return TMC2209_WriteReg(handle, TMC2209_REG_CHOPCONF, chopconf);
}
