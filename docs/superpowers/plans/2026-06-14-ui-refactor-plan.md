# UI 重构 — 两级菜单架构 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `Ui_Controller` 从 6 态线性状态机重构为 2 级栈式菜单架构 (9 页: 主菜单→4子模式+监测子菜单4项+故障页)

**Architecture:** 页面枚举 `Ui_Page` 替代旧 `Ui_Controller_State`，页面跳转由按键直接控制 (非状态推导)，故障检测用边沿触发防无限重入。保留 Draw_Header/WIFI+Mqtt 图标/EMA/能量条/格式化等渲染图元。

**Tech Stack:** C (ARMCC V5), STM32F103 SPL V3.5.0, ST7735 TFT 160×128, Keil MDK-ARM V5. V4.2.1

---

## 文件结构

| 文件 | 动作 | 职责 |
|:---|:---|:---|
| `Keil_Project/Hardware/Ui_Controller.h` | **修改** | 枚举+API 替换 |
| `Keil_Project/Hardware/Ui_Controller.c` | **重写** | 菜单渲染+按键分发+主调度 |
| `Keil_Project/User/App_Network.c` | **微改** | `Get_State()`→`Get_Page()` |
| `Keil_Project/User/main.c` | **微改** | 启动页文字+开机自动联网 |

---

### Task 1: 更新 Ui_Controller.h — 枚举和 API

**Files:**
- Modify: `Keil_Project/Hardware/Ui_Controller.h`

- [ ] **Step 1: 替换枚举和接口**

将 `Keil_Project/Hardware/Ui_Controller.h` 完整替换为:

```c
/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.h
 * @brief   人机界面控制器 — 公开接口 (V10 两级菜单架构)
 * @note    TFT 8行20列彩屏, 4键操作
 *          9 页: MAIN_MENU→MONITOR_SUB_MENU→SWEEP/MONITOR_*/WIFI_SETUP/FAULT
 ******************************************************************************
 */

#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "stm32f10x.h"

/** @brief UI 页面枚举 (9 页两级栈式导航) */
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,   /* 主菜单 — 4项 */
    UI_PAGE_MONITOR_SUB_MENU   = 1,   /* 监测子菜单 — 5项 */
    UI_PAGE_SWEEP              = 2,   /* 扫频页 — 频率进度 */
    UI_PAGE_MONITOR_SUMMARY    = 3,   /* 综合监测 — F/V/I 同屏 */
    UI_PAGE_MONITOR_FREQ       = 4,   /* 监测频率 — 仪表盘 */
    UI_PAGE_MONITOR_VOLT       = 5,   /* 监测电压 — 仪表盘 */
    UI_PAGE_MONITOR_CURR       = 6,   /* 监测电流 — 仪表盘 */
    UI_PAGE_WIFI_SETUP         = 7,   /* 无线配网 — 状态+清除 */
    UI_PAGE_FAULT              = 8    /* 故障清除 — 过流锁存 */
} Ui_Page;

/** @brief 主循环周期调用 — 200ms: 渲染+按键分发+边沿检测 */
void    Ui_Controller_Task(void);
/** @brief 获取当前所在页面 */
Ui_Page Ui_Controller_Get_Page(void);
/** @brief 是否处于无WiFi模式 (远程指令门控用) */
uint8_t Ui_Controller_Is_No_WiFi_Mode(void);

#endif /* UI_CONTROLLER_H */
```

- [ ] **Step 2: 验证编译通过**

在 Keil IDE 中 F7 编译，预期：
- `Ui_Controller.c` 引用了被删除的 `Ui_Controller_State`/`Ui_Controller_Get_Bridge_State` → 编译错误
- `App_Network.c:161` 引用了 `Ui_Controller_Get_State`/`UI_CONTROLLER_STATE_READY` → 编译错误
- 其他文件无编译错误

这些错误是预期的——将在后续 Task 中修复。

---

### Task 2: 重写 Ui_Controller.c — 静态变量 + 辅助函数 (保留部分)

**Files:**
- Modify: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 写入新版文件头部和保留函数**

这是整个文件的重写。下面分步给出完整的 `Ui_Controller.c`。

**第一部分：头部 + 宏定义 + 静态变量:**

