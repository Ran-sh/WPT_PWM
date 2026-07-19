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

/* ── 全局系统状态枚举 ── */
typedef enum {
    SYS_STATE_INIT    = 0,
    SYS_STATE_IDLE    = 1,
    SYS_STATE_SWEEP   = 2,
    SYS_STATE_RUNNING = 3,
    SYS_STATE_FAULT   = 4
} Sys_State;

typedef enum {
    SYS_CONTROL_RESULT_OK = 0,
    SYS_CONTROL_RESULT_POWER_OFF,
    SYS_CONTROL_RESULT_FAULT_LATCHED,
    SYS_CONTROL_RESULT_ADC_NOT_READY,
    SYS_CONTROL_RESULT_INVALID_STATE
} Sys_Control_Result;

typedef enum {
    SYS_FAULT_NONE = 0,
    SYS_FAULT_OVERCURRENT,
    SYS_FAULT_ADC_STALE,
    SYS_FAULT_CONTROL_INVARIANT
} Sys_Fault_Code;

/* ── 统一功率与状态控制 ── */
/** @brief 请求启动软启动扫频
 *  @retval 控制结果；只有返回SYS_CONTROL_RESULT_OK才允许进入/保持运行态
 */
Sys_Control_Result Sys_Core_Request_Start(void);
/** @brief 请求停止PWM，正常停止保留PB10的12V使能状态 */
Sys_Control_Result Sys_Core_Request_Stop(void);
/** @brief 清除FAULT锁存并回到IDLE；不会重新开启PB10 */
Sys_Control_Result Sys_Core_Reset_Fault(void);
/** @brief 触发锁存故障，按PWM→PB10→指示灯顺序安全关断 */
void               Sys_Core_Trigger_Fault(Sys_Fault_Code fault_code);
/** @brief 获取当前系统状态 */
Sys_State          Sys_Core_Get_State(void);
/** @brief 获取锁存的首个故障原因 */
Sys_Fault_Code     Sys_Core_Get_Fault(void);
/** @brief 读取PB10输出锁存状态
 *  @retval 1=12V使能, 0=12V关闭
 */
uint8_t            Sys_Core_Is_Power_Enabled(void);

/* ── 初始化 ── */
void Sys_Clamp_ESP(void);
void Sys_Hardware_Init(void);
void Sys_Startup_Screen(void);
void Sys_Post_Init(void);

/* ── 运行调度 ── */
void Sys_Run_Idle(void);
void Sys_Run_Sweep(void);
void Sys_Run_Running(void);
void Sys_Run_Fault(void);

#endif /* SYS_CORE_H */
