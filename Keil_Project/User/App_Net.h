/**
 ******************************************************************************
 * @file    User/App_Net.h
 * @brief   双脑架构网络应用层 —— 公开接口
 * @note    V4.0: Dual-MCU — 纯 JSON 串口透传, 零 AT 指令
 ******************************************************************************
 */

#ifndef __APP_NET_H
#define __APP_NET_H

#include "stm32f10x.h"

uint8_t App_Net_Init(void);
void    App_Net_Task(void);
uint8_t App_Net_IsConnected(void);  /* 硬件就绪 && ESP8266 已发 STATUS:ONLINE */

#endif /* __APP_NET_H */