```c
/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 V10 — 两级菜单架构
 * @note    TFT 8行×20列, 横屏 160×128, 4键: F+/F-/KEY0/PAGE
 *          配色: 黑底/黄标题/白正文/青数值/红报警/绿正常/灰不可选
 *          9 页: 主菜单→4子模式+监测子菜单→4仪表盘+故障页
 *          WIFI+Mqtt 双图标统一右上角
 ******************************************************************************
 */

#include "Ui_Controller.h"
#include "Tft_Driver.h"
#include "TFT_Img.h"
#include "Key_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Esp8266_Driver.h"
#include "App_Network.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Sys_Timer.h"
#include "Energy_Bar.h"
#include <stdio.h>

#define UI_COLOR_BG      TFT_COLOR_BLACK
#define UI_COLOR_TITLE   TFT_COLOR_YELLOW
#define UI_COLOR_TEXT    TFT_COLOR_WHITE
#define UI_COLOR_VALUE   TFT_COLOR_CYAN
#define UI_COLOR_DATA    TFT_COLOR_BLUE
#define UI_COLOR_ALARM   TFT_COLOR_RED
#define UI_COLOR_OK      TFT_COLOR_GREEN
#define UI_COLOR_DIM     TFT_COLOR_GRAY

#define UI_REFRESH_MS              200
#define UI_OVERCURRENT_THRESHOLD_A 5.0f
#define UI_POWER_V_THRESHOLD_V     12.0f

/* ── 中文串 (UTF-8 hex) ── */
#define S_WPT_PWM   "WPT-PWM"
#define S_SWEEP     "\xe6\x89\xab\xe9\xa2\x91\xe9\xa1\xb5"           /* 扫频页 */
#define S_MONITOR   "\xe7\x8a\xb6\xe6\x80\x81\xe7\x9b\x91\xe6\xb5\x8b" /* 状态监测 */
#define S_MON_FREQ  "\xe7\x9b\x91\xe6\xb5\x8b\xe9\xa2\x91\xe7\x8e\x87" /* 监测频率 */
#define S_MON_VOLT  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe5\x8e\x8b" /* 监测电压 */
#define S_MON_CURR  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe6\xb5\x81" /* 监测电流 */
#define S_LAUNCH    "\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa1\xb5"           /* 启动页 */
#define S_FREQ      "\xe9\xa2\x91\xe7\x8e\x87"                       /* 频率 */
#define S_VOLTAGE   "\xe7\x94\xb5\xe5\x8e\x8b"                       /* 电压 */
#define S_CURRENT   "\xe7\x94\xb5\xe6\xb5\x81"                       /* 电流 */
#define S_STOP      "\xe5\x81\x9c\xe6\xad\xa2"                       /* 停止 */
#define S_CLEAR_WIFI "\xe6\xb8\x85\xe9\x99\xa4WIFI"                  /* 清除WIFI */
#define S_SUMMARY   "\xe7\xbb\xbc\xe5\x90\x88\xe7\x9b\x91\xe6\xb5\x8b" /* 综合监测 */
#define S_BACK      "\xe8\xbf\x94\xe5\x9b\x9e\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95" /* 返回主菜单 */
#define S_DIV       "--------------------"           /* 分割线 */

/* ── 页面状态变量 ── */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_last_pwm_state  = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;    /* 开机默认联网=0, 用户清除WiFi后=1 */
static uint8_t  s_last_page       = 0xFF;
static uint8_t  s_last_cursor     = 0xFF;

/* EMA 平滑 */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

static void Reset_EMA(void) { s_ema_ok = 0; }
```

- [ ] **Step 2: 写入辅助函数 (保留原实现)**

