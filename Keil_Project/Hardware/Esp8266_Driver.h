/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.h
 * @brief   ESP8266 串口通信驱动 — 公开接口
 * @note    Dual-MCU 架构: USART2 115200, 纯 JSON 文本透传
 *          STM32 绝不发 AT 指令, ESP8266 绝不碰 PWM/ADC
 *
 *          V3.0.0: 非阻塞初始化 — Start_Init + Init_Task 替代 3s 忙等
 *          帧格式: 以 \r 或 \n 为分隔符
 *          ISR 安全: Copy_Rx_Frame 原子复制 + 清标志
 ******************************************************************************
 */

#ifndef ESP8266_DRIVER_H
#define ESP8266_DRIVER_H

#include "stm32f10x.h"

void        Esp8266_Driver_Start_Init(void);      /* 启动非阻塞初始化, GPIO+USART 立即配置 */
void        Esp8266_Driver_Init_Task(void);       /* 主循环调用, 驱动 CH_PD 时序状态机 */
void        Esp8266_Driver_Send_String(const char* str);
uint8_t     Esp8266_Driver_Get_Rx_Flag(void);
const char* Esp8266_Driver_Get_Rx_Buffer(void);
void        Esp8266_Driver_Clear_Rx_Buffer(void);
uint16_t    Esp8266_Driver_Copy_Rx_Frame(char* dst, uint16_t max_len);
uint8_t     Esp8266_Driver_Is_Ready(void);        /* Init FSM 到达 READY 时才返回 1 */

/* 由 USART2_IRQHandler 调用, 不对外 */
void Esp8266_Driver_Rx_Char(uint8_t ch);

#endif /* ESP8266_DRIVER_H */
