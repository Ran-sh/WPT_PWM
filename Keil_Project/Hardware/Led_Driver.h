/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.h
 * @brief   LED 指示灯驱动 — 公开接口 (V6.2 6 LED 版)
 * @note    V6.2: 6 LED
 *          PA15=SYSTEM心跳, PB4=WiFi, PB3=PWM, PA10=COM, PA11=POWER, PA12=TEMP
 ******************************************************************************
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    LED_DRIVER_STATE_OFF  = 0,
    LED_DRIVER_STATE_ON   = 1,
    LED_DRIVER_STATE_SLOW = 2,   /* 500ms 周期慢闪 */
    LED_DRIVER_STATE_FAST = 3    /* 200ms 周期快闪 */
} Led_Driver_State;

void Led_Driver_Init(void);
void Led_Driver_Task(void);

void Led_Driver_Set_WiFi(Led_Driver_State state);
void Led_Driver_Set_Pwm(Led_Driver_State state);
void Led_Driver_Set_Com(Led_Driver_State state);
void Led_Driver_Set_Power(Led_Driver_State state);
void Led_Driver_Set_Temp(Led_Driver_State state);
void Led_Driver_Set_System(uint8_t on_off);

#endif /* LED_DRIVER_H */
