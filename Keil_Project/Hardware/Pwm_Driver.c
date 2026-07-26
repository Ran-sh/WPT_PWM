/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.c
 * @brief   全桥PWM驱动，支持20kHz至200kHz硬件钳位 — V5.1.3
 * @note    TIM1保持50%占空比、1000ns死区及UDIS原子更新。
 ******************************************************************************
 */

#include "Pwm_Driver.h"

#define PWM_DRIVER_DEADTIME_CYCLES \
    (((PWM_DRIVER_DEADTIME_NS) * 72U + 500U) / 1000U)

typedef char Pwm_Driver_Deadtime_Check[
    (PWM_DRIVER_DEADTIME_CYCLES <= 127U) ? 1 : -1];

void Pwm_Driver_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim_base;
    TIM_OCInitTypeDef oc;
    TIM_BDTRInitTypeDef bdtr;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_Init(GPIOB, &gpio);

    /* 初始周期仅作关断态寄存器占位；实际输出统一经频率接口钳位。 */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler = 0U;
    tim_base.TIM_Period = 480U - 1U;
    tim_base.TIM_CounterMode = TIM_CounterMode_Up;
    tim_base.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM1, &tim_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse = 240U;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity = TIM_OCNPolarity_Low;
    oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &oc);
    oc.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OC2Init(TIM1, &oc);

    TIM_BDTRStructInit(&bdtr);
    bdtr.TIM_OSSRState = TIM_OSSRState_Disable;
    bdtr.TIM_OSSIState = TIM_OSSIState_Disable;
    bdtr.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
    bdtr.TIM_DeadTime = (uint8_t)PWM_DRIVER_DEADTIME_CYCLES;
    bdtr.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &bdtr);

    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);

    TIM_Cmd(TIM1, DISABLE);
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
}

void Pwm_Driver_Enable(void)
{
    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

void Pwm_Driver_Disable(void)
{
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
    TIM_Cmd(TIM1, DISABLE);
}

uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)
{
    uint32_t primask;
    uint32_t ticks;

    if (freq_hz < PWM_DRIVER_FREQ_MIN_HZ) freq_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (freq_hz > PWM_DRIVER_FREQ_MAX_HZ) freq_hz = PWM_DRIVER_FREQ_MAX_HZ;

    ticks = SystemCoreClock / freq_hz;
    if ((ticks % 2U) != 0U) ticks += 1U;
    if (ticks < 2U) ticks = 2U;
    if (ticks > 65536U) ticks = 65536U;

    primask = __get_PRIMASK();
    __disable_irq();
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR = (uint16_t)(ticks - 1U);
    TIM1->CCR1 = (uint16_t)(ticks / 2U);
    TIM1->CCR2 = (uint16_t)(ticks / 2U);
    TIM1->CR1 &= (uint16_t)(~TIM_CR1_UDIS);
    TIM1->EGR = TIM_EGR_UG;
    __set_PRIMASK(primask);

    return SystemCoreClock / ticks;
}

uint32_t Pwm_Driver_Get_Frequency(void)
{
    uint32_t arr = TIM1->ARR;
    if (arr == 0U) return PWM_DRIVER_FREQ_MIN_HZ;
    return SystemCoreClock / (arr + 1U);
}

uint8_t Pwm_Driver_Is_Enabled(void)
{
    uint8_t counter_enabled;
    uint8_t output_enabled;

    counter_enabled = ((TIM1->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U;
    output_enabled = ((TIM1->BDTR & TIM_BDTR_MOE) != 0U) ? 1U : 0U;
    return (counter_enabled != 0U && output_enabled != 0U) ? 1U : 0U;
}
