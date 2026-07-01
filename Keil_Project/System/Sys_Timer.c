/**
 ******************************************************************************
 * @file    System/Sys_Timer.c
 * @brief   系统时基模块 — V4.3.2
 *
 *  Timing sources:
 *  +--------------------------------------------------------+
 *  |                 STM32F103C8T6                           |
 *  |                                                         |
 *  |    SysTick (Cortex-M3) --- 1ms timebase                 |
 *  |      SysTick_Handler() -> Sys_Timer_IncTick()           |
 *  |      Sys_Timer_Get_Tick() -> uint32_t, 49.7-day wrap-s  |
 *  |                                                         |
 *  |    DWT (Cortex-M3 debug) --- 0.5us high-res counter     |
 *  |      DWT->CYCCNT enabled via DEMCR TRCENA + CTRL CYCCN  |
 *  |                                                         |
 *  |    Core scheduling pattern (periodic tasks):            |
 *  |      static uint32_t last = 0;                          |
 *  |      if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {    |
 *  |          last = Sys_Timer_Get_Tick();                   |
 *  |          // business logic                              |
 *  |      }                                                  |
 *  |      uint32_t unsigned subtract auto-handles 49.7-day   |
 *  |                                                         |
 *  |    Sys_Timer_Delay_Ms() — init phase ONLY, never at ru  |
 *  +--------------------------------------------------------+
 *
 * @note    Single project-wide timebase; SysTick_Handler has ONE line: IncTick
 ******************************************************************************
 */

#include "Sys_Timer.h"

static volatile uint32_t s_sys_tick = 0;

/*
 * DWT 寄存器 (旧版 CMSIS core_cm3.h 不含此段, 手动定义)
 * Cortex-M3 DWT 基址 0xE0001000
 */
#define DWT_REG_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_REG_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DWT_BIT_CYCCNT_ENA  (1u << 0)

/** @brief 初始化 SysTick 1ms + DWT 72MHz 周期计数器 (全局时基) */
/** @brief 初始化 SysTick 1ms + DWT 72MHz 周期计数器 (全局唯一时基) */
void Sys_Timer_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_REG_CYCCNT = 0;
    DWT_REG_CTRL  |= DWT_BIT_CYCCNT_ENA;
}

/** @brief SysTick ISR 回调: 递增毫秒计数 (禁止用户代码调用) */
void Sys_Timer_Inc_Tick(void)
{
    s_sys_tick++;
}

/** @brief 获取毫秒时间戳 (32bit 无符号, ~49.7 天回绕安全) */
uint32_t Sys_Timer_Get_Tick(void)
{
    return s_sys_tick;
}

/** @brief 获取 DWT CPU 周期计数 (亚毫秒高精度定时) */
uint32_t Sys_Timer_Get_Cycles(void)
{
    return DWT_REG_CYCCNT;
}

/** @brief 阻塞延时 ms 毫秒 (仅初始化阶段使用, 运行时禁止阻塞) */
void Sys_Timer_Delay_Ms(uint32_t ms)
{
    uint32_t start = s_sys_tick;
    while ((s_sys_tick - start) < ms);
}