```c
/* ═══════════════════════════════════════════════════════════════
 *  辅助函数 — Center/Right/Fmt_V/Fmt_I/Fmt_F (保留原实现)
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t Center(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : (20 - w) / 2;
}

static uint8_t Right(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : 20 - w;
}

static void Update_EMA(void)
{
    if (!s_ema_ok) {
        s_ema_v = Adc_Driver_Get_Voltage();
        s_ema_i = Adc_Driver_Get_Current();
        s_ema_f = (float)Pwm_Driver_Get_Frequency() / 1000.0f;
        s_ema_ok = 1;
    } else {
        s_ema_v = s_ema_v * 0.75f + Adc_Driver_Get_Voltage()              * 0.25f;
        s_ema_i = s_ema_i * 0.75f + Adc_Driver_Get_Current()              * 0.25f;
        s_ema_f = s_ema_f * 0.75f + (float)Pwm_Driver_Get_Frequency() / 1000.0f * 0.25f;
    }
}

static void Fmt_V(char* buf, float v)
{
    int x = (int)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 99999) x = 99999;
    snprintf(buf, 21, S_VOLTAGE "V:%03d.%02dV", x/100, x%100);
}

static void Fmt_I(char* buf, float c)
{
    char sign = (c < 0) ? '-' : '+';
    float v = (c < 0) ? -c : c;
    int x = (int)(v * 1000.0f + 0.5f);
    snprintf(buf, 21, S_CURRENT "I:%c%d.%03dA", sign, (int)(x/1000), (int)(x%1000));
}

static void Fmt_F(char* buf, float f)
{
    snprintf(buf, 21, S_FREQ "F:%3d.%01dkHz", (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

static uint8_t Is_WiFi_Online(void)
{
    if (s_no_wifi_mode) return 0;
    if (!Esp8266_Driver_Is_Ready()) return 0;
    return (App_Network_Get_Connect_Status() == APP_NETWORK_CONN_ONLINE);
}
```

- [ ] **Step 3: 写入 Draw_Header (保留原实现，微调)**

```c
/* ═══════════════════════════════════════════════════════════════
 *  Draw_Header — 第0行: 左侧标题 + MQTT云(x=128) + WIFI(x=144)
 *  保留原实现，所有界面统一调用
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Header(const char* title)
{
    #define MQTT_ICON_X  128
    #define WIFI_ICON_X  144

    /* 左侧: 标题 */
    Tft_Driver_Show_CN_String(0, 0, title, UI_COLOR_TITLE, UI_COLOR_BG);

    /* ── MQTT 云图标 (x=128) ── */
    {
        uint8_t cs = App_Network_Get_Connect_Status();
        static const uint16_t rainbow[6] = {
            0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0x001F
        };

        if (cs == APP_NETWORK_CONN_ONLINE) {
            Tft_Driver_Draw_Single_Icon(MQTT_ICON_X, 0, MQTT_YES_ICON, UI_COLOR_OK, UI_COLOR_BG);
        } else if (App_Network_Is_Connecting()) {
            uint8_t mqtt_frame = (uint8_t)(Sys_Timer_Get_Tick() / 200) % 6;
            Tft_Driver_Draw_Single_Icon(MQTT_ICON_X, 0,
                MQTT_ANIM[mqtt_frame], rainbow[mqtt_frame], UI_COLOR_BG);
        } else {
            Tft_Driver_Draw_Single_Icon(MQTT_ICON_X, 0, MQTT_NO_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
        }
    }

    /* ── WIFI 图标 (x=144) ── */
    {
        uint8_t  icon_frame;
        uint8_t  cs = App_Network_Get_Connect_Status();
        static const uint16_t blue_grad[6] = {
            0x0018, 0x001B, 0x001F, 0x07FF, 0x07BF, 0x07FF
        };

        if (s_no_wifi_mode) {
            Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
        } else if (!Esp8266_Driver_Is_Ready()) {
            icon_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6;
            Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0,
                WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
        } else if (cs == APP_NETWORK_CONN_ONLINE) {
            int8_t r = App_Network_Get_RSSI();
            if      (r >= -50) icon_frame = 3;
            else if (r >= -60) icon_frame = 2;
            else if (r >= -70) icon_frame = 1;
            else               icon_frame = 0;
            Tft_Driver_Draw_WiFi_Icon(WIFI_ICON_X, 0, icon_frame, UI_COLOR_OK, UI_COLOR_BG);
        } else if (App_Network_Is_Connecting()) {
            icon_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6;
            Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0,
                WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
        } else if (cs == APP_NETWORK_CONN_FAILED) {
            Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
        } else {
            Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_REMOVE_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
        }
    }

    #undef MQTT_ICON_X
    #undef WIFI_ICON_X
}
```

- [ ] **Step 4: 写入菜单通用渲染函数**

