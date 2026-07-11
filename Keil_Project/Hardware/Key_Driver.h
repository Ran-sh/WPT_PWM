/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — V4.5.2 (4 键)
 * @note    PB5=PAGE(确定/启停), PB9=ON(返回), PB8=F_UP, PB7=F_DOWN
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

/* V4.5.2 4 键: index 0=PB5=PAGE(确定/启停), index 3=PB9=ON(返回) */
#define KEY_DRIVER_ID_PAGE      0
#define KEY_DRIVER_ID_FREQ_UP   1
#define KEY_DRIVER_ID_FREQ_DOWN 2
#define KEY_DRIVER_ID_ON_OFF    3

/** @brief 初始化 4 键 GPIO (全 IPU) */
void             Key_Driver_Init(void);
/** @brief 配置按键选项 (在 Key_Driver_Init 之后调用)
 *  @param key_id   按键编号 (0-3)
 *  @param no_double 1=跳过双击检测, 释放去抖后立即发 CLICK (紧急启停键用) */
void             Key_Driver_Configure(uint8_t key_id, uint8_t no_double);
/** @brief 周期驱动 4 键 FSM (每10ms调用) */
void             Key_Driver_Task(void);
/** @brief 批量获取 4 键事件 (单次临界区, 减少 IRQ 抖动)
 *  @param  out[4] 按键事件数组 (0=PAGE/确定 1=F+ 2=F- 3=ON/返回) */
void             Key_Driver_Get_All_Events(Key_Driver_Event out[4]);

#endif /* KEY_DRIVER_H */
