/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.h
 * @brief   有源蜂鸣器驱动 — V4.3.2
 * @note    PB15 -> NPN S8050 基极(串 1k), 集电极->蜂鸣器->5V
 *          高电平驱动, 有源 2.7kHz 电磁式
 ******************************************************************************
 */

#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    BUZZER_DRIVER_STATE_OFF  = 0,
    BUZZER_DRIVER_STATE_ON   = 1,
    BUZZER_DRIVER_STATE_BEEP = 2
} Buzzer_Driver_State;

/** @brief 初始化蜂鸣器 GPIO (PB15 推挽输出) */
void Buzzer_Driver_Init(void);
/** @brief 周期驱动蜂鸣器 (根据状态自动 BEEP ON/OFF) */
void Buzzer_Driver_Task(void);
/** @brief 设置蜂鸣器状态 (OFF=静音, BEEP=间歇蜂鸣)
 *  @param state 目标状态
 */
void Buzzer_Driver_Set_State(Buzzer_Driver_State state);

#endif /* BUZZER_DRIVER_H */
