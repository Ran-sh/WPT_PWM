/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 V5.0.1 — 17 页面 + 圆弧能量条 + 增量刷新
 *
 *  Hardware dependencies (indirect, via Driver layer):
 *  +----------------------------------------------------------+
 *  |                       STM32F103C8T6                       |
 *  |                                                           |
 *  |    Tft_Driver:  SPI1+DMA (PA5/PA7/PA4/PA6/PA0/PB6)  disp  |
 *  |    Key_Driver:  PB9/PB8/PB7/PB5 (IPU)                inp  |
 *  |    Led_Driver:  PA15/PB4/PB3/PA10/PA11               sta  |
 *  |    Buzzer:      PB15 (PP)                             be  |
 *  |    Pwm_Driver:  TIM1 CH1/CH2/CH1N/CH2N               pow  |
 *  |    Sys_Core:    state machine + unified power control     |
 *  |    Sys_Timer:   timebase (200ms inc refresh cycle)        |
 *  |                                                           |
 *  |    9 pages: MAIN_MENU/SUB_MENU/VOLTAGE/CURRENT/           |
 *  |             FREQUENCY/SUMMARY/WIFI_CONFIG/FAULT/          |
 *  |             SWEEP_PROGRESS                                |
 *  |                                                           |
 *  |    UI Phase pipeline (7 phases):                          |
 *  |      P0=TopRight Icons  P1=Fault Detect  P2=Sweep AutoJu  |
 *  |      P3=Key Scan+Disp   P4=Page Change    P5=200ms Dynam  |
 *  |      P6=Cursor Clamp    P7=Draw (full only when dirty)    |
 *  +----------------------------------------------------------+
 *
 * @note    TFT 8x20 cols, 160x128 landscape, 4 keys: ON/OFF/F+/F-/PAGE
 ******************************************************************************
 */

#include "Ui_Controller.h"
#include "Sys_Core.h"
#include "Tft_Driver.h"
#include "Key_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Esp8266_Driver.h"
#include "App_Network.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Sys_Timer.h"
#include "App_Storage.h"
#include <stdio.h>
#include <string.h>
static void Draw_TopRight_Icons(void);

/* ── Energy Bar color table + draw logic (merged from Energy_Bar.c) ── */
static const uint16_t EB_COLOR_TABLE[8] = {
    0x07E0, 0x2FE0, 0x5FE0, 0x87E0, 0xFF80, 0xFD00, 0xF900, 0xF800  /* 绿→黄→红 RGB565 */
};

static void Ui_Energy_Bar_Draw(uint16_t x, uint16_t y, uint16_t max_w, uint16_t h,
                                float value, float min_val, float max_val, uint16_t bg_color)
{
    uint16_t total_w;
    uint8_t  seg_count, i;
    uint16_t seg_w, seg_x;
    static uint16_t s_last_w = 0xFFFF;  /* track for incremental erase */

    {
        float range = max_val - min_val;
        float ratio;
        if (range <= 0.0f) { Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color); s_last_w = 0; return; }
        ratio = (value - min_val) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        total_w = (uint16_t)(ratio * (float)max_w);
    }

    if (total_w == s_last_w) return;  /* no change — skip entirely */

    /* ── Incremental erase: only clear changed area ── */
    if (total_w < s_last_w) {
        /* bar shrunk — erase from new edge to old edge */
        Tft_Driver_Fill_Rect(x + total_w, y, s_last_w - total_w, h, bg_color);
    } else if (s_last_w == 0xFFFF) {
        /* first call — full clear (unknown prior state) */
        Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color);
    }
    /* bar grew — only draw new segments (full erase case already falls through) */
    s_last_w = total_w;
    if (total_w == 0) return;

    seg_count = (uint8_t)(total_w / 4);
    if (seg_count < 1) seg_count = 1;
    if (seg_count > 8) seg_count = 8;
    seg_w = total_w / seg_count;

    for (i = 0; i < seg_count; i++) {
        seg_x = x + i * seg_w;
        {
            uint16_t w = seg_w;
            if (i == seg_count - 1) w = (x + total_w) - seg_x;
            if (w > 0) {
                uint8_t ci = (uint8_t)(((uint16_t)i * 8) / seg_count);
                if (ci >= 8) ci = 7;
                Tft_Driver_Fill_Rect(seg_x, y, w, h, EB_COLOR_TABLE[ci]);
            }
        }
    }
}
/* ════════════════════════════════════════════════════════════
 *  Settings State (preview then confirm)
 * ════════════════════════════════════════════════════════════ */
static uint8_t  s_language         = 1;     /* 0=Chinese, 1=English (默认英文) */
static uint8_t  s_letter_spacing   = 0;     /* inter-char gap 0-3 px (V4.5.2: replaces font_size) */
static uint8_t  sc_preset          = 0;     /* 0-5 preset, 255=custom */
static uint16_t s_color_fg         = 0xFFFF;/* RGB565 default white */
static uint16_t s_color_bg         = 0x0000;/* RGB565 default black */
static uint16_t s_color_accent     = 0xFFE0;/* RGB565 default yellow */
static uint8_t  s_icon_cache_valid = 0U;
static uint8_t  s_last_icon_cs     = 0xFFU;
static uint8_t  s_last_icon_mode   = 0xFFU;
static uint8_t  s_last_wifi_frame  = 0xFFU;
static uint8_t  s_last_mqtt_frame  = 0xFFU;
static uint8_t  s_last_rssi_frame  = 0xFFU;
static uint8_t  s_last_icon_page   = 0xFFU;
static uint16_t s_last_icon_bg     = 0xFFFFU;
static uint16_t s_last_icon_fg     = 0xFFFFU;

/* Settings sub-page cursors */
static uint8_t  s_setting_cursor   = 0;
static uint8_t  s_icon_page        = 0;
static uint8_t  s_icon_cursor      = 0;

/* Preview-before-confirm (Language & Spacing): cursor position for selection */
static uint8_t  s_preview_choice   = 0;     /* visual cursor position, committed on PAGE */

/* Pending save */
static uint8_t  s_settings_dirty       = 0;
static uint8_t  s_last_setting_cursor  = 0xFF; /* for Phase 7 deferred cursor tracking */

/* ═══════════════════════════════════════════════════════════════
 *  Dynamic Color System (V4.5.2) — Uc_*() inline helpers
 * ═══════════════════════════════════════════════════════════════ */
static uint16_t Uc_Bg(void)      { return s_color_bg; }
static uint16_t Uc_Title(void)   { return s_color_accent; }
static uint16_t Uc_Text(void)    { return s_color_fg; }
static uint16_t Uc_Value(void)   { return s_color_accent; }
static uint16_t Uc_Data(void)    { return s_color_fg; }
#define Uc_Alarm()  TFT_COLOR_RED
#define Uc_Ok()     TFT_COLOR_GREEN
#define Uc_Dim()    TFT_COLOR_GRAY

/* ═══════════════════════════════════════════════════════════════
 *  Bilingual String System (V4.5.2)
 *  Pick_CN_EN() inline function replaces macros to avoid ARMCC macro issues.
 *  Used both as snprintf format arg and Show_CN_String arg.
 * ═══════════════════════════════════════════════════════════════ */
static const char* Pick_CN_EN(const char* cn, const char* en) {
    /* CN only when user selected Chinese AND Flash font is available (Bug 1) */
    return (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid()) ? cn : en;
}
#define S_WIFI_TITLE_CN "\xe6\x97\xa0\xe7\xba\xbf\xe7\x8a\xb6\xe6\x80\x81"
#define S_WIFI_TITLE_EN "WiFi Status"

#define UI_REFRESH_MS              200
#define UI_OVERCURRENT_THRESHOLD_A 5.0f
#define UI_POWER_V_THRESHOLD_V     12.0f

/* -------- Bilingual strings (CN / EN) -------- */
#define S_WPT_PWM_CN   "\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95"         /* 主菜单 */
#define S_WPT_PWM_EN   "Main Menu"
#define S_SWEEP_CN     "\xe6\x89\xab\xe9\xa2\x91\xe4\xb8\xad"         /* 扫频中 */
#define S_SWEEP_EN     "Sweeping"
#define S_MONITOR_CN   "\xe7\x8a\xb6\xe6\x80\x81\xe7\x9b\x91\xe6\xb5\x8b" /* 状态监测 */
#define S_MONITOR_EN   "Monitor"
#define S_MON_FREQ_CN  "\xe9\xa2\x91\xe7\x8e\x87\xe8\xa1\xa8\xe7\x9b\x98" /* 频率表盘 */
#define S_MON_FREQ_EN  "Frequency"
#define S_MON_VOLT_CN  "\xe7\x94\xb5\xe5\x8e\x8b\xe8\xa1\xa8\xe7\x9b\x98" /* 电压表盘 */
#define S_MON_VOLT_EN  "Voltage"
#define S_MON_CURR_CN  "\xe7\x94\xb5\xe6\xb5\x81\xe8\xa1\xa8\xe7\x9b\x98" /* 电流表盘 */
#define S_MON_CURR_EN  "Current"
#define S_LAUNCH_CN    "\xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91" /* 无线配网 */
#define S_LAUNCH_EN    "WiFi Setup"
#define S_FREQ_CN      "\xe9\xa2\x91\xe7\x8e\x87"                     /* freq */
#define S_FREQ_EN      "Freq"
#define S_VOLTAGE_CN   "\xe7\x94\xb5\xe5\x8e\x8b"                     /* voltage */
#define S_VOLTAGE_EN   "Volt"
#define S_CURRENT_CN   "\xe7\x94\xb5\xe6\xb5\x81"                     /* current */
#define S_CURRENT_EN   "Curr"
#define S_WIFI_ONLINE_CN  "\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x88\x90\xe5\x8a\x9f" /* 连接成功 */
#define S_WIFI_ONLINE_EN  "Online"
#define S_WIFI_CONN_CN    "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad"           /* 连接中 */
#define S_WIFI_CONN_EN    "Connecting"
#define S_WIFI_OFFLINE_CN "\xe5\xb7\xb2\xe7\xa6\xbb\xe7\xba\xbf"           /* 已离线 */
#define S_WIFI_OFFLINE_EN "Offline"
#define S_WIFI_IDLE_CN    "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5"           /* 未连接 */
#define S_WIFI_IDLE_EN    "Disconnected"
#define S_SUMMARY_CN   "\xe7\xbb\xbc\xe5\x90\x88\xe7\x9b\x91\xe6\xb5\x8b" /* 综合监测 */
#define S_SUMMARY_EN   "Summary"
#define S_BACK_CN      "\xe8\xbf\x94\xe5\x9b\x9e\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95" /* 返回主菜单 */
#define S_BACK_EN      "Back to Menu"
#define S_LABEL_FREQ_CN "\xe9\xa2\x91\xe7\x8e\x87 kHz"
#define S_LABEL_FREQ_EN "Freq kHz"
#define S_LABEL_VOLT_CN "\xe7\x94\xb5\xe5\x8e\x8b V"
#define S_LABEL_VOLT_EN "Volt V"
#define S_LABEL_CURR_CN "\xe7\x94\xb5\xe6\xb5\x81 A"
#define S_LABEL_CURR_EN "Curr A"
#define S_DIV       "--------------------"           /* divider (纯ASCII) */

#define S_PAUSE_CN     "\xe5\xb7\xb2\xe6\x9a\x82\xe5\x81\x9c"           /* 已暂停 */
#define S_PAUSE_EN     "Paused"
#define S_OVERCUR_CN   "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4" /* 过流保护 */
#define S_OVERCUR_EN   "Overcurrent!"
#define S_ADC_STALE_CN "\xe9\x87\x87\xe6\xa0\xb7\xe5\xa4\xb1\xe6\x95\x88" /* 采样失效 */
#define S_ADC_STALE_EN "ADC Stale"
#define S_CONTROL_ERR_CN "\xe6\x8e\xa7\xe5\x88\xb6\xe5\xbc\x82\xe5\xb8\xb8" /* 控制异常 */
#define S_CONTROL_ERR_EN "Control Error"
#define S_UNKNOWN_FAULT_CN "\xe6\x9c\xaa\xe7\x9f\xa5\xe6\x95\x85\xe9\x9a\x9c" /* 未知故障 */
#define S_UNKNOWN_FAULT_EN "Unknown Fault"
#define S_PWM_OFF_CN   "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"       /* PWM已关断 */
#define S_PWM_OFF_EN   "PWM Disabled"
#define S_FAULT_TITLE_CN "\xe6\x95\x85\xe9\x9a\x9c\xe9\xa1\xb5"         /* 故障页 */
#define S_FAULT_TITLE_EN "FAULT"
#define S_RESET_HINT_CN "\xe6\x8c\x89PAGE\xe5\xa4\x8d\xe4\xbd\x8d\xe9\x87\x8d\xe5\x90\xaf"
#define S_RESET_HINT_EN "[PAGE] Reset"
#define S_LONG_CLEAR_CN "\xe9\x95\xbf\xe6\x8c\x89\xe6\xb8\x85\xe9\x99\xa4WIFI"  /* 长按清除WiFi */
#define S_LONG_CLEAR_EN "Long-Press Clear"
#define S_DISCONNECT_CN "\xe6\x96\xad\xe5\xbc\x80"                      /* 断开 */
#define S_DISCONNECT_EN "Disconnect"
#define S_CONNECT_CN    "\xe8\xbf\x9e\xe6\x8e\xa5"                      /* 连接 */
#define S_CONNECT_EN    "Connect"
#define S_STOP_PWM_CN   "1. \xe5\x81\x9c\xe6\xad\xa2PWM"               /* 停止PWM */
#define S_STOP_PWM_EN   "1. Stop PWM"
#define S_START_PWM_CN  "1. \xe5\x90\xaf\xe5\x8a\xa8PWM"               /* 启动PWM */
#define S_START_PWM_EN  "1. Start PWM"
#define S_WIFI_SETUP_CN "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91"
#define S_WIFI_SETUP_EN "3. WiFi Setup"
#define S_FAULT_CLEAR_CN "5. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4"
#define S_FAULT_CLEAR_EN "5. Clear Fault"

