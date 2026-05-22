/**
 ******************************************************************************
 * @file    Hardware/ESP8266.h
 * @brief   ESP8266-01 WiFi 模块驱动公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          依赖: STM32F10x 标准外设库 (SPL)
 *          硬件接口: USART2 (PA2-TX, PA3-RX), PB1 (CH_PD/EN 使能控制), 115200-8N1
 ******************************************************************************
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f10x.h"

/* ── 接收缓冲区容量 ── */
#define ESP8266_RX_BUF_SIZE   512

/* ── AT 指令超时参数 (毫秒) ── */
#define ESP8266_CMD_TIMEOUT    2000   /* 普通 AT 指令超时 */
#define ESP8266_WIFI_TIMEOUT  15000   /* WiFi 连接超时 (需等待 DHCP 分配 IP) */
#define ESP8266_TCP_TIMEOUT     10000   /* TCP 连接超时 */
#define ESP8266_SILENT_TIMEOUT  30000   /* RX 静默超时: ESP8266 掉电/卡死判定 (30s 给操作者留足时间) */

/* ── 公开接口 ── */

void     ESP8266_Init(void);
void     ESP8266_SendString(const char *str);
uint8_t  ESP8266_ConnectToServer(const char *ssid, const char *pwd,
                                  const char *ip, uint16_t port);

/* 接收帧管理 */
uint8_t  ESP8266_GetRxFlag(void);
void     ESP8266_ClearRxFlag(void);
const char* ESP8266_GetRxBuffer(void);
void     ESP8266_ClearRxBuffer(void);

/* 供中断服务函数调用的字符注入 (USART2_IRQHandler → ESP8266_RxChar) */
void     ESP8266_RxChar(uint8_t ch);

/* WaitResponse 轮询回调 (用于 OLED 点动画等, NULL=不使用) */
void     ESP8266_SetWaitCallback(void (*cb)(void));

/* 原子读取帧: 临界区内拷贝+清空, 返回拷贝字节数 */
uint16_t ESP8266_CopyRxFrame(char *dst, uint16_t max_len);
uint8_t  ESP8266_BufferContains(const char *needle);   /* 临界区内 strstr, 供 CIPSEND 等场景 */
uint32_t ESP8266_GetLastRxTime(void);                  /* 最后收到字节的 SysTick, 用于看门狗断线检测 */
void     ESP8266_RefreshLastRxTime(void);              /* 重置静默看门狗时间戳 (联网成功时调用) */

#endif /* __ESP8266_H */
