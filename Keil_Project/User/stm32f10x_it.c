/**
  ******************************************************************************
  * @file    User/stm32f10x_it.c
  * @brief   中断服务函数 (V5.0.2 净化版)
  *
  *  ISR map (Cortex-M3 NVIC):
  *  +------------------------------------------------------------+
  *  |                 STM32F103C8T6  NVIC                        |
  *  |                                                            |
  *  |  SysTick_Handler                                            |
  *  |    +--- Sys_Timer_IncTick()  (only call, 1ms timebase)     |
  *  |    Minimal: NO business logic in ISR                       |
  *  |                                                            |
  *  |  USART2_IRQHandler                                         |
 *  |    +--- USART_IT_RXNE check, preserve valid byte           |
 *  |    +--- USART_FLAG_ORE check, read DR to clear overflow    |
 *  |    +--- USART_IT_TXE check, drain TX ring one byte         |
  *  |                                                            |
  *  |  HardFault/MemManage/BusFault_Handler                       |
  *  |    +--- PWM off first (TIM1 DISABLE + MOE DISABLE)         |
  *  |    +--- Then infinite loop (safety first)                  |
  *  |                                                            |
  *  |  FORBIDDEN:                                                |
  *  |    x Business logic in ISR                                 |
  *  |    x Flag_Task_xxx dispatch flags in stm32f10x_it.c        |
  *  |    x printf / blocking delay in ISR                        |
  *  +------------------------------------------------------------+
  *
  * @note    USART2 on PA2(TX)/PA3(RX), 115200-8-N-1
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "Esp8266_Driver.h"
#include "Sys_Timer.h"
#include "Adc_Driver.h"

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)          { }
/* 故障处理器: 进入死循环前强制关断 PWM 输出 + 拉低 PB10 关 12V, 防止桥臂直通烧毁 MOSFET */
void HardFault_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE); GPIO_ResetBits(GPIOB, GPIO_Pin_10); while (1); }
void MemManage_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE); GPIO_ResetBits(GPIOB, GPIO_Pin_10); while (1); }
void BusFault_Handler(void)     { TIM_CtrlPWMOutputs(TIM1, DISABLE); GPIO_ResetBits(GPIOB, GPIO_Pin_10); while (1); }
void UsageFault_Handler(void)   { TIM_CtrlPWMOutputs(TIM1, DISABLE); GPIO_ResetBits(GPIOB, GPIO_Pin_10); while (1); }
void SVC_Handler(void)          { }
void DebugMon_Handler(void)     { }
void PendSV_Handler(void)       { }

/**
  * @brief  SysTick 中断服务函数 (每 1ms)
  * @note   唯一操作: 递增系统时基计数器
  *         不再包含任何任务调度逻辑 (KEY扫描/OLED刷新/LED闪烁 均已迁移至各模块 Task 函数)
  */
void SysTick_Handler(void)
{
    Sys_Timer_Inc_Tick();
}

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/******************************************************************************/

/** @brief ADC1双通道DMA传输完成中断，每2ms复制一次稳定原始值对 */
void DMA1_Channel1_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        Adc_Driver_DMA_Transfer_Complete_ISR();
    }
}

/**
  * @brief  USART2 接收中断 (ESP8266 数据通道)
  * @note   先 RXNE 保留有效字节，再清 ORE，最后处理 TXE 单字节发送。
  *         接收字节由 ESP8266 驱动负责帧拼接和缓冲区管理。
  */
void USART2_IRQHandler(void)
{
    /* 先RXNE后ORE，优先消费有效字节，再清溢出标志。
     *   旧顺序 (ORE 先) 会因读 DR 清 RXNE 而丢弃 ORE 前最后一个有效字节. */
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = USART_ReceiveData(USART2);
        Esp8266_Driver_Rx_Char(ch);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }

    /* ── 溢出错误处理: 读 DR 清除 ORE (溢出数据已丢弃, 不可恢复) ── */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        USART_ReceiveData(USART2);  /* 哑读清除 ORE */
    }

    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
    {
        Esp8266_Driver_Tx_Ready_ISR();
    }
}

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
