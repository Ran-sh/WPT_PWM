/**
 ******************************************************************************
 * @file    User/Sys_Core.h
 * @brief   系统核心模块 — 全局状态 + 初始化 + 安全监测 + 运行调度 (V5.0.1)
 * @note    合并 Sys_State + Sys_Init + Sys_Safety + Sys_Run 为单一模块
 ******************************************************************************
 */

#ifndef SYS_CORE_H
#define SYS_CORE_H

#include "stm32f10x.h"
#include "Key_Driver.h"

/* ── 全局系统状态枚举 ── */
typedef enum {
    SYS_STATE_INIT    = 0,
    SYS_STATE_IDLE    = 1,
    SYS_STATE_SWEEP   = 2,
    SYS_STATE_RUNNING = 3,
    SYS_STATE_FAULT   = 4
} Sys_State;

extern volatile Sys_State g_sys_state;

/* ── 初始化 ── */
void Sys_Clamp_ESP(void);
void Sys_Hardware_Init(void);
void Sys_Startup_Screen(void);
void Sys_Post_Init(void);

/* ── 安全监测 ── */
void  Sys_Safety_Task(void);
float Sys_Safety_Get_EMA_Voltage(void);
float Sys_Safety_Get_EMA_Current(void);
/** @brief 重置过流 EMA 滤波缓存 (FAULT 复位时调用, 防止 EMA 残留值立即重新触发过流) */
void  Sys_Safety_Reset_EMA(void);

/* ── 运行调度 ── */
void Sys_Run_Idle(void);
void Sys_Run_Sweep(void);
void Sys_Run_Running(void);
void Sys_Run_Fault(void);

/** @brief Handle KEY0 power toggle + coordinate PB10/PWM/POWER LED
 *  @param ke array of 5 key events from Key_Driver_Get_All_Events
 *  @note  Consumes ke[KEY_DRIVER_ID_POWER] after processing */
void Sys_Power_Control_Handle(Key_Driver_Event ke[5]);

#endif /* SYS_CORE_H */