/* V4.5.2 Settings strings */
#define S_SETTINGS_CN    "\xe8\xae\xbe\xe7\xbd\xae"        /* 设置 */
#define S_SETTINGS_EN    "Settings"
#define S_SETTINGS_LANG_CN "\xe8\xaf\xad\xe8\xa8\x80"      /* 语言 */
#define S_SETTINGS_LANG_EN "Language"
#define S_SETTINGS_ICONS_CN "\xe5\x9b\xbe\xe6\xa0\x87"     /* 图标 */
#define S_SETTINGS_ICONS_EN "Icons"
#define S_SETTINGS_FONT_CN "\xe5\xad\x97\xe4\xbd\x93"      /* 字体 */
#define S_SETTINGS_FONT_EN "Font Size"
#define S_SETTINGS_COLOR_CN "\xe9\xa2\x9c\xe8\x89\xb2"     /* 颜色 */
#define S_SETTINGS_COLOR_EN "Color"
#define S_TITLE_LANG_CN    S_SETTINGS_LANG_CN
#define S_TITLE_LANG_EN    S_SETTINGS_LANG_EN
#define S_TITLE_ICONS_CN   S_SETTINGS_ICONS_CN
#define S_TITLE_ICONS_EN   S_SETTINGS_ICONS_EN
#define S_TITLE_FONT_CN    "\xe5\xad\x97\xe4\xbd\x93\xe5\xa4\xa7\xe5\xb0\x8f"
#define S_TITLE_FONT_EN    "Font Size"
#define S_TITLE_COLOR_CN   "\xe9\xa2\x9c\xe8\x89\xb2\xe6\x96\xb9\xe6\xa1\x88"
#define S_TITLE_COLOR_EN   "Color Scheme"
#define S_ON_RETURN_CN     "[ON]\xe8\xbf\x94\xe5\x9b\x9e"
#define S_ON_RETURN_EN     "[ON] Back"
#define S_MON_MENU_EN      "2. Monitor"
#define S_SETTINGS_MENU_CN "4. \xe8\xae\xbe\xe7\xbd\xae"  /* "4. 设置" */
#define S_SETTINGS_MENU_EN "4. Settings"
#define S_FLASH_REQUIRED_CN "\xe9\x9c\x80\xe8\xa6\x81W25\xe9\x97\xaa\xe5\xad\x98"  /* "需要W25闪存" */
#define S_FLASH_REQUIRED_EN "Flash required"

/* -------- Page state variables -------- */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;
static uint8_t  s_last_page       = 0xFF;

/* EMA filtering (V/I from Sys_Safety, F is raw digital — no EMA lag) */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

/* User freq stepping */
static uint32_t s_user_target_hz = 100000;
static uint8_t  s_user_target_synced = 0;

/* ═══════════════════════════════════════════════════════════════
 *  Color Preset Table (V4.5.2: all 6 presets with diverse backgrounds)
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    const char* name_cn;
    const char* name_en;
    uint16_t bg;
    uint16_t fg;
    uint16_t accent;
} ColorPreset;

static const ColorPreset COLOR_PRESETS[6] = {
    {"\xe7\xbb\x8f\xe5\x85\xb8\xe9\xbb\x91",  "Classic",   0x0000,0xFFFF,0xFFE0},
    {"\xe7\x90\xa5\xe7\x8f\x80",             "Amber",     0x1008,0xFD40,0xFC00},
    {"\xe9\x9d\x92\xe8\x93\x9d",             "Cyber",     0x0018,0x07FF,0x07E0},
    {"\xe6\x8a\xa4\xe7\x9c\xbc",             "EyeCare",   0x0820,0xC0E8,0x70B8},
    {"\xe9\xab\x98\xe5\xaf\xb9\xe6\xaf\x94", "HiContrast",0x0000,0xFFFF,0x07E0},
    {"\xe9\x9c\x9c\xe7\x99\xbd",             "Frost",     0xE8E0,0x1C14,0x6040},
};

/* ── Incremental refresh state (V4.2.0) ── */
static uint8_t s_page_drawn         = 0;    /* 0=need full redraw, 1=static content present */
static uint8_t s_last_is_running    = 0xFF; /* tracked PWM running state */
static uint8_t s_last_is_fault_menu = 0xFF; /* tracked FAULT state for menu item 3 */
static uint8_t s_last_sub_visible   = 0;    /* tracked sub-menu visible_top */
static uint8_t s_last_sweep_stopped = 0xFF; /* tracked sweep pause state */
static uint8_t s_last_wifi_cs       = 0xFF; /* tracked WiFi connection status */

/* Cached last formatted value strings — avoid redrawing unchanged values */
static char    s_last_f_str[21];
static char    s_last_v_str[21];
static char    s_last_i_str[21];
static char    s_last_status_buf[42];

/* Gauge value & status cache (for diff-based incremental refresh) */
static char    s_gauge_val_str[24] = "";
static char    s_gauge_status_buf[24] = "";

static void Reset_EMA(void) { s_ema_ok = 0; }

/* ================================================================
 *  Helpers: Center / Right / Fmt_V / Fmt_I / Fmt_F
 * ================================================================ */

static uint8_t Center(const char* s)
{
    uint8_t w = 0; uint8_t sp = Tft_Driver_Get_Letter_Spacing();
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }
        else { w++; s++; }
    }
    /* V4.5.2: factor in letter_spacing extra pixels (convert to char columns) */
    if (sp > 0) w += (uint8_t)((sp + 7) / 8);
    return (w >= 20) ? 0 : (20 - w) / 2;
}

static uint8_t Right(const char* s)
{
    uint8_t w = 0; uint8_t sp = Tft_Driver_Get_Letter_Spacing();
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }
        else { w++; s++; }
    }
    if (sp > 0) w += (uint8_t)((sp + 7) / 8);
    return (w >= 20) ? 0 : 20 - w;
}

/**
 * @brief  UI 层 EMA 滤波: V/I 平滑 Sys_Safety 输出(显示级二次滤波), F 直接读数无迟滞
 * @note   V/I: α=0.25, τ≈800ms, 减少屏幕数值高频抖动
 *         F:   数字寄存器原子值, 零 EMA 迟滞, 保证按键调频跟手
 *         数据源: Sys_Safety 已对 ADC 做一级滤波, 此处仅做显示平滑
 */
static void Update_EMA(void)
{
    if (!s_ema_ok) {
        s_ema_v = Sys_Safety_Get_EMA_Voltage();
        s_ema_i = Sys_Safety_Get_EMA_Current();
        s_ema_f = (float)Pwm_Driver_Get_Frequency() / 1000.0f;
        s_ema_ok = 1;
    } else {
        s_ema_v = s_ema_v * 0.75f + Sys_Safety_Get_EMA_Voltage()  * 0.25f;
        s_ema_i = s_ema_i * 0.75f + Sys_Safety_Get_EMA_Current()  * 0.25f;
        s_ema_f = (float)Pwm_Driver_Get_Frequency() / 1000.0f;  /* 数字量直读, 零迟滞 */
    }
}

static void Fmt_V(char* buf, float v)
{
    int x = (int)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 99999) x = 99999;
    snprintf(buf, 21, "%sV:%03d.%02dV", Pick_CN_EN(S_VOLTAGE_CN, S_VOLTAGE_EN), x/100, x%100);
}

static void Fmt_I(char* buf, float c)
{
    char sign = (c < 0) ? '-' : '+';
    float v = (c < 0) ? -c : c;
    int x = (int)(v * 1000.0f + 0.5f);
    snprintf(buf, 21, "%sI:%c%d.%03dA", Pick_CN_EN(S_CURRENT_CN, S_CURRENT_EN), sign, (int)(x/1000), (int)(x%1000));
}

