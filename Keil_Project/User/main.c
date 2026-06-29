/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   WPT_PWM V4.3.0 — 程序入口 (W25Q128 Flash 集成)
 * @note    V4.3.0: 新增 W25Q_Driver + App_Storage 初始化
 *          状态: INIT → IDLE → SWEEP → RUNNING
 *                          ↑        │        │
 *                          └── FAULT ←───────┘
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Core.h"
#include "Key_Driver.h"
#include "Adc_Driver.h"
#include "App_Network.h"
#include "W25Q_Driver.h"
#include "App_Storage.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* ── 初始化阶段 ── */
    Sys_Clamp_ESP();
    Sys_Hardware_Init();

    /* V4.3.2: W25Q128 在 TFT SPI1 就绪后初始化 (仅验 JEDEC, 不重复配 SPI)
     *         Tft_Driver_Init 已配好 PA5/PA7/PA12 + SPI1, W25Q 复用 */
    W25Q_Driver_Init();
    App_Storage_Init();                              /* 字库 CRC + 恢复黑匣子指针 + 加载参数 */

    Sys_Startup_Screen();                            /* 字库就绪后显示启动画面 */
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
