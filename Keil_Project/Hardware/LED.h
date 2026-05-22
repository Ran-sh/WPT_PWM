/**
 ******************************************************************************
 * @file    Hardware/LED.h
 * @brief   系统 LED 驱动 (PC13 + PB3/PB4/PB5) —— 公开接口
 * @note    PC13 心跳 (低有效) / PB3 WiFi (高有效) / PB4 PWM (高有效) / PB5 Ready (高有效)
 ******************************************************************************
 */

#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* ── LED 闪烁状态枚举 ── */
typedef enum {
    LED_OFF   = 0,   /* 熄灭         */
    LED_SLOW  = 1,   /* 慢闪 1Hz     */
    LED_FAST  = 2,   /* 快闪 5Hz     */
    LED_SOLID = 3    /* 常亮         */
} LedState_t;

void LED_Init(void);
void LED_Task(void);   /* PC13 心跳 500ms 翻转 */

/* 状态更新 (由 UI_Task 每 200ms 调用) */
void LED_Update_WiFi (LedState_t state);   /* PB3 */
void LED_Update_PWM  (LedState_t state);   /* PB4 */
void LED_Update_Ready(uint8_t   on_off);   /* PB5: 1=亮 0=灭 */

/* 瞬时操作 */
void LED_WiFi_ON(void);
void LED_WiFi_OFF(void);

/* 闪烁驱动, UI_Task 内调用 */
void LED_Status_Task(void);

#endif
