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
    SS_STATE_IDLE   = 0,
    SS_STATE_SWEEP  = 1,
    SS_STATE_DONE   = 2,
    SS_STATE_FAULT  = 3
} Inverter_Control_Soft_Start_State;

void     Inverter_Control_Soft_Start_Trigger(void);
void     Inverter_Control_Soft_Start_Task(void);
void     Inverter_Control_Soft_Start_Stop(void);
void     Inverter_Control_Soft_Start_Fault(void);
Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void);
uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void);

void     Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz);
void     Inverter_Control_Freq_Ramp_Task(void);
uint32_t Inverter_Control_Freq_Ramp_Get_Target(void);

#endif /* INVERTER_CONTROL_H */
