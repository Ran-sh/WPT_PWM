/**
 ******************************************************************************
 * @file    User/Sys_Core.c
 * @brief   系统核心模块 — 实现 (状态枚举 + 初始化 + 安全 + 调度)
 * @note    V14: 合并 8 个 Sys_* 文件为 2 个 (Sys_Core.h/.c)
 ******************************************************************************
 */

#include "Sys_Core.h"
#include "Pwm_Driver.h"
#include "Tft_Driver.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Adc_Driver.h"
#include "Key_Driver.h"
#include "Inverter_Control.h"
#include "Ui_Controller.h"
#include "Sys_Timer.h"
#include "App_Network.h"

volatile Sys_State g_sys_state = SYS_STATE_INIT;

/* ═══════════════════════════════════════════════════════════════
 *  1. 初始化 (原 Sys_Init.c)
 * ═══════════════════════════════════════════════════════════════ */

void Sys_Clamp_ESP(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);          /* RST=0 */

    gpio.GPIO_Pin   = GPIO_Pin_11;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_11);         /* CH_PD=0 */
}

void Sys_Hardware_Init(void)
{
    GPIO_InitTypeDef gpio;

    Pwm_Driver_Init();

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_10);         /* 初始关断 12V */

    Tft_Driver_Init();
    Led_Driver_Init();
    Buzzer_Driver_Init();
    Adc_Driver_Init();
    Key_Driver_Init();
}

void Sys_Startup_Screen(void)
{
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...",
                              TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);
}

void Sys_Post_Init(void)
{
    Sys_Timer_Init();
    Led_Driver_Set_System(1);

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1000);
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

    App_Network_Start_Connect();
}

/* ═══════════════════════════════════════════════════════════════
 *  2. 安全监测 (原 Sys_Safety.c)
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_SAFETY_OVERCURRENT_A  5.0f
#define SYS_SAFETY_POWER_V        12.0f

static float  s_safety_ema_v = 0.0f, s_safety_ema_i = 0.0f;
static uint8_t s_safety_ema_ok = 0;

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

float Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }
float Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }

void Sys_Safety_Task(void)
{
    Sys_Safety_Update_EMA();

    /* PB10 电源控制 */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > SYS_SAFETY_POWER_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* 过流检测 → FAULT */
    if (s_safety_ema_i > SYS_SAFETY_OVERCURRENT_A) {
        Inverter_Control_Soft_Start_Fault();
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
        g_sys_state = SYS_STATE_FAULT;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  3. 运行调度 (原 Sys_Run.c)
 * ═══════════════════════════════════════════════════════════════ */

static void Sys_Run_Led_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 200) {
        last = Sys_Timer_Get_Tick();
        Led_Driver_Task();
    }
}

static void Sys_Run_Buzzer_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 50) {
        last = Sys_Timer_Get_Tick();
        Buzzer_Driver_Task();
    }
}

void Sys_Run_Idle(void)
{
    Ui_Controller_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Sweep(void)
{
    Ui_Controller_Task();
    Inverter_Control_Soft_Start_Task();
    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE)
        g_sys_state = SYS_STATE_RUNNING;
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Running(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Fault(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Cancel();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}
