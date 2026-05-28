/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.c
 * @brief   全桥 PWM 驱动 — 实现
 ******************************************************************************
 */

#include "Pwm_Driver.h"

/*
 * 死区寄存器值由宏在编译期计算, 避免运行时浮点开销
 * DTG = DEADTIME_NS * 72MHz / 1e9 / Tdtg_step
 * 线性段 DTG[7:5]=0xx, Tdtg_step = 1/72MHz, DTG = DEADTIME_NS * 72 / 1000
 */
#define DEADTIME_CYCLES  ((PWM_DRIVER_DEADTIME_NS) * 72 + 500) / 1000
typedef char deadtime_linear_check[(DEADTIME_CYCLES <= 127) ? 1 : -1];

static uint32_t s_current_freq = 150000;  /* 上电默认 150kHz, 启动后由软启动覆盖 */

void Pwm_Driver_Init(void)
{
    GPIO_InitTypeDef      gpio;
    TIM_TimeBaseInitTypeDef  tim_base;
    TIM_OCInitTypeDef        oc;
    TIM_BDTRInitTypeDef      bdtr;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* PA8=TIM1_CH1, PA9=TIM1_CH2, PA7=TIM1_CH1N, PB0=TIM1_CH2N */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);

    /* 时基: 中心对齐模式 3, 72MHz/(ARR+1) = 目标频率*2 */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler         = 0;
    tim_base.TIM_Period            = 480 - 1;  /* 初始 150kHz: ARR = 72M/(2*150k) = 240, *2对=480-1 */
    tim_base.TIM_CounterMode       = TIM_CounterMode_CenterAligned3;
    tim_base.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_base);

    /* CH1+CH1N / CH2+CH2N, 50% 占空, 互补输出 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse        = (480 - 1) / 2;  /* 50% */
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity  = TIM_OCNPolarity_High;
    oc.TIM_OCIdleState  = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &oc);
    TIM_OC2Init(TIM1, &oc);

    /* 死区 */
    TIM_BDTRStructInit(&bdtr);
    bdtr.TIM_OSSRState  = TIM_OSSRState_Enable;
    bdtr.TIM_OSSIState  = TIM_OSSIState_Enable;
    bdtr.TIM_LOCKLevel  = TIM_LOCKLevel_OFF;
    bdtr.TIM_DeadTime   = (uint8_t)DEADTIME_CYCLES;
    bdtr.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &bdtr);

    /* 预载使能 (须在 TIM_Cmd 之前) */
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);

    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, DISABLE);  /* MOE 关 → 安全态, 无输出 */
}

void Pwm_Driver_Enable(void)  { TIM_CtrlPWMOutputs(TIM1, ENABLE); }
void Pwm_Driver_Disable(void) { TIM_CtrlPWMOutputs(TIM1, DISABLE); }

uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)
{
    uint32_t arr, ccr;

    if (freq_hz < PWM_DRIVER_FREQ_MIN_HZ) freq_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (freq_hz > PWM_DRIVER_FREQ_MAX_HZ) freq_hz = PWM_DRIVER_FREQ_MAX_HZ;

    /* 中心对齐: TIM1 计数频率 = Fclk / (2 * ARR), ARR = 72M / (2 * freq) */
    arr = SystemCoreClock / (2 * freq_hz);
    if (arr < 2)  arr = 2;
    if (arr > 65535) arr = 65535;
    ccr = arr / 2;   /* 50% 占空 */

    /* 原子更新: UDIS 禁止更新事件 → 写 ARR+CCR → UG 软件更新 → 清 UDIS */
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR = arr;
    TIM1->CCR1 = ccr;
    TIM1->CCR2 = ccr;
    TIM1->EGR  = TIM_EGR_UG;
    TIM1->CR1 &= ~TIM_CR1_UDIS;

    s_current_freq = SystemCoreClock / (2 * arr);
    return s_current_freq;
}

uint32_t Pwm_Driver_Get_Frequency(void)
{
    uint32_t arr = TIM1->ARR;
    if (arr == 0) return PWM_DRIVER_FREQ_MIN_HZ;
    return SystemCoreClock / (2 * arr);
}
