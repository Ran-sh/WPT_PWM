/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.c
 * @brief   有源蜂鸣器驱动 — 实现 (V6.2)
 * @note    PB15 GPIO PP 输出, 高电平→S8050导通→蜂鸣器鸣响
 *          BEEP 模式: 200ms 鸣/ 800ms 停 交替
 ******************************************************************************
 */

#include "Buzzer_Driver.h"
#include "Sys_Timer.h"

#define BUZZER_PIN   GPIO_Pin_15
#define BUZZER_PORT  GPIOB

#define BEEP_ON_MS   200
#define BEEP_OFF_MS  800

static Buzzer_Driver_State s_state = BUZZER_DRIVER_STATE_OFF;

void Buzzer_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin   = BUZZER_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZZER_PORT, &cfg);

    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);  /* 初始关闭 */
}

void Buzzer_Driver_Task(void)
{
    static uint32_t last_beep = 0;
    static uint8_t  beep_on  = 0;

    if (s_state == BUZZER_DRIVER_STATE_OFF) {
        GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);
        beep_on = 0;
        return;
    }

    if (s_state == BUZZER_DRIVER_STATE_ON) {
        GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);
        beep_on = 0;
        return;
    }

    /* BEEP 间歇模式 */
    {
        uint32_t now = Sys_Timer_Get_Tick();
        uint32_t period = beep_on ? BEEP_ON_MS : BEEP_OFF_MS;

        if (now - last_beep >= period) {
            last_beep = now;
            beep_on = !beep_on;
            GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN,
                          beep_on ? Bit_SET : Bit_RESET);
        }
    }
}

void Buzzer_Driver_Set_State(Buzzer_Driver_State state)
{
    s_state = state;
}
