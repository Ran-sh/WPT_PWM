/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   WPT_PWM V4.3.2 — 程序入口 (W25Q128 全字库)
 * @note    V4.3.2: 初始化铁序 TFT → W25Q → Font → 其余
 *          W25Q_Driver_Init 后必须调用 Tft_Driver_Font_Init(),
 *          否则 Flash 字库永远回退 ROM 76 字
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Core.h"
#include "Key_Driver.h"
#include "Adc_Driver.h"
#include "App_Network.h"
#include "W25Q_Driver.h"
#include "Tft_Driver.h"
#include "Sys_Timer.h"
#include "App_Storage.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* ── 初始化阶段 ── */
    Sys_Clamp_ESP();
    Sys_Hardware_Init();

    /* Sys_Timer 必须在 SPLASH 之前 — Tft_Driver_Show_Splash 内部 Sys_Timer_Delay_Ms 依赖 SysTick */
    Sys_Timer_Init();

    W25Q_Driver_Init();
    Tft_Driver_Font_Init();                          /* 必须在 W25Q_Driver_Init 之后! */
    App_Storage_Init();

    Sys_Startup_Screen();
    Sys_Post_Init();
    g_sys_state = SYS_STATE_IDLE;

    /* ── 主循环 ── */
    while (1) {
        Key_Driver_Task();
        Adc_Driver_Filter_Task();
        App_Network_Task();
        Sys_Safety_Task();

        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;
            default: break;
        }

        IWDG_ReloadCounter();
        __WFI();
    }
}