static void Fmt_F(char* buf, float f)
{
    snprintf(buf, 21, "%sF:%3d.%01dkHz", Pick_CN_EN(S_FREQ_CN, S_FREQ_EN), (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

/* ================================================================
 *  Draw_Header: line0 title(left) — icons via Draw_TopRight_Icons
 * ================================================================ */
/**
 * @brief  绘制页面标题 (全行擦除 + 重绘图标, 防止语言切换残影)
 * @note   Bug 1: 全行 160px Fill_Rect 彻底清除旧像素, 图标紧随重绘。
 *         调用方不再单独调 Draw_TopRight_Icons (避免双泵浪费)。
 */
static void Draw_Header(const char* title)
{
    Tft_Driver_Fill_Rect(0, 0, TFT_WIDTH, TFT_FONT_HEIGHT, Uc_Bg());
    Tft_Driver_Show_CN_String(0, 0, title, Uc_Title(), Uc_Bg());
    Draw_TopRight_Icons();
}

/* ================================================================
 *  Cursor: ICON_STAR at pixel x=0 — draw/erase (16x16 pixel update)
 * ================================================================ */
static void Draw_Cursor(uint8_t line)
{
    /* ICON_STAR (diamond) at left edge — black star on cyan bg for selected row */
    Tft_Driver_Draw_Icon_By_Id(0, (uint16_t)line * TFT_FONT_HEIGHT,
                                ICON_ID_STAR, 0, Uc_Bg(), Uc_Value());
}

static void Erase_Cursor(uint8_t line)
{
    /* Erase the 16x16 icon area with black */
    Tft_Driver_Erase_Pixel_Area(0, (uint16_t)line * TFT_FONT_HEIGHT, 16, 16);
}

/* ================================================================
 *  Line primitives
 * ================================================================ */
static void Erase_Line(uint8_t line)
{
    Tft_Driver_Erase_Pixel_Area(0, (uint16_t)line * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT);
}

static void Draw_Divider(uint8_t line)
{
    Tft_Driver_Show_String(line, 0, S_DIV, Uc_Dim(), Uc_Bg());
}

/* ── Draw menu text at line,col (precise erase excluding cursor zone, text at col>=2 for star) ── */
static void Draw_Menu_Text(uint8_t line, uint8_t col, const char* text, uint8_t enabled)
{
    uint16_t color = enabled ? Uc_Text() : Uc_Dim();
    Tft_Driver_Erase_Pixel_Area(col * 8, (uint16_t)line * TFT_FONT_HEIGHT, TFT_WIDTH - col * 8, TFT_FONT_HEIGHT);
    Tft_Driver_Show_CN_String(line, col, text, color, Uc_Bg());
}

/* ================================================================
 *  Page draw: MAIN_MENU (4/5 items) — covers all 8 rows
 * ================================================================ */
static void Draw_Main_Menu_Full(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    uint8_t i;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }
    is_fault = (Sys_Core_Get_State() == SYS_STATE_FAULT);

    Draw_Header(Pick_CN_EN(S_WPT_PWM_CN, S_WPT_PWM_EN));
    Draw_Divider(1);

    /* V4.5.2: 5 项常驻 — 第 5 项非故障时灰色禁用 */
    for (i = 0; i < 5; i++) {
        const char* text;
        uint8_t enabled = 1;
        switch (i) {
            case 0:
                text = is_running
                    ? Pick_CN_EN(S_STOP_PWM_CN, S_STOP_PWM_EN)
                    : Pick_CN_EN(S_START_PWM_CN, S_START_PWM_EN);
                break;
            case 1: text = Pick_CN_EN("2. " S_MONITOR_CN, S_MON_MENU_EN); break;
            case 2: text = Pick_CN_EN(S_WIFI_SETUP_CN, S_WIFI_SETUP_EN); break;
            case 3: text = Pick_CN_EN(S_SETTINGS_MENU_CN, S_SETTINGS_MENU_EN); break;
            case 4: text = Pick_CN_EN(S_FAULT_CLEAR_CN, S_FAULT_CLEAR_EN);
                    enabled = is_fault ? 1 : 0;
                    break;
            default: text = ""; break;
        }
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    Draw_Cursor(2 + s_menu_cursor);

    Erase_Line(7);

    s_last_is_running    = is_running;
    s_last_is_fault_menu = is_fault;
}

/* ── MAIN_MENU cursor move: erase old ★ + draw new ★ ── */
static void Main_Menu_Cursor_Update(uint8_t old_cursor)
{
    Erase_Cursor(2 + old_cursor);
    Draw_Cursor(2 + s_menu_cursor);
}

/* ── MAIN_MENU 200ms dynamic: only redraw if PWM/fault state changed ── */
static void Main_Menu_Dynamic_Update(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }
    is_fault = (Sys_Core_Get_State() == SYS_STATE_FAULT);

    if (is_running != s_last_is_running) {
        const char* text = is_running
            ? Pick_CN_EN(S_STOP_PWM_CN, S_STOP_PWM_EN)
            : Pick_CN_EN(S_START_PWM_CN, S_START_PWM_EN);
        Draw_Menu_Text(2, 2, text, 1);
        if (s_menu_cursor == 0) Draw_Cursor(2);
        s_last_is_running = is_running;
    }

    if (is_fault != s_last_is_fault_menu) {
        const char* text = Pick_CN_EN(S_FAULT_CLEAR_CN, S_FAULT_CLEAR_EN);
        uint8_t enabled = is_fault ? 1 : 0;
        Draw_Menu_Text(5, 2, text, enabled);
        if (s_menu_cursor == 4) Draw_Cursor(5);
        s_last_is_fault_menu = is_fault;
    }
}

/* ================================================================
 *  Page draw: MONITOR_SUB_MENU (5 items, 4-row window) — covers all 8 rows
 * ================================================================ */
static const char* Sub_Item_Name(uint8_t idx)
{
    switch (idx) {
        case 0: return Pick_CN_EN(S_SUMMARY_CN, S_SUMMARY_EN);
        case 1: return Pick_CN_EN(S_MON_FREQ_CN, S_MON_FREQ_EN);
        case 2: return Pick_CN_EN(S_MON_VOLT_CN, S_MON_VOLT_EN);
        case 3: return Pick_CN_EN(S_MON_CURR_CN, S_MON_CURR_EN);
        case 4: return Pick_CN_EN(S_BACK_CN, S_BACK_EN);
        default: return "";
    }
}

static void Draw_Sub_Menu_Full(void)
{
    uint8_t visible_top = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t i, line;

    Draw_Header(Pick_CN_EN(S_MONITOR_CN, S_MONITOR_EN));       /* row 0 */
    Draw_Divider(1);              /* row 1 */

    for (line = 2; line <= 5; line++) {
        i = visible_top + (line - 2);
        if (i < 5) {
            char item_buf[22];
            snprintf(item_buf, sizeof(item_buf), "%d. %s", i + 1, Sub_Item_Name(i));
            Draw_Menu_Text(line, 2, item_buf, 1);
        } else {
            Erase_Line(line);
        }
    }

    Draw_Cursor(2 + (s_menu_cursor - visible_top));

    Erase_Line(6);
    Erase_Line(7);

    s_last_sub_visible = visible_top;
}

/* ── Sub-menu cursor ── */
static void Sub_Menu_Cursor_Update(uint8_t old_cursor)
{
    uint8_t old_visible = s_last_sub_visible;
    uint8_t new_visible = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t old_line = 2 + (old_cursor - old_visible);
    uint8_t new_line = 2 + (s_menu_cursor - new_visible);

    if (new_visible != old_visible) {
        /* Scroll happened — redraw all 4 visible lines */
        uint8_t i, line;
        Erase_Cursor(old_line);

        for (line = 2; line <= 5; line++) {
            i = new_visible + (line - 2);
            if (i < 5) {
                char item_buf[22];
                snprintf(item_buf, sizeof(item_buf), "%d. %s", i + 1, Sub_Item_Name(i));
                Draw_Menu_Text(line, 2, item_buf, 1);
            } else {
                Erase_Line(line);
            }
        }
        Draw_Cursor(new_line);
    } else {
        /* Simple cursor move within same window */
        Erase_Cursor(old_line);
        Draw_Cursor(new_line);
    }

    s_last_sub_visible = new_visible;
}

/* ═══════════════════════════════════════════════════════════════
 *  SWEEP page — covers all 8 rows
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Sweep_Full(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    Draw_Header(Pick_CN_EN(S_SWEEP_CN, S_SWEEP_EN));         /* row 0 */
    Draw_Divider(1);              /* row 1 */

    /* row 2: Frequency */
    snprintf(buf, sizeof(buf), "%sF:%3lu.%1lukHz", Pick_CN_EN(S_FREQ_CN, S_FREQ_EN),
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, Uc_Value(), Uc_Bg());
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* row 3: Progress bar area */
    {
        uint32_t progress;
        if (is_stopped) {
            progress = 0;
        } else {
            int32_t delta = (int32_t)SOFTSTART_START_FREQ_HZ - (int32_t)f;
            if (delta <= 0) {
                progress = 0;
            } else {
                progress = (uint32_t)(delta * 10 / ((int32_t)SOFTSTART_START_FREQ_HZ - (int32_t)SOFTSTART_TARGET_FREQ_HZ));
                if (progress > 10) progress = 10;
            }
        }
        Tft_Driver_Erase_Pixel_Area(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8);
        if (!is_stopped) {
            Ui_Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                           14 * TFT_FONT_WIDTH, 8,
                           (float)progress, 0.0f, 10.0f, Uc_Bg());
            snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
            if (buf[0]) Tft_Driver_Show_String(3, 8, buf, Uc_Text(), Uc_Bg());
        } else {
            Tft_Driver_Show_CN_String(3, 5, Pick_CN_EN(S_PAUSE_CN, S_PAUSE_EN), Uc_Alarm(), Uc_Bg());
        }
    }

    /* row 4: Voltage */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, Uc_Data(), Uc_Bg());
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* row 5: Current */
    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, Uc_Data(), Uc_Bg());
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    Erase_Line(6);
    Erase_Line(7);

    s_last_sweep_stopped = is_stopped;
}

/* ── SWEEP 200ms ── */
static void Sweep_Dynamic_Update(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    /* Frequency */
    snprintf(buf, sizeof(buf), "%sF:%3lu.%1lukHz", Pick_CN_EN(S_FREQ_CN, S_FREQ_EN),
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Erase_Line(2);
        Tft_Driver_Show_CN_String(2, 0, buf, Uc_Value(), Uc_Bg());
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Progress bar — V4.5.2: 变更检测防闪烁, 仅在进度变化时重绘 */
    {
        static uint32_t s_last_progress = 0xFFFFFFFFU;
        static uint8_t  s_last_stopped  = 0xFF;
        uint32_t progress;
        uint8_t  draw = 0;

        if (!is_stopped) {
            progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                     / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
            if (progress > 10) progress = 10;
            if (progress != s_last_progress || s_last_stopped != 0) {
                draw = 1;
                s_last_progress = progress;
                s_last_stopped  = 0;
            }
        } else {
            if (s_last_stopped != 1) {
                draw = 1;
                s_last_stopped = 1;
            }
        }

        if (draw) {
            Tft_Driver_Erase_Pixel_Area(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8);
            if (!is_stopped) {
                Ui_Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                               14 * TFT_FONT_WIDTH, 8,
                               (float)progress, 0.0f, 10.0f, Uc_Bg());
                snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
                if (buf[0]) Tft_Driver_Show_String(3, 8, buf, Uc_Text(), Uc_Bg());
            } else {
                Tft_Driver_Show_CN_String(3, 5, Pick_CN_EN(S_PAUSE_CN, S_PAUSE_EN), Uc_Alarm(), Uc_Bg());
            }
        }
    }

    /* Voltage */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Erase_Line(4);
        Tft_Driver_Show_CN_String(4, 0, buf, Uc_Data(), Uc_Bg());
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, Adc_Driver_Get_Current());
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Erase_Line(5);
        Tft_Driver_Show_CN_String(5, 0, buf, Uc_Data(), Uc_Bg());
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* Bottom hint */
    if (is_stopped != s_last_sweep_stopped) {
        Erase_Line(7);
        s_last_sweep_stopped = is_stopped;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  MONITOR_SUMMARY (line 2=F, 3=V, 4=I) — covers all 8 rows
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Summary_Full(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    Draw_Header(Pick_CN_EN(S_SUMMARY_CN, S_SUMMARY_EN));       /* row 0 */
    Draw_Divider(1);              /* row 1 */

    /* row 2: Freq */
    if (is_running) { Fmt_F(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), "%sF:0.0kHz", Pick_CN_EN(S_FREQ_CN, S_FREQ_EN)); }
    Tft_Driver_Show_CN_String(2, Center(buf), buf, Uc_Value(), Uc_Bg());
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* row 3: Voltage */
    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, Center(buf), buf, Uc_Value(), Uc_Bg());
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* row 4: Current */
    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, Center(buf), buf, Uc_Value(), Uc_Bg());
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    /* row 5: blank */
    Erase_Line(5);

    Erase_Line(6);
    Erase_Line(7);

    s_last_is_running = is_running;
}

/* ── Summary 200ms ── */
static void Summary_Dynamic_Update(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    /* Frequency */
    if (is_running) { Fmt_F(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), "%sF:---.-kHz", Pick_CN_EN(S_FREQ_CN, S_FREQ_EN)); }
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Erase_Line(2);
        Tft_Driver_Show_CN_String(2, Center(buf), buf, Uc_Value(), Uc_Bg());
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Voltage */
    Fmt_V(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Erase_Line(3);
        Tft_Driver_Show_CN_String(3, Center(buf), buf, Uc_Value(), Uc_Bg());
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Erase_Line(4);
        Tft_Driver_Show_CN_String(4, Center(buf), buf, Uc_Value(), Uc_Bg());
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* Bottom hint */
    if (is_running != s_last_is_running) {
        Erase_Line(7);
        s_last_is_running = is_running;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Ring Gauge Engine — sin table + polar coords + thick line + hub
 * ═══════════════════════════════════════════════════════════════ */
static const int16_t GAUGE_SIN[] = {
      0,   175,   349,   523,   698,   872,  1045,  1219,  1392,  1564,
   1736,  1908,  2079,  2250,  2419,  2588,  2756,  2924,  3090,  3256,
   3420,  3584,  3746,  3907,  4067,  4226,  4384,  4540,  4695,  4848,
   5000,  5150,  5299,  5446,  5592,  5736,  5878,  6018,  6157,  6293,
   6428,  6561,  6691,  6820,  6947,  7071,  7193,  7314,  7431,  7547,
   7660,  7771,  7880,  7986,  8090,  8192,  8290,  8387,  8480,  8572,
   8660,  8746,  8829,  8910,  8988,  9063,  9135,  9205,  9272,  9336,
   9397,  9455,  9511,  9563,  9613,  9659,  9703,  9744,  9781,  9816,
   9848,  9877,  9903,  9925,  9945,  9962,  9976,  9986,  9994,  9998,
  10000,  9998,  9994,  9986,  9976,  9962,  9945,  9925,  9903,  9877,
   9848,  9816,  9781,  9744,  9703,  9659,  9613,  9563,  9511,  9455,
   9397,  9336,  9272,  9205,  9135,  9063,  8988,  8910,  8829,  8746,
   8660,  8572,  8480,  8387,  8290,  8192,  8090,  7986,  7880,  7771,
   7660,  7547,  7431,  7314,  7193,  7071,  6947,  6820,  6691,  6561,
   6428,  6293,  6157,  6018,  5878,  5736,  5592,  5446,  5299,  5150,
   5000,  4848,  4695,  4540,  4406,  4270,  4133,  3995,  3856,  3716,
   3575,  3433,  3290,  3146,  3001,  2856,  2709,  2562,  2414,  2266,
   2117,  1968,  1818,  1668,  1518,  1367,  1217,  1066,   915,   764,
    613,   462,   311,   160,     0
};

typedef struct {
    float    range_min, range_max;
    float    big_step, mid_step, fine_step;
    float    red_start;
    char     label;        /* 'V' 'C' 'F' */
} GaugeConfig;

/* ── 电压表盘: 0~50V (共50个小格, 每格 1V) ── */
/* 大刻度: 0, 10, 20, 30, 40, 50 */
static const GaugeConfig GAUGE_V = {0.0f,  50.0f, 10.0f, 5.0f, 1.0f, 42.0f, 'V'};

/* ── 电流表盘: 0~2A (共20个小格, 每格 0.1A) ── */
/* 大刻度: 0.0, 0.5, 1.0, 1.5, 2.0 */
static const GaugeConfig GAUGE_C = {0.0f,   2.0f,  0.5f, 0.25f, 0.1f,  1.8f, 'C'};

/* ── 频率表盘: 90~150kHz (共60个小格, 每格 1kHz) ── */
/* 大刻度: 90, 100, 110, 120, 130, 140, 150 */
static const GaugeConfig GAUGE_F = {90.0f, 150.0f, 10.0f, 5.0f, 1.0f, 140.0f, 'F'};

static float s_last_val_v = -1.0f, s_last_val_c = -1.0f, s_last_val_f = -1.0f;
static const char* s_last_gauge_label = NULL;  /* cross-gauge label cache, reset on page change */

/* ── polar: angle 0°=left, 90°=top, 180°=right, center (G_CX, G_CY) ── */
#define G_CX   80
#define G_CY   84   /* lowered 18px from original 66 for better gauge proportion */
static void Gauge_Polar(uint8_t a, uint16_t r, int16_t *px, int16_t *py)
{
    int16_t s, c;
    if (a > 180) a = 180;
    s = GAUGE_SIN[a];                             /* sin(a) */
    if (a <= 90) c = GAUGE_SIN[90 - a];          /* cos = sin(90-a) */
    else        c = -GAUGE_SIN[a - 90];           /* cos = -sin(a-90) */
    /* 0°=left→180°=right: invert X for left=0° */
    *px = (int16_t)(G_CX - (int32_t)r * c / 10000);
    *py = (int16_t)(G_CY - (int32_t)r * s / 10000);
}

/* ── Bresenham 1px line, pixel pushed via DMA ── */
static void Bres_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);
    while (1) {
        Tft_Driver_Fill_Rect((uint16_t)x0, (uint16_t)y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        { int16_t e2 = (int16_t)(err * 2);
          if (e2 > -dy) { err = (int16_t)(err - dy); x0 = (int16_t)(x0 + sx); }
          if (e2 <  dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); } }
    }
}

