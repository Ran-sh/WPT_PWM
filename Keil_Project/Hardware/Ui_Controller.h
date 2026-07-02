/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.h
 * @brief   人机界面控制器 — V4.5.0 (17 页面 + 圆弧能量条 + 增量刷新)
 * @note    TFT 8 行 20 列彩屏, 4 键操作
 *          9 pages: MAIN_MENU -> MONITOR_SUB -> SWEEP/MONITOR_x/WIFI_SETUP/FAULT
 ******************************************************************************
 */

#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"

/** @brief UI 页面枚举 (17 页两级栈式导航, V4.5.0 +2 BL子页) */
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,   /* 主菜单 - 4/5项 */
    UI_PAGE_MONITOR_SUB_MENU   = 1,   /* 监测子菜单 - 5项 */
    UI_PAGE_SWEEP              = 2,   /* 扫频页 - 频率进度 */
    UI_PAGE_MONITOR_SUMMARY    = 3,   /* 综合监测 - F/V/I 同屏 */
    UI_PAGE_MONITOR_FREQ       = 4,   /* 监测频率 - 仪表盘 */
    UI_PAGE_MONITOR_VOLT       = 5,   /* 监测电压 - 仪表盘 */
    UI_PAGE_MONITOR_CURR       = 6,   /* 监测电流 - 仪表盘 */
    UI_PAGE_WIFI_SETUP         = 7,   /* 无线配网 - 状态+清除 */
    UI_PAGE_FAULT              = 8,   /* 故障清除 - 过流锁存 */
    /* V4.5.0 设置 (8 pages) */
    UI_PAGE_SETTING            = 9,   /* 设置主菜单 */
    UI_PAGE_SETTING_LANG       = 10,  /* 语言切换 */
    UI_PAGE_SETTING_SPACING    = 11,  /* [V4.5.0] 字间距 0-3px */
    UI_PAGE_SETTING_ICONS      = 12,  /* 图标浏览 */
    UI_PAGE_SETTING_BL         = 13,  /* 亮度二级菜单 */
    UI_PAGE_SETTING_BL_MANUAL  = 14,  /* [V4.5.0] 手动调亮度 1-100% */
    UI_PAGE_SETTING_BL_BREATHE = 15,  /* [V4.5.0] 呼吸灯参数 */
    UI_PAGE_SETTING_COLOR      = 16,  /* 颜色方案 */
} Ui_Page;

/** @brief 主循环周期调用 - 200ms: 渲染+按键分发+边沿检测 */
void    Ui_Controller_Task(void);
/** @brief 获取当前所在页面 */
Ui_Page Ui_Controller_Get_Page(void);
/** @brief 外部强制跳转到目标页面 (远程指令/系统状态迁移同步用) */
void    Ui_Controller_Force_Page(Ui_Page page);
/** @brief 外部强制跳转到目标页面并重置菜单光标 (远程 CMD:ON/OFF 专用, 避免远端操作后本地光标错位) */
void    Ui_Controller_Force_Page_And_Reset(Ui_Page page);
/** @brief 是否处于无WiFi模式 (远程指令门控用) */
uint8_t Ui_Controller_Is_No_WiFi_Mode(void);
/** @brief [V4.5.0] 加载持久化设置参数 (由 Sys_Post_Init 调用)
 *  @note font param (renamed from font_size) is now ignored — spacing replaces it */
void    Ui_Controller_Apply_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                      uint8_t spacing, uint8_t preset,
                                      uint16_t fg, uint16_t bg);

#endif /* UI_CONTROLLER_H */
