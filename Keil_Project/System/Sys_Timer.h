/**
 ******************************************************************************
 * @file    System/Sys_Timer.h
 * @brief   系统时基模块 — 公开接口
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

void     Sys_Timer_Init(void);
void     Sys_Timer_Inc_Tick(void);          /* ISR 内调用, 禁止用户代码直接调用 */
uint32_t Sys_Timer_Get_Tick(void);          /* 毫秒时间戳, 32 位无符号, 约 49.7 天回绕 */
uint32_t Sys_Timer_Get_Cycles(void);        /* DWT CPU 周期计数, 亚毫秒定时专用 */
void     Sys_Timer_Delay_Ms(uint32_t ms);   /* [已弃用] 阻塞延时, 仅允许在初始化阶段使用 */

#endif /* SYS_TIMER_H */
