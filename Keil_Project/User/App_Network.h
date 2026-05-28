/**
 ******************************************************************************
 * @file    User/App_Network.h
 * @brief   网络应用层 — 公开接口
 * @note    Dual-MCU 架构: 串口 JSON ↔ STM32 透传
 *          负责 ESP8266 联网管理 + OneNET 遥测门控
 ******************************************************************************
 */

#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include "stm32f10x.h"

uint8_t App_Network_Start_Connect(void);         /* 启动联网, ~3s 阻塞 (ESP 硬件复位) */
uint8_t App_Network_Soft_Reset(void);            /* 软复位状态机 (CMD:CLEAR 后) */

uint8_t App_Network_Get_Connect_Status(void);    /* 0=空闲 1=连接中 2=已连接 3=失败 */
uint8_t App_Network_Get_Retry_Count(void);
uint8_t App_Network_Is_Connected(void);

void    App_Network_Task(void);                  /* 主循环周期调用: 接收指令 + 发送遥测 */

#endif /* APP_NETWORK_H */
