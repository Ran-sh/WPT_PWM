/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   LED 指示灯驱动 — 实现 (V6.2 6 LED 版)
 * @note    V6.2 引脚:
 *          PA15=LED_SYSETM (黄色系统心跳), PB4=LED_WIFI (蓝色WiFi),
 *          PB3=LED_PWM (绿色PWM), PA10=LED_COM (蓝色通信),
 *          PA11=LED_POWER (绿色电源), PA12=LED_TEMP (红色温度)
 *
 *          JTAG 禁用释放 PB3/PB4 + PA15 作为 GPIO
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

/* ── V6.2 LED 引脚定义 ── */
#define LED_WIFI_PIN    GPIO_Pin_4   /* PB4 — WiFi状态灯 */
#define LED_PWM_PIN     GPIO_Pin_3   /* PB3 — PWM运行灯 */
#define LED_COM_PIN     GPIO_Pin_10  /* PA10 — 通信灯 */
#define LED_POWER_PIN   GPIO_Pin_11  /* PA11 — 供电状态灯 */
#define LED_TEMP_PIN    GPIO_Pin_12  /* PA12 — 温度告警灯 */
#define LED_SYSTEM_PIN  GPIO_Pin_15  /* PA15 — 系统心跳灯 */

#define LED_PORT_A      GPIOA
#define LED_PORT_B      GPIOB

#define BLINK_SLOW_PERIOD_MS   500
#define BLINK_FAST_PERIOD_MS   200

/* ── LED 状态变量 ── */
static Led_Driver_State s_wifi_state  = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_pwm_state   = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_com_state   = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_power_state = LED_DRIVER_STATE_ON;   /* 电源正常默认亮 */
static Led_Driver_State s_temp_state  = LED_DRIVER_STATE_OFF;
static uint8_t          s_system_on   = 0;

static uint32_t s_wifi_last  = 0;
static uint32_t s_pwm_last   = 0;
static uint32_t s_com_last   = 0;
static uint32_t s_power_last = 0;
static uint32_t s_temp_last  = 0;
static uint32_t s_system_last = 0;

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

    /* 需要禁用 JTAG 释放 PB3/PB4 + PA15 (保留 SWD: PA13/PA14) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    /* GPIOA: PA10=COM, PA11=POWER, PA12=TEMP, PA15=SYSTEM */
    cfg.GPIO_Pin = LED_COM_PIN | LED_POWER_PIN | LED_TEMP_PIN | LED_SYSTEM_PIN;
    GPIO_Init(LED_PORT_A, &cfg);
    GPIO_ResetBits(LED_PORT_A, LED_COM_PIN | LED_POWER_PIN | LED_TEMP_PIN | LED_SYSTEM_PIN);

    /* GPIOB: PB3=PWM, PB4=WiFi */
    cfg.GPIO_Pin = LED_PWM_PIN | LED_WIFI_PIN;
    GPIO_Init(LED_PORT_B, &cfg);
    GPIO_ResetBits(LED_PORT_B, LED_PWM_PIN | LED_WIFI_PIN);

    /* 上电自检: 全亮 1 秒 */
    GPIO_SetBits(LED_PORT_A, LED_COM_PIN | LED_POWER_PIN | LED_TEMP_PIN | LED_SYSTEM_PIN);
    GPIO_SetBits(LED_PORT_B, LED_PWM_PIN | LED_WIFI_PIN);
    { volatile uint32_t i; for (i = 0; i < 350000; i++) __NOP(); }  /* ~1s @72MHz */
    GPIO_ResetBits(LED_PORT_A, LED_COM_PIN | LED_POWER_PIN | LED_TEMP_PIN | LED_SYSTEM_PIN);
    GPIO_ResetBits(LED_PORT_B, LED_PWM_PIN | LED_WIFI_PIN);
}

void Led_Driver_Task(void)
{
    /* WiFi + PWM + COM + POWER + TEMP 闪烁 */
    Drive_Pin(LED_PORT_B, LED_WIFI_PIN,  s_wifi_state,  &s_wifi_last);
    Drive_Pin(LED_PORT_B, LED_PWM_PIN,   s_pwm_state,   &s_pwm_last);
    Drive_Pin(LED_PORT_A, LED_COM_PIN,   s_com_state,   &s_com_last);
    Drive_Pin(LED_PORT_A, LED_POWER_PIN, s_power_state, &s_power_last);
    Drive_Pin(LED_PORT_A, LED_TEMP_PIN,  s_temp_state,  &s_temp_last);

    /* SYSTEM: 心跳闪烁 (500ms) */
    {
        static uint32_t s_heartbeat_last = 0;
        uint32_t now = Sys_Timer_Get_Tick();
        if (now - s_heartbeat_last >= 500) {
            s_heartbeat_last = now;
            GPIO_WriteBit(LED_PORT_A, LED_SYSTEM_PIN,
                          (BitAction)!GPIO_ReadOutputDataBit(LED_PORT_A, LED_SYSTEM_PIN));
        }
    }
}

/* ── 公开设置接口 ── */
void Led_Driver_Set_WiFi(Led_Driver_State state)   { s_wifi_state  = state; }
void Led_Driver_Set_Pwm(Led_Driver_State state)    { s_pwm_state   = state; }
void Led_Driver_Set_Com(Led_Driver_State state)    { s_com_state   = state; }
void Led_Driver_Set_Power(Led_Driver_State state)  { s_power_state = state; }
void Led_Driver_Set_Temp(Led_Driver_State state)   { s_temp_state  = state; }
void Led_Driver_Set_System(uint8_t on_off)          { s_system_on   = on_off; }
