#ifndef STM32F10X_IT_H
#define STM32F10X_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/* Cortex-M3处理器异常处理函数。 */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* STM32外设中断处理函数。 */
void DMA1_Channel1_IRQHandler(void);
void USART2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* 中断处理接口结束 */
