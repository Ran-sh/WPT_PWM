/**
 ******************************************************************************
 * @file    User/Sys_Safety.c
 * @brief   系统安全监测 — 实现
 * @note    PB10 电源控制 + 过流保护 + EMA 滤波
 *          每个主循环周期调用 Sys_Safety_Task()
 ******************************************************************************
 */

#include "Sys_Safety.h"
#include "Sys_State.h"
#include "Adc_Driver.h"
#include "Inverter_Control.h"
#include "Buzzer_Driver.h"

/** @brief 过流保护阈值 (A), EMA 平滑值超过此值触发 SYS_STATE_FAULT */
#define SYS_SAFETY_OVERCURRENT_A  5.0f
/** @brief 12V 电源使能电压阈值 (V), 高于此值拉高 PB10 使能动力电源 */
#define SYS_SAFETY_POWER_V        12.0f

static float  s_safety_ema_v = 0.0f, s_safety_ema_i = 0.0f;
static uint8_t s_safety_ema_ok = 0;

/** @brief 指数移动平均 (EMA) 更新, α=0.25, τ≈800ms */
static void Sys_Safety_Update_EMA(void)
{
    float v = Adc_Driver_Get_Voltage();
    float c = Adc_Driver_Get_Current();
    if (s_safety_ema_ok) {
        s_safety_ema_v = s_safety_ema_v * 0.75f + v * 0.25f;
        s_safety_ema_i = s_safety_ema_i * 0.75f + c * 0.25f;
    } else {
        s_safety_ema_v = v;
        s_safety_ema_i = c;
        s_safety_ema_ok = 1;
    }
}

/** @brief 获取 EMA 平滑电压值 (V) */
float Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }
/** @brief 获取 EMA 平滑电流值 (A) */
float Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }

/**
 * @brief  系统安全监测任务 — 每个主循环周期调用
 * @note   PB10 电源控制 (电压阈值) + 过流检测 → 锁存 FAULT
 */
void Sys_Safety_Task(void)
{
    /* ── EMA 更新 ── */
    Sys_Safety_Update_EMA();

    /* ── PB10 电源控制 (电压阈值) ── */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > SYS_SAFETY_POWER_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* ── 过流检测 → 锁存 FAULT ── */
    if (s_safety_ema_i > SYS_SAFETY_OVERCURRENT_A) {
        Inverter_Control_Soft_Start_Fault();
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
        g_sys_state = SYS_STATE_FAULT;
    }
}
