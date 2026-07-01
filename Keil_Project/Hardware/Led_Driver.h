/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.h
 * @brief   LED 指示灯驱动 — V4.3.2 (5 LED)
 * @note    V4.3.0: PA12 已让给 W25Q128 Flash CS, LED_TEMP 禁用
 *          PA15=SYSTEM 心跳, PB4=WiFi, PB3=PWM, PA10=COM, PA11=POWER
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

/** @brief 初始化 5 LED GPIO + 禁用 JTAG 释放 PB3/PB4 (PA12 已让给 Flash CS) */
void Led_Driver_Init(void);
/** @brief 周期驱动所有 LED (根据状态自动快闪/慢闪/常亮/灭) */
void Led_Driver_Task(void);

/** @brief 设置 WiFi 状态 LED (PB4) */
void Led_Driver_Set_WiFi(Led_Driver_State state);
/** @brief 设置 PWM 运行 LED (PB3) */
void Led_Driver_Set_Pwm(Led_Driver_State state);
/** @brief 设置通信 LED (PA10, MQTT 在线) */
void Led_Driver_Set_Com(Led_Driver_State state);
/** @brief 设置电源 LED (PA11, 电压>12V 亮) */
void Led_Driver_Set_Power(Led_Driver_State state);
/** @brief 设置温度 LED (PA12, 暂未启用) */
void Led_Driver_Set_Temp(Led_Driver_State state);
/** @brief 设置系统心跳 LED (PA15, 500ms 翻转)
 *  @param on_off 1=启用心跳, 0=关闭
 */
void Led_Driver_Set_System(uint8_t on_off);

#endif /* LED_DRIVER_H */
