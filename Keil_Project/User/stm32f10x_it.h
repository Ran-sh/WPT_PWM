/**
  ******************************************************************************
  * @file    User/stm32f10x_it.h
  * @brief   中断服务函数头文件 — V4.3.2
  *
  *  Cortex-M3 异常向量 + STM32 外设中断向量声明
  *  所有定时调度已迁移至 Sys_Timer 时间戳差值法, 不再依赖 ISR 标志位
  ******************************************************************************
  */
#ifndef STM32F10X_IT_H
#define STM32F10X_IT_H

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

/* STM32 外设中断处理函数 */
void USART2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32F10X_IT_H */