/* ── WIFI icon + MQTT cloud (top-right, for all pages) ── */
static void Draw_TopRight_Icons(void)
{
    #define WX 128
    #define MX 144
    uint8_t  cs = App_Network_Get_Connect_Status(), icon_frame;
    uint8_t  wifi_frame = 0xFFU;
    uint8_t  mqtt_frame = 0xFFU;
    uint8_t  rssi_frame = 0xFFU;
    uint8_t  connecting = App_Network_Is_Connecting();
    uint8_t  offline = App_Network_Is_Offline();
    uint8_t  ready = Esp8266_Driver_Is_Ready();
    static const uint16_t blue_grad[6] = {0x0018,0x001B,0x001F,0x07FF,0x07BF,0x07FF};
    static const uint16_t rainbow[6] = {0xF800,0xFD20,0xFFE0,0x07E0,0x07FF,0x001F};

    /* Bug 2: 先强制清除 16x16 像素槽再绘制新帧, 防止动态图标残影叠加 */
    Tft_Driver_Fill_Rect(WX, 0, 16, 16, Uc_Bg());
    Tft_Driver_Fill_Rect(MX, 0, 16, 16, Uc_Bg());

    /* ── WIFI icon (x=128) ── */
    if ((s_no_wifi_mode == 0U) && (offline == 0U) &&
        ((connecting != 0U) || (ready == 0U))) {
        wifi_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150U) % 6U;
    }
    if (connecting != 0U) {
        mqtt_frame = (uint8_t)(Sys_Timer_Get_Tick() / 200U) % 6U;
    }

    if (s_no_wifi_mode || offline) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_OFF, 0, Uc_Alarm(), Uc_Bg());
    } else if (!ready) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_CONNECT_ANIM, wifi_frame, blue_grad[wifi_frame], Uc_Bg());
    } else if (cs == APP_NETWORK_CONN_ONLINE) {
        int8_t r = App_Network_Get_RSSI();
        if (r >= -50) icon_frame=3; else if (r >= -60) icon_frame=2; else if (r >= -70) icon_frame=1; else icon_frame=0;
        rssi_frame = icon_frame;
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_SIGNAL, icon_frame, Uc_Ok(), Uc_Bg());
    } else if (connecting) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_CONNECT_ANIM, wifi_frame, blue_grad[wifi_frame], Uc_Bg());
    } else {  /* IDLE */
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_REMOVE, 0, Uc_Alarm(), Uc_Bg());
    }

    /* ── MQTT cloud (x=144) ── */
    if (cs == APP_NETWORK_CONN_ONLINE) {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_YES, 0, Uc_Ok(), Uc_Bg());
    } else if (connecting) {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_ANIM, mqtt_frame, rainbow[mqtt_frame], Uc_Bg());
    } else {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_NO, 0, Uc_Alarm(), Uc_Bg());
    }
    if (!Tft_Driver_Is_Draw_Blocked()) {
        s_icon_cache_valid = 1U;
        s_last_icon_cs = cs;
        s_last_icon_mode = s_no_wifi_mode;
        s_last_wifi_frame = wifi_frame;
        s_last_mqtt_frame = mqtt_frame;
        s_last_rssi_frame = rssi_frame;
        s_last_icon_page = (uint8_t)s_page;
        s_last_icon_bg = Uc_Bg();
        s_last_icon_fg = Uc_Text();
    }
    #undef WX
    #undef MX
}

/* ── FULL redraw: energy bar (progressive arc) + info cabin + header/bottom ── */
static void Draw_Gauge_Full(const GaugeConfig* cfg, float val)
{
    #define R_TICK  56   /* energy bar outer radius */
    #define R_BIG   50   /* main tick inner (6px length) */
    #define R_FINE  53   /* fine tick inner (3px length) */
    #define CPS(x)  ((uint8_t)(x))
    uint16_t a, na;
    uint16_t slot_color = 0x18C3;  /* dark grey background slot */
    float v;
    char buf[32];

    if (val < cfg->range_min) val = cfg->range_min;
    if (val > cfg->range_max) val = cfg->range_max;

    /* ── 1. Compute needle angle (0=left, 180=right) ── */
    na = (uint16_t)((val - cfg->range_min) /
          (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    if (na > 180) na = 180;

    /* ── 3. Draw all tick lines as 1px Bresenham ──
     *     a <= na  → lit (red in red-zone, else white/grey)
     *     a >  na  → dark grey slot (background track)
     * ─────────────────────────────────────────────────── */
    for (v = cfg->range_min; v <= cfg->range_max + cfg->fine_step*0.1f;
         v += cfg->fine_step) {
        uint8_t is_red = (v >= cfg->red_start);
        uint8_t is_big = 0;
        uint16_t ir, color;
        {
            float d = v - (float)((int)((double)v / (double)cfg->big_step + 0.5))
                       * cfg->big_step;
            if (d < 0.0f) d = -d;
            if (d < cfg->fine_step * 0.2f) is_big = 1;
        }
        ir = is_big ? R_BIG : R_FINE;

        a = (uint16_t)((v - cfg->range_min) /
              (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;

        if (a <= na) {
            if (is_big || is_red)
                color = is_red ? Uc_Alarm() : Uc_Text();
            else
                color = Uc_Dim();
        } else {
            color = slot_color;
        }

        {
            int16_t xo, yo, xi, yi;
            Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
            Gauge_Polar(CPS(a), ir, &xi, &yi);
            Bres_Line(xi, yi, xo, yo, color);
        }
    }

    /* ── 4. Micro labels (5x10 font) at big-step positions ── */
    for (v = cfg->range_min; v <= cfg->range_max + cfg->big_step*0.1f;
         v += cfg->big_step) {
        a = (uint16_t)((v - cfg->range_min) /
              (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;
        {
            int16_t x, y;
            char nb[6];
            uint8_t len;
            uint16_t w;
            int32_t s, c;
            int16_t draw_x, draw_y;
            uint16_t color = (v >= cfg->red_start) ? Uc_Alarm() : Uc_Text();

            s = GAUGE_SIN[a];
            c = (a <= 90) ? GAUGE_SIN[90 - a] : -GAUGE_SIN[a - 90];

            Gauge_Polar(CPS(a), R_TICK, &x, &y);

            if (v == (float)((int)v))
                snprintf(nb, sizeof(nb), "%d", (int)v);
            else if (cfg->big_step < 1.0f)
                snprintf(nb, sizeof(nb), "%.1f", (double)v);
            else
                snprintf(nb, sizeof(nb), "%d", (int)v);

            len = (uint8_t)strlen(nb);
            w = len * 7 - 2;

            /* edge-repulsion: push outward from tick outer tip */
            draw_x = x - (int16_t)(w / 2) - (int32_t)(2 + w / 2) * c / 10000;
            draw_y = y - 5 - (int32_t)(2 + 5) * s / 10000;

            Tft_Driver_Show_5x10_String_Pixel((uint16_t)draw_x, (uint16_t)draw_y,
                                              nb, color, Uc_Bg());
        }
    }

    /* ── 5. Info cabin: row 4 status → row 5 value → row 6 label ── */
    {
        /* -- Row 4 (Y=64): status stamp (OK/WRN/HI or SWP/DON/IDL), sits inside arc ── */
        {
            const char* status_text;
            uint16_t status_color;
            if (cfg->label == 'F') {
                Inverter_Control_Soft_Start_State st = Inverter_Control_Soft_Start_Get_State();
                if      (st == INVERTER_CONTROL_SS_STATE_SWEEP)
                    { status_text = "SWP"; status_color = Uc_Value(); }
                else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                    { status_text = "DON"; status_color = Uc_Ok(); }
                else
                    { status_text = "IDL"; status_color = Uc_Dim(); }
            } else {
                float thr_warn = (cfg->label == 'V') ? 36.0f : 1.2f;
                if (val >= cfg->red_start)
                    { status_text = "HI"; status_color = Uc_Alarm(); }
                else if (val >= thr_warn)
                    { status_text = "WRN"; status_color = Uc_Value(); }
                else
                    { status_text = "OK"; status_color = Uc_Ok(); }
            }
            Tft_Driver_Show_CN_String(4, Center(status_text), status_text,
                                      status_color, Uc_Bg());
            strncpy(s_gauge_status_buf, status_text, sizeof(s_gauge_status_buf));
            s_gauge_status_buf[sizeof(s_gauge_status_buf) - 1] = '\0';
        }

        /* -- Row 5 (Y=80): numeric value, format by table ── */
        {
            uint8_t is_running_f = (cfg->label == 'F')
                && (Inverter_Control_Soft_Start_Get_State()
                     == INVERTER_CONTROL_SS_STATE_DONE
                 || Inverter_Control_Soft_Start_Get_State()
                     == INVERTER_CONTROL_SS_STATE_SWEEP);
            uint16_t num_color = (cfg->label == 'F' && !is_running_f)
                ? Uc_Dim() : TFT_COLOR_YELLOW;

            if (cfg->label == 'C')
                snprintf(buf, sizeof(buf), "%.3f", (double)val);
            else if (cfg->label == 'F' && !is_running_f)
                snprintf(buf, sizeof(buf), "0");
            else
                snprintf(buf, sizeof(buf), "%.2f", (double)val);
            Tft_Driver_Show_CN_String(5, Center(buf), buf, num_color, Uc_Bg());
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }

        /* -- Row 6 (Y=96): metric label with unit suffix, center-aligned ── */
        if (cfg->label == 'F')
            Tft_Driver_Show_CN_String(6, Center(Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN)), Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN), Uc_Value(), Uc_Bg());
        else if (cfg->label == 'V')
            Tft_Driver_Show_CN_String(6, Center(Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN)), Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN), Uc_Value(), Uc_Bg());
        else
            Tft_Driver_Show_CN_String(6, Center(Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN)), Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN), Uc_Value(), Uc_Bg());
    }

    /* ── 6. Footer: top-right icons only (gauge pages are full-screen, no divider/bottom bar) ── */
    Draw_TopRight_Icons();

    #undef R_TICK
    #undef R_BIG
    #undef R_FINE
    #undef CPS
}

