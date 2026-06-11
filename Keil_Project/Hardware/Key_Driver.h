/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — 公开接口 (V9 4 键版)
 * @note    PB9=ON/OFF, PB8=F_UP, PB7=F_DOWN, PB5=PAGE
 *          全部 GPIO IPU, 低电平按下, 10ms 去抖 + FSM 状态机
 ******************************************************************************
 */

#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    KEY_DRIVER_EVENT_NONE        = 0,
    KEY_DRIVER_EVENT_CLICK       = 1,
    KEY_DRIVER_EVENT_DOUBLE_CLICK = 2,
    KEY_DRIVER_EVENT_LONG_PRESS  = 3
} Key_Driver_Event;

/* V9 4 键编号: PB9=启停, PB8=频率+, PB7=频率-, PB5=翻页 */
#define KEY_DRIVER_ID_ON_OFF    0
#define KEY_DRIVER_ID_FREQ_UP   1
#define KEY_DRIVER_ID_FREQ_DOWN 2
#define KEY_DRIVER_ID_PAGE      3

void             Key_Driver_Init(void);
void             Key_Driver_Task(void);
Key_Driver_Event Key_Driver_Get_Event(uint8_t key_id);

#endif /* KEY_DRIVER_H */
