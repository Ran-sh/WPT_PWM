/**
 ******************************************************************************
 * @file    User/Sys_Init.c
 * @brief   系统上电初始化 — 实现
 * @note    V14: 阶段0-4 全部从 main() 迁移到此
 ******************************************************************************
 */

#include "Sys_Init.h"
#include "stm32f10x.h"
#include "Pwm_Driver.h"
#include "Tft_Driver.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Adc_Driver.h"
#include "Key_Driver.h"
#include "Sys_Timer.h"
#include "App_Network.h"

/* ── 阶段0: 最早钳位 ESP8266 (RST=0, CH_PD=0) ── */
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

/* ── 阶段1: 硬件层初始化 — TIM1 全关(MOE+CEN), PB10 拉低关 12V ── */
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

/* ── 启动欢迎页 ── */
void Sys_Startup_Screen(void)
{
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...",
                              TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);
}

/* ── 阶段2-4: 系统时基 + 看门狗 + 开机联网 ── */
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
