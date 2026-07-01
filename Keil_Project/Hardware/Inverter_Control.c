/**
 ******************************************************************************
 * @file    Hardware/Inverter_Control.c
/** @brief 逆变器控制 - 软启动状态机 + 频率斜坡 (V4.3.2) */

#include "Inverter_Control.h"
#include "Pwm_Driver.h"
#include "Sys_Timer.h"

/* ═══════════════════════════════════════════════════════════════
 *  软启动状态机
 * ═══════════════════════════════════════════════════════════════ */

static Inverter_Control_Soft_Start_State s_ss_state        = INVERTER_CONTROL_SS_STATE_IDLE;
static uint32_t s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
static uint32_t s_ss_last_ms      = 0;
static Inverter_Control_Ramp_State s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;

static void Inverter_Control_Set_State_Atomic(Inverter_Control_Soft_Start_State new_state)
{
    s_ss_state = new_state;
    /* Cortex-M3 对 32-bit 对齐存储保证单指令原子写入, Get_State 无需禁用 IRQ */
}

/** @brief 触发软启动: IDLE -> SWEEP, 频率从 150kHz 向下斜坡 */
void Inverter_Control_Soft_Start_Trigger(void)
{
    if (s_ss_state != INVERTER_CONTROL_SS_STATE_IDLE) return;

    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
    s_ramp_state       = INVERTER_CONTROL_RAMP_IDLE;
    Pwm_Driver_Set_Frequency(s_ss_current_freq);
    Pwm_Driver_Enable();
    s_ss_last_ms = Sys_Timer_Get_Tick();
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_SWEEP);
}

/** @brief 软启动周期任务: 驱动频率斜坡 150k->100kHz, 非阻塞 */
void Inverter_Control_Soft_Start_Task(void)
{
    if (s_ss_state != INVERTER_CONTROL_SS_STATE_SWEEP) return;

    if (Sys_Timer_Get_Tick() - s_ss_last_ms >= SOFTSTART_STEP_MS) {
        s_ss_last_ms = Sys_Timer_Get_Tick();

        if (s_ss_current_freq <= SOFTSTART_TARGET_FREQ_HZ + SOFTSTART_STEP_HZ) {
            Pwm_Driver_Set_Frequency(SOFTSTART_TARGET_FREQ_HZ);
            s_ss_current_freq = SOFTSTART_TARGET_FREQ_HZ;
            Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_DONE);
        } else {
            s_ss_current_freq -= SOFTSTART_STEP_HZ;
            if (s_ss_current_freq < SOFTSTART_TARGET_FREQ_HZ)
                s_ss_current_freq = SOFTSTART_TARGET_FREQ_HZ;
            Pwm_Driver_Set_Frequency(s_ss_current_freq);
        }
    }
}

/** @brief 停止软启动: 关 PWM -> 回 IDLE */
void Inverter_Control_Soft_Start_Stop(void)
{
    Pwm_Driver_Disable();
    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_IDLE);
}

/** @brief 故障刹车: 立即关断 PWM, 进入 FAULT 状态 */
void Inverter_Control_Soft_Start_Fault(void)
{
    Pwm_Driver_Disable();
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_FAULT);
}

/** @brief 复位软启动状态机 (FAULT 恢复后调用) */
void Inverter_Control_Soft_Start_Reset(void)
{
    Pwm_Driver_Disable();
    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
    s_ramp_state       = INVERTER_CONTROL_RAMP_IDLE;
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_IDLE);
}

/** @brief 获取软启动状态机当前状态 (原子读取, 无需关 IRQ) */
Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void)
{
    return s_ss_state;
}

/** @brief 获取软启动当前实际频率 (Hz) */
uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void)
{
    return s_ss_current_freq;
}

/* ═══════════════════════════════════════════════════════════════
 *  频率斜坡 (运行时微调)
 * ═══════════════════════════════════════════════════════════════ */

static uint32_t                    s_ramp_target  = 0;
static uint32_t s_ramp_last_ms  = 0;

/** @brief 触发频率斜坡到目标值 (1kHz/步, 非阻塞)
 *  @param target_hz 目标频率 (Hz) */
void Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz)
{
    if (target_hz < PWM_DRIVER_FREQ_MIN_HZ) target_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (target_hz > PWM_DRIVER_FREQ_MAX_HZ) target_hz = PWM_DRIVER_FREQ_MAX_HZ;

    s_ramp_target  = target_hz;
    s_ramp_state   = INVERTER_CONTROL_RAMP_ACTIVE;
    s_ramp_last_ms = Sys_Timer_Get_Tick();
}

/** @brief 频率斜坡周期任务: 逐级调到目标频率 */
void Inverter_Control_Freq_Ramp_Task(void)
{
    if (s_ramp_state == INVERTER_CONTROL_RAMP_IDLE) return;

    uint32_t current = Pwm_Driver_Get_Frequency();

    /* 频率由硬件整数分频决定, 实际值可能偏离目标一个步进以内 */
    {
        int32_t diff = (int32_t)(current - s_ramp_target);
        if (diff >= -(int32_t)FREQ_RAMP_STEP_HZ && diff <= (int32_t)FREQ_RAMP_STEP_HZ) {
            Pwm_Driver_Set_Frequency(s_ramp_target);
            s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
            return;
        }
    }

    if (Sys_Timer_Get_Tick() - s_ramp_last_ms >= FREQ_RAMP_STEP_MS) {
        s_ramp_last_ms = Sys_Timer_Get_Tick();

        if (current < s_ramp_target) {
            current += FREQ_RAMP_STEP_HZ;
            if (current > s_ramp_target) current = s_ramp_target;
        } else {
            current -= FREQ_RAMP_STEP_HZ;
            if (current < s_ramp_target) current = s_ramp_target;
        }
        Pwm_Driver_Set_Frequency(current);
    }
}

/** @brief 获取频率斜坡目标值 (Hz) */
uint32_t Inverter_Control_Freq_Ramp_Get_Target(void)
{
    return s_ramp_target;
}

/** @brief 取消当前频率斜坡 (FAULT 或手动停止时调用) */
void Inverter_Control_Freq_Ramp_Cancel(void)
{
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
}
