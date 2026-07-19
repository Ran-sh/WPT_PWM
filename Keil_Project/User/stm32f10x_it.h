/**
  ******************************************************************************
  * @file    User/stm32f10x_it.h
  * @brief   中断服务函数头文件 (V5.0.2 净化版)
  * @note    删除所有外部调度标志位 (Flag_Task_xxx)
  *          删除 g_MsTick 全局计数器 (由 System/Sys_Timer 模块替代)
  *          USART2_IRQHandler 由 ESP8266 驱动使用, 保留声明
  *          所有定时调度迁移至 System/Sys_Timer 时间戳差值法
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
void DMA1_Channel1_IRQHandler(void);
void USART2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32F10X_IT_H */
