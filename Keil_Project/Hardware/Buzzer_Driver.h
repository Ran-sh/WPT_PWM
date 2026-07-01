/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.h
 * @brief   有源蜂鸣器驱动 — V4.3.2
 *
 *  接线: PB15 -> R(1k) -> S8050 基极(NPN), 集电极->蜂鸣器->VCC, 发射极->GND
 *        高电平驱动, HIGH=S8050导通=蜂鸣器响, 2.7kHz 有源电磁式
 *        BEEP 模式: 200ms 响 / 800ms 停 (20% 占空比, 足够引起注意但避免持续刺耳)
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

/** @brief 初始化 PB15 推挽输出, 初始低电平 -> 蜂鸣器静音 */
void Buzzer_Driver_Init(void);
/** @brief 周期调用: 根据 s_state 自动控制 ON/OFF/BEEP 交替 */
void Buzzer_Driver_Task(void);
/** @brief 设置蜂鸣器工作模式
 *  @param state  OFF=静音, ON=持续响, BEEP=200ms/800ms 间歇 */
void Buzzer_Driver_Set_State(Buzzer_Driver_State state);

#endif /* BUZZER_DRIVER_H */