```c
/* ── 菜单项通用渲染: line=行, cursor=当前光标位置, idx=此项索引, text=菜单文字, enabled=1可选项 ── */
static void Draw_Menu_Item(uint8_t line, uint8_t cursor, uint8_t idx, const char* text, uint8_t enabled)
{
    uint16_t color = UI_COLOR_TEXT;
    if (!enabled)
        color = UI_COLOR_DIM;
    else if (cursor == idx)
        color = UI_COLOR_VALUE;

    if (cursor == idx) {
        /* 选中项: ▶ 前缀 */
        Tft_Driver_Show_CN_String(line, 0, "\xe2\x96\xb6", color, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(line, 1, text, color, UI_COLOR_BG);
    } else {
        /* 非选中项: 空格填充 */
        Tft_Driver_Show_String(line, 0, "  ", UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(line, 1, text, color, UI_COLOR_BG);
    }
}

/* ── 分割线: 画在指定行 ── */
static void Draw_Divider(uint8_t line)
{
    Tft_Driver_Show_String(line, 0, S_DIV, UI_COLOR_DIM, UI_COLOR_BG);
}
```

---

### Task 3: 写入 7 个 Draw 函数

**Files:**
- Modify: `Keil_Project/Hardware/Ui_Controller.c` (续写)

- [ ] **Step 1: Draw_Main_Menu — 主菜单 4 项**

```c
static void Draw_Main_Menu(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }

    Draw_Header(S_WPT_PWM);
    Draw_Divider(1);

    /* Item 1: 启动PWM / 停止PWM (动态文字) */
    {
        const char* t1 = is_running ? "1. " S_STOP "PWM" : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";
        Draw_Menu_Item(2, s_menu_cursor, 0, t1, 1);
    }

    /* Item 2: 状态监测 */
    Draw_Menu_Item(3, s_menu_cursor, 1, "2. " S_MONITOR, 1);

    /* Item 3: 无线配网 */
    Draw_Menu_Item(4, s_menu_cursor, 2, "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91", 1);

    /* Item 4: 故障清除 (仅故障时可进入) */
    Draw_Menu_Item(5, s_menu_cursor, 3, "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4", !is_fault ? 0 : 1);

    Draw_Divider(6);

    /* 底栏操作指南 (仅在光标不在故障项且故障不可选时使用) */
    {
        uint8_t max_cursor = is_fault ? 3 : 2;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    Tft_Driver_Show_CN_String(7, Center("[F+/F-:\xe4\xb8\x8a\xe4\xb8\x8b KEY0:\xe7\xa1\xae\xe5\xae\x9a]"),
        "[F+/F-:\xe4\xb8\x8a\xe4\xb8\x8b KEY0:\xe7\xa1\xae\xe5\xae\x9a]", UI_COLOR_TEXT, UI_COLOR_BG);
}
```

- [ ] **Step 2: Draw_Monitor_Sub_Menu — 监测子菜单 5 项**

```c
static void Draw_Monitor_Sub_Menu(void)
{
    Draw_Header(S_MONITOR);
    Draw_Divider(1);

    Draw_Menu_Item(2, s_menu_cursor, 0, "1. " S_SUMMARY, 1);
    Draw_Menu_Item(3, s_menu_cursor, 1, "2. " S_MON_FREQ, 1);
    Draw_Menu_Item(4, s_menu_cursor, 2, "3. " S_MON_VOLT, 1);
    Draw_Menu_Item(5, s_menu_cursor, 3, "4. " S_MON_CURR, 1);

    Draw_Divider(6);

    Draw_Menu_Item(7, s_menu_cursor, 4, "5. " S_BACK, 1);
}
```

- [ ] **Step 3: Draw_Sweep_Page — 扫频页**

```c
static void Draw_Sweep_Page(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char buf[21];

    progress = (SOFTSTART_START_FREQ_HZ - f) * 10
             / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
    if (progress > 10) progress = 10;

    Draw_Header(S_SWEEP);
    Draw_Divider(1);

    /* 频率 */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* 能量条 + 百分比 */
    Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                   14 * TFT_FONT_WIDTH, 8,
                   (float)progress, 0.0f, 10.0f,
                   ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
    snprintf(buf, sizeof(buf), "%d%%", progress * 10);
    if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);

    /* V/I */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("[KEY0:" S_STOP " PAGE:" S_BACK "]"),
        "[KEY0:" S_STOP " PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
}
```

- [ ] **Step 4: Draw_Monitor_Summary — 综合监测 (双模式)**

