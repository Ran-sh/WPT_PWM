/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   四路状态指示灯驱动 — V5.1.0
 *
 *  硬件连接（关闭JTAG后，PB3、PB4和PA15作为普通引脚）:
 *  +----------------------------------------------------------+
 *  |                    STM32F103C8T6                          |
 *  |                                                           |
 *  |    PA15 --- 状态灯（黄色）                                |
 *  |              停机或故障时灭，扫频时慢闪，运行时常亮       |
 *  |                                                           |
 *  |    PC13 --- 心跳灯（板载蓝灯）                            |
 *  |              低电平点亮，每500ms翻转一次                 |
 *  |                                                           |
 *  |    PB4  --- 无线状态灯（蓝色）                            |
 *  |              在线时常亮，重连时慢闪，离线时熄灭           |
 *  |                                                           |
 *  |    PB3  --- 电源状态灯（绿色）                            |
 *  |              12V开启时常亮，关闭时熄灭                    |
 *  |                                                           |
 *  |    外接灯：引脚经220欧电阻接发光二极管正极，再接地        |
 *  |    板载心跳灯为低电平有效                                 |
 *  +----------------------------------------------------------+
 *
 * @note    PA10和PA11不再使用，PA12已经改作显示屏背光控制。
 *          PA15反映PWM状态，PC13只表示主程序仍在运行。
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

#define LED_DRIVER_WIFI_PIN       GPIO_Pin_4   /* PB4：无线状态灯，高电平点亮 */
#define LED_DRIVER_POWER_PIN      GPIO_Pin_3   /* PB3：12V电源灯，高电平点亮 */
#define LED_DRIVER_STATUS_PIN     GPIO_Pin_15  /* PA15：PWM状态灯，高电平点亮 */
#define LED_DRIVER_HEARTBEAT_PIN  GPIO_Pin_13  /* PC13：程序心跳灯，低电平点亮 */

#define LED_DRIVER_PORT_A      GPIOA
#define LED_DRIVER_PORT_B      GPIOB
#define LED_DRIVER_PORT_C      GPIOC

#define LED_DRIVER_BLINK_SLOW_PERIOD_MS   500
#define LED_DRIVER_BLINK_FAST_PERIOD_MS   200
#define LED_DRIVER_HEARTBEAT_PERIOD_MS    500

/* 模块内部状态。 */
static Led_Driver_State s_wifi_state      = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_status_state    = LED_DRIVER_STATE_OFF;
static uint8_t          s_power_on        = 0;
static uint8_t          s_heartbeat_on    = 0;

static uint32_t s_wifi_last      = 0;
static uint32_t s_status_last    = 0;
static uint32_t s_heartbeat_last = 0;

/* 高电平有效指示灯的闪烁驱动。 */
static void Led_Driver_Drive_Pin(GPIO_TypeDef* port, uint16_t pin,
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

void Led_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    /* PA15状态灯采用高电平点亮。 */
    cfg.GPIO_Pin = LED_DRIVER_STATUS_PIN;
    GPIO_Init(LED_DRIVER_PORT_A, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);

    /* PC13心跳灯采用低电平点亮，初始化时先保持熄灭。 */
    cfg.GPIO_Pin = LED_DRIVER_HEARTBEAT_PIN;
    GPIO_Init(LED_DRIVER_PORT_C, &cfg);
    GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);

    /* PB3用于电源状态，PB4用于无线状态。 */
    cfg.GPIO_Pin = LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN;
    GPIO_Init(LED_DRIVER_PORT_B, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);

    /* 上电自检时四个指示灯同时点亮500ms。 */
    GPIO_SetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);   /* 点亮PA15状态灯 */
    GPIO_ResetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);  /* 拉低PC13点亮心跳灯 */
    GPIO_SetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
    Sys_Timer_Delay_Ms(500U);
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);
    GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);   /* 拉高PC13熄灭心跳灯 */
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
}

void Led_Driver_Task(void)
{
    Led_Driver_Drive_Pin(LED_DRIVER_PORT_B, LED_DRIVER_WIFI_PIN,   s_wifi_state,   &s_wifi_last);
    Led_Driver_Drive_Pin(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN,  s_status_state, &s_status_last);

    /* 电源灯直接跟随12V使能状态，不参与闪烁状态机。 */
    if (s_power_on)
        GPIO_SetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);
    else
        GPIO_ResetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);

    /* 心跳灯每500ms翻转一次，用于表明主循环仍在运行。 */
    if (s_heartbeat_on) {
        uint32_t now = Sys_Timer_Get_Tick();
        if (now - s_heartbeat_last >= LED_DRIVER_HEARTBEAT_PERIOD_MS) {
            s_heartbeat_last = now;
            GPIO_WriteBit(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN,
                          (BitAction)!GPIO_ReadOutputDataBit(
                              LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN));
        }
    } else {
        GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);  /* 高电平熄灭 */
    }
}

void Led_Driver_Set_WiFi(Led_Driver_State state)    { s_wifi_state    = state; }
void Led_Driver_Set_Power(uint8_t on)                { s_power_on      = on;    }
void Led_Driver_Set_Status(Led_Driver_State state)   { s_status_state  = state; }
void Led_Driver_Set_Heartbeat(uint8_t on)            { s_heartbeat_on  = on;    }
