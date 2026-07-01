/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   LED 指示灯驱动 — V4.3.2 (5 LEDs)
 *
 *  Pinout (JTAG disabled -> PB3/PB4/PA15 freed as GPIO):
 *  +----------------------------------------------------------+
 *  |                    STM32F103C8T6                          |
 *  |                                                           |
 *  |    PA15 --- GPIO_PP --- LED_SYSTEM (yellow) heartbeat     |
 *  |              ON=normal  BLINK_SLOW=abnormal  BLINK_FAST=  |
 *  |                                                           |
 *  |    PB4  --- GPIO_PP --- LED_WIFI   (blue)  WiFi status    |
 *  |              ON=online  BLINK_SLOW=reconnect  OFF=offlin  |
 *  |                                                           |
 *  |    PB3  --- GPIO_PP --- LED_PWM    (green) PWM running    |
 *  |              ON=running  BLINK_SLOW=sweep  OFF=idle       |
 *  |                                                           |
 *  |    PA10 --- GPIO_PP --- LED_COM    (blue)  comm activity  |
 *  |              ON=tx/rx  OFF=idle                           |
 *  |                                                           |
 *  |    PA11 --- GPIO_PP --- LED_POWER  (green) power indicat  |
 *  |              ON=12V enabled  OFF=12V disabled             |
 *  |                                                           |
 *  |    PA12 === W25Q128_CS (reassigned to Flash, LED_TEMP of  |
 *  |                                                           |
 *  |    Each LED: GPIO -> R (220 ohm) -> LED anode -> GND      |
 *  +----------------------------------------------------------+
 *
 * @note    PA15=LED_SYSTEM, PB4=LED_WIFI, PB3=LED_PWM,
 *          PA10=LED_COM, PA11=LED_POWER
 *          PA12 reassigned to W25Q128 Flash CS
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

#define LED_DRIVER_WIFI_PIN    GPIO_Pin_4   /* PB4 — WiFi状态灯 */
#define LED_DRIVER_PWM_PIN     GPIO_Pin_3   /* PB3 — PWM运行灯 */
#define LED_DRIVER_COM_PIN     GPIO_Pin_10  /* PA10 — 通信灯 */
#define LED_DRIVER_POWER_PIN   GPIO_Pin_11  /* PA11 — 供电状态灯 */
#define LED_DRIVER_TEMP_PIN    GPIO_Pin_12  /* PA12 — 已让给 W25Q128 Flash CS, LED_TEMP 禁用 */
#define LED_DRIVER_SYSTEM_PIN  GPIO_Pin_15  /* PA15 — 系统心跳灯 */

#define LED_DRIVER_PORT_A      GPIOA
#define LED_DRIVER_PORT_B      GPIOB

#define LED_DRIVER_BLINK_SLOW_PERIOD_MS   500
#define LED_DRIVER_BLINK_FAST_PERIOD_MS   200

static Led_Driver_State s_wifi_state  = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_pwm_state   = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_com_state   = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_power_state = LED_DRIVER_STATE_ON;   /* 电源正常默认亮 */
static uint8_t          s_system_on   = 0;
/* PA12→Flash CS, s_temp_state/s_temp_last 废弃 */

static uint32_t s_wifi_last  = 0;
static uint32_t s_pwm_last   = 0;
static uint32_t s_com_last   = 0;
static uint32_t s_power_last = 0;

static void Drive_Pin(GPIO_TypeDef* port, uint16_t pin,
                      Led_Driver_State state, uint32_t* p_last)
{
    uint32_t now = Sys_Timer_Get_Tick();

    switch (state) {
        case LED_DRIVER_STATE_ON:
            GPIO_SetBits(port, pin);
            break;
        case LED_DRIVER_STATE_OFF:
            GPIO_ResetBits(port, pin);
            break;
        case LED_DRIVER_STATE_SLOW:
            if (now - *p_last >= LED_DRIVER_BLINK_SLOW_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin,
                    (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
        case LED_DRIVER_STATE_FAST:
            if (now - *p_last >= LED_DRIVER_BLINK_FAST_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin,
                    (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
    }
}

/** @brief 初始化 5 LED GPIO + 禁用 JTAG 释放 PB3/PB4 (PA12 已让给 Flash CS) */
void Led_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    /* GPIOA: PA10=COM, PA11=POWER, PA15=SYSTEM (PA12 已让给 W25Q128 Flash CS) */
    cfg.GPIO_Pin = LED_DRIVER_COM_PIN | LED_DRIVER_POWER_PIN |
                   LED_DRIVER_SYSTEM_PIN;
    GPIO_Init(LED_DRIVER_PORT_A, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_A,
        LED_DRIVER_COM_PIN | LED_DRIVER_POWER_PIN |
        LED_DRIVER_SYSTEM_PIN);

    /* GPIOB: PB3=PWM, PB4=WiFi */
    cfg.GPIO_Pin = LED_DRIVER_PWM_PIN | LED_DRIVER_WIFI_PIN;
    GPIO_Init(LED_DRIVER_PORT_B, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_PWM_PIN | LED_DRIVER_WIFI_PIN);

    /* 上电自检: 全亮 500ms (SysTimer 尚未初始化, 使用粗略 busy-wait) */
    GPIO_SetBits(LED_DRIVER_PORT_A,
        LED_DRIVER_COM_PIN | LED_DRIVER_POWER_PIN |
        LED_DRIVER_SYSTEM_PIN);
    GPIO_SetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_PWM_PIN | LED_DRIVER_WIFI_PIN);
    { volatile uint32_t i; for (i = 0; i < 175000; i++) __NOP(); }  /* ~500ms @72MHz */
    GPIO_ResetBits(LED_DRIVER_PORT_A,
        LED_DRIVER_COM_PIN | LED_DRIVER_POWER_PIN |
        LED_DRIVER_SYSTEM_PIN);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_PWM_PIN | LED_DRIVER_WIFI_PIN);
}

/** @brief 周期驱动所有 LED: 根据状态自动 ON/OFF/SLOW/FAST 闪烁 */
void Led_Driver_Task(void)
{
    Drive_Pin(LED_DRIVER_PORT_B, LED_DRIVER_WIFI_PIN,  s_wifi_state,  &s_wifi_last);
    Drive_Pin(LED_DRIVER_PORT_B, LED_DRIVER_PWM_PIN,   s_pwm_state,   &s_pwm_last);
    Drive_Pin(LED_DRIVER_PORT_A, LED_DRIVER_COM_PIN,   s_com_state,   &s_com_last);
    Drive_Pin(LED_DRIVER_PORT_A, LED_DRIVER_POWER_PIN, s_power_state, &s_power_last);
    /* PA12=TEMP 已禁用 — 让给 W25Q128 Flash CS */

    /* SYSTEM: 心跳闪烁 (500ms) */
    {
        static uint32_t s_heartbeat_last = 0;
        uint32_t now = Sys_Timer_Get_Tick();
        if (s_system_on && (now - s_heartbeat_last >= 500)) {
            s_heartbeat_last = now;
            GPIO_WriteBit(LED_DRIVER_PORT_A, LED_DRIVER_SYSTEM_PIN,
                          (BitAction)!GPIO_ReadOutputDataBit(
                              LED_DRIVER_PORT_A, LED_DRIVER_SYSTEM_PIN));
        }
    }
}

void Led_Driver_Set_WiFi(Led_Driver_State state)   { s_wifi_state  = state; }
void Led_Driver_Set_Pwm(Led_Driver_State state)    { s_pwm_state   = state; }
void Led_Driver_Set_Com(Led_Driver_State state)    { s_com_state   = state; }
void Led_Driver_Set_Power(Led_Driver_State state)  { s_power_state = state; }
void Led_Driver_Set_Temp(Led_Driver_State state)   { /* PA12→Flash CS, 函数保留占位 */ }
void Led_Driver_Set_System(uint8_t on_off)          { s_system_on   = on_off; }
