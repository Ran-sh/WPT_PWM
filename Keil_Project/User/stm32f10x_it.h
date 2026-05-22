/**
  ******************************************************************************
  * @file    User/stm32f10x_it.h
  * @brief   中断服务函数头文件 (净化版)
  * @note    存放路径: 项目根目录\User\
  *
  *          【重构要点】
  *          - 删除所有外部调度标志位 (Flag_Task_Key1/Key2/OLED/LED)
  *          - 删除 g_MsTick 全局计数器 (由 System/SysTimer 模块替代)
   *          - 所有定时调度迁移至 System/SysTimer 时间戳差值法
  ******************************************************************************
  */

#ifndef __STM32F10x_IT_H
#define __STM32F10x_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* Cortex-M3 处理器异常处理函数 */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F10x_IT_H */
