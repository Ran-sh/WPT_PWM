/**
  ******************************************************************************
  * @file    User/stm32f10x_it.c
  * @brief   中断服务函数 — V5.1.2
  *
  *  中断映射（Cortex-M3嵌套向量中断控制器）:
  *  +------------------------------------------------------------+
  *  |                 STM32F103C8T6  NVIC                        |
  *  |                                                            |
  *  |  系统滴答中断处理函数                                      |
  *  |    +--- 仅递增1ms系统时基                                  |
  *  |    +--- 中断内不执行任何业务逻辑                           |
  *  |                                                            |
  *  |  串口2中断处理函数                                         |
  *  |    +--- 先接收有效字节，再处理溢出错误                     |
  *  |    +--- 发送寄存器空时从发送环形队列取出一个字节           |
  *  |                                                            |
  *  |  硬故障、存储器故障、总线故障和用法故障处理函数            |
  *  |    +--- 先关闭TIM1主输出和12V电源，再进入死循环            |
  *  |                                                            |
  *  |  禁止事项：中断内业务逻辑、调度标志、打印和阻塞延时       |
  *  +------------------------------------------------------------+
  *
  * @note    串口2使用PA2发送、PA3接收，参数为115200-8-N-1。
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "Esp8266_Driver.h"
#include "Sys_Timer.h"
#include "Adc_Driver.h"

/******************************************************************************/
/* Cortex-M3处理器异常处理函数                                                */
/******************************************************************************/

/* 致命异常不能依赖库函数或栈上复杂状态，直接关闭定时器输出和12V电源。 */
static void Stm32f10x_It_Fatal_Safe_Loop(void)
{
    TIM1->BDTR &= (uint16_t)(~TIM_BDTR_MOE);
    TIM1->CR1 &= (uint16_t)(~TIM_CR1_CEN);
    GPIOB->BRR = GPIO_Pin_10;
    while (1) { __NOP(); }
}

void NMI_Handler(void)          { Stm32f10x_It_Fatal_Safe_Loop(); }
void HardFault_Handler(void)    { Stm32f10x_It_Fatal_Safe_Loop(); }
void MemManage_Handler(void)    { Stm32f10x_It_Fatal_Safe_Loop(); }
void BusFault_Handler(void)     { Stm32f10x_It_Fatal_Safe_Loop(); }
void UsageFault_Handler(void)   { Stm32f10x_It_Fatal_Safe_Loop(); }
void SVC_Handler(void)          { }
void DebugMon_Handler(void)     { }
void PendSV_Handler(void)       { }

/**
  * @brief  系统滴答中断服务函数，每1ms执行一次
  * @note   仅递增系统时基计数器；按键、显示和指示灯任务均由主循环调度。
  */
void SysTick_Handler(void)
{
    Sys_Timer_Inc_Tick();
}

/******************************************************************************/
/* STM32F10x外设中断处理函数                                                  */
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

/** @brief ADC模拟看门狗过流中断，先清硬件标志再执行快速PWM关断 */
void ADC1_2_IRQHandler(void)
{
    if (ADC_GetITStatus(ADC1, ADC_IT_AWD) != RESET)
    {
        ADC_ClearITPendingBit(ADC1, ADC_IT_AWD);
        Adc_Driver_Analog_Watchdog_ISR();
    }
}

/**
  * @brief  串口2收发中断，服务ESP8266数据通道
  * @note   先保存接收寄存器中的有效字节，再清除溢出错误，最后处理发送。
  *         接收字节由ESP8266驱动负责帧拼接和缓冲区管理。
  */
void USART2_IRQHandler(void)
{
    /* 先读取有效字节再处理溢出；若先读数据寄存器清溢出，可能丢失最后一个有效字节。 */
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = USART_ReceiveData(USART2);
        Esp8266_Driver_Rx_Char(ch);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }

    /* 读取数据寄存器清除溢出标志；已经溢出的数据无法恢复。 */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        USART_ReceiveData(USART2);  /* 通过哑读清除溢出标志。 */
    }

    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
    {
        Esp8266_Driver_Tx_Ready_ISR();
    }
}
