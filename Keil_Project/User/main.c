/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   WPT_PWM V14 — 程序入口
 * @note    V14: 全局系统状态机, main() 极度简洁
 *
 *          状态: INIT → IDLE → SWEEP → RUNNING
 *                          ↑        │        │
 *                          └── FAULT ←───────┘
 *
 *          模块化: Sys_Init.h (初始化) + Sys_Safety.h (安全) + Sys_Run.h (运行)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Core.h"
#include "Key_Driver.h"
#include "Adc_Driver.h"
#include "App_Network.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* ── 初始化阶段 ── */
    Sys_Clamp_ESP();
    Sys_Hardware_Init();
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
