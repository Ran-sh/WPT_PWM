/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.c
 * @brief   有源蜂鸣器驱动 — V5.1.0
 *
 *  硬件连接:
 *  +-------------------------------------------------------+
 *  |                    STM32F103C8T6                       |
 *  |                                                        |
 *  |    PB15 --- 1k限流电阻 --- S8050基极                   |
 *  |    S8050集电极 ---------- 有源蜂鸣器负极              |
 *  |    S8050发射极 ---------- GND                          |
 *  |                                                        |
 *  |    PB15输出高电平时，S8050导通，蜂鸣器发声             |
 *  |    间歇模式：响200ms，停800ms                          |
 *  +-------------------------------------------------------+
 *
 * @note    使用三极管隔离蜂鸣器负载，禁止由引脚直接承担大电流。
 ******************************************************************************
 */

#include "Buzzer_Driver.h"
#include "Sys_Timer.h"

#define BUZZER_DRIVER_PIN         GPIO_Pin_15
#define BUZZER_DRIVER_PORT        GPIOB
#define BUZZER_DRIVER_BEEP_ON_MS  200
#define BUZZER_DRIVER_BEEP_OFF_MS 800

static Buzzer_Driver_State s_state = BUZZER_DRIVER_STATE_OFF;
static uint32_t s_last_beep = 0U;
static uint8_t s_beep_on = 0U;

void Buzzer_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin   = BUZZER_DRIVER_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZZER_DRIVER_PORT, &cfg);

    GPIO_ResetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
}

void Buzzer_Driver_Task(void)
{
    if (s_state == BUZZER_DRIVER_STATE_OFF) {
        GPIO_ResetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
        s_beep_on = 0;
        return;
    }

    if (s_state == BUZZER_DRIVER_STATE_ON) {
        GPIO_SetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
        s_beep_on = 0;
        return;
    }

    /* 间歇模式采用响200ms、停800ms的节奏，兼顾提醒效果与听感。 */
    {
        uint32_t now    = Sys_Timer_Get_Tick();
        uint32_t period = s_beep_on ? BUZZER_DRIVER_BEEP_ON_MS
                                    : BUZZER_DRIVER_BEEP_OFF_MS;

        if (now - s_last_beep >= period) {
            s_last_beep = now;
            s_beep_on = !s_beep_on;
            GPIO_WriteBit(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN,
                          s_beep_on ? Bit_SET : Bit_RESET);
        }
    }
}

void Buzzer_Driver_Set_State(Buzzer_Driver_State state)
{
    if (state == BUZZER_DRIVER_STATE_OFF) {
        s_beep_on = 0U;
        GPIO_ResetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
    }
    else if (state == BUZZER_DRIVER_STATE_ON) {
        s_beep_on = 0U;
        GPIO_SetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
    }
    else if (state == BUZZER_DRIVER_STATE_BEEP &&
             s_state != BUZZER_DRIVER_STATE_BEEP) {
        s_beep_on = 1U;
        s_last_beep = Sys_Timer_Get_Tick();
        GPIO_SetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
    }
    s_state = state;
}
