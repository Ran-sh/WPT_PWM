/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.c
 * @brief   全桥 PWM 驱动 — V4.3.2
 *
 *  Pinout (TIM1 full-bridge, default mapping):
 *  +---------------------------------------------------------+
 *  |                     STM32F103C8T6                        |
 *  |                                                          |
 *  |    PA8  --- TIM1_CH1  ---+--- High-side Q1 (PWM1)        |
 *  |    PB13 --- TIM1_CH1N ---+--- Low-side  Q2 (complement,  |
 *  |                                                          |
 *  |    PA9  --- TIM1_CH2  ---+--- High-side Q3 (PWM2)        |
 *  |    PB14 --- TIM1_CH2N ---+--- Low-side  Q4 (complement,  |
 *  |                                                          |
 *  |    Up mode, 50% duty, deadtime 1000ns                    |
 *  |    Freq 95~150kHz (1kHz step)                            |
 *  |    OC/OCN polarity=Low, UDIS shadow-register atomic upd  |
 *  |    Power-on safe: TIM_Cmd(DISABLE)+MOE(DISABLE)          |
 *  +---------------------------------------------------------+
 *
 * @note    PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N
 *          Deadtime 1000ns, 50% duty, compile-time DTG calc
 ******************************************************************************
 */

#include "Pwm_Driver.h"

/*
 * 死区寄存器值编译期计算, 避免运行时浮点开销
 * DTG = DEADTIME_NS * 72MHz / 1e9 / Tdtg_step
 * 线性段 DTG[7:5]=0xx, Tdtg_step = 1/72MHz, DTG = DEADTIME_NS * 72 / 1000
 */
#define PWM_DRIVER_DEADTIME_CYCLES \
    ((PWM_DRIVER_DEADTIME_NS) * 72 + 500) / 1000
typedef char Pwm_Driver_Deadtime_Check[(PWM_DRIVER_DEADTIME_CYCLES <= 127) ? 1 : -1];  /* 编译期断言: DTG 必须 ≤ 127 (7位线性段) */

/** @brief 初始化 TIM1 全桥 PWM: 4通道+死区+预载, 初始全关零输出 */
void Pwm_Driver_Init(void)
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tim_base;
    TIM_OCInitTypeDef       oc;
    TIM_BDTRInitTypeDef     bdtr;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* 默认映射: PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_Init(GPIOB, &gpio);

    /* 时基: Up 计数, 72MHz/(ARR+1) = 目标频率 */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler         = 0;
    tim_base.TIM_Period            = 480 - 1;  /* 初始 150kHz */
    tim_base.TIM_CounterMode       = TIM_CounterMode_Up;
    tim_base.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_base);

    /* CH1=PWM1, CH2=PWM2 全桥对角线交替导通 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode       = TIM_OCMode_PWM1;
    oc.TIM_OutputState  = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse        = 240;  /* 50% @ 150kHz */
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity  = TIM_OCNPolarity_Low;    /* IR2103S LIN 低有效 */
    oc.TIM_OCIdleState  = TIM_OCIdleState_Reset;   /* MOE=0 CH=低 → 上管关 */
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;  /* MOE=0 CHN=低 → LIN反相后高 → 下管关 */
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

    /* 仅配置定时器但不启动, 按ON后再由 Soft_Start_Trigger 统一启动 */
    TIM_Cmd(TIM1, DISABLE);
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
}

/** @brief 开启 PWM 输出: 启动计数器 + MOE 使能 */
void Pwm_Driver_Enable(void)  { TIM_Cmd(TIM1, ENABLE); TIM_CtrlPWMOutputs(TIM1, ENABLE); }
/** @brief 关闭 PWM 输出: MOE 关断 + 计数器停止, 全桥归零 */
void Pwm_Driver_Disable(void) { TIM_CtrlPWMOutputs(TIM1, DISABLE); TIM_Cmd(TIM1, DISABLE); }

/** @brief 设置 PWM 频率并原子更新寄存器 (钳位 95~150kHz, 强制偶数 ticks 防偏磁)
 *  @param freq_hz 目标频率 (Hz)
 *  @retval 实际设定频率 (Hz) */
uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)
{
    uint32_t ticks;

    if (freq_hz < PWM_DRIVER_FREQ_MIN_HZ) freq_hz = PWM_DRIVER_FREQ_MIN_HZ;
    if (freq_hz > PWM_DRIVER_FREQ_MAX_HZ) freq_hz = PWM_DRIVER_FREQ_MAX_HZ;

    ticks = SystemCoreClock / freq_hz;
    if (ticks % 2 != 0) ticks += 1;   /* 强制偶数: 全桥拓扑需要对称驱动, 奇数分频导致两半周不对称→变压器偏磁饱和 */
    if (ticks < 2)  ticks = 2;
    if (ticks > 65536) ticks = 65536;

    /* 原子更新: UDIS 禁止影子寄存器刷新 → 写 ARR+CCR → UG 软件触发一次性加载 → 清 UDIS 恢复。确保新频率的 ARR 和 CCR 同步生效, 防止周期中裁剪导致输出毛刺 */
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR = (uint16_t)(ticks - 1);
    TIM1->CCR1 = (uint16_t)(ticks / 2);
    TIM1->CCR2 = (uint16_t)(ticks / 2);
    TIM1->EGR  = TIM_EGR_UG;
    TIM1->CR1 &= ~TIM_CR1_UDIS;

    return SystemCoreClock / ticks;
}

/** @brief 获取当前 PWM 频率 (Hz), 从 TIM1->ARR 实时计算 */
uint32_t Pwm_Driver_Get_Frequency(void)
{
    uint32_t arr = TIM1->ARR;
    if (arr == 0) return PWM_DRIVER_FREQ_MIN_HZ;
    return SystemCoreClock / (arr + 1);
}
