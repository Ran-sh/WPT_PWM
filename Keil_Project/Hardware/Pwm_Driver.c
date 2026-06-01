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

    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE);

    /* PA8=TIM1_CH1, PA9=TIM1_CH2, PA7=TIM1_CH1N, PB0=TIM1_CH2N (需 PartialRemap) */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);

    /* 时基: Up 计数, 72MHz/(ARR+1) = 目标频率 */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler         = 0;
    tim_base.TIM_Period            = 480 - 1;  /* 初始 150kHz: ARR = 72M/150k - 1 = 479 */
    tim_base.TIM_CounterMode       = TIM_CounterMode_Up;
    tim_base.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_base);

    /* CH1=PWM1, CH2=PWM2 实现全桥对角线交替导通 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse        = 240;  /* 50% @ 150kHz */
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity  = TIM_OCNPolarity_Low;    /* IR2103S LIN 低有效 */
    oc.TIM_OCIdleState  = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Set;   /* MOE=0 时下管关断 */
    TIM_OC1Init(TIM1, &oc);

    oc.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OC2Init(TIM1, &oc);

    /* 死区 */
    TIM_BDTRStructInit(&bdtr);
    bdtr.TIM_OSSRState  = TIM_OSSRState_Disable;
    bdtr.TIM_OSSIState  = TIM_OSSIState_Disable;
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
    uint32_t ticks;

    if (freq_hz < PWM_DRIVER_FREQ_MIN_HZ) freq_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (freq_hz > PWM_DRIVER_FREQ_MAX_HZ) freq_hz = PWM_DRIVER_FREQ_MAX_HZ;

    /* Up 计数: f = SystemCoreClock / (ARR+1), ARR+1 = ticks */
    ticks = SystemCoreClock / freq_hz;
    if (ticks % 2 != 0) ticks += 1;   /* 强制偶数, 防偏磁 */
    if (ticks < 2)  ticks = 2;
    if (ticks > 65536) ticks = 65536;

    /* 原子更新: UDIS 禁止更新事件 → 写 ARR+CCR → UG 软件更新 → 清 UDIS */
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR = (uint16_t)(ticks - 1);
    TIM1->CCR1 = (uint16_t)(ticks / 2);
    TIM1->CCR2 = (uint16_t)(ticks / 2);
    TIM1->EGR  = TIM_EGR_UG;
    TIM1->CR1 &= ~TIM_CR1_UDIS;

    s_current_freq = SystemCoreClock / ticks;
    return s_current_freq;
}

uint32_t Pwm_Driver_Get_Frequency(void)
{
    uint32_t arr = TIM1->ARR;
    if (arr == 0) return PWM_DRIVER_FREQ_MIN_HZ;
    return SystemCoreClock / (arr + 1);
}
