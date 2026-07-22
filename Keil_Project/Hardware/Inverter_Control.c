/**
 ******************************************************************************
 * @file    Hardware/Inverter_Control.c
 * @brief   逆变器软启动与频率斜坡控制 — V5.0.2
 *
 *  相关硬件连接:
 *  +----------------------------------------------------------+
 *  |                       STM32F103C8T6                       |
 *  |                                                           |
 *  |    PA8  --- TIM1_CH1  ------- 全桥高侧输入甲              |
 *  |    PA9  --- TIM1_CH2  ------- 全桥高侧输入乙              |
 *  |    PB13 --- TIM1_CH1N ------- 全桥低侧互补输入甲          |
 *  |    PB14 --- TIM1_CH2N ------- 全桥低侧互补输入乙          |
 *  |                                                           |
 *  |    PB10 --- 推挽输出 ---------- 12V电源使能                |
 *  |              高电平开启，低电平关闭                       |
 *  |                                                           |
 *  |    软启动：150kHz降至100kHz，每10ms降低200Hz              |
 *  |    故障处理：立即停止PWM并取消全部频率斜坡                |
 *  +----------------------------------------------------------+
 *
 * @note    所有斜坡均由时间戳驱动，不阻塞主循环。
 ******************************************************************************
 */

#include "Inverter_Control.h"
#include "Pwm_Driver.h"
#include "Sys_Timer.h"

/* ==============================================================
 *  软启动状态机
 * ============================================================== */

static Inverter_Control_Soft_Start_State s_ss_state        = INVERTER_CONTROL_SS_STATE_IDLE;
static uint32_t s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
static uint32_t s_ss_last_ms      = 0;
static Inverter_Control_Ramp_State s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;

static void Inverter_Control_Set_State_Atomic(Inverter_Control_Soft_Start_State new_state)
{
    s_ss_state = new_state;
    /* Cortex-M3对32位对齐数据执行单指令写入，读取状态时无需关闭中断。 */
}

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

void Inverter_Control_Soft_Start_Stop(void)
{
    Pwm_Driver_Disable();
    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
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
    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
    s_ramp_state       = INVERTER_CONTROL_RAMP_IDLE;
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

/* ==============================================================
 *  频率斜坡 (运行时微调)
 * ============================================================== */

static uint32_t                    s_ramp_target  = 0;
static uint32_t s_ramp_last_ms  = 0;

void Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz)
{
    if (target_hz < PWM_DRIVER_FREQ_MIN_HZ) target_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (target_hz > PWM_DRIVER_FREQ_MAX_HZ) target_hz = PWM_DRIVER_FREQ_MAX_HZ;

    s_ramp_target  = target_hz;
    s_ramp_state   = INVERTER_CONTROL_RAMP_ACTIVE;
    s_ramp_last_ms = Sys_Timer_Get_Tick();
}

void Inverter_Control_Freq_Ramp_Task(void)
{
    uint32_t current;

    if (s_ramp_state == INVERTER_CONTROL_RAMP_IDLE) return;

    current = Pwm_Driver_Get_Frequency();

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

void Inverter_Control_Freq_Ramp_Cancel(void)
{
    s_ramp_state = INVERTER_CONTROL_RAMP_IDLE;
}
