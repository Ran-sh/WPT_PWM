/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   LED 指示灯驱动 — 实现
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

#define LED_HEARTBEAT_PIN   GPIO_Pin_13
#define LED_WIFI_PIN        GPIO_Pin_3
#define LED_PWM_PIN         GPIO_Pin_4
#define LED_READY_PIN       GPIO_Pin_5

#define LED_PORT_HEARTBEAT  GPIOC
#define LED_PORT_OTHERS     GPIOB

#define HEARTBEAT_PERIOD_MS    500
#define BLINK_SLOW_PERIOD_MS   500
#define BLINK_FAST_PERIOD_MS   200

static Led_Driver_State s_wifi_state  = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_pwm_state   = LED_DRIVER_STATE_OFF;
static uint8_t          s_ready_on    = 0;
static uint32_t         s_wifi_last   = 0;
static uint32_t         s_pwm_last    = 0;

/* ── 内部: 驱动单个 LED 引脚 ── */
static void Drive_Pin(GPIO_TypeDef* port, uint16_t pin, Led_Driver_State state, uint32_t* p_last)
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
            if (now - *p_last >= BLINK_SLOW_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin, (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
        case LED_DRIVER_STATE_FAST:
            if (now - *p_last >= BLINK_FAST_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin, (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
    }
}

void Led_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);  /* 释放 PB3/PB4 */

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    cfg.GPIO_Pin = LED_HEARTBEAT_PIN;
    GPIO_Init(LED_PORT_HEARTBEAT, &cfg);
    GPIO_SetBits(LED_PORT_HEARTBEAT, LED_HEARTBEAT_PIN);  /* PC13 低电平点亮, 初始灭 */

    cfg.GPIO_Pin = LED_WIFI_PIN | LED_PWM_PIN | LED_READY_PIN;
    GPIO_Init(LED_PORT_OTHERS, &cfg);
    GPIO_ResetBits(LED_PORT_OTHERS, LED_WIFI_PIN | LED_PWM_PIN | LED_READY_PIN);
}

void Led_Driver_Task(void)
{
    static uint32_t s_heartbeat_last = 0;
    uint32_t now = Sys_Timer_Get_Tick();

    /* 心跳: PC13 低电平点亮, 高电平熄灭 */
    if (now - s_heartbeat_last >= HEARTBEAT_PERIOD_MS) {
        s_heartbeat_last = now;
        GPIO_WriteBit(LED_PORT_HEARTBEAT, LED_HEARTBEAT_PIN,
                      (BitAction)!GPIO_ReadOutputDataBit(LED_PORT_HEARTBEAT, LED_HEARTBEAT_PIN));
    }

    /* WiFi / PWM 闪烁 */
    Drive_Pin(LED_PORT_OTHERS, LED_WIFI_PIN,  s_wifi_state, &s_wifi_last);
    Drive_Pin(LED_PORT_OTHERS, LED_PWM_PIN,   s_pwm_state,  &s_pwm_last);

    /* Ready: 静态电平 */
    if (s_ready_on)
        GPIO_SetBits(LED_PORT_OTHERS, LED_READY_PIN);
    else
        GPIO_ResetBits(LED_PORT_OTHERS, LED_READY_PIN);
}

void Led_Driver_Set_WiFi(Led_Driver_State state) { s_wifi_state = state; }
void Led_Driver_Set_Pwm(Led_Driver_State state)  { s_pwm_state  = state; }
void Led_Driver_Set_Ready(uint8_t on_off)         { s_ready_on   = on_off; }
