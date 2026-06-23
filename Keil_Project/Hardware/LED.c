/**
 ******************************************************************************
 * @file    Hardware/LED.c
 * @brief   系统 LED 驱动 (PA15 心跳 + PB3 PWM状态 + PB4 Ready)
 * @note    PA15=SYSTEM 心跳 (高有效, 500ms 翻转)
 *          PB3=PWM 状态 (高有效, 扫频快闪/稳态慢闪), PB4=Ready (高有效)
 *          PB3=JTDO→GPIO, 需禁用 JTAG 保留 SWD
 *          ST-Link 用 SWD (PA13/PA14), 不受 JTAG 禁用影响
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"
#include "LED.h"

/* ── 闪烁半周期 (ms) ── */
#define LED_SLOW_MS   500
#define LED_FAST_MS   100

/* ── 引脚定义 ── */
#define LED_SYSTEM_PIN   GPIO_Pin_15   /* PA15 — 系统心跳 (高有效) */
#define LED_SYSTEM_PORT  GPIOA
#define LED_PWM_PIN      GPIO_Pin_3    /* PB3 — PWM 运行状态 (高有效) */
#define LED_PWM_PORT     GPIOB
#define LED_READY_PIN    GPIO_Pin_4    /* PB4 — Ready 就绪 (高有效) */
#define LED_READY_PORT   GPIOB

/* ── 私有状态 ── */
static LedState_t s_pwm_state  = LED_OFF;

/* 每引脚独立时间戳, 避免互窜闪烁节拍 */
static uint32_t   s_pwm_last   = 0;

/* ═══════════════════════════════════════════════════════════════
 *  LED_Init — JTAG→GPIO + 全部引脚初始化
 * ═══════════════════════════════════════════════════════════════ */

void LED_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);

    /*
     * 禁用 JTAG → PB3/PB4/JTDO/JNTRST 释放为 GPIO
     * SWD (PA13/PA14) 保留, ST-Link 不受影响
     */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* ── PA15 系统心跳 (高有效) ── */
    gpio.GPIO_Pin = LED_SYSTEM_PIN;
    GPIO_Init(LED_SYSTEM_PORT, &gpio);
    GPIO_ResetBits(LED_SYSTEM_PORT, LED_SYSTEM_PIN);   /* 初始灭 */

    /* ── PB3 PWM 状态 (高有效) ── */
    gpio.GPIO_Pin = LED_PWM_PIN;
    GPIO_Init(LED_PWM_PORT, &gpio);
    GPIO_ResetBits(LED_PWM_PORT, LED_PWM_PIN);         /* 初始灭 */

    /* ── PB4 Ready (高有效) ── */
    gpio.GPIO_Pin = LED_READY_PIN;
    GPIO_Init(LED_READY_PORT, &gpio);
    GPIO_ResetBits(LED_READY_PORT, LED_READY_PIN);     /* 初始灭 */

    /*
     * 上电自检: PB3/PB4/PA15 三灯全亮 1 秒 → 全灭
     * 确认硬件焊接无误, 然后进入正常状态逻辑。
     * SysTimer 此时未初始化, 用 volatile 空循环做粗延时 (~1s @72MHz)
     */
    GPIO_SetBits(LED_PWM_PORT, LED_PWM_PIN);           /* PB3 亮 */
    GPIO_SetBits(LED_READY_PORT, LED_READY_PIN);        /* PB4 亮 */
    GPIO_SetBits(LED_SYSTEM_PORT, LED_SYSTEM_PIN);     /* PA15 亮 */

    {
        volatile uint32_t i;
        for (i = 0; i < 7200000; i++) { __NOP(); }  /* ~1s 粗延时 */
    }

    GPIO_ResetBits(LED_PWM_PORT, LED_PWM_PIN);          /* PB3 灭 */
    GPIO_ResetBits(LED_READY_PORT, LED_READY_PIN);      /* PB4 灭 */
    GPIO_ResetBits(LED_SYSTEM_PORT, LED_SYSTEM_PIN);   /* PA15 灭 */
}

/* ═══════════════════════════════════════════════════════════════
 *  PA15 心跳 (500ms 翻转, 1Hz 慢闪)
 * ═══════════════════════════════════════════════════════════════ */

void LED_Task(void)
{
    static uint32_t last = 0;

    if (SysTimer_GetTick() - last >= 500)
    {
        last = SysTimer_GetTick();
        GPIO_WriteBit(LED_SYSTEM_PORT, LED_SYSTEM_PIN,
            (BitAction)(1 - GPIO_ReadOutputDataBit(LED_SYSTEM_PORT, LED_SYSTEM_PIN)));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  通用闪烁驱动
 *  p_last 指向对应引脚的时间戳, 保证各引脚独立节拍
 * ═══════════════════════════════════════════════════════════════ */

static void LED_Drive(GPIO_TypeDef *port, uint16_t pin, LedState_t state,
                      uint32_t *p_last)
{
    uint32_t half_period;

    switch (state)
    {
        case LED_OFF:
            GPIO_ResetBits(port, pin);
            return;

        case LED_SOLID:
            GPIO_SetBits(port, pin);
            return;

        case LED_SLOW:
            half_period = LED_SLOW_MS;
            break;

        case LED_FAST:
            half_period = LED_FAST_MS;
            break;

        default:
            return;
    }

    if (SysTimer_GetTick() - *p_last >= half_period)
    {
        *p_last = SysTimer_GetTick();
        GPIO_WriteBit(port, pin,
            (BitAction)(1 - GPIO_ReadOutputDataBit(port, pin)));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */

void LED_Update_PWM(LedState_t state)
{
    s_pwm_state = state;
    if (state == LED_OFF || state == LED_SOLID)
        LED_Drive(LED_PWM_PORT, LED_PWM_PIN, state, &s_pwm_last);
}

void LED_Update_Ready(uint8_t on_off)
{
    if (on_off)
        GPIO_SetBits(LED_READY_PORT, LED_READY_PIN);
    else
        GPIO_ResetBits(LED_READY_PORT, LED_READY_PIN);
}

/*
 * 闪烁 Task — 由 UI_Task 内部调用
 * 驱动 PB3 的慢闪/快闪 (PB4 = Ready 为静态电平无需驱动)
 */
void LED_Status_Task(void)
{
    if (s_pwm_state == LED_SLOW || s_pwm_state == LED_FAST)
        LED_Drive(LED_PWM_PORT, LED_PWM_PIN, s_pwm_state, &s_pwm_last);
}
