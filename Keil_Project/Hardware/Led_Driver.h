/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.h
 * @brief   LED 指示灯驱动 — 公开接口
 * @note    PC13=心跳, PB3=WiFi, PB4=PWM, PB5=Ready
 *          LED 状态由 Ui_Controller 层调用更新, 底层只负责 GPIO 驱动
 ******************************************************************************
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    LED_STATE_OFF  = 0,
    LED_STATE_ON   = 1,
    LED_STATE_SLOW = 2,   /* 500ms 周期慢闪 */
    LED_STATE_FAST = 3    /* 200ms 周期快闪 */
} Led_Driver_State;

void Led_Driver_Init(void);
void Led_Driver_Task(void);                      /* 心跳 + 闪烁调度, 主循环周期性调用 */

void Led_Driver_Set_WiFi(Led_Driver_State state);
void Led_Driver_Set_Pwm(Led_Driver_State state);
void Led_Driver_Set_Ready(uint8_t on_off);

#endif /* LED_DRIVER_H */
