#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    LED_DRIVER_STATE_OFF  = 0,
    LED_DRIVER_STATE_ON   = 1,
    LED_DRIVER_STATE_SLOW = 2,   /* 每500ms翻转一次 */
    LED_DRIVER_STATE_FAST = 3    /* 每200ms翻转一次 */
} Led_Driver_State;

/** @brief 关闭JTAG并初始化四个指示灯引脚 */
void Led_Driver_Init(void);
/** @brief 周期刷新指示灯状态，应由主循环持续调用 */
void Led_Driver_Task(void);

/** @brief 设置PB4无线状态灯的工作状态 */
void Led_Driver_Set_WiFi(Led_Driver_State state);
/** @brief 设置PB3电源状态灯
 *  @param on 1表示12V已开启，0表示12V已关闭
 */
void Led_Driver_Set_Power(uint8_t on);
/** @brief 设置PA15的PWM状态灯
 *  @param state 熄灭表示停机或故障，慢闪表示扫频，常亮表示运行
 */
void Led_Driver_Set_Status(Led_Driver_State state);
/** @brief 启用或停用PC13主程序心跳灯
 *  @param on 1表示启用500ms翻转，0表示保持熄灭
 */
void Led_Driver_Set_Heartbeat(uint8_t on);

#endif /* 指示灯驱动接口结束 */
