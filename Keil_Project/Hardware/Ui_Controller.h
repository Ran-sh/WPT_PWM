#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"
#include "Key_Driver.h"

/** @brief 十四个界面页面的固定编号 */
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,   /* 主菜单 - 4/5项 */
    UI_PAGE_MONITOR_SUB_MENU   = 1,   /* 监测子菜单 - 5项 */
    UI_PAGE_SWEEP              = 2,   /* 扫频页 - 频率进度 */
    UI_PAGE_MONITOR_SUMMARY    = 3,   /* 综合监测 - 频率、电压和电流同屏 */
    UI_PAGE_MONITOR_FREQ       = 4,   /* 监测频率 - 仪表盘 */
    UI_PAGE_MONITOR_VOLT       = 5,   /* 监测电压 - 仪表盘 */
    UI_PAGE_MONITOR_CURR       = 6,   /* 监测电流 - 仪表盘 */
    UI_PAGE_WIFI_SETUP         = 7,   /* 无线配网 - 状态+清除 */
    UI_PAGE_FAULT              = 8,   /* 故障清除 - 过流锁存 */
    /* 以下五项为设置页面，与前九项合计十四页。 */
    UI_PAGE_SETTING            = 9,   /* 设置主菜单 */
    UI_PAGE_SETTING_LANG       = 10,  /* 语言切换 */
    UI_PAGE_SETTING_SPACING    = 11,  /* 字符间距四档 */
    UI_PAGE_SETTING_ICONS      = 12,  /* 图标浏览 */
    UI_PAGE_SETTING_COLOR      = 13,  /* 颜色方案 */
    UI_PAGE_COUNT              = 14
} Ui_Page;

/** @brief 根据系统核心层过滤后的按键事件刷新页面并分发操作
 *  @param events 五键事件快照，其中电源键事件已由系统核心层优先消费
 */
void    Ui_Controller_Task(
    const Key_Driver_Event events[KEY_DRIVER_COUNT]);
/** @brief 外部强制跳转到目标页面并重置菜单光标，供远程启停同步界面 */
void    Ui_Controller_Force_Page_And_Reset(Ui_Page page);
/** @brief 判断是否处于无线网络关闭模式
 *  @retval 1表示主动关闭无线网络，0表示允许联网
 */
uint8_t Ui_Controller_Is_No_WiFi_Mode(void);
/** @brief 加载持久化设置并应用到界面和显示驱动
 *  @param lang 语言选项
 *  @param font 历史字体字段，仅用于兼容旧配置
 *  @param bl 历史背光字段，仅用于兼容旧配置
 *  @param spacing 字符间距选项
 *  @param preset 配色预设编号
 *  @param fg RGB565前景色
 *  @param bg RGB565背景色
 */
void    Ui_Controller_Apply_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                      uint8_t spacing, uint8_t preset,
                                      uint16_t fg, uint16_t bg);

#endif /* 人机界面控制接口结束 */