/* ── 200ms incremental: energy bar slot diff + info cabin value/status update ── */
static void Gauge_Dynamic_Update(const GaugeConfig* cfg, float val, float old_val)
{
    #define R_TICK  56
    #define R_BIG   50
    #define R_FINE  53
    #define CPS(x)  ((uint8_t)(x))
    uint16_t oa, na;
    uint16_t slot_color = 0x18C3;
    char buf[32];

    if (val < cfg->range_min) val = cfg->range_min;
    if (val > cfg->range_max) val = cfg->range_max;
    if (old_val < cfg->range_min) old_val = cfg->range_min;
    if (old_val > cfg->range_max) old_val = cfg->range_max;

    oa = (uint16_t)((old_val - cfg->range_min) /
           (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    na = (uint16_t)((val - cfg->range_min) /
           (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    if (oa > 180) oa = 180;
    if (na > 180) na = 180;

    /* ── 1. Energy-bar slot differential update (1px Bres_Line only) ── */
    if (oa != na) {
        float v;
        uint16_t a;
        if (na > oa) {
            /* Light up slots from oa to na */
            for (v = cfg->range_min; v <= cfg->range_max + cfg->fine_step*0.1f;
                 v += cfg->fine_step) {
                a = (uint16_t)((v - cfg->range_min) /
                      (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
                if (a > 180) a = 180;
                if (a > oa && a <= na) {
                    uint8_t is_red = (v >= cfg->red_start);
                    uint8_t is_big = 0;
                    uint16_t ir, color;
                    {
                        float d = v - (float)((int)((double)v / (double)cfg->big_step + 0.5))
                                   * cfg->big_step;
                        if (d < 0.0f) d = -d;
                        if (d < cfg->fine_step * 0.2f) is_big = 1;
                    }
                    ir = is_big ? R_BIG : R_FINE;
                    if (is_big || is_red)
                        color = is_red ? Uc_Alarm() : Uc_Text();
                    else
                        color = Uc_Dim();
                    {
                        int16_t xo, yo, xi, yi;
                        Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
                        Gauge_Polar(CPS(a), ir, &xi, &yi);
                        Bres_Line(xi, yi, xo, yo, color);
                    }
                }
            }
        } else {
            /* Fade back to dark grey slots from na to oa */
            for (v = cfg->range_min; v <= cfg->range_max + cfg->fine_step*0.1f;
                 v += cfg->fine_step) {
                a = (uint16_t)((v - cfg->range_min) /
                      (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
                if (a > 180) a = 180;
                if (a > na && a <= oa) {
                    uint8_t is_big = 0;
                    uint16_t ir;
                    {
                        float d = v - (float)((int)((double)v / (double)cfg->big_step + 0.5))
                                   * cfg->big_step;
                        if (d < 0.0f) d = -d;
                        if (d < cfg->fine_step * 0.2f) is_big = 1;
                    }
                    ir = is_big ? R_BIG : R_FINE;
                    {
                        int16_t xo, yo, xi, yi;
                        Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
                        Gauge_Polar(CPS(a), ir, &xi, &yi);
                        Bres_Line(xi, yi, xo, yo, slot_color);
                    }
                }
            }
        }
    }

    /* ── 2. Info cabin diff: row 4 status → row 5 value → row 6 label ── */
    /* -- Row 4 (Y=64): status stamp, inside arc -- */
    {
        const char* status_text;
        uint16_t status_color;
        if (cfg->label == 'F') {
            Inverter_Control_Soft_Start_State st = Inverter_Control_Soft_Start_Get_State();
            if      (st == INVERTER_CONTROL_SS_STATE_SWEEP)
                { status_text = "SWP"; status_color = Uc_Value(); }
            else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                { status_text = "DON"; status_color = Uc_Ok(); }
            else
                { status_text = "IDL"; status_color = Uc_Dim(); }
        } else {
            float thr_warn = (cfg->label == 'V') ? 36.0f : 1.2f;
            if (val >= cfg->red_start)
                { status_text = "HI"; status_color = Uc_Alarm(); }
            else if (val >= thr_warn)
                { status_text = "WRN"; status_color = Uc_Value(); }
            else
                { status_text = "OK"; status_color = Uc_Ok(); }
        }

        if (strncmp(status_text, s_gauge_status_buf, sizeof(s_gauge_status_buf)) != 0) {
            Tft_Driver_Erase_Pixel_Area(24, 64, 112, 16);
            Tft_Driver_Show_CN_String(4, Center(status_text), status_text,
                                      status_color, Uc_Bg());
            strncpy(s_gauge_status_buf, status_text, sizeof(s_gauge_status_buf));
            s_gauge_status_buf[sizeof(s_gauge_status_buf) - 1] = '\0';
        }
    }

    /* -- Row 5 (Y=80): numeric value (3 decimal for current, 2 otherwise) -- */
    {
        uint8_t is_running_f = (cfg->label == 'F')
            && (Inverter_Control_Soft_Start_Get_State()
                 == INVERTER_CONTROL_SS_STATE_DONE
             || Inverter_Control_Soft_Start_Get_State()
                 == INVERTER_CONTROL_SS_STATE_SWEEP);
        uint16_t num_color = (cfg->label == 'F' && !is_running_f)
            ? Uc_Dim() : TFT_COLOR_YELLOW;

        if (cfg->label == 'C')
            snprintf(buf, sizeof(buf), "%.3f", (double)val);
        else if (cfg->label == 'F' && !is_running_f)
            snprintf(buf, sizeof(buf), "0");
        else
            snprintf(buf, sizeof(buf), "%.2f", (double)val);
        if (strncmp(buf, s_gauge_val_str, sizeof(s_gauge_val_str)) != 0) {
            Tft_Driver_Erase_Pixel_Area(24, 80, 112, 16);
            Tft_Driver_Show_CN_String(5, Center(buf), buf, num_color, Uc_Bg());
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }
    }

    /* -- Row 6 (Y=96): metric label with unit suffix -- */
    {
        const char* label_text;
        if (cfg->label == 'F')      label_text = Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN);
        else if (cfg->label == 'V') label_text = Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN);
        else                        label_text = Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN);
        if (label_text != s_last_gauge_label) {
            s_last_gauge_label = label_text;
            Tft_Driver_Erase_Pixel_Area(24, 96, 112, 16);
            Tft_Driver_Show_CN_String(6, Center(label_text), label_text, Uc_Value(), Uc_Bg());
        }
    }

    #undef R_TICK
    #undef R_BIG
    #undef R_FINE
    #undef CPS
}

/* ── 6 thin wrappers for the old function-pointer call sites ── */
static void Draw_Freq_Full(void) {
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_DONE
                       || Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_SWEEP);
    Update_EMA();
    /* PWM 未运行时强制 val=0, 防止 EMA 取到默认 90kHz 导致能量条非零 */
    float display_val = is_running ? s_ema_f : 0.0f;
    Draw_Gauge_Full(&GAUGE_F, display_val);
    /* PWM 停止时频率显示 0 (电压电流继续实时监测) */
    if (!is_running) {
        Tft_Driver_Erase_Pixel_Area(24, 80, 112, 16);
        Tft_Driver_Show_CN_String(5, Center("0kHz"), "0kHz", Uc_Dim(), Uc_Bg());
        strncpy(s_gauge_val_str, "0kHz", sizeof(s_gauge_val_str));
    }
    s_last_val_f = s_ema_f;
}
static void Freq_Dynamic_Update(void) {
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_DONE
                       || Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_SWEEP);
    float old = s_last_val_f;
    Update_EMA();
    if (is_running) {
        Gauge_Dynamic_Update(&GAUGE_F, s_ema_f, old);
        s_last_val_f = s_ema_f;
    } else {
        /* PWM 停止时能量条回零 + 数值灰0 */
        Gauge_Dynamic_Update(&GAUGE_F, 0.0f, old);
        s_last_val_f = 0.0f;  /* Bug 3: must track actual displayed=0, not s_ema_f */
    }
}
static void Draw_Volt_Full(void) {
    Update_EMA(); Draw_Gauge_Full(&GAUGE_V, s_ema_v); s_last_val_v = s_ema_v;
}
static void Volt_Dynamic_Update(void) {
    float old = s_last_val_v; Update_EMA();
    Gauge_Dynamic_Update(&GAUGE_V, s_ema_v, old); s_last_val_v = s_ema_v;
}
static void Draw_Curr_Full(void) {
    Update_EMA(); Draw_Gauge_Full(&GAUGE_C, s_ema_i); s_last_val_c = s_ema_i;
}
static void Curr_Dynamic_Update(void) {
    float old = s_last_val_c; Update_EMA();
    Gauge_Dynamic_Update(&GAUGE_C, s_ema_i, old); s_last_val_c = s_ema_i;
}

/* ═══════════════════════════════════════════════════════════════
 *  WIFI_SETUP — covers all 8 rows
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_WiFi_Full(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    const char* status_text;
    const char* hint_text;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = Pick_CN_EN(S_WIFI_ONLINE_CN, S_WIFI_ONLINE_EN);
    else if (App_Network_Is_Connecting())
        status_text = Pick_CN_EN(S_WIFI_CONN_CN, S_WIFI_CONN_EN);
    else if (App_Network_Is_Offline())
        status_text = Pick_CN_EN(S_WIFI_OFFLINE_CN, S_WIFI_OFFLINE_EN);
    else  /* IDLE */
        status_text = Pick_CN_EN(S_WIFI_IDLE_CN, S_WIFI_IDLE_EN);

    if (App_Network_Is_Offline()) {
        hint_text = Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
    } else {
        hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? Pick_CN_EN(S_DISCONNECT_CN, S_DISCONNECT_EN) : Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
    }

    Draw_Header(Pick_CN_EN(S_LAUNCH_CN, S_LAUNCH_EN));         /* row 0 */
    Draw_Divider(1);               /* row 1 */

    /* row 2: Status */
    {
        char buf[42];
        snprintf(buf, sizeof(buf), "%s: %s", Pick_CN_EN(S_WIFI_TITLE_CN, S_WIFI_TITLE_EN), status_text);
        Tft_Driver_Show_CN_String(2, 0, buf, Uc_Text(), Uc_Bg());
        strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
        s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
    }

    /* row 3~4: blank */
    Erase_Line(3);
    Erase_Line(4);

    /* row 5: action hint */
    Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, Uc_Text(), Uc_Bg());
    /* row 6: long-press clear hint */
    Tft_Driver_Show_CN_String(6, Right(Pick_CN_EN(S_LONG_CLEAR_CN, S_LONG_CLEAR_EN)), Pick_CN_EN(S_LONG_CLEAR_CN, S_LONG_CLEAR_EN), Uc_Alarm(), Uc_Bg());
    Erase_Line(7);

    s_last_wifi_cs = cs;
}

static void WiFi_Dynamic_Update(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    uint8_t retry = App_Network_Get_Retry_Count();
    const char* status_text;
    uint8_t need_hint_update = 0;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = Pick_CN_EN(S_WIFI_ONLINE_CN, S_WIFI_ONLINE_EN);
    else if (App_Network_Is_Connecting())
        status_text = Pick_CN_EN(S_WIFI_CONN_CN, S_WIFI_CONN_EN);
    else if (App_Network_Is_Offline())
        status_text = Pick_CN_EN(S_WIFI_OFFLINE_CN, S_WIFI_OFFLINE_EN);
    else  /* IDLE */
        status_text = Pick_CN_EN(S_WIFI_IDLE_CN, S_WIFI_IDLE_EN);

    if (cs != s_last_wifi_cs) {
        char buf[42];
        snprintf(buf, sizeof(buf), "%s: %s", Pick_CN_EN(S_WIFI_TITLE_CN, S_WIFI_TITLE_EN), status_text);
        if (strncmp(buf, s_last_status_buf, sizeof(s_last_status_buf)) != 0) {
            Erase_Line(2);
            Tft_Driver_Show_CN_String(2, 0, buf, Uc_Text(), Uc_Bg());
            strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
            s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
        }
        need_hint_update = 1;
        s_last_wifi_cs = cs;
    }

    /* V4.5.2: 仅在提示文本变化时擦除重绘, 消除 200ms 闪烁 */
    if (need_hint_update) {
        static char s_last_hint[32] = "";
        const char* new_hint;
        if (App_Network_Is_Offline()) {
            new_hint = Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
        } else {
            new_hint = (cs == APP_NETWORK_CONN_ONLINE) ? Pick_CN_EN(S_DISCONNECT_CN, S_DISCONNECT_EN) : Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
        }
        if (strncmp(s_last_hint, new_hint, sizeof(s_last_hint)) != 0) {
            Erase_Line(3);
            Tft_Driver_Show_CN_String(5, Right(new_hint), new_hint, Uc_Text(), Uc_Bg());
            strncpy(s_last_hint, new_hint, sizeof(s_last_hint));
            s_last_hint[sizeof(s_last_hint) - 1] = '\0';
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  FAULT — fully static, covers all 8 rows
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Fault_Full(void)
{
    const char* fault_cn;
    const char* fault_en;
    Sys_Fault_Code fault_code;

    fault_cn = S_UNKNOWN_FAULT_CN;
    fault_en = S_UNKNOWN_FAULT_EN;
    fault_code = Sys_Core_Get_Fault();
    switch (fault_code) {
        case SYS_FAULT_OVERCURRENT:
            fault_cn = S_OVERCUR_CN;
            fault_en = S_OVERCUR_EN;
            break;
        case SYS_FAULT_ADC_STALE:
            fault_cn = S_ADC_STALE_CN;
            fault_en = S_ADC_STALE_EN;
            break;
        case SYS_FAULT_CONTROL_INVARIANT:
            fault_cn = S_CONTROL_ERR_CN;
            fault_en = S_CONTROL_ERR_EN;
            break;
        case SYS_FAULT_NONE:
        default:
            break;
    }

    Draw_Header(Pick_CN_EN(S_FAULT_TITLE_CN, S_FAULT_TITLE_EN));     /* row 0 */
    Draw_Divider(1);                /* row 1 */

    Tft_Driver_Show_CN_String(2, Center(Pick_CN_EN(fault_cn, fault_en)),
        Pick_CN_EN(fault_cn, fault_en), Uc_Alarm(), Uc_Bg());      /* row 2 */
    Tft_Driver_Show_CN_String(3, Center(Pick_CN_EN(S_PWM_OFF_CN, S_PWM_OFF_EN)),
        Pick_CN_EN(S_PWM_OFF_CN, S_PWM_OFF_EN), Uc_Text(), Uc_Bg());        /* row 3 */

    Erase_Line(4);                  /* row 4: blank spacer */

    Tft_Driver_Show_CN_String(5, Center(Pick_CN_EN(S_RESET_HINT_CN, S_RESET_HINT_EN)),
        Pick_CN_EN(S_RESET_HINT_CN, S_RESET_HINT_EN), Uc_Value(), Uc_Bg());    /* row 5 */

    Erase_Line(6);
    Erase_Line(7);
}

/* ================================================================
 *  LED Update
 * ================================================================ */
static void Update_Leds(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI || cs == APP_NETWORK_CONN_MQTT)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else if (App_Network_Is_Offline())
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
}

/* ================================================================
 *  Key Dispatch
 * ================================================================ */
static void Handle_Keys_by_Page(Ui_Page page,
                                Key_Driver_Event k1, Key_Driver_Event k2,
                                Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t is_running = 0;
    Sys_State sys_state;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }
    sys_state = Sys_Core_Get_State();

    if (page == UI_PAGE_WIFI_SETUP &&
        k4 == KEY_DRIVER_EVENT_LONG_PRESS) {
        if (Sys_Core_Get_State() == SYS_STATE_IDLE &&
            Pwm_Driver_Is_Enabled() == 0U) {
            if (Esp8266_Driver_Send_String("CMD:CLEAR\n") !=
                ESP8266_DRIVER_TX_OK) {
                return;
            }
        }
        return;
    }

    /* UP (k2): 频率+/上移 */
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                /* V4.5.2: F_UP wraps 0->3 when not fault, 0->4 when fault */
                if (sys_state == SYS_STATE_FAULT) {
                    if (s_menu_cursor == 0) s_menu_cursor = 4;
                    else s_menu_cursor--;
                } else {
                    if (s_menu_cursor == 0) s_menu_cursor = 3;
                    else s_menu_cursor--;
                }
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
                if (is_running) {
                    if (!s_user_target_synced) {
                        s_user_target_hz = ((Pwm_Driver_Get_Frequency() + 500) / 1000) * 1000;
                        s_user_target_synced = 1;
                    }
                    s_user_target_hz += 1000;
                    if (s_user_target_hz > PWM_DRIVER_FREQ_MAX_HZ) s_user_target_hz = PWM_DRIVER_FREQ_MAX_HZ;
                    Inverter_Control_Freq_Ramp_Cancel();
                    Pwm_Driver_Set_Frequency(s_user_target_hz);
                }
                break;
            default: break;
        }
    }

    /* DOWN (k3): 频率-/下移 */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                /* V4.5.2: F_DOWN 到第5项(灰色)时如果非故障则回第1项 */
                if (sys_state == SYS_STATE_FAULT) {
                    if (s_menu_cursor >= 4) s_menu_cursor = 0;
                    else s_menu_cursor++;
                } else {
                    if (s_menu_cursor >= 3) s_menu_cursor = 0;
                    else s_menu_cursor++;
                }
                break;
            case UI_PAGE_MONITOR_SUB_MENU:
                if (s_menu_cursor >= 4) s_menu_cursor = 0;
                else s_menu_cursor++;
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
                if (is_running) {
                    if (!s_user_target_synced) {
                        s_user_target_hz = ((Pwm_Driver_Get_Frequency() + 500) / 1000) * 1000;
                        s_user_target_synced = 1;
                    }
                    if (s_user_target_hz >= PWM_DRIVER_FREQ_MIN_HZ + 1000) s_user_target_hz -= 1000;
                    Inverter_Control_Freq_Ramp_Cancel();
                    Pwm_Driver_Set_Frequency(s_user_target_hz);
                }
                break;
            default: break;
        }
    }

    /* CONFIRM (k4): 确定/启停 */
    if (k4 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                switch (s_menu_cursor) {
                    case 0:
                        if (is_running) {
                            (void)Sys_Core_Request_Stop();
                        } else {
                            if (Sys_Core_Request_Start() == SYS_CONTROL_RESULT_OK) {
                                s_page = UI_PAGE_SWEEP;
                                Reset_EMA();
                            }
                        }
                        break;
                    case 1:
                        s_page = UI_PAGE_MONITOR_SUB_MENU;
                        s_menu_cursor = 0;
                        break;
                    case 2:
                        s_page = UI_PAGE_WIFI_SETUP;
                        break;
                    case 3:
                        s_page = UI_PAGE_SETTING;
                        s_setting_cursor = 0;
                        break;
                    default: break;  /* item 4 — disabled unless FAULT */
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
                {
                    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
                    if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) {
                        (void)Sys_Core_Request_Stop();
                    } else if (ss == INVERTER_CONTROL_SS_STATE_IDLE) {
                        if (Sys_Core_Request_Start() == SYS_CONTROL_RESULT_OK) {
                            Reset_EMA();
                        }
                    }
                }
                break;

            case UI_PAGE_WIFI_SETUP: {
                uint8_t cs = App_Network_Get_Connect_Status();
                if (cs == APP_NETWORK_CONN_ONLINE || App_Network_Is_Connecting()) {
                    App_Network_Manual_Disconnect(); s_no_wifi_mode = 1;  /* 在线→主动离线 */
                } else if (cs == APP_NETWORK_CONN_OFFLINE_ACTIVE) {
                    s_no_wifi_mode = 0; App_Network_Manual_Connect();    /* 主动离线→重连 */
                } else if (cs == APP_NETWORK_CONN_OFFLINE_PASSIVE) {
                    s_no_wifi_mode = 0; App_Network_Resume_From_Offline(); /* 被动离线→嗅探恢复 */
                } else {
                    s_no_wifi_mode = 0; App_Network_Start_Connect();     /* IDLE→完整初始化 */
                }
                break;
            }

            case UI_PAGE_FAULT:
                if (Sys_Core_Reset_Fault() == SYS_CONTROL_RESULT_OK) {
                    s_page = UI_PAGE_MAIN_MENU;
                    s_menu_cursor = 0;
                    s_was_fault_state = 0;
                    Reset_EMA();
                }
                break;

            default: break;
        }
    }

    /* BACK (k1): double-click -> jump to MAIN MENU directly */
    if (k1 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_page = UI_PAGE_MAIN_MENU;
        s_menu_cursor = 0;
        s_setting_cursor = 0;
        /* fall through to skip CLICK processing — no return needed since
           DOUBLE_CLICK and CLICK are mutually exclusive per-key */
    }
    /* BACK (k1): single click — return to previous page.
       Settings 子页由 Handle_Settings_Keys 拦截, 但这里作为保底 */
    else if (k1 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                break;
            case UI_PAGE_MONITOR_SUB_MENU:
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
            case UI_PAGE_SETTING:
                s_page = UI_PAGE_MAIN_MENU;
                s_menu_cursor = 3;
                break;
            default: break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  V4.5.2 Settings Pages
 * ═══════════════════════════════════════════════════════════════ */

/* ── Settings Menu Item Text — V4.5.2: spacing replaces font ── */
static const char* Get_Menu_Setting_Text(uint8_t idx)
{
    switch (idx) {
        case 0: return Pick_CN_EN("1. \xe8\xaf\xad\xe8\xa8\x80", "1. Language");
        case 1: return Pick_CN_EN("2. \xe5\xad\x97\xe9\x97\xb4\xe8\xb7\x9d", "2. Spacing");
        case 2: return Pick_CN_EN("3. \xe5\x9b\xbe\xe6\xa0\x87", "3. Icons");
        case 3: return Pick_CN_EN("4. \xe9\xa2\x9c\xe8\x89\xb2", "4. Color");
        default: return "";
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  S1. SETTING Main Menu
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Setting_Full(void)
{
    uint8_t i;
    uint8_t enabled;
    const char* text;

    Draw_Header(Pick_CN_EN(S_SETTINGS_CN, S_SETTINGS_EN));
    Draw_Divider(1);

    /* Row 2-5: language, spacing, icons and color. */
    for (i = 0U; i < 4U; i++) {
        text = Get_Menu_Setting_Text(i);
        enabled = (i == 2U) ? Tft_Driver_Is_Font_Flash_Valid() : 1U;
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    Draw_Cursor(2 + s_setting_cursor);
}

static void Handle_Setting_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                                 Key_Driver_Event k3, Key_Driver_Event k4)
{
    /* BACK -> main menu + flush settings to Flash if dirty */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (s_settings_dirty) {
            App_Storage_Request_Save_Settings(s_language, 0U, 100U,
                                              s_letter_spacing, sc_preset,
                                              s_color_fg, s_color_bg);
            /* V4.5.2: spacing in flash is 0-3 choice, map to actual px */
            Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));
            s_settings_dirty = 0;
        }
        s_page = UI_PAGE_MAIN_MENU; s_menu_cursor = 3; s_page_drawn = 0; return;
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor == 0U) s_setting_cursor = 3U;
        else s_setting_cursor--;
    }
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor >= 3U) s_setting_cursor = 0U;
        else s_setting_cursor++;
    }
    if (k4 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor == 2 && !Tft_Driver_Is_Font_Flash_Valid()) return;  /* Icons requires Flash */
        switch (s_setting_cursor) {
            case 0: s_page = UI_PAGE_SETTING_LANG;
                    s_preview_choice = s_language;  /* init preview cursor from saved value */
                    break;
            case 1: s_page = UI_PAGE_SETTING_SPACING;
                    s_preview_choice = s_letter_spacing;
                    break;
            case 2: s_page = UI_PAGE_SETTING_ICONS; s_icon_page = 0; s_icon_cursor = 0; break;
            case 3: s_page = UI_PAGE_SETTING_COLOR;
                    s_preview_choice = sc_preset;  /* init preview cursor from saved value */
                    break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  S2. Language — preview-before-confirm
 *  UP/DOWN → preview cursor, bottom shows live example
 *  PAGE    → commit selection, ON → cancel (restore old value)
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Lang_Full(void)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();

    Draw_Header(Pick_CN_EN(S_TITLE_LANG_CN, S_TITLE_LANG_EN));

    Erase_Line(3);
    Tft_Driver_Show_CN_String(3, 3,
        Pick_CN_EN((s_preview_choice == 0) ? "* \xe4\xb8\xad\xe6\x96\x87" : "  \xe4\xb8\xad\xe6\x96\x87",
                   (s_preview_choice == 0) ? "* Chinese" : "  Chinese"),
        (s_preview_choice == 0 && flash_ok) ? Uc_Value() : Uc_Text(), Uc_Bg());
    Erase_Line(4);
    Tft_Driver_Show_CN_String(4, 3,
        Pick_CN_EN((s_preview_choice == 1) ? "* \xe8\x8b\xb1\xe6\x96\x87" : "  \xe8\x8b\xb1\xe6\x96\x87",
                   (s_preview_choice == 1) ? "* English" : "  English"),
        (s_preview_choice == 1 || !flash_ok) ? Uc_Value() : Uc_Text(), Uc_Bg());

    Draw_Cursor(s_preview_choice == 0 ? 3 : 4);

    /* Bottom rows: live preview + Flash diagnostic */
    Erase_Line(6);
    if (flash_ok)
        Tft_Driver_Show_String(6, 0, " Flash:OK  Chinese OK", Uc_Dim(), Uc_Bg());
    else
        Tft_Driver_Show_String(6, 0, " Flash:--  EN only", 0xE8E4U, Uc_Bg());
    Erase_Line(7);
    if (s_preview_choice == 0 && flash_ok)
        Tft_Driver_Show_CN_String(7, Center("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"), "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c", Uc_Dim(), Uc_Bg());
    else
        Tft_Driver_Show_String(7, Center("Hello World"), "Hello World", Uc_Dim(), Uc_Bg());
}

