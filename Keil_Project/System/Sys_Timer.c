/**
 ******************************************************************************
 * @file    System/Sys_Timer.c
 * @brief   系统时基模块 — V5.0.1
 *
 *  Timing sources:
 *  +--------------------------------------------------------+
 *  |                 STM32F103C8T6                           |
 *  |                                                         |
 *  |    SysTick (Cortex-M3) --- 1ms timebase                 |
 *  |      SysTick_Handler() -> Sys_Timer_IncTick()           |
 *  |      Sys_Timer_Get_Tick() -> uint32_t, 49.7-day wrap-s  |
 *  |                                                         |
 *  |    Core scheduling pattern (periodic tasks):            |
 *  |      static uint32_t last = 0;                          |
 *  |      if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {    |
 *  |          last = Sys_Timer_Get_Tick();                   |
 *  |          run_periodic_business_logic();                 |
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

void Sys_Timer_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);
}

void Sys_Timer_Inc_Tick(void)
{
    s_sys_tick++;
}

uint32_t Sys_Timer_Get_Tick(void)
{
    return s_sys_tick;
}

void Sys_Timer_Delay_Ms(uint32_t ms)
{
    uint32_t start;

    start = s_sys_tick;
    while ((uint32_t)(s_sys_tick - start) < ms) {
        __WFI();
    }
}
