/**
 ******************************************************************************
 * @file    User/App_Network.h
 * @brief   网络应用层 — V4.5.2
 * @note    OFFLINE 双模式 (被动自动嗅探/主动手动恢复) + 5 次有限重试
 *          Dual-MCU 架构: 串口 JSON <-> STM32 透传
 *          负责 ESP8266 联网管理 (非阻塞初始化) + OneNET 遥测门控
 ******************************************************************************
 */

#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include "stm32f10x.h"

/** @brief 连接状态枚举 */
typedef enum {
    APP_NETWORK_CONN_IDLE            = 0,  /* 未启动 */
    APP_NETWORK_CONN_WIFI            = 1,  /* WiFi 连接中 */
    APP_NETWORK_CONN_MQTT            = 2,  /* MQTT 连接中 */
    APP_NETWORK_CONN_ONLINE          = 3,  /* 设备在线, 可收发 */
    APP_NETWORK_CONN_OFFLINE_PASSIVE = 4,  /* 被动离线 (热点断开, 自动嗅探恢复) */
    APP_NETWORK_CONN_OFFLINE_ACTIVE  = 5   /* 主动离线 (用户按键OFF, 需手动ON恢复) */
} App_Network_Conn_State;

/** @brief 启动联网 (非阻塞, 立即返回) */
uint8_t App_Network_Start_Connect(void);
/** @brief 软复位状态机 (进入无WIFI模式时调用, 仅复位网络状态) */
uint8_t App_Network_Soft_Reset(void);

/** @brief 获取连接状态 (0=空闲 1=WiFi 2=MQTT 3=在线 4=被动离线 5=主动离线) */
uint8_t App_Network_Get_Connect_Status(void);
uint8_t App_Network_Get_Retry_Count(void);
uint8_t App_Network_Is_Connected(void);
/** @brief 是否处于离线状态 (被动或主动) */
uint8_t App_Network_Is_Offline(void);
/** @brief 用户手动触发连接 (从主动离线恢复) */
void    App_Network_Manual_Connect(void);
/** @brief 用户主动断开 (进入主动离线, 不自动重连) */
void    App_Network_Manual_Disconnect(void);
/** @brief 从被动离线恢复连接 (ESP 已在运行, 不发硬件 RST) */
void    App_Network_Resume_From_Offline(void);
/** @brief 获取 WIFI 信号强度 RSSI (dBm), 默认 -100 */
int8_t  App_Network_Get_RSSI(void);
/** @brief 判断是否正在连接中 (WIFI 或 MQTT) */
uint8_t App_Network_Is_Connecting(void);

/** @brief 主循环周期调用: 驱动 ESP8266 初始化 + 接收指令 + 发送遥测 */
void    App_Network_Task(void);

#endif /* APP_NETWORK_H */