static void Handle_Lang_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                              Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t up   = (k2 == KEY_DRIVER_EVENT_CLICK);
    uint8_t down = (k3 == KEY_DRIVER_EVENT_CLICK);
    uint8_t back = (k1 == KEY_DRIVER_EVENT_CLICK);
    uint8_t ok   = (k4 == KEY_DRIVER_EVENT_CLICK);

    /* BACK -> cancel, restore old cursor, no save */
    if (back) {
        s_preview_choice = s_language;
        s_page = UI_PAGE_SETTING; s_setting_cursor = 0; s_page_drawn = 0; return;
    }

    /* UP/DOWN → move preview cursor with wrap-around */
    if (up)   { s_preview_choice = (s_preview_choice == 0) ? 1 : 0; s_page_drawn = 0; }
    if (down) { s_preview_choice = (s_preview_choice == 0) ? 1 : 0; s_page_drawn = 0; }

    /* CONFIRM -> commit selection */
    if (ok && s_preview_choice != s_language) {
        s_language  = s_preview_choice;
        s_settings_dirty = 1;
        s_page_drawn = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  S3. Icon Browser — 7 cols × 5 rows = 35 icons/page, labels from 1
 * ═══════════════════════════════════════════════════════════════ */
#define ICON_COLS       7   /* Bug 2: swapped — 7 columns × 5 rows */
#define ICON_ROWS       5
#define ICON_CELL_SZ    18
#define ICON_GRID_X     ((160 - ICON_COLS * ICON_CELL_SZ) / 2)  /* (160-126)/2 = 17 */
#define ICON_PER_PAGE   (ICON_COLS * ICON_ROWS)  /* 35 */
#define ICON_TOTAL      35
#define ICON_TOTAL_PAGES ((ICON_TOTAL + ICON_PER_PAGE - 1) / ICON_PER_PAGE)

/* ── 35-icon name table (localized via Pick_CN_EN) ── */
static const char* Get_Icon_Name(uint8_t icon_id)
{
    switch (icon_id) {
        case 0:  return Pick_CN_EN("WIFI_SIG",    "WIFI_SIG");
        case 1:  return Pick_CN_EN("WIFI_CONN",   "WIFI_CONN");
        case 2:  return Pick_CN_EN("WIFI_OFF",    "WIFI_OFF");
        case 3:  return Pick_CN_EN("WIFI_RMV",    "WIFI_RMV");
        case 4:  return Pick_CN_EN("MQTT",        "MQTT");
        case 5:  return Pick_CN_EN("MQTT_YES",    "MQTT_YES");
        case 6:  return Pick_CN_EN("MQTT_NO",     "MQTT_NO");
        case 7:  return Pick_CN_EN("MQTT_ANIM",   "MQTT_ANIM");
        case 8:  return Pick_CN_EN("\xe6\x98\x9f\xe6\xa0\x87",  "STAR");
        case 9:  return Pick_CN_EN("\xe5\x85\x89\xe6\xa0\x87\xe5\x8a\xa8", "STAR_CUR");
        case 10: return Pick_CN_EN("\xe7\x81\xab\xe7\xae\xad",  "ROCKET");
        case 11: return Pick_CN_EN("\xe7\x94\xb5\xe6\xb1\xa0",  "BATTERY");
        case 12: return Pick_CN_EN("\xe8\xad\xa6\xe5\x91\x8a",  "WARNING");
        case 13: return Pick_CN_EN("\xe5\x8b\xbe",    "CHECK");
        case 14: return Pick_CN_EN("\xe5\x8f\x89",    "CROSS");
        case 15: return Pick_CN_EN("\xe7\x94\xb5\xe6\xba\x90",  "POWER");
        case 16: return Pick_CN_EN("\xe9\x97\xaa\xe7\x94\xb5",  "LIGHTNING");
        case 17: return Pick_CN_EN("\xe6\xb8\xa9\xe5\xba\xa6",  "TEMP");
        case 18: return Pick_CN_EN("\xe9\xa3\x8e\xe6\x89\x87",  "FAN");
        case 19: return Pick_CN_EN("\xe9\x94\x81",    "LOCK");
        case 20: return Pick_CN_EN("\xe4\xb8\xbb\xe9\xa1\xb5",  "HOME");
        case 21: return Pick_CN_EN("\xe8\xae\xbe\xe7\xbd\xae",  "GEAR");
        case 22: return Pick_CN_EN("\xe5\x88\xb7\xe6\x96\xb0",  "REFRESH");
        case 23: return Pick_CN_EN("\xe4\xb8\x8a\xe7\xae\xad",  "ARROW_UP");
        case 24: return Pick_CN_EN("\xe4\xb8\x8b\xe7\xae\xad",  "ARROW_DN");
        case 25: return Pick_CN_EN("\xe5\xb7\xa6\xe7\xae\xad",  "ARROW_LT");
        case 26: return Pick_CN_EN("\xe5\x8f\xb3\xe7\xae\xad",  "ARROW_RT");
        case 27: return Pick_CN_EN("\xe4\xbf\xa1\xe5\x8f\xb7",  "SIGNAL");
        case 28: return Pick_CN_EN("\xe5\x85\xa8\xe7\x90\x83",  "GLOBE");
        case 29: return Pick_CN_EN("\xe5\x9b\xbe\xe8\xa1\xa8",  "CHART");
        case 30: return Pick_CN_EN("\xe6\x97\xb6\xe9\x92\x9f",  "CLOCK");
        case 31: return Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "1", "EXTRA1");
        case 32: return Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "2", "EXTRA2");
        case 33: return Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "3", "EXTRA3");
        case 34: return Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "4", "EXTRA4");
        default: return "?";
    }
}

