#ifndef STM32F10X_IT_H
#define STM32F10X_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f10x.h"

/** @brief Cortex-M3不可屏蔽中断入口。 */
void NMI_Handler(void);
/** @brief 硬故障入口，执行安全关断后停止。 */
void HardFault_Handler(void);
/** @brief 存储器管理故障入口。 */
void MemManage_Handler(void);
/** @brief 总线故障入口。 */
void BusFault_Handler(void);
/** @brief 用法故障入口。 */
void UsageFault_Handler(void);
/** @brief 系统服务调用入口。 */
void SVC_Handler(void);
/** @brief 调试监视入口。 */
void DebugMon_Handler(void);
/** @brief 可挂起服务入口。 */
void PendSV_Handler(void);
/** @brief 系统滴答入口，仅递增系统时基。 */
void SysTick_Handler(void);

/** @brief ADC1 DMA传输完成入口，提交一组双通道原始采样。 */
void DMA1_Channel1_IRQHandler(void);
/** @brief ADC1模拟看门狗入口，执行快速过流关断。 */
void ADC1_2_IRQHandler(void);
/** @brief USART2接收、溢出恢复和发送空入口。 */
void USART2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* 中断处理接口结束 */