```c
static void Draw_Monitor_Summary(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    Draw_Header(S_SUMMARY);
    Draw_Divider(1);

    /* 频率 */
    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* 电压 */
    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* 电流 */
    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* 能量条 (仅发波时) */
    if (is_running) {
        Energy_Bar_Draw(0, 5 * TFT_FONT_HEIGHT, TFT_WIDTH, 12,
                       s_ema_v, 0.0f, 48.0f,
                       ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
    }

    Draw_Divider(6);

    if (is_running) {
        Tft_Driver_Show_CN_String(7, Right("[F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:" S_BACK "]"),
            "[F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
            "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
    }
}
```

- [ ] **Step 5: Draw_Monitor_Freq / Volt / Curr — 单个仪表盘**

```c
static void Draw_Monitor_Freq(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();
    Draw_Header(S_MON_FREQ);
    Draw_Divider(1);

    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   is_running ? s_ema_f : 0.0f, 95.0f, 150.0f,
                   ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "95", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 17, "150", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    if (is_running) {
        Tft_Driver_Show_CN_String(7, Right("[F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:" S_BACK "]"),
            "[F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
            "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

static void Draw_Monitor_Volt(void)
{
    char buf[21];

    Update_EMA();
    Draw_Header(S_MON_VOLT);
    Draw_Divider(1);

    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_v, 0.0f, 48.0f,
                   ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 17, "48", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
        "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
}

static void Draw_Monitor_Curr(void)
{
    char buf[21];

    Update_EMA();
    Draw_Header(S_MON_CURR);
    Draw_Divider(1);

    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_i, 0.0f, 3.0f,
                   ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 18, "3", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
        "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
}
```

- [ ] **Step 6: Draw_WiFi_Setup — 无线配网**

```c
static void Draw_WiFi_Setup(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    const char* status_text;
    char buf[21];

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = "\xe5\xb7\xb2\xe8\xbf\x9e\xe7\xba\xbf\xe4\xb8\x8a\xe7\xba\xbf";  /* 已连线上线 */
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = "\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5";              /* 连接失败 */
    else if (App_Network_Is_Connecting())
        status_text = "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad";                          /* 连接中 */
    else
        status_text = "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5";                          /* 未连接 */

    Draw_Header(S_LAUNCH);
    Draw_Divider(1);

    snprintf(buf, sizeof(buf), "\xe6\x97\xa0\xe7\xba\xbf\xe7\x8a\xb6\xe6\x80\x81: %s", status_text);
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_TEXT, UI_COLOR_BG);

    if (App_Network_Is_Connecting()) {
        snprintf(buf, sizeof(buf), "\xe9\x87\x8d\xe8\xaf\x95 %d/%d", App_Network_Get_Retry_Count(), 3);
        Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
    }

    Tft_Driver_Show_CN_String(5, 0,
        "\xe9\x95\xbf\xe6\x8c\x89" "ON:" S_CLEAR_WIFI, UI_COLOR_ALARM, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
        "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
}
```

- [ ] **Step 7: Draw_Fault_Page — 故障页**

```c
static void Draw_Fault_Page(void)
{
    Draw_Header("!!!\xe6\x95\x85\xe9\x9a\x9c!!!");  /* !!!故障!!! */
    Draw_Divider(1);

    Tft_Driver_Show_CN_String(2, Center("\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4"),
        "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4", UI_COLOR_ALARM, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(3, Center("PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"),
        "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad", UI_COLOR_TEXT, UI_COLOR_BG);

    Tft_Driver_Show_CN_String(5, Center("\xe6\x8c\x89" "KEY0" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf"),
        "\xe6\x8c\x89" "KEY0" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf", UI_COLOR_VALUE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("[PAGE:" S_BACK "]"),
        "[PAGE:" S_BACK "]", UI_COLOR_TEXT, UI_COLOR_BG);
}
```

---

### Task 4: 写入按键分发 + LED + 主调度

**Files:**
- Modify: `Keil_Project/Hardware/Ui_Controller.c` (续写)

- [ ] **Step 1: Update_Leds — 修正类型签名**

