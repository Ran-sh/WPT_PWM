/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.c
 * @brief   有源蜂鸣器驱动 - V4.3.2
 */

#include "Buzzer_Driver.h"
#include "Sys_Timer.h"

#define BUZZER_DRIVER_PIN         GPIO_Pin_15   /* PB15 - 蜂鸣器 I/O */
#define BUZZER_DRIVER_PORT        GPIOB         /* 端口 B */
#define BUZZER_DRIVER_BEEP_ON_MS  200    /* BEEP 响 200ms */
#define BUZZER_DRIVER_BEEP_OFF_MS 800    /* BEEP 停 800ms */

static Buzzer_Driver_State s_state = BUZZER_DRIVER_STATE_OFF;

/** @brief 初始化 PB15 推挽输出, 初始低电平 -> 蜂鸣器静音 */
void Buzzer_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin   = BUZZER_DRIVER_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZZER_DRIVER_PORT, &cfg);

    GPIO_ResetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);  /* 初始低电平, 静音 */
}

/** @brief 周期任务: 根据 s_state 自动控制 ON/OFF/BEEP 间歇蜂鸣 */
void Buzzer_Driver_Task(void)
{
    static uint32_t s_last_beep = 0;               /* 上次翻转时间戳 */
    static uint8_t  s_beep_on   = 0;               /* 当前蜂鸣器电平状态 */

    if (s_state == BUZZER_DRIVER_STATE_OFF) {      /* 静音 -> 拉低关断 */
        GPIO_ResetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
        s_beep_on = 0;
        return;
    }

    if (s_state == BUZZER_DRIVER_STATE_ON) {       /* 持续响 -> 拉高导通 */
        GPIO_SetBits(BUZZER_DRIVER_PORT, BUZZER_DRIVER_PIN);
        s_beep_on = 0;
        return;
    }

    /* BEEP 间歇模式: 200ms ON + 800ms OFF = 20% 占空比, 引起注意但不刺耳 */
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

/** @brief 设置蜂鸣器工作模式 (OFF=静音, ON=持续响, BEEP=间歇 200ms/800ms) */
void Buzzer_Driver_Set_State(Buzzer_Driver_State state)
{
    s_state = state;
}
