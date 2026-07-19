/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — V5.0.1 (5 键)
 * @note    PB9=KEY0(电源), PB8=KEY1(返回), PB7=KEY2(UP), PB6=KEY3(DOWN), PB5=KEY4(确定)
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

/* V5.0 5-key ID */
#define KEY_DRIVER_ID_POWER     0   /* PB9 — KEY0 电源开关 */
#define KEY_DRIVER_ID_BACK      1   /* PB8 — KEY1 返回 */
#define KEY_DRIVER_ID_UP        2   /* PB7 — KEY2 UP/加 */
#define KEY_DRIVER_ID_DOWN      3   /* PB6 — KEY3 DOWN/减 */
#define KEY_DRIVER_ID_CONFIRM   4   /* PB5 — KEY4 确定/启停 */

/* Independent per-key capability flags. A zero value means immediate click only. */
#define KEY_DRIVER_CFG_DOUBLE_ENABLE  0x01U
#define KEY_DRIVER_CFG_LONG_ENABLE    0x02U

/** @brief Init 5 keys GPIO (all PBx, IPU) */
void             Key_Driver_Init(void);
/** @brief Configure key behavior
 *  @param key_id  Key index (0-4)
 *  @param config  ORed KEY_DRIVER_CFG_DOUBLE_ENABLE/LONG_ENABLE flags */
void             Key_Driver_Configure(uint8_t key_id, uint8_t config);
/** @brief Periodic key FSM drive (call every 10ms) */
void             Key_Driver_Task(void);
/** @brief Batch read 5 key events (single critical section)
 *  @param out[5] Key events array (0=POWER 1=BACK 2=UP 3=DOWN 4=CONFIRM) */
void             Key_Driver_Get_All_Events(Key_Driver_Event out[5]);

#endif /* KEY_DRIVER_H */