```c
/* ═══════════════════════════════════════════════════════════════
 *  LED 更新 — 根据当前页面 + 网络状态同步 6 路 LED
 * ═══════════════════════════════════════════════════════════════ */
static void Update_Leds(Ui_Page page)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);

    if (page == UI_PAGE_SWEEP)
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Power(
        Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Temp(LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Com(
        cs == APP_NETWORK_CONN_ONLINE
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);
}
```

- [ ] **Step 2: Handle_Keys_by_Page — 按键分发**

```c
/* ═══════════════════════════════════════════════════════════════
 *  按键分发 — 按键总线映射终版
 * ═══════════════════════════════════════════════════════════════ */
static void Handle_Keys_by_Page(Ui_Page page,
                                Key_Driver_Event k0, Key_Driver_Event k1,
                                Key_Driver_Event k2, Key_Driver_Event k3)
{
    uint8_t is_running = 0;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }

    /* ── F_UP (k1): 光标上移 或 频率+1k ── */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
            case UI_PAGE_MONITOR_SUB_MENU:
                if (s_menu_cursor > 0) { s_menu_cursor--; }
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
                if (is_running) {
                    uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
                    if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
                }
                break;
            default: break;
        }
    }

    /* ── F_DOWN (k2): 光标下移 或 频率-1k ── */
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU: {
                uint8_t is_fault = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT);
                uint8_t max_cursor = is_fault ? 3 : 2;
                if (s_menu_cursor < max_cursor) { s_menu_cursor++; }
                break;
            }
            case UI_PAGE_MONITOR_SUB_MENU:
                if (s_menu_cursor < 4) { s_menu_cursor++; }
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
                if (is_running) {
                    uint32_t f = Pwm_Driver_Get_Frequency();
                    if (f >= PWM_DRIVER_FREQ_MIN_HZ + 1000) Pwm_Driver_Set_Frequency(f - 1000);
                }
                break;
            default: break;
        }
    }

    /* ── KEY0 (k0): 确定/动作 ── */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                switch (s_menu_cursor) {
                    case 0: /* 启动PWM / 停止PWM */
                        if (is_running) {
                            Inverter_Control_Soft_Start_Stop();
                        } else {
                            Inverter_Control_Soft_Start_Trigger();
                            s_page = UI_PAGE_SWEEP;
                            Reset_EMA();
                        }
                        break;
                    case 1: /* 状态监测 */
                        s_page = UI_PAGE_MONITOR_SUB_MENU;
                        s_menu_cursor = 0;
                        break;
                    case 2: /* 无线配网 */
                        s_page = UI_PAGE_WIFI_SETUP;
                        break;
                    case 3: /* 故障清除 */
                        if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT) {
                            s_page = UI_PAGE_FAULT;
                        }
                        break;
                }
                break;

            case UI_PAGE_MONITOR_SUB_MENU:
                switch (s_menu_cursor) {
                    case 0: s_page = UI_PAGE_MONITOR_SUMMARY; Reset_EMA(); break;
                    case 1: s_page = UI_PAGE_MONITOR_FREQ;    Reset_EMA(); break;
                    case 2: s_page = UI_PAGE_MONITOR_VOLT;    Reset_EMA(); break;
                    case 3: s_page = UI_PAGE_MONITOR_CURR;    Reset_EMA(); break;
                    case 4: s_page = UI_PAGE_MAIN_MENU;       s_menu_cursor = 0; break;
                }
                break;

            case UI_PAGE_SWEEP:
                /* 停止扫频 */
                Inverter_Control_Soft_Start_Stop();
                break;

            case UI_PAGE_FAULT:
                /* 复位重启 → 回主菜单 */
                Inverter_Control_Soft_Start_Reset();
                s_page = UI_PAGE_MAIN_MENU;
                s_menu_cursor = 0;
                s_was_fault_state = 0;
                Reset_EMA();
                break;

            default: break;
        }
    }

    /* ── KEY0 长按清除 WiFi (任意页面可用) ── */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            s_no_wifi_mode = 1;
            s_page = UI_PAGE_MAIN_MENU;
            s_menu_cursor = 0;
            Reset_EMA();
        }
    }

    /* ── PAGE (k3): 返回上一层 ── */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MONITOR_SUB_MENU:
                s_page = UI_PAGE_MAIN_MENU;
                s_menu_cursor = 0;
                break;
            case UI_PAGE_SWEEP:
            case UI_PAGE_WIFI_SETUP:
            case UI_PAGE_FAULT:
                s_page = UI_PAGE_MAIN_MENU;
                s_menu_cursor = 0;
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
            case UI_PAGE_MONITOR_VOLT:
            case UI_PAGE_MONITOR_CURR:
                s_page = UI_PAGE_MONITOR_SUB_MENU;
                s_menu_cursor = 0;
                break;
            default: break;
        }
    }
}
```

