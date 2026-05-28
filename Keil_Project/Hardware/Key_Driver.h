/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — 公开接口
 * @note    双按键: PB12=KEY0, PB13=KEY1, 内部上拉, 低电平按下
 *          10ms 去抖 + 状态机: 支持单击/双击/长按检测
 ******************************************************************************
 */

#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    KEY_EVENT_NONE        = 0,
    KEY_EVENT_CLICK       = 1,
    KEY_EVENT_DOUBLE_CLICK = 2,
    KEY_EVENT_LONG_PRESS  = 3
} Key_Driver_Event;

void Key_Driver_Init(void);
void Key_Driver_Task(void);                      /* 10ms 周期调用 */
Key_Driver_Event Key_Driver_Get_Event(uint8_t key_id);  /* key_id: 0=KEY0, 1=KEY1 */

#endif /* KEY_DRIVER_H */
