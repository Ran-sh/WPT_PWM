/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — 公开接口 (V4.2.0 4 键版)
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

/* V4.2.0 4 键编号: PB9=启停, PB8=频率+, PB7=频率-, PB5=翻页 */
#define KEY_DRIVER_ID_ON_OFF    0
#define KEY_DRIVER_ID_FREQ_UP   1
#define KEY_DRIVER_ID_FREQ_DOWN 2
#define KEY_DRIVER_ID_PAGE      3

/** @brief 初始化 4 键 GPIO (全 IPU) */
void             Key_Driver_Init(void);
/** @brief 周期驱动 4 键 FSM (每10ms调用) */
void             Key_Driver_Task(void);
/** @brief 获取按键事件并清空 (临界区保护, 阅后即焚)
 *  @param  key_id 按键编号 (0=ON/OFF 1=F+ 2=F- 3=PAGE)
 *  @return 事件类型 (NONE/CLICK/DOUBLE_CLICK/LONG_PRESS)
 */
Key_Driver_Event Key_Driver_Get_Event(uint8_t key_id);
/** @brief 批量获取 4 键事件 (单次临界区, 减少 IRQ 抖动) */
void             Key_Driver_Get_All_Events(Key_Driver_Event out[4]);

#endif /* KEY_DRIVER_H */