- [ ] **Step 3: Ui_Controller_Task — 主调度**

```c
/* ═══════════════════════════════════════════════════════════════
 *  主调度 — 200ms 周期: 边沿检测 → 按键 → 页面绘制 → LED
 * ═══════════════════════════════════════════════════════════════ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last_ui_ms = 0;
    uint8_t need_draw = 0;

    /* ── 1. 故障边沿检测 (每帧) ── */
    {
        uint8_t current_fault = (Inverter_Control_Soft_Start_Get_State()
                                 == INVERTER_CONTROL_SS_STATE_FAULT);
        if (current_fault && !s_was_fault_state) {
            /* 边沿 0→1: 强制跳入故障页 */
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
            Tft_Driver_Clear(UI_COLOR_BG);
        }
        if (!current_fault) {
            s_was_fault_state = 0;
        }
    }

    /* ── 2. 扫频完成检测: SWEEP → 自动跳综合监测 ── */
    if (s_page == UI_PAGE_SWEEP) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
            s_page = UI_PAGE_MONITOR_SUMMARY;
            Reset_EMA();
        }
    }

    /* ── 3. 页面变更 → 清屏 ── */
    if ((uint8_t)s_page != s_last_page || s_menu_cursor != s_last_cursor) {
        s_last_page   = (uint8_t)s_page;
        s_last_cursor = s_menu_cursor;
        /* Tft_Driver_Clear 不在菜单光标移动时清屏 — 菜单光标变化用局部重绘 */
        if (s_page != UI_PAGE_MAIN_MENU && s_page != UI_PAGE_MONITOR_SUB_MENU) {
            /* 非菜单页: 页面变更时清屏 */
        }
        Tft_Driver_Clear(UI_COLOR_BG);
        need_draw = 1;
    }

    /* ── 4. 200ms 周期刷新 ── */
    if (Sys_Timer_Get_Tick() - s_last_ui_ms >= UI_REFRESH_MS) {
        s_last_ui_ms = Sys_Timer_Get_Tick();
        need_draw = 1;
    }

    /* ── 5. 按键采集 (每帧) ── */
    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);

    Handle_Keys_by_Page(s_page, k0, k1, k2, k3);

    /* ── 6. PB10 PowerContrl ── */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* ── 7. 过流保护 (边沿触发故障) ── */
    if (s_page == UI_PAGE_SWEEP ||
        s_page == UI_PAGE_MONITOR_SUMMARY ||
        s_page == UI_PAGE_MONITOR_FREQ) {
        Update_EMA();
        if (s_ema_i > UI_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            /* s_was_fault_state=0 → 下一帧边沿检测触发跳转 */
        }
    }

    if (s_page != UI_PAGE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

    /* ── 8. 绘制 ── */
    if (need_draw) {
        switch (s_page) {
            case UI_PAGE_MAIN_MENU:        Draw_Main_Menu();        break;
            case UI_PAGE_MONITOR_SUB_MENU: Draw_Monitor_Sub_Menu(); break;
            case UI_PAGE_SWEEP:            Draw_Sweep_Page();       break;
            case UI_PAGE_MONITOR_SUMMARY:  Draw_Monitor_Summary();  break;
            case UI_PAGE_MONITOR_FREQ:     Draw_Monitor_Freq();     break;
            case UI_PAGE_MONITOR_VOLT:     Draw_Monitor_Volt();     break;
            case UI_PAGE_MONITOR_CURR:     Draw_Monitor_Curr();     break;
            case UI_PAGE_WIFI_SETUP:       Draw_WiFi_Setup();       break;
            case UI_PAGE_FAULT:            Draw_Fault_Page();       break;
        }
        Update_Leds(s_page);
    }
}
```

- [ ] **Step 4: 写入公开接口函数**

```c
/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */
Ui_Page Ui_Controller_Get_Page(void)      { return s_page; }
uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }
```

---

