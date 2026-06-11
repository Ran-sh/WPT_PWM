/**
 ******************************************************************************
 * @file    Hardware/Inverter_Control.h
 * @brief   逆变器应用控制 — 软启动状态机 + 频率渐变
 * @note    依赖 Pwm_Driver 硬件抽象层
 *
 *          软启动: SS_IDLE → Trigger → SS_SWEEP (150k→100kHz, 10ms/步, ~2.5s)
 *                  → SS_DONE → Stop → SS_IDLE
 *          故障:   任意状态 → SS_FAULT (MOE 关断, KEY 复位)
 *
 *          频率斜坡: Freq_Ramp_Trigger → 10ms/步, 1kHz/步, 渐变到目标频率
 ******************************************************************************
 */

#ifndef INVERTER_CONTROL_H
#define INVERTER_CONTROL_H

#include "stm32f10x.h"

#define SOFTSTART_START_FREQ_HZ   150000
#define SOFTSTART_TARGET_FREQ_HZ  100000
#define SOFTSTART_STEP_HZ         200
#define SOFTSTART_STEP_MS         10

#define FREQ_RAMP_STEP_HZ         1000
#define FREQ_RAMP_STEP_MS         10

typedef enum {
    INVERTER_CONTROL_SS_STATE_IDLE   = 0,
    INVERTER_CONTROL_SS_STATE_SWEEP  = 1,
    INVERTER_CONTROL_SS_STATE_DONE   = 2,
    INVERTER_CONTROL_SS_STATE_FAULT  = 3
} Inverter_Control_Soft_Start_State;

/** @brief 频率斜坡状态 */
typedef enum {
    INVERTER_CONTROL_RAMP_IDLE  = 0,
    INVERTER_CONTROL_RAMP_ACTIVE = 1
} Inverter_Control_Ramp_State;

/** @brief 触发软启动扫频 (IDLE → SWEEP, 150k→100kHz) */
void     Inverter_Control_Soft_Start_Trigger(void);
/** @brief 周期驱动软启动状态机 (每10ms降200Hz) */
void     Inverter_Control_Soft_Start_Task(void);
/** @brief 停止逆变器 (任意状态 → IDLE, 关PWM+MOE) */
void     Inverter_Control_Soft_Start_Stop(void);
/** @brief 故障保护 (关PWM+MOE → FAULT 锁存) */
void     Inverter_Control_Soft_Start_Fault(void);
/** @brief 从 FAULT 恢复 (关PWM+重置频率+清斜坡 → IDLE) */
void     Inverter_Control_Soft_Start_Reset(void);
/** @brief 获取软启动当前状态 (Cortex-M3 单指令原子读, 无需 IRQ 保护) */
Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void);
/** @brief 获取软启动当前频率 (Hz) */
uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void);

/** @brief 触发频率渐变斜坡 (仅 DONE 状态有效)
 *  @param target_hz 目标频率 (Hz), 范围 95k~150k, 自动钳位
 */
void     Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz);
/** @brief 周期驱动频率斜坡状态机 (每10ms步进1kHz) */
void     Inverter_Control_Freq_Ramp_Task(void);
/** @brief 获取频率斜坡目标值 (Hz) */
uint32_t Inverter_Control_Freq_Ramp_Get_Target(void);

#endif /* INVERTER_CONTROL_H */
