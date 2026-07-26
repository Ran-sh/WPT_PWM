/**
 ******************************************************************************
 * @file    Hardware/Inverter_Control.c
 * @brief   逆变器双档软启动与运行期频率斜坡控制 — V5.1.2
 * @note    低档从99.9kHz按100Hz/10ms降频，高档从200kHz按1kHz/10ms降频。
 ******************************************************************************
 */

#include "Inverter_Control.h"
#include "Pwm_Driver.h"
#include "Sys_Timer.h"

static Inverter_Control_Soft_Start_State s_ss_state =
    INVERTER_CONTROL_SS_STATE_IDLE;
static uint32_t s_ss_current_freq = 200000U;
static uint32_t s_ss_requested_freq = 200000U;
static uint32_t s_ss_last_ms = 0U;
static Inverter_Control_Ramp_State s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
static Inverter_Control_Startup_Band s_startup_band =
    INVERTER_CONTROL_STARTUP_HIGH;
static uint32_t s_startup_low_freq = 20000U;
static uint32_t s_startup_high_freq = 100000U;
static uint32_t s_sweep_start_freq = 200000U;
static uint32_t s_sweep_target_freq = 100000U;
static uint32_t s_sweep_step_hz = 1000U;
static uint32_t s_ramp_target = 0U;
static uint32_t s_ramp_last_ms = 0U;

static void Inverter_Control_Set_State_Atomic(
    Inverter_Control_Soft_Start_State new_state)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_ss_state = new_state;
    __set_PRIMASK(primask);
}

static uint8_t Inverter_Control_Is_Startup_Config_Valid(
    Inverter_Control_Startup_Band band, uint32_t low_freq_hz,
    uint32_t high_freq_hz)
{
    if (band != INVERTER_CONTROL_STARTUP_LOW &&
        band != INVERTER_CONTROL_STARTUP_HIGH) return 0U;
    if (low_freq_hz < PWM_DRIVER_FREQ_MIN_HZ ||
        low_freq_hz > 99900U || (low_freq_hz % 100U) != 0U) return 0U;
    if (high_freq_hz < 100000U ||
        high_freq_hz > PWM_DRIVER_FREQ_MAX_HZ ||
        (high_freq_hz % 1000U) != 0U) return 0U;
    return 1U;
}

void Inverter_Control_Configure_Startup(Inverter_Control_Startup_Band band,
                                        uint32_t low_freq_hz,
                                        uint32_t high_freq_hz)
{
    if (Inverter_Control_Is_Startup_Config_Valid(band, low_freq_hz,
                                                  high_freq_hz) == 0U) {
        s_startup_band = INVERTER_CONTROL_STARTUP_HIGH;
        s_startup_low_freq = 20000U;
        s_startup_high_freq = 100000U;
        return;
    }

    s_startup_band = band;
    s_startup_low_freq = low_freq_hz;
    s_startup_high_freq = high_freq_hz;
}

uint32_t Inverter_Control_Get_Sweep_Start_Freq(void)
{
    return s_sweep_start_freq;
}

uint32_t Inverter_Control_Get_Sweep_Target_Freq(void)
{
    return s_sweep_target_freq;
}

void Inverter_Control_Soft_Start_Trigger(void)
{
    if (s_ss_state != INVERTER_CONTROL_SS_STATE_IDLE) return;

    if (s_startup_band == INVERTER_CONTROL_STARTUP_LOW) {
        s_sweep_start_freq = 99900U;
        s_sweep_target_freq = s_startup_low_freq;
        s_sweep_step_hz = 100U;
    } else {
        s_sweep_start_freq = 200000U;
        s_sweep_target_freq = s_startup_high_freq;
        s_sweep_step_hz = 1000U;
    }

    s_ss_requested_freq = s_sweep_start_freq;
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
    s_ss_current_freq = Pwm_Driver_Set_Frequency(s_ss_requested_freq);
    Pwm_Driver_Enable();
    s_ss_last_ms = Sys_Timer_Get_Tick();
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_SWEEP);
}

void Inverter_Control_Soft_Start_Task(void)
{
    if (s_ss_state != INVERTER_CONTROL_SS_STATE_SWEEP) return;
    if (Sys_Timer_Get_Tick() - s_ss_last_ms < SOFTSTART_STEP_MS) return;

    s_ss_last_ms = Sys_Timer_Get_Tick();
    if (s_ss_requested_freq <= s_sweep_target_freq + s_sweep_step_hz) {
        s_ss_requested_freq = s_sweep_target_freq;
        s_ss_current_freq = Pwm_Driver_Set_Frequency(s_ss_requested_freq);
        Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_DONE);
    } else {
        s_ss_requested_freq -= s_sweep_step_hz;
        s_ss_current_freq = Pwm_Driver_Set_Frequency(s_ss_requested_freq);
    }
}

void Inverter_Control_Soft_Start_Stop(void)
{
    Pwm_Driver_Disable();
    s_ss_requested_freq = s_sweep_start_freq;
    s_ss_current_freq = s_sweep_start_freq;
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_IDLE);
}

void Inverter_Control_Soft_Start_Fault(void)
{
    Pwm_Driver_Disable();
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_FAULT);
}

void Inverter_Control_Soft_Start_Reset(void)
{
    Pwm_Driver_Disable();
    s_ss_requested_freq = s_sweep_start_freq;
    s_ss_current_freq = s_sweep_start_freq;
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_IDLE);
}

Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void)
{
    return s_ss_state;
}

uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void)
{
    return s_ss_current_freq;
}

void Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz)
{
    if (target_hz < PWM_DRIVER_FREQ_MIN_HZ) target_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (target_hz > PWM_DRIVER_FREQ_MAX_HZ) target_hz = PWM_DRIVER_FREQ_MAX_HZ;

    s_ramp_target = target_hz;
    s_ramp_state = INVERTER_CONTROL_RAMP_ACTIVE;
    s_ramp_last_ms = Sys_Timer_Get_Tick();
}

void Inverter_Control_Freq_Ramp_Task(void)
{
    uint32_t current;
    int32_t diff;

    if (s_ramp_state == INVERTER_CONTROL_RAMP_IDLE) return;

    current = Pwm_Driver_Get_Frequency();
    diff = (int32_t)current - (int32_t)s_ramp_target;
    if (diff >= -(int32_t)FREQ_RAMP_STEP_HZ &&
        diff <= (int32_t)FREQ_RAMP_STEP_HZ) {
        s_ss_current_freq = Pwm_Driver_Set_Frequency(s_ramp_target);
        s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
        return;
    }

    if (Sys_Timer_Get_Tick() - s_ramp_last_ms < FREQ_RAMP_STEP_MS) return;

    s_ramp_last_ms = Sys_Timer_Get_Tick();
    if (current < s_ramp_target) {
        current += FREQ_RAMP_STEP_HZ;
        if (current > s_ramp_target) current = s_ramp_target;
    } else {
        current -= FREQ_RAMP_STEP_HZ;
        if (current < s_ramp_target) current = s_ramp_target;
    }
    s_ss_current_freq = Pwm_Driver_Set_Frequency(current);
}

void Inverter_Control_Freq_Ramp_Cancel(void)
{
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
}