### Task 5: 更新 App_Network.c + main.c

**Files:**
- Modify: `Keil_Project/User/App_Network.c:161`
- Modify: `Keil_Project/User/main.c`

- [ ] **Step 1: 修复 App_Network.c 引用**

在 `App_Network.c:161`，将:

```c
if (Ui_Controller_Get_State() < UI_CONTROLLER_STATE_READY) allow_telemetry = 0;
```

改为:

```c
{
    Ui_Page page = Ui_Controller_Get_Page();
    if (page == UI_PAGE_MAIN_MENU || page == UI_PAGE_MONITOR_SUB_MENU
        || page == UI_PAGE_WIFI_SETUP || page == UI_PAGE_SWEEP
        || page == UI_PAGE_FAULT)
        allow_telemetry = 0;
}
```

- [ ] **Step 2: 更新 main.c 启动阶段4**

将第 90 行注释:

```c
/* ── 阶段4: 开机默认无WIFI模式, 用户双击ON手动联网 ── */
```

改为:

```c
/* ── 阶段4: 开机自动联网 (ESP8266 WiFiManager 记忆上次配网) ── */
App_Network_Start_Connect();
```

- [ ] **Step 3: 更新 main.c 启动页**

将第 73-74 行启动页文字:

```c
Tft_Driver_Show_CN_String(3, 2, "\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
```

改为:

```c
Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
```

---

### Task 6: 编译验证 + 手动审查

**Files:** 无新文件

- [ ] **Step 1: 编译**

在 Keil IDE 中 F7 编译。预期输出:

```
Build target 'Project'
compiling Ui_Controller.c...
compiling App_Network.c...
compiling main.c...
linking...
Program Size: Code=xxxxx RO-data=xxxx RW-data=xxxx ZI-data=xxxx
FromELF: creating hex file...
".\Objects\Project.hex" - 0 Error(s), 0 Warning(s).
Build Time Elapsed: 00:00:xx
```

- [ ] **Step 2: 检查 ARMCC 常见警告**

确认无以下警告:
- `#1293-D`: `if ((p = ...)` 赋值在条件中 (已在 Draw_Header 中处理)
- `#27-D`: UTF-8 字符串拼接 (已在宏中处理)
- `#68-D`: 整数转换为指针

- [ ] **Step 3: 静态审查 — 变量引用完整性**

验证以下旧变量/函数已完全移除:
```bash
rg "Ui_Controller_State" Keil_Project/  # 应仅在 .h(新枚举) 中出现
rg "Calc_Ui_State" Keil_Project/        # 应无匹配
rg "Ui_Controller_Get_State" Keil_Project/  # 应仅在 .h 出现
rg "Ui_Controller_Get_Bridge_State" Keil_Project/  # 应无匹配
rg "s_ui_state" Keil_Project/           # 应无匹配
```

---

### Task 7: 代码审查

- [ ] **Step 1: 使用 code-reviewer 审查**

```bash
git diff HEAD -- Keil_Project/Hardware/Ui_Controller.c \
                 Keil_Project/Hardware/Ui_Controller.h \
                 Keil_Project/User/App_Network.c \
                 Keil_Project/User/main.c
```

关注点:
- 菜单光标边界检查 (s_menu_cursor 不越界)
- 故障边沿触发逻辑正确
- 扫频完成自动跳转逻辑
- PAGE 返回栈正确性
- EMA 重置时机
- 长按 WiFi 清除全局可用
- 文件总行数 ≤ 900

- [ ] **Step 2: 修复审查发现的问题**

- [ ] **Step 3: 提交**

```bash
git add Keil_Project/Hardware/Ui_Controller.h \
        Keil_Project/Hardware/Ui_Controller.c \
        Keil_Project/User/App_Network.c \
        Keil_Project/User/main.c \
        docs/superpowers/specs/2026-06-14-ui-refactor-design.md \
        docs/superpowers/plans/
git commit -m "feat: UI重构 — V10两级菜单架构

主菜单4项+监测子菜单5项+扫频/综合/频率/电压/电流/配网/故障页
替换6态线性状态机为9页枚举
按键总线: F+/F-导航+调频, KEY0确定/动作, PAGE返回
故障边沿触发防无限重入

详见 docs/superpowers/specs/2026-06-14-ui-refactor-design.md"
```
