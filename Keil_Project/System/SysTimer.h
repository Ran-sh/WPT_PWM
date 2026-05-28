/**
 ******************************************************************************
 * @file    System/SysTimer.h
 * @brief   系统时基模块 —— 公开接口
 * @note    存放路径: 项目根目录\System\
 *
 *          设计理念:
 *            本项目统一的毫秒级时钟源。SysTick 被配置为每 1ms 触发一次中断，
 *            中断服务函数仅调用 SysTimer_IncTick() 递增内部计数器。
 *            所有上层模块通过 SysTimer_GetTick() 获取绝对时间戳，
 *            采用"时间戳差值法"实现非阻塞延时与周期调度。
 *
 *          时间戳差值法 (Timestamp-Difference Pattern):
 *            static uint32_t last = 0;
 *            if (SysTimer_GetTick() - last >= PERIOD_MS) {
 *                last = SysTimer_GetTick();
 *                // 周期性业务代码
 *            }
 *
 *            该模式利用 uint32_t 无符号溢出回绕特性，
 *            即使计数器溢出 (约 49.7 天) 差值计算依然正确。
 *
 *          依赖: STM32F10x 标准外设库 (SPL)
 ******************************************************************************
 */

#ifndef __SYS_TIMER_H
#define __SYS_TIMER_H

#include "stm32f10x.h"

/* ── 公开接口 ── */

void     SysTimer_Init(void);                  /* 初始化 SysTick, 配置 1ms 中断 + DWT 周期计数器 */
void     SysTimer_IncTick(void);               /* 由 SysTick_Handler 调用, 递增 1ms */
uint32_t SysTimer_GetTick(void);               /* 获取上电以来的毫秒时间戳 */
uint32_t SysTimer_GetCycles(void);             /* 获取 DWT 周期计数器 (CPU 时钟, 亚毫秒定时) */
void     SysTimer_DelayMs(uint32_t ms);        /* 阻塞毫秒延时 (使用前须先 SysTimer_Init) */

#endif
