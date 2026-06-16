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

#define SYS_OVERCURRENT_THRESHOLD_A  5.0f
#define SYS_POWER_V_THRESHOLD_V      12.0f

static float  s_ema_v = 0.0f, s_ema_i = 0.0f;
static uint8_t s_ema_ok = 0;

static void Update_EMA(void)
{
    float v = Adc_Driver_Get_Voltage();
    float c = Adc_Driver_Get_Current();
    if (s_ema_ok) {
        s_ema_v = s_ema_v * 0.75f + v * 0.25f;
        s_ema_i = s_ema_i * 0.75f + c * 0.25f;
    } else {
        s_ema_v = v;
        s_ema_i = c;
        s_ema_ok = 1;
    }
}

float Sys_Safety_Get_EMA_Voltage(void)  { return s_ema_v; }
float Sys_Safety_Get_EMA_Current(void)  { return s_ema_i; }

void Sys_Safety_Task(void)
{
    /* ── EMA 更新 ── */
    Update_EMA();

    /* ── PB10 电源控制 (电压阈值) ── */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > SYS_POWER_V_THRESHOLD_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* ── 过流检测 → 锁存 FAULT ── */
    if (s_ema_i > SYS_OVERCURRENT_THRESHOLD_A) {
        Inverter_Control_Soft_Start_Fault();
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
        g_sys_state = SYS_STATE_FAULT;
    }
}
