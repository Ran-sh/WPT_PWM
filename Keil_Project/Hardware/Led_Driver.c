/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   LED 指示灯驱动 — V5.0.1 (4 LEDs)
 *
 *  Pinout (JTAG disabled -> PB3/PB4 freed as GPIO):
 *  +----------------------------------------------------------+
 *  |                    STM32F103C8T6                          |
 *  |                                                           |
 *  |    PA15 --- GPIO_PP --- LED_STATUS   (yellow) PWM indicat |
 *  |              OFF=idle/fault  SLOW=sweep  ON=running       |
 *  |                                                           |
 *  |    PC13 --- GPIO_PP --- LED_HEARTBEAT (blue)  MCU alive   |
 *  |              active LOW: LOW=ON, HIGH=OFF, 500ms toggle   |
 *  |                                                           |
 *  |    PB4  --- GPIO_PP --- LED_WIFI     (blue)  WiFi status  |
 *  |              ON=online  SLOW=reconnect  OFF=offline       |
 *  |                                                           |
 *  |    PB3  --- GPIO_PP --- LED_POWER    (green) 12V indicat  |
 *  |              ON=12V enabled  OFF=12V disabled             |
 *  |                                                           |
 *  |    Each LED: GPIO -> R (220 ohm) -> LED anode -> GND      |
 *  |    PC13: onboard LED, active LOW (cathode to PC13)        |
 *  +----------------------------------------------------------+
 *
 * @note    V5.0: PA10/PA11 removed, PA12->TFT_BL
 *          STATUS LED (PA15) reflects PWM state
 *          HEARTBEAT LED (PC13) = MCU program alive indicator
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

#define LED_DRIVER_WIFI_PIN       GPIO_Pin_4   /* PB4 — WiFi status, active HIGH */
#define LED_DRIVER_POWER_PIN      GPIO_Pin_3   /* PB3 — 12V power, active HIGH */
#define LED_DRIVER_STATUS_PIN     GPIO_Pin_15  /* PA15 — PWM state, active HIGH */
#define LED_DRIVER_HEARTBEAT_PIN  GPIO_Pin_13  /* PC13 — MCU alive, active LOW */

#define LED_DRIVER_PORT_A      GPIOA
#define LED_DRIVER_PORT_B      GPIOB
#define LED_DRIVER_PORT_C      GPIOC

#define LED_DRIVER_BLINK_SLOW_PERIOD_MS   500
#define LED_DRIVER_BLINK_FAST_PERIOD_MS   200
#define LED_DRIVER_HEARTBEAT_PERIOD_MS    500

/* -- Static state -- */
static Led_Driver_State s_wifi_state      = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_status_state    = LED_DRIVER_STATE_OFF;
static uint8_t          s_power_on        = 0;
static uint8_t          s_heartbeat_on    = 0;

static uint32_t s_wifi_last      = 0;
static uint32_t s_status_last    = 0;
static uint32_t s_heartbeat_last = 0;

/* -- Per-pin blink driver (active HIGH) -- */
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

void Led_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    /* PA15 = STATUS LED (active HIGH) */
    cfg.GPIO_Pin = LED_DRIVER_STATUS_PIN;
    GPIO_Init(LED_DRIVER_PORT_A, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);

    /* PC13 = HEARTBEAT LED (active LOW: HIGH=OFF initially) */
    cfg.GPIO_Pin = LED_DRIVER_HEARTBEAT_PIN;
    GPIO_Init(LED_DRIVER_PORT_C, &cfg);
    GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);

    /* PB3 = POWER, PB4 = WIFI */
    cfg.GPIO_Pin = LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN;
    GPIO_Init(LED_DRIVER_PORT_B, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);

    /* Power-on self-test: all ON 500ms (SysTimer not yet ready, busy-wait) */
    GPIO_SetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);   /* PA15 = ON */
    GPIO_ResetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);  /* PC13 LOW = ON */
    GPIO_SetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
    { volatile uint32_t i; for (i = 0; i < 175000; i++) __NOP(); }  /* ~500ms @72MHz */
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);
    GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);   /* PC13 HIGH = OFF */
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
}

void Led_Driver_Task(void)
{
    Drive_Pin(LED_DRIVER_PORT_B, LED_DRIVER_WIFI_PIN,   s_wifi_state,   &s_wifi_last);
    Drive_Pin(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN,  s_status_state, &s_status_last);

    /* POWER LED: direct GPIO follow s_power_on (no state machine) */
    if (s_power_on)
        GPIO_SetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);
    else
        GPIO_ResetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);

    /* HEARTBEAT LED (PC13, active LOW): 500ms toggle, MCU alive indicator */
    if (s_heartbeat_on) {
        uint32_t now = Sys_Timer_Get_Tick();
        if (now - s_heartbeat_last >= LED_DRIVER_HEARTBEAT_PERIOD_MS) {
            s_heartbeat_last = now;
            GPIO_WriteBit(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN,
                          (BitAction)!GPIO_ReadOutputDataBit(
                              LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN));
        }
    } else {
        GPIO_SetBits(LED_DRIVER_PORT_C, LED_DRIVER_HEARTBEAT_PIN);  /* HIGH = OFF */
    }
}

void Led_Driver_Set_WiFi(Led_Driver_State state)    { s_wifi_state    = state; }
void Led_Driver_Set_Power(uint8_t on)                { s_power_on      = on;    }
void Led_Driver_Set_Status(Led_Driver_State state)   { s_status_state  = state; }
void Led_Driver_Set_Heartbeat(uint8_t on)            { s_heartbeat_on  = on;    }
