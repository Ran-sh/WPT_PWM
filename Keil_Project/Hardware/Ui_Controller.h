/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.h
 * @brief   人机界面控制器 — 公开接口 (V7 多页仪表盘版)
 * @note    TFT 8行20列彩屏, 4键操作
 *          6 态: INIT → FAILED → READY → SWEEPING → RUNNING → FAULT
 ******************************************************************************
 */

#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"

typedef enum {
    UI_CONTROLLER_STATE_INIT       = 0,
    UI_CONTROLLER_STATE_FAILED     = 1,
    UI_CONTROLLER_STATE_READY      = 2,
    UI_CONTROLLER_STATE_SWEEPING   = 3,
    UI_CONTROLLER_STATE_RUNNING    = 4,
    UI_CONTROLLER_STATE_FAULT      = 5
} Ui_Controller_State;

void                Ui_Controller_Task(void);
Ui_Controller_State Ui_Controller_Get_State(void);
uint8_t             Ui_Controller_Get_Bridge_State(void);
uint8_t             Ui_Controller_Is_No_WiFi_Mode(void);

#endif /* UI_CONTROLLER_H */
