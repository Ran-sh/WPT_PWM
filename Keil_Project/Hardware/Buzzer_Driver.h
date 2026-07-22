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
/** @brief 周期驱动蜂鸣器，并根据状态生成间歇蜂鸣节奏 */
void Buzzer_Driver_Task(void);
/** @brief 设置蜂鸣器工作状态
 *  @param state 目标状态
 */
void Buzzer_Driver_Set_State(Buzzer_Driver_State state);

#endif /* 蜂鸣器驱动接口结束 */
