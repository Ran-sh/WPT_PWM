/**
 ******************************************************************************
 * @file    User/App_Net.h
 * @brief   网络应用层 —— 公开接口
 * @note    存放路径: 项目根目录\User\
 *
 *          职责: 封装 WiFi 联网逻辑与 TCP 透传数据交互
 *          App_Net_Init(): ESP8266 硬件初始化 + AT 联网状态机 (阻塞)
 *          App_Net_Task(): 周期性 JSON 遥测上报 + 远程指令解析 (非阻塞)
 *
 *          依赖: Hardware/ESP8266, Hardware/UI, System/SysTimer
 ******************************************************************************
 */

#ifndef __APP_NET_H
#define __APP_NET_H

uint8_t App_Net_Init(void);  /* 返回 0=成功, 1~6=错误码; 失败后可按 KEY0 重试 */
void    App_Net_Task(void);
uint8_t App_Net_IsConnected(void);  /* WiFi 已连接? 1=是 0=否 */

#endif
