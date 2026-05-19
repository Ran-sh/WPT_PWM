/**
 ******************************************************************************
 * @file    Hardware/LED.c
 * @brief   系统 LED 驱动 (PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready)
 * @note    PB3=JTDO→GPIO, PB4=JNTRST→GPIO, 需禁用 JTAG 保留 SWD
 *          PB3/PB4/PB5 高电平亮, PC13 低电平亮
 *          ST-Link 用 SWD (PA13/PA14), 不受 JTAG 禁用影响
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"
#include "LED.h"

/* ── 闪烁半周期 (ms) ── */
#define LED_SLOW_MS   500
#define LED_FAST_MS   100

/* ── 私有状态 ── */
static LedState_t s_wifi_state = LED_OFF;
static LedState_t s_pwm_state  = LED_OFF;

/* 每引脚独立时间戳, 避免互窜闪烁节拍 */
static uint32_t   s_wifi_last  = 0;
static uint32_t   s_pwm_last   = 0;

/* ═══════════════════════════════════════════════════════════════
 *  LED_Init — JTAG→GPIO + 全部引脚初始化
 * ═══════════════════════════════════════════════════════════════ */

void LED_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC |
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

    /* ── PC13 心跳 (低有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);   /* 高=灭 */

    /* ── PB3 WiFi (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_3);  /* 低=灭 */

    /* ── PB4 PWM (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_4);

    /* ── PB5 Ready (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_5;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_5);    /* 初始亮=可操作 */

    /*
     * 上电自检: PB3/PB4/PB5 三灯全亮 1 秒 → 全灭
     * 确认硬件焊接无误, 然后进入正常状态逻辑。
     * SysTimer 此时未初始化, 用 volatile 空循环做粗延时 (~1s @72MHz)
     */
    GPIO_SetBits(GPIOB, GPIO_Pin_3);   /* PB3 亮 */
    GPIO_SetBits(GPIOB, GPIO_Pin_4);   /* PB4 亮 */
    /* PB5 已亮 */

    {
        volatile uint32_t i;
        for (i = 0; i < 7200000; i++) { __NOP(); }  /* ~1s 粗延时 */
    }

    GPIO_ResetBits(GPIOB, GPIO_Pin_3);  /* PB3 灭 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_4);  /* PB4 灭 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_5);  /* PB5 灭 */
}

/* ═══════════════════════════════════════════════════════════════
 *  PC13 心跳 (500ms 翻转, 1Hz 慢闪)
 * ═══════════════════════════════════════════════════════════════ */

void LED_Task(void)
{
    static uint32_t last = 0;

    if (SysTimer_GetTick() - last >= 500)
    {
        last = SysTimer_GetTick();
        GPIO_WriteBit(GPIOC, GPIO_Pin_13,
            (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13)));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  通用闪烁驱动
 *  p_last 指向对应引脚的时间戳, 保证 PB3/PB4 独立节拍
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

void LED_Update_WiFi(LedState_t state)
{
    s_wifi_state = state;
    if (state == LED_OFF || state == LED_SOLID)
        LED_Drive(GPIOB, GPIO_Pin_3, state, &s_wifi_last);
}

void LED_Update_PWM(LedState_t state)
{
    s_pwm_state = state;
    if (state == LED_OFF || state == LED_SOLID)
        LED_Drive(GPIOB, GPIO_Pin_4, state, &s_pwm_last);
}

void LED_Update_Ready(uint8_t on_off)
{
    if (on_off)
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
    else
        GPIO_ResetBits(GPIOB, GPIO_Pin_5);
}

void LED_WiFi_ON(void)  { GPIO_SetBits(GPIOB, GPIO_Pin_3); }
void LED_WiFi_OFF(void) { GPIO_ResetBits(GPIOB, GPIO_Pin_3); }

/*
 * 闪烁 Task — 由 UI_Task 内部调用
 * 驱动 PB3/PB4 的慢闪/快闪 (PB5 为静态电平无需驱动)
 */
void LED_Status_Task(void)
{
    if (s_wifi_state == LED_SLOW || s_wifi_state == LED_FAST)
        LED_Drive(GPIOB, GPIO_Pin_3, s_wifi_state, &s_wifi_last);

    if (s_pwm_state == LED_SLOW || s_pwm_state == LED_FAST)
        LED_Drive(GPIOB, GPIO_Pin_4, s_pwm_state, &s_pwm_last);
}
