/**
 ******************************************************************************
 * @file    System/Sys_Timer.c
 * @brief   系统时基模块 — V5.1.3
 *
 *  时间来源与调度方式:
 *  +--------------------------------------------------------+
 *  |                 STM32F103C8T6                           |
 *  |                                                         |
 *  |    Cortex-M3系统滴答定时器提供1ms统一时基                |
 *  |    中断处理函数只负责把毫秒计数递增一次                  |
 *  |    32位无符号时间戳约49.7天自然回绕                      |
 *  |                                                         |
 *  |    周期任务统一使用无符号时间戳差值判断：                |
 *  |      static uint32_t last = 0;                          |
 *  |      if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {    |
 *  |          last = Sys_Timer_Get_Tick();                   |
 *  |          执行周期任务；                                  |
 *  |      }                                                  |
 *  |    无符号减法可以自然处理时间戳回绕                      |
 *  |                                                         |
 *  |    毫秒延时只允许在启动阶段使用，运行阶段禁止阻塞        |
 *  +--------------------------------------------------------+
 *
 * @note    全项目只保留这一套毫秒时基，系统滴答中断不承载业务逻辑。
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
