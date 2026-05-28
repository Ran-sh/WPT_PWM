/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.h
 * @brief   人机界面控制器 — 公开接口
 * @note    7 界面状态机: INIT → CONNECTING → READY → SWEEPING → RUNNING → FAULT
 *          双击 KEY0 切控制面板/监测模式
 *          监测模式下仅双击切回有效, 其余按键全部禁用
 ******************************************************************************
 */

#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"

typedef enum {
    UI_CONTROLLER_STATE_INIT       = 0,
    UI_CONTROLLER_STATE_CONNECTING = 1,
    UI_CONTROLLER_STATE_READY      = 2,
    UI_CONTROLLER_STATE_SWEEPING   = 3,
    UI_CONTROLLER_STATE_RUNNING    = 4,
    UI_CONTROLLER_STATE_FAULT      = 5
} Ui_Controller_State;

void              Ui_Controller_Task(void);
Ui_Controller_State Ui_Controller_Get_State(void);
uint8_t           Ui_Controller_Get_Bridge_State(void);   /* 0=停止, 1=运行中 */

#endif /* UI_CONTROLLER_H */
