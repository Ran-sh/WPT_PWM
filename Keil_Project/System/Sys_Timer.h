/**
 ******************************************************************************
 * @file    System/Sys_Timer.h
 * @brief   系统时基模块 — V4.3.2
 * @note    统一毫秒时钟 + DWT 亚毫秒周期计数器
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

/** @brief 初始化 SysTick 1ms + DWT 72MHz 周期计数器 (全局时基) */
void     Sys_Timer_Init(void);
/** @brief SysTick ISR 内部递增 tick (禁止用户代码调用) */
void     Sys_Timer_Inc_Tick(void);
/** @brief 获取毫秒时间戳 (32位无符号, ~49.7天回绕) */
uint32_t Sys_Timer_Get_Tick(void);
/** @brief 获取 DWT CPU 周期计数 (亚毫秒精确定时) */
uint32_t Sys_Timer_Get_Cycles(void);
/** @brief [已弃用] 阻塞毫秒延时 (仅允许初始化阶段)
 *  @param ms 延时毫秒数 (阻塞期间不喂狗, >1600ms 将触发 IWDG 复位)
 */
void     Sys_Timer_Delay_Ms(uint32_t ms);

#endif /* SYS_TIMER_H */
