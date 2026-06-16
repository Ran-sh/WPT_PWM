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

/* ── LED 降频到 200ms (与 UI 同频) ── */
static void Led_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 200) {
        last = Sys_Timer_Get_Tick();
        Led_Driver_Task();
    }
}

/* ── Buzzer 降频到 50ms ── */
static void Buzzer_Tick(void)
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
    Led_Tick();
    Buzzer_Tick();
}

void Sys_Run_Sweep(void)
{
    Ui_Controller_Task();
    Inverter_Control_Soft_Start_Task();

    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE)
        g_sys_state = SYS_STATE_RUNNING;

    Led_Tick();
    Buzzer_Tick();
}

void Sys_Run_Running(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Task();
    Led_Tick();
    Buzzer_Tick();
}

void Sys_Run_Fault(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Cancel();
    Led_Tick();
    Buzzer_Tick();
}
