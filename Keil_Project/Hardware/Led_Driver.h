/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.h
 * @brief   LED 指示灯驱动 — V5.0.2 (4 LED)
 * @note    PB4=WIFI, PB3=POWER(12V), PA15=STATUS(PWM), PC13=HEARTBEAT(运行)
 ******************************************************************************
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    LED_DRIVER_STATE_OFF  = 0,
    LED_DRIVER_STATE_ON   = 1,
    LED_DRIVER_STATE_SLOW = 2,   /* 500ms period blink */
    LED_DRIVER_STATE_FAST = 3    /* 200ms period blink */
} Led_Driver_State;

/** @brief Init 4 LED GPIOs + JTAG disable to free PB3/PB4 */
void Led_Driver_Init(void);
/** @brief Periodic LED drive (call from main loop, ~200ms) */
void Led_Driver_Task(void);

/** @brief Set WiFi status LED (PB4, active HIGH) */
void Led_Driver_Set_WiFi(Led_Driver_State state);
/** @brief Set POWER LED ON/OFF (PB3, direct GPIO, no state machine)
 *  @param on 1=12V active (ON), 0=12V off */
void Led_Driver_Set_Power(uint8_t on);
/** @brief Set STATUS LED (PA15, active HIGH) — PWM state indicator
 *  @param state OFF=idle/fault, SLOW=sweep, ON=running */
void Led_Driver_Set_Status(Led_Driver_State state);
/** @brief Enable/disable HEARTBEAT LED (PC13, active LOW, 500ms toggle)
 *  @param on 1=heartbeat active, 0=LED off */
void Led_Driver_Set_Heartbeat(uint8_t on);

#endif /* LED_DRIVER_H */
