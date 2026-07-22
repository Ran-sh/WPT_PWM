/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.c
 * @brief   全桥 PWM 驱动 — V5.0.2
 *
 *  硬件连接（TIM1全桥默认映射）:
 *  +---------------------------------------------------------+
 *  |                     STM32F103C8T6                        |
 *  |                                                          |
 *  |    PA8  --- TIM1_CH1  ------- 高侧驱动输入甲             |
 *  |    PB13 --- TIM1_CH1N ------- 低侧互补驱动输入甲         |
 *  |                                                          |
 *  |    PA9  --- TIM1_CH2  ------- 高侧驱动输入乙             |
 *  |    PB14 --- TIM1_CH2N ------- 低侧互补驱动输入乙         |
 *  |                                                          |
 *  |    向上计数，固定50%占空比，死区时间1000ns               |
 *  |    允许频率95kHz至150kHz                                 |
 *  |    预装载寄存器在同一更新事件中装载，避免半周期畸变      |
 *  |    上电时计数器和主输出均关闭，保证无脉冲输出            |
 *  +---------------------------------------------------------+
 *
 * @note    频率更新强制使用偶数周期计数，降低全桥偏磁风险。
 ******************************************************************************
 */

#include "Pwm_Driver.h"

/*
 * 死区寄存器值编译期计算, 避免运行时浮点开销
 * 死区计数值等于死区时间乘以72MHz时钟频率。
 * 在线性编码区间内，一个死区计数对应一个定时器时钟周期。
 */
#define PWM_DRIVER_DEADTIME_CYCLES \
    ((PWM_DRIVER_DEADTIME_NS) * 72 + 500) / 1000
typedef char Pwm_Driver_Deadtime_Check[(PWM_DRIVER_DEADTIME_CYCLES <= 127) ? 1 : -1];  /* 编译期断言: DTG 必须 ≤ 127 (7位线性段) */

void Pwm_Driver_Init(void)
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tim_base;
    TIM_OCInitTypeDef       oc;
    TIM_BDTRInitTypeDef     bdtr;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* 使用TIM1默认映射：PA8、PA9为主通道，PB13、PB14为互补通道。 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_Init(GPIOB, &gpio);

    /* 采用向上计数，输出频率等于72MHz除以自动重装值加一。 */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler         = 0;
    tim_base.TIM_Period            = 480 - 1;  /* 初始 150kHz */
    tim_base.TIM_CounterMode       = TIM_CounterMode_Up;
    tim_base.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_base);

    /* 两个主通道驱动全桥两组桥臂，并由互补通道加入死区。 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode       = TIM_OCMode_PWM1;
    oc.TIM_OutputState  = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse        = 240;  /* 150kHz时保持50%占空比 */
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity  = TIM_OCNPolarity_Low;    /* IR2103S低侧输入采用低有效逻辑 */
    oc.TIM_OCIdleState  = TIM_OCIdleState_Reset;   /* 关闭主输出时主通道保持低电平 */
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;  /* 关闭主输出时互补通道保持安全电平 */
    TIM_OC1Init(TIM1, &oc);

    oc.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OC2Init(TIM1, &oc);

    /* 死区 */
    TIM_BDTRStructInit(&bdtr);
    bdtr.TIM_OSSRState       = TIM_OSSRState_Disable;
    bdtr.TIM_OSSIState       = TIM_OSSIState_Disable;
    bdtr.TIM_LOCKLevel       = TIM_LOCKLevel_OFF;
    bdtr.TIM_DeadTime        = (uint8_t)PWM_DRIVER_DEADTIME_CYCLES;
    bdtr.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &bdtr);

    /* 预载使能 */
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);

    /* 初始化只配置定时器，必须由软启动控制层统一开启输出。 */
    TIM_Cmd(TIM1, DISABLE);
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
}

void Pwm_Driver_Enable(void)  { TIM_Cmd(TIM1, ENABLE); TIM_CtrlPWMOutputs(TIM1, ENABLE); }
void Pwm_Driver_Disable(void) { TIM_CtrlPWMOutputs(TIM1, DISABLE); TIM_Cmd(TIM1, DISABLE); }

uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)
{
    uint32_t primask;
    uint32_t ticks;

    if (freq_hz < PWM_DRIVER_FREQ_MIN_HZ) freq_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (freq_hz > PWM_DRIVER_FREQ_MAX_HZ) freq_hz = PWM_DRIVER_FREQ_MAX_HZ;

    ticks = SystemCoreClock / freq_hz;
    if (ticks % 2 != 0) ticks += 1;   /* 强制偶数周期，避免两半周不对称引起变压器偏磁。 */
    if (ticks < 2)  ticks = 2;
    if (ticks > 65536) ticks = 65536;

    /* 临界区内只写寄存器，避免中断观察到半更新状态。先恢复更新事件，
     * 再触发UG，确保ARR/CCR在同一边界装载且UG不会被UDIS屏蔽。 */
    primask = __get_PRIMASK();
    __disable_irq();
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR = (uint16_t)(ticks - 1);
    TIM1->CCR1 = (uint16_t)(ticks / 2);
    TIM1->CCR2 = (uint16_t)(ticks / 2);
    TIM1->CR1 &= ~TIM_CR1_UDIS;
    TIM1->EGR  = TIM_EGR_UG;
    __set_PRIMASK(primask);

    return SystemCoreClock / ticks;
}

uint32_t Pwm_Driver_Get_Frequency(void)
{
    uint32_t arr = TIM1->ARR;
    if (arr == 0) return PWM_DRIVER_FREQ_MIN_HZ;
    return SystemCoreClock / (arr + 1);
}

uint8_t Pwm_Driver_Is_Enabled(void)
{
    uint8_t counter_enabled;
    uint8_t output_enabled;

    counter_enabled = ((TIM1->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U;
    output_enabled = ((TIM1->BDTR & TIM_BDTR_MOE) != 0U) ? 1U : 0U;
    return (counter_enabled != 0U && output_enabled != 0U) ? 1U : 0U;
}
