#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f10x.h"

#define KEY_DRIVER_COUNT  5U

typedef enum {
    KEY_DRIVER_EVENT_NONE        = 0,
    KEY_DRIVER_EVENT_CLICK       = 1,
    KEY_DRIVER_EVENT_DOUBLE_CLICK = 2,
    KEY_DRIVER_EVENT_LONG_PRESS  = 3
} Key_Driver_Event;

/* 五个按键的固定编号。 */
#define KEY_DRIVER_ID_POWER     0   /* PB9 — KEY0 电源开关 */
#define KEY_DRIVER_ID_BACK      1   /* PB8 — KEY1 返回 */
#define KEY_DRIVER_ID_UP        2   /* PB7 — KEY2 上移或增加 */
#define KEY_DRIVER_ID_DOWN      3   /* PB6 — KEY3 下移或减少 */
#define KEY_DRIVER_ID_CONFIRM   4   /* PB5 — KEY4 确定/启停 */

/* 每个按键独立配置双击和长按能力；零值表示只产生单击事件。 */
#define KEY_DRIVER_CFG_DOUBLE_ENABLE  0x01U
#define KEY_DRIVER_CFG_LONG_ENABLE    0x02U

/** @brief 初始化五个使用内部上拉的按键引脚 */
void             Key_Driver_Init(void);
/** @brief 配置指定按键允许产生的扩展事件
 *  @param key_id 按键编号，范围为0至4
 *  @param config 双击与长按能力标志的按位组合
 */
void             Key_Driver_Configure(uint8_t key_id, uint8_t config);
/** @brief 周期推进五个按键状态机，应每10ms调用一次 */
void             Key_Driver_Task(void);
/** @brief 在一个临界区内批量读取并清除五个按键事件
 *  @param out 五元素事件数组，顺序为电源、返回、上移、下移、确定
 */
void             Key_Driver_Get_All_Events(Key_Driver_Event out[5]);

#endif /* 按键驱动接口结束 */
