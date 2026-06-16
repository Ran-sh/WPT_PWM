/**
 ******************************************************************************
 * @file    User/Sys_Run.c
 * @brief   系统运行状态机 — 实现
 * @note    每个状态仅运行所需的 Task 子集, 消除无效调用
 *          LED/Buzzer 降频到 200ms/50ms 而非每圈 ~1ms
 ******************************************************************************
 */

#include "Sys_Run.h"
#include "Sys_State.h"
#include "Inverter_Control.h"
#include "Ui_Controller.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Sys_Timer.h"

/** @brief LED 降频到 200ms (与 UI 同频, 消除每圈 ~1ms 无效调用) */
static void Sys_Run_Led_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 200) {
        last = Sys_Timer_Get_Tick();
        Led_Driver_Task();
    }
}

/** @brief Buzzer 降频到 50ms (蜂鸣器 BEEP 间歇模式无需 ~1ms 高频轮询) */
static void Sys_Run_Buzzer_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 50) {
        last = Sys_Timer_Get_Tick();
        Buzzer_Driver_Task();
    }
}

/** @brief IDLE 模式: 待机 — 主菜单/配网页, PWM 关断, 仅 UI+LED+Buzzer */
void Sys_Run_Idle(void)
{
    Ui_Controller_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

/**
 * @brief  SWEEP 模式: 软启动扫频中 150k→100kHz
 * @note   扫频完成 (DONE) 自动转移至 SYS_STATE_RUNNING
 */
void Sys_Run_Sweep(void)
{
    Ui_Controller_Task();
    Inverter_Control_Soft_Start_Task();

    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE)
        g_sys_state = SYS_STATE_RUNNING;

    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

/** @brief RUNNING 模式: PWM 稳态输出, 频率斜坡活跃, 仪表盘实时刷新 */
void Sys_Run_Running(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

/**
 * @brief  FAULT 模式: 过流锁存, PWM 已关断, 等待按键复位
 * @note   取消所有频率斜坡, 防止误触发 PWM 输出
 */
void Sys_Run_Fault(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Cancel();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}
