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
    APP_NETWORK_CONN_OFFLINE_ACTIVE  = 5   /* 主动离线，必须由用户手动恢复 */
} App_Network_Conn_State;

/** @brief 启动非阻塞联网流程并立即返回 */
uint8_t App_Network_Start_Connect(void);
/** @brief 获取当前连接状态编号 */
uint8_t App_Network_Get_Connect_Status(void);
/** @brief 获取本轮连接已执行的重试次数 */
uint8_t App_Network_Get_Retry_Count(void);
/** @brief 是否处于离线状态 (被动或主动) */
uint8_t App_Network_Is_Offline(void);
/** @brief 用户手动触发连接 (从主动离线恢复) */
void    App_Network_Manual_Connect(void);
/** @brief 用户主动断开 (进入主动离线, 不自动重连) */
void    App_Network_Manual_Disconnect(void);
/** @brief 从被动离线恢复连接，不复位仍在运行的ESP */
void    App_Network_Resume_From_Offline(void);
/** @brief 获取无线信号强度，单位为dBm；无有效值时返回-100 */
int8_t  App_Network_Get_RSSI(void);
/** @brief 判断是否处于无线连接或消息连接阶段 */
uint8_t App_Network_Is_Connecting(void);

/** @brief 网络主任务，由主循环周期调用，负责初始化、命令接收和遥测发送 */
void    App_Network_Task(void);

#endif /* 网络应用层接口结束 */
