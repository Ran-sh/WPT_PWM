/**
 ******************************************************************************
 * @file    Hardware/ESP8266.h
 * @brief   ESP8266-01 WiFi 模块驱动公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          依赖: STM32F10x 标准外设库 (SPL)
 *          硬件接口: USART2 (PA2-TX, PA3-RX), 115200-8N1
 ******************************************************************************
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f10x.h"

/* ── 接收缓冲区容量 ── */
#define ESP8266_RX_BUF_SIZE   512

/* ── 公开接口 ── */

void     ESP8266_Init(void);
void     ESP8266_SendString(const char *str);
/* 接收帧管理 */
uint8_t  ESP8266_GetRxFlag(void);
const char* ESP8266_GetRxBuffer(void);
void     ESP8266_ClearRxBuffer(void);

/* 供中断服务函数调用的字符注入 (USART2_IRQHandler → ESP8266_RxChar) */
void     ESP8266_RxChar(uint8_t ch);

/* 原子读取帧: 临界区内拷贝+清空, 返回拷贝字节数 */
uint16_t ESP8266_CopyRxFrame(char *dst, uint16_t max_len);
#endif /* __ESP8266_H */
