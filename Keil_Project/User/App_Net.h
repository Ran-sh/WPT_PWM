/**
 ******************************************************************************
 * @file    User/App_Net.h
 * @brief   网络应用层 —— 公开接口
 * @note    V1.0.0: App_Net_Connect_Task 非阻塞联网状态机替代阻塞 App_Net_Init
 ******************************************************************************
 */

#ifndef __APP_NET_H
#define __APP_NET_H

#include "stm32f10x.h"

/* ── 联网状态 ── */
typedef enum {
    NET_IDLE = 0,
    NET_STEP_AT,
    NET_STEP_CWMODE,
    NET_STEP_CWJAP,
    NET_STEP_CIPSTART,
    NET_STEP_CIPMODE,
    NET_STEP_CIPSEND,
    NET_SUCCESS,
    NET_FAIL
} NetState_t;

uint8_t    App_Net_Init(void);            /* 阻塞联网 (保留兼容), 0=成功 1~6=失败 */
void       App_Net_Task(void);            /* JSON 遥测 + 指令解析 */
uint8_t    App_Net_IsConnected(void);     /* WiFi 已连接? */

/* V1.0.0: 非阻塞联网 */
void        App_Net_Connect_Trigger(void);     /* 触发联网 (仅 NET_IDLE 时有效) */
void        App_Net_Connect_Cancel(void);      /* 取消联网 (KEY1) */
void        App_Net_Connect_Task(void);        /* 主循环每轮调用, 非阻塞步进 */
NetState_t  App_Net_GetConnectState(void);     /* 供 UI 查询当前步骤 */
uint8_t     App_Net_GetErrorCode(void);         /* 失败时返回错误码 1~6 */

#endif
