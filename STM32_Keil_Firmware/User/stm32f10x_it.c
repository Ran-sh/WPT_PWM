/**
  ******************************************************************************
  * @file    User/stm32f10x_it.c
  * @brief   中断服务函数实现 (净化版)
  * @note    存放路径: 项目根目录\User\
  *
  *          【重构要点】
  *          - SysTick_Handler 极简化: 仅调用 SysTimer_IncTick()
  *          - 删除所有软件定时器变量 (time_key/time_oled/time_led)
  *          - 删除所有外部调度标志位 (Flag_Task_xxx)
  *          - 删除 g_MsTick 全局计数器
  *          - 删除 KEY_Scan_All() 调用 (迁移至 KEY_Task 时间戳调度)
  *          - USART2_IRQHandler 保留 ORE 溢出防锁死机制
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "ESP8266.h"
#include "SysTimer.h"

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)          { }
void HardFault_Handler(void)    { while (1); }
void MemManage_Handler(void)    { while (1); }
void BusFault_Handler(void)     { while (1); }
void UsageFault_Handler(void)   { while (1); }
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
    SysTimer_IncTick();
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
        ESP8266_RxChar(ch);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
