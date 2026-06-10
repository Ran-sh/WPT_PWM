/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.h
 * @brief   有源蜂鸣器驱动 — 公开接口 (V6.2)
 * @note    PB15 → NPN S8050 基极 (串 1kΩ), 集电极→蜂鸣器→5V
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

void Buzzer_Driver_Init(void);
void Buzzer_Driver_Task(void);
void Buzzer_Driver_Set_State(Buzzer_Driver_State state);

#endif /* BUZZER_DRIVER_H */
