#ifndef __APP_NET_H
#define __APP_NET_H

#include "stm32f10x.h"

uint8_t App_Net_StartConnect(void);    /* 启动联网 (上电/KEY0触发, 含硬件复位) */
uint8_t App_Net_SoftReset(void);       /* 软复位状态 (CMD:CLEAR后, 不触发硬件复位) */
void    App_Net_Task(void);
uint8_t App_Net_IsConnected(void);     /* 硬件就绪 && STATUS:ONLINE */
uint8_t App_Net_GetConnectStatus(void);/* 0=空闲 1=连接中 2=已连接 3=失败 */
uint8_t App_Net_GetRetryCount(void);   /* 当前重试次数 */

#endif