static void Draw_Icons_Full(void)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();

    if (!flash_ok) {
        Draw_Header(Pick_CN_EN("\xe5\x9b\xbe\xe6\xa0\x87", "Icons"));
        Tft_Driver_Show_String(3, 2, Pick_CN_EN(S_FLASH_REQUIRED_CN, S_FLASH_REQUIRED_EN), Uc_Alarm(), Uc_Bg());
        return;
    }

    {
        char buf[24];
        snprintf(buf, 24, "%s [%d/%d]", Pick_CN_EN(S_TITLE_ICONS_CN, S_TITLE_ICONS_EN),
                 s_icon_page + 1, ICON_TOTAL_PAGES);
        Draw_Header(buf);
    }

    /* 7×5 grid (7 cols × 5 rows), 18px cells, starting y=16 */
    {
        uint8_t row, col;
        for (row = 0U; row < ICON_ROWS; row++) {
            for (col = 0U; col < ICON_COLS; col++) {
                uint8_t icon_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + row * ICON_COLS + col);
                uint16_t x = (uint16_t)(ICON_GRID_X + (uint16_t)col * ICON_CELL_SZ);
                uint16_t y = (uint16_t)((uint16_t)row * ICON_CELL_SZ + 16U);
                if (icon_id < ICON_TOTAL) {
                    uint8_t  cursor_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + s_icon_cursor);
                    uint16_t fg = Uc_Text();
                    uint16_t bg = Uc_Bg();
                    if (icon_id == cursor_id) {
                        Tft_Driver_Fill_Rect(x - 1U, y - 1U, 18U, 18U, Uc_Value());
                        bg = Uc_Value();
                    }
                    Tft_Driver_Draw_Icon_By_Id(x, y, icon_id, 0, fg, bg);
                }
            }
        }
    }

    /* bottom info bar: icon name + label (1-based) */
    {
        uint8_t  icon_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + s_icon_cursor);
        if (icon_id < ICON_TOTAL) {
            char buf[32];
            snprintf(buf, 32, "%s [#%d]", Get_Icon_Name(icon_id), (int)(icon_id + 1));
            uint8_t col = Center(buf);
            Tft_Driver_Show_String(7, col, buf, Uc_Value(), Uc_Bg());
        }
    }
}

static void Handle_Icons_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                               Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();
    uint8_t up    = (k2 == KEY_DRIVER_EVENT_CLICK);
    uint8_t down  = (k3 == KEY_DRIVER_EVENT_CLICK);
    uint8_t back  = (k1 == KEY_DRIVER_EVENT_CLICK);
    uint8_t page  = s_icon_page;
    uint8_t cur   = s_icon_cursor;

    /* BACK -> settings menu */
    if (back) {
        s_page = UI_PAGE_SETTING; s_setting_cursor = 2; s_page_drawn = 0; return;
    }

    if (!flash_ok) return;

    /* UP: dec cursor, wrap to prev page */
    if (up) {
        if (cur == 0) {
            if (page > 0) { page--; cur = (uint8_t)(ICON_PER_PAGE - 1U); }
            else          { cur   = (uint8_t)(ICON_PER_PAGE - 1U); }  /* wrap on page 0 */
        } else { cur--; }
    }

    /* DOWN: inc cursor, wrap to next page */
    if (down) {
        uint8_t items_on_page = ICON_PER_PAGE;
        if (page == (ICON_TOTAL_PAGES - 1U)) {
            items_on_page = (uint8_t)(ICON_TOTAL - page * ICON_PER_PAGE);
        }
        if (cur + 1U >= items_on_page) {
            if (page + 1U < ICON_TOTAL_PAGES) { page++; cur = 0; }
            else                              { cur   = 0; }  /* wrap on last page */
        } else { cur++; }
    }

    /* ── Safety clamp: enforce bounds before writing to state ── */
    if (page >= ICON_TOTAL_PAGES) page = (uint8_t)(ICON_TOTAL_PAGES - 1U);
    {
        uint8_t items_on_cur_page = ICON_PER_PAGE;
        if (page == (ICON_TOTAL_PAGES - 1U)) {
            items_on_cur_page = (uint8_t)(ICON_TOTAL - page * ICON_PER_PAGE);
        }
        if (cur >= items_on_cur_page) cur = (uint8_t)(items_on_cur_page - 1U);
    }

    /* Note: CONFIRM (k4) ignored in icon browser */
    if (page != s_icon_page || cur != s_icon_cursor) {
        s_icon_page   = page;
        s_icon_cursor = cur;
        s_page_drawn   = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  S3. Letter Spacing (V4.5.2: replaces Font Size)
 *  UP/DOWN → preview cursor 0-3, bottom shows live spacing sample
 *  PAGE    → commit, ON → cancel
 * ═══════════════════════════════════════════════════════════════ */
#define S_SPACING_TITLE_CN "\xe5\xad\x97\xe9\x97\xb4\xe8\xb7\x9d"
#define S_SPACING_TITLE_EN "Spacing"

    /* Spacing labels V4.5.2 — each label shows check on confirmed value, star on preview.
     *   The labels themselves contain embedded spacing for demo effect.
     *   ARMCC V5 multibyte-safe: all chars are ASCII or verified UTF-8 sequences. */
static const char* Spacing_Label(uint8_t v)
{
    switch (v) {
        case 0: return Pick_CN_EN("  \xe6\x97\xa0 (0px)", "  None (0)");
        case 1: return Pick_CN_EN("  \xe5\xb0\x8f (2px)", "  Small (2)");
        case 2: return Pick_CN_EN("  \xe4\xb8\xad (4px)", "  Medium (4)");
        case 3: return Pick_CN_EN("  \xe5\xa4\xa7 (6px)", "  Large (6)");
        default: return "";
    }
}

static void Draw_Spacing_Full(void)
{
    uint8_t i;
    Draw_Header(Pick_CN_EN(S_SPACING_TITLE_CN, S_SPACING_TITLE_EN));

    for (i = 0; i < 4; i++) {
        Erase_Line(3 + i);
        /* Show star on preview choice, apply spacing ONLY to label text via local set/restore */
        {
            uint8_t saved = s_letter_spacing;
            Tft_Driver_Set_Letter_Spacing((uint8_t)(i * 2));
            Tft_Driver_Show_CN_String(3 + i, 2,
                Spacing_Label(i),
                (s_preview_choice == i) ? Uc_Value() : Uc_Text(), Uc_Bg());
            Tft_Driver_Set_Letter_Spacing((uint8_t)(saved * 2));
        }
        if (s_preview_choice == i) {
            Tft_Driver_Show_String(3 + i, 0, "*", Uc_Value(), Uc_Bg());
        }
    }

    Draw_Cursor(3 + s_preview_choice);

    /* Bottom row 7: live preview with spacing applied to sample text */
    Erase_Line(7);
    {
        char preview_buf[21] = "";
        /* Build localized sample string at runtime to avoid ARMCC multibyte warnings */
        snprintf(preview_buf, 21, "%s", Pick_CN_EN("Aa\xe4\xb8\xad\xe6\x96\x87\xe6\xa8\xa1\xe5\xbc\x8f",
                                                    "Aa Zh Demo"));
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_preview_choice * 2));
        Tft_Driver_Show_CN_String(7, Center(preview_buf), preview_buf, Uc_Dim(), Uc_Bg());
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));  /* restore */
    }
}

static void Handle_Spacing_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                                 Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t back = (k1 == KEY_DRIVER_EVENT_CLICK);
    uint8_t ok   = (k4 == KEY_DRIVER_EVENT_CLICK);
    uint8_t up   = (k2 == KEY_DRIVER_EVENT_CLICK);
    uint8_t down = (k3 == KEY_DRIVER_EVENT_CLICK);

    if (back) {
        s_preview_choice = s_letter_spacing;
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));
        s_page = UI_PAGE_SETTING; s_setting_cursor = 1; s_page_drawn = 0; return;
    }

    /* UP/DOWN -> move preview cursor with wrap-around (0->3/3->0) */
    if (up)   { s_preview_choice = (s_preview_choice == 0) ? 3 : s_preview_choice - 1; s_page_drawn = 0; }
    if (down) { s_preview_choice = (s_preview_choice >= 3) ? 0 : s_preview_choice + 1; s_page_drawn = 0; }

    /* PAGE --> commit: write spacing × actual pixel gap */
    if (ok && s_preview_choice != s_letter_spacing) {
        s_letter_spacing = s_preview_choice;
        /* V4.5.2: map choice 0/1/2/3 -> actual px 0/2/4/6 */
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_preview_choice * 2));
        s_settings_dirty = 1;
        s_page_drawn = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  S6. Color Scheme — preview-before-confirm (V4.5.2)
 *  UP/DOWN → preview cursor (visual only), bottom shows 3-color swatches
 *  PAGE    → commit + full clear + force redraw
 *  ON      → cancel (restore old preset)
 * ═══════════════════════════════════════════════════════════════ */
static void Apply_Color_Preset(uint8_t preset_idx)
{
    const ColorPreset* p = &COLOR_PRESETS[preset_idx];
    s_color_fg      = p->fg;
    s_color_bg      = p->bg;
    s_color_accent  = p->accent;
    sc_preset = preset_idx;
}
static void Draw_Color_Full(void)
{
    uint8_t i;

    Draw_Header(Pick_CN_EN(S_TITLE_COLOR_CN, S_TITLE_COLOR_EN));

    for (i = 0; i < 6; i++) {
        char buf[24];
        const char* name = Pick_CN_EN(COLOR_PRESETS[i].name_cn, COLOR_PRESETS[i].name_en);
        Erase_Line(2 + i);
        /* V4.5.2: show check-mark on confirmed (active) preset, star on preview choice */
        {
            const char* prefix = (sc_preset == i) ? "\xe2\x9c\x93 " :
                                (s_preview_choice == i) ? "* " : "  ";
            snprintf(buf, 24, "%s%s", prefix, name);
            Tft_Driver_Show_CN_String(2 + i, 2, buf,
                (s_preview_choice == i) ? Uc_Value() : Uc_Text(), Uc_Bg());
        }
    }

    Draw_Cursor(2 + s_preview_choice);

    /* Bottom row 7: 3-color preview bar using current preview choice */
    Erase_Line(7);
    {
        const ColorPreset* p = &COLOR_PRESETS[s_preview_choice];
        uint16_t bar_y = 7 * TFT_FONT_HEIGHT;
        uint16_t bw = 53;
        /* 3 equal-width blocks: BG (left), FG (middle), Accent (right) */
        Tft_Driver_Fill_Rect(0,  bar_y, bw, TFT_FONT_HEIGHT, p->bg);
        Tft_Driver_Fill_Rect(bw, bar_y, bw, TFT_FONT_HEIGHT, p->fg);
        Tft_Driver_Fill_Rect((uint16_t)(bw * 2), bar_y, bw, TFT_FONT_HEIGHT, p->accent);
        /* Label each block */
        {
            uint16_t bg_label_fg = (p->bg == 0x0000 || p->bg < 0x2104) ? Uc_Text() : TFT_COLOR_BLACK;
            Tft_Driver_Show_String(7, 0,  "B", bg_label_fg, p->bg);
            Tft_Driver_Show_String(7, 7,  "F", (p->fg < 0x8410) ? Uc_Text() : TFT_COLOR_BLACK, p->fg);
            Tft_Driver_Show_String(7, 14, "A", (p->accent < 0x8410) ? Uc_Text() : TFT_COLOR_BLACK, p->accent);
        }
    }

}

