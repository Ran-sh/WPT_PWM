/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.h
 * @brief   人机界面控制器 -- 公开接口 (V10 两级菜单架构)
 * @note    TFT 8行20列彩屏, 4键操作
 *          9 pages: MAIN_MENU -> MONITOR_SUB_MENU -> SWEEP/MONITOR_x/WIFI_SETUP/FAULT
 ******************************************************************************
 */

#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"

/** @brief UI 页面枚举 (9 页两级栈式导航) */
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,   /* 主菜单 - 4项 */
    UI_PAGE_MONITOR_SUB_MENU   = 1,   /* 监测子菜单 - 5项 */
    UI_PAGE_SWEEP              = 2,   /* 扫频页 - 频率进度 */
    UI_PAGE_MONITOR_SUMMARY    = 3,   /* 综合监测 - F/V/I 同屏 */
    UI_PAGE_MONITOR_FREQ       = 4,   /* 监测频率 - 仪表盘 */
    UI_PAGE_MONITOR_VOLT       = 5,   /* 监测电压 - 仪表盘 */
    UI_PAGE_MONITOR_CURR       = 6,   /* 监测电流 - 仪表盘 */
    UI_PAGE_WIFI_SETUP         = 7,   /* 无线配网 - 状态+清除 */
    UI_PAGE_FAULT              = 8    /* 故障清除 - 过流锁存 */
} Ui_Page;

/** @brief 主循环周期调用 - 200ms: 渲染+按键分发+边沿检测 */
void    Ui_Controller_Task(void);
/** @brief 获取当前所在页面 */
Ui_Page Ui_Controller_Get_Page(void);
/** @brief 是否处于无WiFi模式 (远程指令门控用) */
uint8_t Ui_Controller_Is_No_WiFi_Mode(void);

#endif /* UI_CONTROLLER_H */
