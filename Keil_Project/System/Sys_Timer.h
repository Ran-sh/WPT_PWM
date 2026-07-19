/**
 ******************************************************************************
 * @file    System/Sys_Timer.h
 * @brief   系统时基模块 — V5.0.2
 * @note    统一SysTick毫秒时钟
 *
 *          时间戳差值法 (所有周期任务的标准模式):
 *            static uint32_t last = 0;
 *            if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
 *                last = Sys_Timer_Get_Tick();
 *                ...
 *            }
 *
 *          依赖: STM32F10x SPL
 ******************************************************************************
 */

#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include "stm32f10x.h"

/** @brief 初始化 SysTick 1ms 全局时基 */
void     Sys_Timer_Init(void);
/** @brief SysTick ISR 内部递增 tick (禁止用户代码调用) */
void     Sys_Timer_Inc_Tick(void);
/** @brief 获取毫秒时间戳 (32位无符号, ~49.7天回绕) */
uint32_t Sys_Timer_Get_Tick(void);
/** @brief 低功耗毫秒延时 (仅允许启动阶段、IWDG启用前使用)
 *  @param ms 延时毫秒数，等待期间由SysTick中断唤醒
 */
void     Sys_Timer_Delay_Ms(uint32_t ms);

#endif /* SYS_TIMER_H */