static void Handle_Color_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                               Key_Driver_Event k3, Key_Driver_Event k4)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        s_preview_choice = sc_preset;  /* restore saved */
        s_page = UI_PAGE_SETTING; s_setting_cursor = 3; s_page_drawn = 0; return;
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        if (s_preview_choice == 0) s_preview_choice = 5;
        else s_preview_choice--;
        s_page_drawn = 0;
    }
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        if (s_preview_choice >= 5) s_preview_choice = 0;
        else s_preview_choice++;
        s_page_drawn = 0;
    }

    /* CONFIRM -> commit: apply new color and let Phase 7 redraw once */
    if (k4 == KEY_DRIVER_EVENT_CLICK && s_preview_choice != sc_preset) {
        Apply_Color_Preset(s_preview_choice);
        s_settings_dirty = 1;
        s_page_drawn = 0;  /* trigger Phase 7 full-page redraw with new bg */
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Settings Key Dispatch
 * ═══════════════════════════════════════════════════════════════ */
static uint8_t Handle_Settings_Keys(Ui_Page page,
    Key_Driver_Event k1, Key_Driver_Event k2,
    Key_Driver_Event k3, Key_Driver_Event k4)
{
    switch (page) {
        case UI_PAGE_SETTING:           Handle_Setting_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_LANG:      Handle_Lang_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_SPACING:   Handle_Spacing_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_ICONS:     Handle_Icons_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_COLOR:     Handle_Color_Keys(k1,k2,k3,k4); return 1;
        default: return 0;
    }
}

/* ================================================================
 *  Main Scheduler — V4.2.0 incremental refresh architecture
 *
 *  Phase 0: Global Top-Right Icons Manager (Always On, Auto-Sync)
 *  Phase 1: Fault edge detection → may set s_page, s_page_drawn=0
 *  Phase 2: Sweep complete detection → may set s_page
 *  Phase 3: Key scan + dispatch → may set s_page or s_menu_cursor
 *  Phase 4: Page change → s_page_drawn=0; all tracking invalidated
 *  Phase 5: 200ms tick → dynamic incremental update (values only)
 *  Phase 6: Cursor boundary clamp
 *  Phase 7: Draw — full page only when s_page_drawn==0
 * ================================================================ */
void Ui_Controller_Task(
    const Key_Driver_Event events[KEY_DRIVER_COUNT])
{
    static uint32_t s_last_ui_ms = 0;
    uint8_t old_cursor;
    uint8_t cursor_changed = 0;
    uint8_t tick_200ms = 0;

    Tft_Driver_Begin_Draw_Cycle();

    /* ── Phase 0: Global Top-Right Icons Manager (Always On, Auto-Sync) ── */
    {
        uint8_t cs = App_Network_Get_Connect_Status();
        uint8_t connecting = App_Network_Is_Connecting();
        uint8_t offline = App_Network_Is_Offline();
        uint8_t ready = Esp8266_Driver_Is_Ready();
        uint8_t wifi_frame = 0xFFU;
        uint8_t mqtt_frame = 0xFFU;
        uint8_t rssi_frame = 0xFFU;

        if ((s_no_wifi_mode == 0U) && (offline == 0U) &&
            ((connecting != 0U) || (ready == 0U))) {
            wifi_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150U) % 6U;
        }
        if (connecting != 0U) {
            mqtt_frame = (uint8_t)(Sys_Timer_Get_Tick() / 200U) % 6U;
        }
        if (cs == APP_NETWORK_CONN_ONLINE) {
            int8_t rssi = App_Network_Get_RSSI();
            if (rssi >= -50) rssi_frame = 3U;
            else if (rssi >= -60) rssi_frame = 2U;
            else if (rssi >= -70) rssi_frame = 1U;
            else rssi_frame = 0U;
        }

        if (s_page_drawn) {
            /* 触发条件：网络状态改变 OR WiFi开关改变 OR 动画帧跳动 OR 刚发生过切页 */
            if ((s_icon_cache_valid == 0U) ||
                cs != s_last_icon_cs ||
                s_no_wifi_mode != s_last_icon_mode ||
                wifi_frame != s_last_wifi_frame ||
                mqtt_frame != s_last_mqtt_frame ||
                rssi_frame != s_last_rssi_frame ||
                (uint8_t)s_page != s_last_icon_page ||
                Uc_Bg() != s_last_icon_bg ||
                Uc_Text() != s_last_icon_fg) {

                /* 直接局部泵送图标，不再重绘 Header */
                Draw_TopRight_Icons();
            }
        } else {
            /* 页面跳转瞬间，强制失效状态，以便进入新页后图标能立刻跟进刷新 */
            s_icon_cache_valid = 0U;
        }
    }

    /* ── Phase 1: System Fault detection ── */
    {
        uint8_t sys_fault = (Sys_Core_Get_State() == SYS_STATE_FAULT);
        if (sys_fault != s_was_fault_state) {
            s_was_fault_state = sys_fault;
            s_last_is_fault_menu = 0xFF;  /* force Main_Menu_Dynamic_Update redraw */
        }
    }

    /* ── Phase 2: Sweep complete → auto-jump SUMMARY ── */
    if (s_page == UI_PAGE_SWEEP) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
            s_page = UI_PAGE_MONITOR_SUMMARY;
            s_page_drawn = 0;
            Reset_EMA();
            s_user_target_synced = 0;
        }
    }

    /* ── Phase 3: Key scan + dispatch (单次临界区批量读取) ── */
    old_cursor = s_menu_cursor;
    {
        uint8_t old_setting_cur = s_setting_cursor;
        /* Settings key dispatch (includes back-navigation) */
        if (!Handle_Settings_Keys(s_page,
                                   events[KEY_DRIVER_ID_BACK],
                                   events[KEY_DRIVER_ID_UP],
                                   events[KEY_DRIVER_ID_DOWN],
                                   events[KEY_DRIVER_ID_CONFIRM])) {
            Handle_Keys_by_Page(s_page,
                                events[KEY_DRIVER_ID_BACK],
                                events[KEY_DRIVER_ID_UP],
                                events[KEY_DRIVER_ID_DOWN],
                                events[KEY_DRIVER_ID_CONFIRM]);
        }
        if (s_setting_cursor != old_setting_cur) {
            s_last_setting_cursor = old_setting_cur;
            cursor_changed = 2;  /* signal: s_setting_cursor moved */
        }
    }
    if (s_menu_cursor != old_cursor) cursor_changed = 1;

    /* ── Phase 4: Page change detection ── */
    if ((uint8_t)s_page != s_last_page) {
        s_last_page  = (uint8_t)s_page;
        s_page_drawn = 0;
        /* Invalidate all tracking state */
        s_last_is_running    = 0xFF;
        s_last_is_fault_menu = 0xFF;
        s_last_sweep_stopped = 0xFF;
        s_last_wifi_cs       = 0xFF;
        s_last_sub_visible   = 0;
        s_last_gauge_label   = NULL;
        s_last_f_str[0] = '\0';
        s_last_v_str[0] = '\0';
        s_last_i_str[0] = '\0';
        s_last_status_buf[0] = '\0';
    }

    /* ── Phase 5: 200ms tick ── */
    if (Sys_Timer_Get_Tick() - s_last_ui_ms >= UI_REFRESH_MS) {
        s_last_ui_ms = Sys_Timer_Get_Tick();
        tick_200ms = 1;
    }

    /* ── Phase 6: Cursor boundary clamp ── */
    if (s_page == UI_PAGE_MAIN_MENU) {
        /* 非故障时第5项不可达, 光标最多到3 */
        uint8_t max_cursor = (Sys_Core_Get_State() == SYS_STATE_FAULT) ? 4 : 3;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    if (s_page == UI_PAGE_MONITOR_SUB_MENU) {
        if (s_menu_cursor > 4) s_menu_cursor = 0;
    }
    if (s_page == UI_PAGE_SETTING) {
        if (s_setting_cursor > 3U) s_setting_cursor = 0U;
    }

    /* ════════════════════════════════════════════════════════════
     *  Phase 7: Draw — full or incremental
     * ════════════════════════════════════════════════════════════ */

    if (!s_page_drawn) {
        /* Full page draw: clear screen to current bg, then render */
        Tft_Driver_Clear(Uc_Bg());
        switch (s_page) {
            case UI_PAGE_MAIN_MENU:        Draw_Main_Menu_Full();   break;
            case UI_PAGE_MONITOR_SUB_MENU: Draw_Sub_Menu_Full();    break;
            case UI_PAGE_SWEEP:            Draw_Sweep_Full();       break;
            case UI_PAGE_MONITOR_SUMMARY:  Draw_Summary_Full();     break;
            case UI_PAGE_MONITOR_FREQ:     Draw_Freq_Full();        break;
            case UI_PAGE_MONITOR_VOLT:     Draw_Volt_Full();        break;
            case UI_PAGE_MONITOR_CURR:     Draw_Curr_Full();        break;
            case UI_PAGE_WIFI_SETUP:       Draw_WiFi_Full();        break;
            case UI_PAGE_FAULT:            Draw_Fault_Full();       break;
            case UI_PAGE_SETTING:          Draw_Setting_Full();     break;
            case UI_PAGE_SETTING_LANG:      Draw_Lang_Full();        break;
            case UI_PAGE_SETTING_SPACING:   Draw_Spacing_Full();     break;
            case UI_PAGE_SETTING_ICONS:     Draw_Icons_Full();       break;
            case UI_PAGE_SETTING_COLOR:     Draw_Color_Full();       break;
        }
        Update_Leds();
        if (!Tft_Driver_Is_Draw_Blocked()) {
            s_page_drawn = 1;
            cursor_changed = 0;
        }
    } else {
        /* ── Incremental updates — only touch changed pixels ── */

        if (cursor_changed) {
            if (cursor_changed == 2) {
                /* V4.5.2: cursor moved in settings — only for SETTING main menu pages.
                 *   Other pages use s_preview_choice and redraw via s_page_drawn=0. */
                if (s_page == UI_PAGE_SETTING) {
                    Erase_Cursor(2 + s_last_setting_cursor);
                    Draw_Cursor(2 + s_setting_cursor);
                }
            } else {
                switch (s_page) {
                    case UI_PAGE_MAIN_MENU:
                        Main_Menu_Cursor_Update(old_cursor);
                        break;
                    case UI_PAGE_MONITOR_SUB_MENU:
                        Sub_Menu_Cursor_Update(old_cursor);
                        break;
                    default:
                        s_page_drawn = 0;
                        break;
                }
            }
        }

        if (tick_200ms) {
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:        Main_Menu_Dynamic_Update(); break;
                case UI_PAGE_MONITOR_SUB_MENU: /* static */               break;
                case UI_PAGE_SWEEP:            Sweep_Dynamic_Update();    break;
                case UI_PAGE_MONITOR_SUMMARY:  Summary_Dynamic_Update();  break;
                case UI_PAGE_MONITOR_FREQ:     Freq_Dynamic_Update();     break;
                case UI_PAGE_MONITOR_VOLT:     Volt_Dynamic_Update();     break;
                case UI_PAGE_MONITOR_CURR:     Curr_Dynamic_Update();     break;
                case UI_PAGE_WIFI_SETUP:       WiFi_Dynamic_Update();     break;
                case UI_PAGE_FAULT:            /* static */               break;
                case UI_PAGE_SETTING:          /* static */               break;
                case UI_PAGE_SETTING_LANG:      /* static */               break;
                case UI_PAGE_SETTING_SPACING:   /* static */               break;
                case UI_PAGE_SETTING_ICONS:     /* static */               break;
                case UI_PAGE_SETTING_COLOR:     /* static */               break;
            }
            Update_Leds();
        }
        if (Tft_Driver_Is_Draw_Blocked()) {
            s_page_drawn = 0;
        }
    }
}

/* ================================================================
 *  Public Interface
 * ================================================================ */
Ui_Page Ui_Controller_Get_Page(void)      { return s_page; }
uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }

/**
 * @brief  外部强制跳转到目标页面 (物理级)
 * @note   远程指令触发或系统状态迁移时, 调用此函数同步 UI 页面, 防止 UI 展示不同步
 */
void Ui_Controller_Force_Page(Ui_Page page)
{
    s_page = page;
    s_page_drawn = 0;  /* 强制全量重绘 */
}

/**
 * @brief  外部强制跳转到目标页面并重置菜单光标
 * @note   远程 CMD:ON/OFF 专用 — 除页面跳转外, 还强制重置 s_menu_cursor=0
 *         防止远端操作后本地菜单光标停留在已失效的旧菜单项上
 */
void Ui_Controller_Force_Page_And_Reset(Ui_Page page)
{
    s_page        = page;
    s_menu_cursor = 0;
    s_page_drawn  = 0;
}

/**
 * @brief  [V4.5.2] 从 App_Storage 加载设置参数并应用到 UI + 驱动
 * @note   Sys_Post_Init 中 App_Storage_Init 之后调用。
 *         The legacy backlight field is ignored because PA12 is GPIO on/off.
 */
void Ui_Controller_Apply_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                   uint8_t spacing, uint8_t preset,
                                   uint16_t fg, uint16_t bg)
{
    s_language        = lang;
    s_letter_spacing  = spacing;
    (void)font;  /* font_size replaced by letter_spacing in V4.5.2, retained for Flash compat */
    (void)bl;
    Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));  /* 0-3 -> actual px 0/2/4/6 */
    if (preset < 6) {
        Apply_Color_Preset(preset);
    } else {
        s_color_fg     = fg;
        s_color_bg     = bg;
        sc_preset = 255;
    }
    /* Init preview cursors to saved values */
    s_preview_choice = sc_preset;
}
