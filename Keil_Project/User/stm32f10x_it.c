/**
  ******************************************************************************
  * @file    User/stm32f10x_it.c
  * @brief   中断服务函数 (V4.3.2 净化版)
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
  *  |    +--- USART_FLAG_ORE check  (MUST be first, anti-lock)   |
  *  |    |    +--- USART_ReceiveData()  read DR to clear ORE     |
  *  |    +--- USART_IT_RXNE check                                |
  *  |         +--- Esp8266_Driver_Rx_Char(ch)  push ring buffer  |
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

/**
  * @brief  USART2 接收中断 (ESP8266 数据通道)
  * @note   先处理溢出错误 (ORE) 防止中断锁死, 再处理正常接收。
  *         收到字节注入 ESP8266_RxChar(), 由该函数负责帧拼接和缓冲区管理。
  */
void USART2_IRQHandler(void)
{
    /* ── 优先处理溢出错误，防止中断锁死 ── */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        USART_ReceiveData(USART2);  /* 读 DR 清除 ORE (溢出数据丢弃) */
    }

    /* ── 正常接收 ── */
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = USART_ReceiveData(USART2);
        Esp8266_Driver_Rx_Char(ch);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
