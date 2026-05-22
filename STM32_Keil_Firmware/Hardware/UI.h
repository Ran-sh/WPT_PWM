/**
 ******************************************************************************
 * @file    Hardware/UI.h
 * @brief   人机交互界面 —— 公开接口
 * @note    纯本地控制版: OLED 显示 + 按键分发 + LED 状态驱动
 ******************************************************************************
 */

#ifndef __UI_H
#define __UI_H

void UI_Task(void);

/* 桥状态接口: 确保与参考项目接口兼容 */
void    UI_SetBridgeState(uint8_t on_off);
uint8_t UI_GetBridgeState(void);
void    UI_SetWiFiConnected(uint8_t on_off);   /* 纯本地版: 无操作, 接口保留 */

#endif
