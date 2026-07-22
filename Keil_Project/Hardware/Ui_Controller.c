/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面页面、按键导航与增量刷新控制 — V5.0.2
 *
 *  模块依赖关系:
 *  +----------------------------------------------------------+
 *  |                       STM32F103C8T6                       |
 *  |                                                           |
 *  |    显示驱动：SPI1和DMA，负责彩屏绘制                       |
 *  |    按键驱动：PB9至PB5，负责五键事件                       |
 *  |    指示灯驱动：PA15、PB4、PB3和PC13                       |
 *  |    蜂鸣器驱动：PB15                                      |
 *  |    PWM驱动：TIM1主通道与互补通道                          |
 *  |    系统核心：状态机、安全保护和统一电源控制               |
 *  |    系统时基：提供200ms增量刷新节拍                        |
 *  |                                                           |
 *  |    页面数量：9个主功能或监测页面，6个设置页面             |
 *  |                                                           |
 *  |    调度分为0至7共八个阶段：图标、故障、扫频、按键、       |
 *  |    页面切换、动态数据、光标边界和最终绘制                 |
 *  +----------------------------------------------------------+
 *
 * @note    彩屏横向160乘128像素，字符布局为8行20列，使用五键导航。
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
static void Ui_Controller_Draw_TopRight_Icons(void);
static const char* Ui_Controller_Get_Icon_Name(uint8_t icon_id);

/* 能量条颜色表与增量绘制逻辑。 */
static const uint16_t EB_COLOR_TABLE[8] = {
    0x07E0, 0x2FE0, 0x5FE0, 0x87E0, 0xFF80, 0xFD00, 0xF900, 0xF800  /* 绿→黄→红 RGB565 */
};

static void Ui_Controller_Energy_Bar_Draw(uint16_t x, uint16_t y, uint16_t max_w, uint16_t h,
                                float value, float min_val, float max_val, uint16_t bg_color)
{
    uint16_t total_w;
    uint8_t  seg_count, i;
    uint16_t seg_w, seg_x;
    static uint16_t s_last_w = 0xFFFF;  /* 记录上次宽度，供增量擦除使用 */

    {
        float range = max_val - min_val;
        float ratio;
        if (range <= 0.0f) { Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color); s_last_w = 0; return; }
        ratio = (value - min_val) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        total_w = (uint16_t)(ratio * (float)max_w);
    }

    if (total_w == s_last_w) return;  /* 宽度未变化时不重复绘制。 */

    /* 只擦除宽度发生变化的区域。 */
    if (total_w < s_last_w) {
        /* 能量条缩短时，擦除新旧边界之间的区域。 */
        Tft_Driver_Fill_Rect(x + total_w, y, s_last_w - total_w, h, bg_color);
    } else if (s_last_w == 0xFFFF) {
        /* 首次调用无法确定旧状态，需要清除完整区域。 */
        Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color);
    }
    /* 能量条增长时，只补画新增部分。 */
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
/* ============================================================
 *  设置页面状态，先预览再确认
 * ============================================================ */
static uint8_t  s_language         = 1;     /* 0表示中文，1表示英文，默认英文 */
static uint8_t  s_letter_spacing   = 0;     /* 字符间距选项为0至3，替代旧字体大小选项 */
static uint8_t  sc_preset          = 0;     /* 0至5为预设方案，255表示自定义 */
static uint16_t s_color_fg         = 0xFFFF;/* 默认前景色为白色 */
static uint16_t s_color_bg         = 0x0000;/* 默认背景色为黑色 */
static uint16_t s_color_accent     = 0xFFE0;/* 默认强调色为黄色 */
static uint32_t s_startup_low_freq_hz = 20000U;
static uint32_t s_startup_high_freq_hz = 100000U;
static uint8_t s_startup_freq_band = APP_STORAGE_FREQ_BAND_HIGH;
static uint8_t s_frequency_edit_band = APP_STORAGE_FREQ_BAND_LOW;
static uint32_t s_frequency_edit_value = 20000U;
static uint8_t s_frequency_editing = 0U;
static uint8_t s_menu_cursor_icon = 0U;
static const uint8_t s_cursor_icon_ids[8] = {
    ICON_ID_STAR, ICON_ID_CHECK, ICON_ID_ROCKET_ANIM, ICON_ID_LIGHTNING,
    ICON_ID_HOME, ICON_ID_GEAR, ICON_ID_REFRESH, ICON_ID_ARROW_RT
};
static uint8_t  s_icon_cache_valid = 0U;
static uint8_t  s_last_icon_cs     = 0xFFU;
static uint8_t  s_last_icon_mode   = 0xFFU;
static uint8_t  s_last_wifi_frame  = 0xFFU;
static uint8_t  s_last_mqtt_frame  = 0xFFU;
static uint8_t  s_last_rssi_frame  = 0xFFU;
static uint8_t  s_last_icon_page   = 0xFFU;
static uint16_t s_last_icon_bg     = 0xFFFFU;
static uint16_t s_last_icon_fg     = 0xFFFFU;

/* 设置子页面光标。 */
static uint8_t  s_setting_cursor   = 0;
static uint8_t  s_icon_cursor      = 0;

/* 语言、间距和配色页面共用的预览光标。 */
static uint8_t  s_preview_choice   = 0;     /* 确认后才把预览位置写入正式设置 */

/* 等待持久化的设置状态。 */
static uint8_t  s_settings_dirty       = 0;
static uint32_t s_settings_saved_until_ms = 0U;
static uint8_t  s_last_setting_cursor  = 0xFF; /* 第七阶段用于判断设置光标是否改变 */

typedef enum {
    UI_SETTING_ITEM_LANGUAGE = 0,
    UI_SETTING_ITEM_FREQUENCY,
    UI_SETTING_ITEM_SPACING,
    UI_SETTING_ITEM_ICONS,
    UI_SETTING_ITEM_COLOR,
    UI_SETTING_ITEM_COUNT
} Ui_Setting_Item;

#define UI_FREQUENCY_LOW_MIN_HZ   20000U
#define UI_FREQUENCY_LOW_MAX_HZ   99900U
#define UI_FREQUENCY_HIGH_MIN_HZ  100000U
#define UI_FREQUENCY_HIGH_MAX_HZ  200000U
#define UI_FREQUENCY_LOW_STEP_HZ  100U
#define UI_FREQUENCY_HIGH_STEP_HZ 1000U
#define UI_SETTINGS_SAVED_MS      1200U
#define UI_CURSOR_ICON_COUNT      8U

/* ==============================================================
 *  动态配色辅助函数
 * ============================================================== */
static uint16_t Ui_Controller_Get_Background_Color(void)      { return s_color_bg; }
static uint16_t Ui_Controller_Get_Title_Color(void)   { return s_color_accent; }
static uint16_t Ui_Controller_Get_Text_Color(void)    { return s_color_fg; }
static uint16_t Ui_Controller_Get_Value_Color(void)   { return s_color_accent; }
static uint16_t Ui_Controller_Get_Data_Color(void)    { return s_color_fg; }
#define Uc_Alarm()  TFT_COLOR_RED
#define Uc_Ok()     TFT_COLOR_GREEN
#define Uc_Dim()    TFT_COLOR_GRAY

/* ==============================================================
 *  中英文字符串选择
 *  使用函数选择字符串，避免ARMCC对复杂宏展开处理不一致。
 *  返回值既可作为格式化模板，也可直接交给混合字符串绘制函数。
 * ============================================================== */
static const char* Ui_Controller_Pick_CN_EN(const char* cn, const char* en) {
    /* 只有用户选择中文且外部字库有效时才返回中文字符串。 */
    return (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid()) ? cn : en;
}
#define S_WIFI_TITLE_CN "\xe6\x97\xa0\xe7\xba\xbf\xe7\x8a\xb6\xe6\x80\x81"
#define S_WIFI_TITLE_EN "WiFi Status"

#define UI_REFRESH_MS              200
#define UI_OVERCURRENT_THRESHOLD_A 5.0f
#define UI_POWER_V_THRESHOLD_V     12.0f

/* 中英文界面字符串。 */
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
#define S_FREQ_CN      "\xe9\xa2\x91\xe7\x8e\x87"                     /* 频率 */
#define S_FREQ_EN      "Freq"
#define S_VOLTAGE_CN   "\xe7\x94\xb5\xe5\x8e\x8b"                     /* 电压 */
#define S_VOLTAGE_EN   "Volt"
#define S_CURRENT_CN   "\xe7\x94\xb5\xe6\xb5\x81"                     /* 电流 */
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
#define S_DIV       "--------------------"           /* 纯单字节分隔线 */

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

/* 设置页面字符串。 */
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

/* 页面状态变量。 */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;
static uint8_t  s_last_page       = 0xFF;

/* 电压和电流进行指数平滑，频率直接使用数字寄存器读数。 */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

/* 用户调频目标与步进值。 */
static uint32_t s_user_target_hz = 100000;
static uint8_t  s_user_target_synced = 0;

/* ==============================================================
 *  六组配色预设表
 * ============================================================== */
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

/* 增量刷新状态。 */
static uint8_t s_page_drawn         = 0;    /* 0表示需要整页重绘，1表示静态内容已存在 */
static uint8_t s_last_is_running    = 0xFF; /* 上一次PWM运行状态 */
static uint8_t s_last_is_fault_menu = 0xFF; /* 上一次主菜单故障项状态 */
static uint8_t s_last_sub_visible   = 0;    /* 上一次子菜单可见窗口起点 */
static uint8_t s_last_sweep_stopped = 0xFF; /* 上一次扫频暂停状态 */
static uint8_t s_last_wifi_cs       = 0xFF; /* 上一次无线连接状态 */

/* 缓存上次格式化后的数值字符串，避免重复绘制未变化内容。 */
static char    s_last_f_str[21];
static char    s_last_v_str[21];
static char    s_last_i_str[21];
static char    s_last_status_buf[42];

/* 仪表盘数值和状态缓存，用于差分增量刷新。 */
static char    s_gauge_val_str[24] = "";
static char    s_gauge_status_buf[24] = "";

static void Ui_Controller_Reset_EMA(void) { s_ema_ok = 0; }

/* ================================================================
 *  文本对齐和数值格式化辅助函数
 * ================================================================ */

static uint8_t Ui_Controller_Center_Text(const char* s)
{
    uint8_t w = 0; uint8_t sp = Tft_Driver_Get_Letter_Spacing();
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }
        else { w++; s++; }
    }
    /* 把附加像素间距折算为字符列宽后再计算居中位置。 */
    if (sp > 0) w += (uint8_t)((sp + 7) / 8);
    return (w >= 20) ? 0 : (20 - w) / 2;
}

static uint8_t Ui_Controller_Right_Text(const char* s)
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
 * @brief  对界面电压和电流执行二次指数平滑，频率保持直接读取
 * @note   电压和电流的平滑系数为0.25，用于减少屏幕数值高频抖动。
 *         频率直接读取数字寄存器值，避免平滑延迟影响按键调频手感。
 *         数据源来自模数转换驱动的显示窗口，本函数不参与安全判定。
 */
static void Ui_Controller_Update_EMA(void)
{
    if (!s_ema_ok) {
        s_ema_v = Adc_Driver_Get_Display_Voltage();
        s_ema_i = Adc_Driver_Get_Display_Current();
        s_ema_f = (float)Pwm_Driver_Get_Frequency() / 1000.0f;
        s_ema_ok = 1;
    } else {
        s_ema_v = s_ema_v * 0.75f + Adc_Driver_Get_Display_Voltage() * 0.25f;
        s_ema_i = s_ema_i * 0.75f + Adc_Driver_Get_Display_Current() * 0.25f;
        s_ema_f = (float)Pwm_Driver_Get_Frequency() / 1000.0f;  /* 数字量直接读取，不增加平滑延迟。 */
    }
}

static void Ui_Controller_Format_Voltage(char* buf, float v)
{
    int x = (int)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 99999) x = 99999;
    snprintf(buf, 21, "%sV:%03d.%02dV", Ui_Controller_Pick_CN_EN(S_VOLTAGE_CN, S_VOLTAGE_EN), x/100, x%100);
}

static void Ui_Controller_Format_Current(char* buf, float c)
{
    char sign = (c < 0) ? '-' : '+';
    float v = (c < 0) ? -c : c;
    int x = (int)(v * 1000.0f + 0.5f);
    snprintf(buf, 21, "%sI:%c%d.%03dA", Ui_Controller_Pick_CN_EN(S_CURRENT_CN, S_CURRENT_EN), sign, (int)(x/1000), (int)(x%1000));
}

static void Ui_Controller_Format_Frequency(char* buf, float f)
{
    snprintf(buf, 21, "%sF:%3d.%01dkHz", Ui_Controller_Pick_CN_EN(S_FREQ_CN, S_FREQ_EN), (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

/* ================================================================
 *  页面标题绘制：第0行左侧显示标题，右侧显示连接状态图标
 * ================================================================ */
/**
 * @brief  绘制页面标题，先清除整行再重绘文字和图标
 * @note   清除完整160像素行可避免语言切换后残留旧文字。
 *         图标在本函数中统一重绘，调用方不得重复绘制。
 */
static void Ui_Controller_Draw_Header(const char* title)
{
    Tft_Driver_Fill_Rect(0, 0, TFT_WIDTH, TFT_FONT_HEIGHT, Ui_Controller_Get_Background_Color());
    Tft_Driver_Show_CN_String(0, 0, title, Ui_Controller_Get_Title_Color(), Ui_Controller_Get_Background_Color());
    Ui_Controller_Draw_TopRight_Icons();
}

/* ================================================================
 *  菜单光标：在横坐标0处绘制或擦除16乘16星形图标
 * ================================================================ */
static void Ui_Controller_Draw_Menu_Cursor(uint8_t row, uint8_t selected)
{
    uint8_t icon_id;

    Tft_Driver_Erase_Pixel_Area(0U,
        (uint16_t)row * TFT_FONT_HEIGHT, 16U, TFT_FONT_HEIGHT);
    if (selected == 0U) return;

    icon_id = s_cursor_icon_ids[s_menu_cursor_icon];
    if (Tft_Driver_Draw_Icon_By_Id(0U,
        (uint16_t)row * TFT_FONT_HEIGHT, icon_id, 0U,
        Ui_Controller_Get_Value_Color(),
        Ui_Controller_Get_Background_Color()) == 0U) {
        Tft_Driver_Draw_Icon_By_Id(0U,
            (uint16_t)row * TFT_FONT_HEIGHT, ICON_ID_STAR, 0U,
            Ui_Controller_Get_Value_Color(),
            Ui_Controller_Get_Background_Color());
    }
}

static void Ui_Controller_Draw_Cursor(uint8_t line)
{
    Ui_Controller_Draw_Menu_Cursor(line, 1U);
}

static void Ui_Controller_Erase_Cursor(uint8_t line)
{
    /* 用背景色擦除16乘16光标区域。 */
    Tft_Driver_Erase_Pixel_Area(0, (uint16_t)line * TFT_FONT_HEIGHT, 16, 16);
}

/* ================================================================
 *  行级绘制基础函数
 * ================================================================ */
static void Ui_Controller_Erase_Line(uint8_t line)
{
    Tft_Driver_Erase_Pixel_Area(0, (uint16_t)line * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT);
}

static void Ui_Controller_Draw_Divider(uint8_t line)
{
    Tft_Driver_Show_String(line, 0, S_DIV, Uc_Dim(), Ui_Controller_Get_Background_Color());
}

/* 绘制菜单文字时只擦除文本区域，保留左侧光标区域。 */
static void Ui_Controller_Draw_Menu_Text(uint8_t line, uint8_t col, const char* text, uint8_t enabled)
{
    uint16_t color = enabled ? Ui_Controller_Get_Text_Color() : Uc_Dim();
    Tft_Driver_Erase_Pixel_Area(col * 8, (uint16_t)line * TFT_FONT_HEIGHT, TFT_WIDTH - col * 8, TFT_FONT_HEIGHT);
    Tft_Driver_Show_CN_String(line, col, text, color, Ui_Controller_Get_Background_Color());
}

/* ================================================================
 *  主菜单整页绘制：正常4项，故障时增加第5项
 * ================================================================ */
static void Ui_Controller_Draw_Main_Menu_Full(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    uint8_t i;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }
    is_fault = (Sys_Core_Get_State() == SYS_STATE_FAULT);

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_WPT_PWM_CN, S_WPT_PWM_EN));
    Ui_Controller_Draw_Divider(1);

    /* 5项常驻，第5项非故障时灰色禁用。 */
    for (i = 0; i < 5; i++) {
        const char* text;
        uint8_t enabled = 1;
        switch (i) {
            case 0:
                text = is_running
                    ? Ui_Controller_Pick_CN_EN(S_STOP_PWM_CN, S_STOP_PWM_EN)
                    : Ui_Controller_Pick_CN_EN(S_START_PWM_CN, S_START_PWM_EN);
                break;
            case 1: text = Ui_Controller_Pick_CN_EN("2. " S_MONITOR_CN, S_MON_MENU_EN); break;
            case 2: text = Ui_Controller_Pick_CN_EN(S_WIFI_SETUP_CN, S_WIFI_SETUP_EN); break;
            case 3: text = Ui_Controller_Pick_CN_EN(S_SETTINGS_MENU_CN, S_SETTINGS_MENU_EN); break;
            case 4: text = Ui_Controller_Pick_CN_EN(S_FAULT_CLEAR_CN, S_FAULT_CLEAR_EN);
                    enabled = is_fault ? 1 : 0;
                    break;
            default: text = ""; break;
        }
        Ui_Controller_Erase_Line(2 + i);
        Ui_Controller_Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    Ui_Controller_Draw_Cursor(2 + s_menu_cursor);

    Ui_Controller_Erase_Line(7);

    s_last_is_running    = is_running;
    s_last_is_fault_menu = is_fault;
}

/* 主菜单光标移动：擦除旧星标并绘制新星标。 */
static void Ui_Controller_Main_Menu_Cursor_Update(uint8_t old_cursor)
{
    Ui_Controller_Erase_Cursor(2 + old_cursor);
    Ui_Controller_Draw_Cursor(2 + s_menu_cursor);
}

/* 主菜单每200ms检查一次，只在PWM或故障状态变化时更新。 */
static void Ui_Controller_Main_Menu_Dynamic_Update(void)
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
            ? Ui_Controller_Pick_CN_EN(S_STOP_PWM_CN, S_STOP_PWM_EN)
            : Ui_Controller_Pick_CN_EN(S_START_PWM_CN, S_START_PWM_EN);
        Ui_Controller_Draw_Menu_Text(2, 2, text, 1);
        if (s_menu_cursor == 0) Ui_Controller_Draw_Cursor(2);
        s_last_is_running = is_running;
    }

    if (is_fault != s_last_is_fault_menu) {
        const char* text = Ui_Controller_Pick_CN_EN(S_FAULT_CLEAR_CN, S_FAULT_CLEAR_EN);
        uint8_t enabled = is_fault ? 1 : 0;
        Ui_Controller_Draw_Menu_Text(5, 2, text, enabled);
        if (s_menu_cursor == 4) Ui_Controller_Draw_Cursor(5);
        s_last_is_fault_menu = is_fault;
    }
}

/* ================================================================
 *  监测子菜单整页绘制：5个选项，使用4行滚动窗口
 * ================================================================ */
static const char* Ui_Controller_Get_Sub_Item_Name(uint8_t idx)
{
    switch (idx) {
        case 0: return Ui_Controller_Pick_CN_EN(S_SUMMARY_CN, S_SUMMARY_EN);
        case 1: return Ui_Controller_Pick_CN_EN(S_MON_FREQ_CN, S_MON_FREQ_EN);
        case 2: return Ui_Controller_Pick_CN_EN(S_MON_VOLT_CN, S_MON_VOLT_EN);
        case 3: return Ui_Controller_Pick_CN_EN(S_MON_CURR_CN, S_MON_CURR_EN);
        case 4: return Ui_Controller_Pick_CN_EN(S_BACK_CN, S_BACK_EN);
        default: return "";
    }
}

static void Ui_Controller_Draw_Sub_Menu_Full(void)
{
    uint8_t visible_top = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t i, line;

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_MONITOR_CN, S_MONITOR_EN));       /* 第0行 */
    Ui_Controller_Draw_Divider(1);              /* 第1行 */

    for (line = 2; line <= 5; line++) {
        i = visible_top + (line - 2);
        if (i < 5) {
            char item_buf[22];
            snprintf(item_buf, sizeof(item_buf), "%d. %s", i + 1, Ui_Controller_Get_Sub_Item_Name(i));
            Ui_Controller_Draw_Menu_Text(line, 2, item_buf, 1);
        } else {
            Ui_Controller_Erase_Line(line);
        }
    }

    Ui_Controller_Draw_Cursor(2 + (s_menu_cursor - visible_top));

    Ui_Controller_Erase_Line(6);
    Ui_Controller_Erase_Line(7);

    s_last_sub_visible = visible_top;
}

/* 监测子菜单光标更新。 */
static void Ui_Controller_Sub_Menu_Cursor_Update(uint8_t old_cursor)
{
    uint8_t old_visible = s_last_sub_visible;
    uint8_t new_visible = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t old_line = 2 + (old_cursor - old_visible);
    uint8_t new_line = 2 + (s_menu_cursor - new_visible);

    if (new_visible != old_visible) {
        /* 可见窗口发生滚动时，重绘全部四行菜单。 */
        uint8_t i, line;
        Ui_Controller_Erase_Cursor(old_line);

        for (line = 2; line <= 5; line++) {
            i = new_visible + (line - 2);
            if (i < 5) {
                char item_buf[22];
                snprintf(item_buf, sizeof(item_buf), "%d. %s", i + 1, Ui_Controller_Get_Sub_Item_Name(i));
                Ui_Controller_Draw_Menu_Text(line, 2, item_buf, 1);
            } else {
                Ui_Controller_Erase_Line(line);
            }
        }
        Ui_Controller_Draw_Cursor(new_line);
    } else {
        /* 光标仍在同一窗口内时只更新新旧光标。 */
        Ui_Controller_Erase_Cursor(old_line);
        Ui_Controller_Draw_Cursor(new_line);
    }

    s_last_sub_visible = new_visible;
}

/* ==============================================================
 *  扫频页面，占用全部八行
 * ============================================================== */
static uint32_t Ui_Controller_Get_Sweep_Progress(uint32_t current_freq,
                                                  uint8_t is_stopped)
{
    uint32_t start_freq;
    uint32_t target_freq;

    if (is_stopped != 0U) return 0U;

    start_freq = Inverter_Control_Get_Sweep_Start_Freq();
    target_freq = Inverter_Control_Get_Sweep_Target_Freq();

    /* 扫频固定为降频；零跨度配置在触发后应直接显示为完成。 */
    if (start_freq <= target_freq) {
        return (current_freq <= target_freq) ? 10U : 0U;
    }
    if (current_freq >= start_freq) return 0U;
    if (current_freq <= target_freq) return 10U;

    return ((start_freq - current_freq) * 10U) /
           (start_freq - target_freq);
}

static void Ui_Controller_Draw_Sweep_Full(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_SWEEP_CN, S_SWEEP_EN));         /* 第0行 */
    Ui_Controller_Draw_Divider(1);              /* 第1行 */

    /* 第2行显示频率。 */
    snprintf(buf, sizeof(buf), "%sF:%3lu.%1lukHz", Ui_Controller_Pick_CN_EN(S_FREQ_CN, S_FREQ_EN),
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* 第3行显示扫频进度条。 */
    {
        uint32_t progress;
        progress = Ui_Controller_Get_Sweep_Progress(f, is_stopped);
        Tft_Driver_Erase_Pixel_Area(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8);
        if (!is_stopped) {
            Ui_Controller_Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                           14 * TFT_FONT_WIDTH, 8,
                           (float)progress, 0.0f, 10.0f, Ui_Controller_Get_Background_Color());
            snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
            if (buf[0]) Tft_Driver_Show_String(3, 8, buf, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
        } else {
            Tft_Driver_Show_CN_String(3, 5, Ui_Controller_Pick_CN_EN(S_PAUSE_CN, S_PAUSE_EN), Uc_Alarm(), Ui_Controller_Get_Background_Color());
        }
    }

    /* 第4行显示电压。 */
    Ui_Controller_Format_Voltage(buf, Adc_Driver_Get_Display_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, Ui_Controller_Get_Data_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* 第5行显示电流。 */
    Ui_Controller_Format_Current(buf, Adc_Driver_Get_Display_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, Ui_Controller_Get_Data_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    Ui_Controller_Erase_Line(6);
    Ui_Controller_Erase_Line(7);

    s_last_sweep_stopped = is_stopped;
}

/* 扫频页面每200ms执行一次增量更新。 */
static void Ui_Controller_Sweep_Dynamic_Update(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    /* 更新频率。 */
    snprintf(buf, sizeof(buf), "%sF:%3lu.%1lukHz", Ui_Controller_Pick_CN_EN(S_FREQ_CN, S_FREQ_EN),
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Ui_Controller_Erase_Line(2);
        Tft_Driver_Show_CN_String(2, 0, buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* 仅在扫频进度变化时重绘进度条，避免无效刷新造成闪烁。 */
    {
        static uint32_t s_last_progress = 0xFFFFFFFFU;
        static uint8_t  s_last_stopped  = 0xFF;
        uint32_t progress;
        uint8_t  draw = 0;

        if (!is_stopped) {
            progress = Ui_Controller_Get_Sweep_Progress(f, is_stopped);
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
                Ui_Controller_Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                               14 * TFT_FONT_WIDTH, 8,
                               (float)progress, 0.0f, 10.0f, Ui_Controller_Get_Background_Color());
                snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
                if (buf[0]) Tft_Driver_Show_String(3, 8, buf, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
            } else {
                Tft_Driver_Show_CN_String(3, 5, Ui_Controller_Pick_CN_EN(S_PAUSE_CN, S_PAUSE_EN), Uc_Alarm(), Ui_Controller_Get_Background_Color());
            }
        }
    }

    /* 更新电压。 */
    Ui_Controller_Format_Voltage(buf, Adc_Driver_Get_Display_Voltage());
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Ui_Controller_Erase_Line(4);
        Tft_Driver_Show_CN_String(4, 0, buf, Ui_Controller_Get_Data_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* 更新电流。 */
    Ui_Controller_Format_Current(buf, Adc_Driver_Get_Display_Current());
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Ui_Controller_Erase_Line(5);
        Tft_Driver_Show_CN_String(5, 0, buf, Ui_Controller_Get_Data_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* 更新底部操作提示。 */
    if (is_stopped != s_last_sweep_stopped) {
        Ui_Controller_Erase_Line(7);
        s_last_sweep_stopped = is_stopped;
    }
}

/* ==============================================================
 *  综合监测页面：第2行频率，第3行电压，第4行电流
 * ============================================================== */
static void Ui_Controller_Draw_Summary_Full(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Ui_Controller_Update_EMA();

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_SUMMARY_CN, S_SUMMARY_EN));       /* 第0行 */
    Ui_Controller_Draw_Divider(1);              /* 第1行 */

    /* 第2行显示频率。 */
    if (is_running) { Ui_Controller_Format_Frequency(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), "%sF:0.0kHz", Ui_Controller_Pick_CN_EN(S_FREQ_CN, S_FREQ_EN)); }
    Tft_Driver_Show_CN_String(2, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* 第3行显示电压。 */
    Ui_Controller_Format_Voltage(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* 第4行显示电流。 */
    Ui_Controller_Format_Current(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    /* 第5行留空。 */
    Ui_Controller_Erase_Line(5);

    Ui_Controller_Erase_Line(6);
    Ui_Controller_Erase_Line(7);

    s_last_is_running = is_running;
}

/* 综合监测页面每200ms执行一次增量更新。 */
static void Ui_Controller_Summary_Dynamic_Update(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Ui_Controller_Update_EMA();

    /* 更新频率。 */
    if (is_running) { Ui_Controller_Format_Frequency(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), "%sF:---.-kHz", Ui_Controller_Pick_CN_EN(S_FREQ_CN, S_FREQ_EN)); }
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Ui_Controller_Erase_Line(2);
        Tft_Driver_Show_CN_String(2, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* 更新电压。 */
    Ui_Controller_Format_Voltage(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Ui_Controller_Erase_Line(3);
        Tft_Driver_Show_CN_String(3, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* 更新电流。 */
    Ui_Controller_Format_Current(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Ui_Controller_Erase_Line(4);
        Tft_Driver_Show_CN_String(4, Ui_Controller_Center_Text(buf), buf, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* 更新底部操作提示。 */
    if (is_running != s_last_is_running) {
        Ui_Controller_Erase_Line(7);
        s_last_is_running = is_running;
    }
}

/* ==============================================================
 *  半圆仪表盘绘制：正弦查表、极坐标换算和线段绘制
 * ============================================================== */
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
    char     label;        /* 电压、电流或频率的单字节标记 */
} GaugeConfig;

/* 电压表盘范围0至50V，共50个小格，每格1V。 */
/* 大刻度: 0, 10, 20, 30, 40, 50 */
static const GaugeConfig GAUGE_V = {0.0f,  50.0f, 10.0f, 5.0f, 1.0f, 42.0f, 'V'};

/* 电流表盘范围0至2A，共20个小格，每格0.1A。 */
/* 大刻度: 0.0, 0.5, 1.0, 1.5, 2.0 */
static const GaugeConfig GAUGE_C = {0.0f,   2.0f,  0.5f, 0.25f, 0.1f,  1.8f, 'C'};

/* 频率表盘范围90至150kHz，共60个小格，每格1kHz。 */
/* 大刻度: 90, 100, 110, 120, 130, 140, 150 */
static const GaugeConfig GAUGE_F = {90.0f, 150.0f, 10.0f, 5.0f, 1.0f, 140.0f, 'F'};

static float s_last_val_v = -1.0f, s_last_val_c = -1.0f, s_last_val_f = -1.0f;
static const char* s_last_gauge_label = NULL;  /* 跨表盘标签缓存，切换页面时清除 */

/* 极坐标约定：0度在左，90度在上，180度在右。 */
#define G_CX   80
#define G_CY   84   /* 相比原位置下移18像素，使仪表盘比例更协调 */
static void Ui_Controller_Gauge_Polar(uint8_t a, uint16_t r, int16_t *px, int16_t *py)
{
    int16_t s, c;
    if (a > 180) a = 180;
    s = GAUGE_SIN[a];                             /* 查询当前角度正弦值 */
    if (a <= 90) c = GAUGE_SIN[90 - a];          /* 第一、二象限余弦换算 */
    else        c = -GAUGE_SIN[a - 90];           /* 第三、四象限余弦换算 */
    /* 横坐标取反，使0度位于左端、180度位于右端。 */
    *px = (int16_t)(G_CX - (int32_t)r * c / 10000);
    *py = (int16_t)(G_CY - (int32_t)r * s / 10000);
}

/* 使用整数增量算法绘制1像素宽线段。 */
static void Ui_Controller_Bres_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
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

/* 所有页面右上角共用的无线与消息连接图标。 */
static void Ui_Controller_Draw_TopRight_Icons(void)
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

    /* 绘制新帧前先清除16乘16图标槽，防止动画帧叠加残影。 */
    Tft_Driver_Fill_Rect(WX, 0, 16, 16, Ui_Controller_Get_Background_Color());
    Tft_Driver_Fill_Rect(MX, 0, 16, 16, Ui_Controller_Get_Background_Color());

    /* 横坐标128处绘制无线状态图标。 */
    if ((s_no_wifi_mode == 0U) && (offline == 0U) &&
        ((connecting != 0U) || (ready == 0U))) {
        wifi_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150U) % 6U;
    }
    if (connecting != 0U) {
        mqtt_frame = (uint8_t)(Sys_Timer_Get_Tick() / 200U) % 6U;
    }

    if (s_no_wifi_mode || offline) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_OFF, 0, Uc_Alarm(), Ui_Controller_Get_Background_Color());
    } else if (!ready) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_CONNECT_ANIM, wifi_frame, blue_grad[wifi_frame], Ui_Controller_Get_Background_Color());
    } else if (cs == APP_NETWORK_CONN_ONLINE) {
        int8_t r = App_Network_Get_RSSI();
        if (r >= -50) icon_frame=3; else if (r >= -60) icon_frame=2; else if (r >= -70) icon_frame=1; else icon_frame=0;
        rssi_frame = icon_frame;
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_SIGNAL, icon_frame, Uc_Ok(), Ui_Controller_Get_Background_Color());
    } else if (connecting) {
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_CONNECT_ANIM, wifi_frame, blue_grad[wifi_frame], Ui_Controller_Get_Background_Color());
    } else {  /* 空闲状态 */
        Tft_Driver_Draw_Icon_By_Id(WX, 0, ICON_ID_WIFI_REMOVE, 0, Uc_Alarm(), Ui_Controller_Get_Background_Color());
    }

    /* 横坐标144处绘制消息连接状态图标。 */
    if (cs == APP_NETWORK_CONN_ONLINE) {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_YES, 0, Uc_Ok(), Ui_Controller_Get_Background_Color());
    } else if (connecting) {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_ANIM, mqtt_frame, rainbow[mqtt_frame], Ui_Controller_Get_Background_Color());
    } else {
        Tft_Driver_Draw_Icon_By_Id(MX, 0, ICON_ID_MQTT_NO, 0, Uc_Alarm(), Ui_Controller_Get_Background_Color());
    }
    if (!Tft_Driver_Is_Draw_Blocked()) {
        s_icon_cache_valid = 1U;
        s_last_icon_cs = cs;
        s_last_icon_mode = s_no_wifi_mode;
        s_last_wifi_frame = wifi_frame;
        s_last_mqtt_frame = mqtt_frame;
        s_last_rssi_frame = rssi_frame;
        s_last_icon_page = (uint8_t)s_page;
        s_last_icon_bg = Ui_Controller_Get_Background_Color();
        s_last_icon_fg = Ui_Controller_Get_Text_Color();
    }
    #undef WX
    #undef MX
}

/* 整页重绘：能量弧、信息舱和页面标题。 */
static void Ui_Controller_Draw_Gauge_Full(const GaugeConfig* cfg, float val)
{
    #define R_TICK  56   /* 能量弧外半径 */
    #define R_BIG   50   /* 主刻度内半径，线长6像素 */
    #define R_FINE  53   /* 细刻度内半径，线长3像素 */
    #define CPS(x)  ((uint8_t)(x))
    uint16_t a, na;
    uint16_t slot_color = 0x18C3;  /* 未点亮刻度使用深灰色 */
    float v;
    char buf[32];

    if (val < cfg->range_min) val = cfg->range_min;
    if (val > cfg->range_max) val = cfg->range_max;

    /* 第一步：把当前数值换算为0至180度的能量弧终点。 */
    na = (uint16_t)((val - cfg->range_min) /
          (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    if (na > 180) na = 180;

    /* 第三步：绘制全部1像素刻度线。
     * 当前终点以内使用亮色，进入报警区后改用红色；
     * 当前终点以外使用深灰色，形成未点亮轨道。 */
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
                color = is_red ? Uc_Alarm() : Ui_Controller_Get_Text_Color();
            else
                color = Uc_Dim();
        } else {
            color = slot_color;
        }

        {
            int16_t xo, yo, xi, yi;
            Ui_Controller_Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
            Ui_Controller_Gauge_Polar(CPS(a), ir, &xi, &yi);
            Ui_Controller_Bres_Line(xi, yi, xo, yo, color);
        }
    }

    /* 第四步：在主刻度位置绘制5乘10微型数字标签。 */
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
            uint16_t color = (v >= cfg->red_start) ? Uc_Alarm() : Ui_Controller_Get_Text_Color();

            s = GAUGE_SIN[a];
            c = (a <= 90) ? GAUGE_SIN[90 - a] : -GAUGE_SIN[a - 90];

            Ui_Controller_Gauge_Polar(CPS(a), R_TICK, &x, &y);

            if (v == (float)((int)v))
                snprintf(nb, sizeof(nb), "%d", (int)v);
            else if (cfg->big_step < 1.0f)
                snprintf(nb, sizeof(nb), "%.1f", (double)v);
            else
                snprintf(nb, sizeof(nb), "%d", (int)v);

            len = (uint8_t)strlen(nb);
            w = len * 7 - 2;

            /* 标签从外圈刻度端点向外偏移，避免压住能量弧。 */
            draw_x = x - (int16_t)(w / 2) - (int32_t)(2 + w / 2) * c / 10000;
            draw_y = y - 5 - (int32_t)(2 + 5) * s / 10000;

            Tft_Driver_Show_5x10_String_Pixel((uint16_t)draw_x, (uint16_t)draw_y,
                                              nb, color, Ui_Controller_Get_Background_Color());
        }
    }

    /* 第五步：信息舱依次显示状态、数值和指标名称。 */
    {
        /* 第4行显示位于能量弧内部的状态缩写。 */
        {
            const char* status_text;
            uint16_t status_color;
            if (cfg->label == 'F') {
                Inverter_Control_Soft_Start_State st = Inverter_Control_Soft_Start_Get_State();
                if      (st == INVERTER_CONTROL_SS_STATE_SWEEP)
                    { status_text = "SWP"; status_color = Ui_Controller_Get_Value_Color(); }
                else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                    { status_text = "DON"; status_color = Uc_Ok(); }
                else
                    { status_text = "IDL"; status_color = Uc_Dim(); }
            } else {
                float thr_warn = (cfg->label == 'V') ? 36.0f : 1.2f;
                if (val >= cfg->red_start)
                    { status_text = "HI"; status_color = Uc_Alarm(); }
                else if (val >= thr_warn)
                    { status_text = "WRN"; status_color = Ui_Controller_Get_Value_Color(); }
                else
                    { status_text = "OK"; status_color = Uc_Ok(); }
            }
            Tft_Driver_Show_CN_String(4, Ui_Controller_Center_Text(status_text), status_text,
                                      status_color, Ui_Controller_Get_Background_Color());
            strncpy(s_gauge_status_buf, status_text, sizeof(s_gauge_status_buf));
            s_gauge_status_buf[sizeof(s_gauge_status_buf) - 1] = '\0';
        }

        /* 第5行按指标类型格式化并显示数值。 */
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
            Tft_Driver_Show_CN_String(5, Ui_Controller_Center_Text(buf), buf, num_color, Ui_Controller_Get_Background_Color());
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }

        /* 第6行居中显示指标名称和单位。 */
        if (cfg->label == 'F')
            Tft_Driver_Show_CN_String(6, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN)), Ui_Controller_Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN), Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        else if (cfg->label == 'V')
            Tft_Driver_Show_CN_String(6, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN)), Ui_Controller_Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN), Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        else
            Tft_Driver_Show_CN_String(6, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN)), Ui_Controller_Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN), Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
    }

    /* 第六步：仪表盘占满页面，仅补画右上角状态图标。 */
    Ui_Controller_Draw_TopRight_Icons();

    #undef R_TICK
    #undef R_BIG
    #undef R_FINE
    #undef CPS
}

/* 每200ms差分更新能量弧和信息舱。 */
static void Ui_Controller_Gauge_Dynamic_Update(const GaugeConfig* cfg, float val, float old_val)
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

    /* 第一步：只更新终点变化范围内的1像素能量弧刻度。 */
    if (oa != na) {
        float v;
        uint16_t a;
        if (na > oa) {
            /* 数值增加时点亮旧终点到新终点之间的刻度。 */
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
                        color = is_red ? Uc_Alarm() : Ui_Controller_Get_Text_Color();
                    else
                        color = Uc_Dim();
                    {
                        int16_t xo, yo, xi, yi;
                        Ui_Controller_Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
                        Ui_Controller_Gauge_Polar(CPS(a), ir, &xi, &yi);
                        Ui_Controller_Bres_Line(xi, yi, xo, yo, color);
                    }
                }
            }
        } else {
            /* 数值减小时把新终点到旧终点之间的刻度恢复为深灰色。 */
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
                        Ui_Controller_Gauge_Polar(CPS(a), R_TICK, &xo, &yo);
                        Ui_Controller_Gauge_Polar(CPS(a), ir, &xi, &yi);
                        Ui_Controller_Bres_Line(xi, yi, xo, yo, slot_color);
                    }
                }
            }
        }
    }

    /* 第二步：差分更新信息舱的状态、数值和指标名称。 */
    /* 第4行更新能量弧内部的状态缩写。 */
    {
        const char* status_text;
        uint16_t status_color;
        if (cfg->label == 'F') {
            Inverter_Control_Soft_Start_State st = Inverter_Control_Soft_Start_Get_State();
            if      (st == INVERTER_CONTROL_SS_STATE_SWEEP)
                { status_text = "SWP"; status_color = Ui_Controller_Get_Value_Color(); }
            else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                { status_text = "DON"; status_color = Uc_Ok(); }
            else
                { status_text = "IDL"; status_color = Uc_Dim(); }
        } else {
            float thr_warn = (cfg->label == 'V') ? 36.0f : 1.2f;
            if (val >= cfg->red_start)
                { status_text = "HI"; status_color = Uc_Alarm(); }
            else if (val >= thr_warn)
                { status_text = "WRN"; status_color = Ui_Controller_Get_Value_Color(); }
            else
                { status_text = "OK"; status_color = Uc_Ok(); }
        }

        if (strncmp(status_text, s_gauge_status_buf, sizeof(s_gauge_status_buf)) != 0) {
            Tft_Driver_Erase_Pixel_Area(24, 64, 112, 16);
            Tft_Driver_Show_CN_String(4, Ui_Controller_Center_Text(status_text), status_text,
                                      status_color, Ui_Controller_Get_Background_Color());
            strncpy(s_gauge_status_buf, status_text, sizeof(s_gauge_status_buf));
            s_gauge_status_buf[sizeof(s_gauge_status_buf) - 1] = '\0';
        }
    }

    /* 第5行更新数值；电流保留三位小数，其余保留两位。 */
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
            Tft_Driver_Show_CN_String(5, Ui_Controller_Center_Text(buf), buf, num_color, Ui_Controller_Get_Background_Color());
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }
    }

    /* 第6行更新指标名称和单位。 */
    {
        const char* label_text;
        if (cfg->label == 'F')      label_text = Ui_Controller_Pick_CN_EN(S_LABEL_FREQ_CN, S_LABEL_FREQ_EN);
        else if (cfg->label == 'V') label_text = Ui_Controller_Pick_CN_EN(S_LABEL_VOLT_CN, S_LABEL_VOLT_EN);
        else                        label_text = Ui_Controller_Pick_CN_EN(S_LABEL_CURR_CN, S_LABEL_CURR_EN);
        if (label_text != s_last_gauge_label) {
            s_last_gauge_label = label_text;
            Tft_Driver_Erase_Pixel_Area(24, 96, 112, 16);
            Tft_Driver_Show_CN_String(6, Ui_Controller_Center_Text(label_text), label_text, Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());
        }
    }

    #undef R_TICK
    #undef R_BIG
    #undef R_FINE
    #undef CPS
}

/* 六个轻量包装函数，用于保持既有页面绘制调用形式。 */
static void Ui_Controller_Draw_Freq_Full(void) {
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_DONE
                       || Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_SWEEP);
    float display_val;

    Ui_Controller_Update_EMA();
    /* PWM未运行时强制显示0，避免默认频率让停机仪表盘出现非零能量弧。 */
    display_val = is_running ? s_ema_f : 0.0f;
    Ui_Controller_Draw_Gauge_Full(&GAUGE_F, display_val);
    /* PWM停止时频率显示0，电压和电流仍保持实时监测。 */
    if (!is_running) {
        Tft_Driver_Erase_Pixel_Area(24, 80, 112, 16);
        Tft_Driver_Show_CN_String(5, Ui_Controller_Center_Text("0kHz"), "0kHz", Uc_Dim(), Ui_Controller_Get_Background_Color());
        strncpy(s_gauge_val_str, "0kHz", sizeof(s_gauge_val_str));
    }
    s_last_val_f = s_ema_f;
}
static void Ui_Controller_Freq_Dynamic_Update(void) {
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_DONE
                       || Inverter_Control_Soft_Start_Get_State()
                         == INVERTER_CONTROL_SS_STATE_SWEEP);
    float old = s_last_val_f;
    Ui_Controller_Update_EMA();
    if (is_running) {
        Ui_Controller_Gauge_Dynamic_Update(&GAUGE_F, s_ema_f, old);
        s_last_val_f = s_ema_f;
    } else {
        /* PWM停止时把能量弧归零，并用灰色显示数值0。 */
        Ui_Controller_Gauge_Dynamic_Update(&GAUGE_F, 0.0f, old);
        s_last_val_f = 0.0f;  /* 缓存必须记录屏幕实际显示值0，而不是内部频率值。 */
    }
}
static void Ui_Controller_Draw_Volt_Full(void) {
    Ui_Controller_Update_EMA(); Ui_Controller_Draw_Gauge_Full(&GAUGE_V, s_ema_v); s_last_val_v = s_ema_v;
}
static void Ui_Controller_Volt_Dynamic_Update(void) {
    float old = s_last_val_v; Ui_Controller_Update_EMA();
    Ui_Controller_Gauge_Dynamic_Update(&GAUGE_V, s_ema_v, old); s_last_val_v = s_ema_v;
}
static void Ui_Controller_Draw_Curr_Full(void) {
    Ui_Controller_Update_EMA(); Ui_Controller_Draw_Gauge_Full(&GAUGE_C, s_ema_i); s_last_val_c = s_ema_i;
}
static void Ui_Controller_Curr_Dynamic_Update(void) {
    float old = s_last_val_c; Ui_Controller_Update_EMA();
    Ui_Controller_Gauge_Dynamic_Update(&GAUGE_C, s_ema_i, old); s_last_val_c = s_ema_i;
}

/* ==============================================================
 *  无线设置页面，占用全部八行
 * ============================================================== */
static void Ui_Controller_Draw_WiFi_Full(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    const char* status_text;
    const char* hint_text;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_ONLINE_CN, S_WIFI_ONLINE_EN);
    else if (App_Network_Is_Connecting())
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_CONN_CN, S_WIFI_CONN_EN);
    else if (App_Network_Is_Offline())
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_OFFLINE_CN, S_WIFI_OFFLINE_EN);
    else  /* 空闲状态 */
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_IDLE_CN, S_WIFI_IDLE_EN);

    if (App_Network_Is_Offline()) {
        hint_text = Ui_Controller_Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
    } else {
        hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? Ui_Controller_Pick_CN_EN(S_DISCONNECT_CN, S_DISCONNECT_EN) : Ui_Controller_Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
    }

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_LAUNCH_CN, S_LAUNCH_EN));         /* 第0行 */
    Ui_Controller_Draw_Divider(1);               /* 第1行 */

    /* 第2行显示连接状态。 */
    {
        char buf[42];
        snprintf(buf, sizeof(buf), "%s: %s", Ui_Controller_Pick_CN_EN(S_WIFI_TITLE_CN, S_WIFI_TITLE_EN), status_text);
        Tft_Driver_Show_CN_String(2, 0, buf, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
        strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
        s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
    }

    /* 第3行和第4行留空。 */
    Ui_Controller_Erase_Line(3);
    Ui_Controller_Erase_Line(4);

    /* 第5行显示连接或断开操作提示。 */
    Tft_Driver_Show_CN_String(5, Ui_Controller_Right_Text(hint_text), hint_text, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
    /* 第6行显示长按清除配网信息提示。 */
    Tft_Driver_Show_CN_String(6, Ui_Controller_Right_Text(Ui_Controller_Pick_CN_EN(S_LONG_CLEAR_CN, S_LONG_CLEAR_EN)), Ui_Controller_Pick_CN_EN(S_LONG_CLEAR_CN, S_LONG_CLEAR_EN), Uc_Alarm(), Ui_Controller_Get_Background_Color());
    Ui_Controller_Erase_Line(7);

    s_last_wifi_cs = cs;
}

static void Ui_Controller_WiFi_Dynamic_Update(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    uint8_t retry = App_Network_Get_Retry_Count();
    const char* status_text;
    uint8_t need_hint_update = 0;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_ONLINE_CN, S_WIFI_ONLINE_EN);
    else if (App_Network_Is_Connecting())
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_CONN_CN, S_WIFI_CONN_EN);
    else if (App_Network_Is_Offline())
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_OFFLINE_CN, S_WIFI_OFFLINE_EN);
    else  /* 空闲状态 */
        status_text = Ui_Controller_Pick_CN_EN(S_WIFI_IDLE_CN, S_WIFI_IDLE_EN);

    if (cs != s_last_wifi_cs) {
        char buf[42];
        snprintf(buf, sizeof(buf), "%s: %s", Ui_Controller_Pick_CN_EN(S_WIFI_TITLE_CN, S_WIFI_TITLE_EN), status_text);
        if (strncmp(buf, s_last_status_buf, sizeof(s_last_status_buf)) != 0) {
            Ui_Controller_Erase_Line(2);
            Tft_Driver_Show_CN_String(2, 0, buf, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
            strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
            s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
        }
        need_hint_update = 1;
        s_last_wifi_cs = cs;
    }

    /* 仅在提示文字变化时擦除重绘，避免200ms周期刷新造成闪烁。 */
    if (need_hint_update) {
        static char s_last_hint[32] = "";
        const char* new_hint;
        if (App_Network_Is_Offline()) {
            new_hint = Ui_Controller_Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
        } else {
            new_hint = (cs == APP_NETWORK_CONN_ONLINE) ? Ui_Controller_Pick_CN_EN(S_DISCONNECT_CN, S_DISCONNECT_EN) : Ui_Controller_Pick_CN_EN(S_CONNECT_CN, S_CONNECT_EN);
        }
        if (strncmp(s_last_hint, new_hint, sizeof(s_last_hint)) != 0) {
            Ui_Controller_Erase_Line(3);
            Tft_Driver_Show_CN_String(5, Ui_Controller_Right_Text(new_hint), new_hint, Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
            strncpy(s_last_hint, new_hint, sizeof(s_last_hint));
            s_last_hint[sizeof(s_last_hint) - 1] = '\0';
        }
    }
}

/* ==============================================================
 *  故障页面，全部八行均为静态内容
 * ============================================================== */
static void Ui_Controller_Draw_Fault_Full(void)
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

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_FAULT_TITLE_CN, S_FAULT_TITLE_EN));     /* 第0行 */
    Ui_Controller_Draw_Divider(1);                /* 第1行 */

    Tft_Driver_Show_CN_String(2, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(fault_cn, fault_en)),
        Ui_Controller_Pick_CN_EN(fault_cn, fault_en), Uc_Alarm(), Ui_Controller_Get_Background_Color());      /* 第2行 */
    Tft_Driver_Show_CN_String(3, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(S_PWM_OFF_CN, S_PWM_OFF_EN)),
        Ui_Controller_Pick_CN_EN(S_PWM_OFF_CN, S_PWM_OFF_EN), Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());        /* 第3行 */

    Ui_Controller_Erase_Line(4);                  /* 第4行作为空白间隔。 */

    Tft_Driver_Show_CN_String(5, Ui_Controller_Center_Text(Ui_Controller_Pick_CN_EN(S_RESET_HINT_CN, S_RESET_HINT_EN)),
        Ui_Controller_Pick_CN_EN(S_RESET_HINT_CN, S_RESET_HINT_EN), Ui_Controller_Get_Value_Color(), Ui_Controller_Get_Background_Color());    /* 第5行 */

    Ui_Controller_Erase_Line(6);
    Ui_Controller_Erase_Line(7);
}

/* ================================================================
 *  指示灯状态更新
 * ================================================================ */
static void Ui_Controller_Update_Leds(void)
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
 *  按键事件分发
 * ================================================================ */
static void Ui_Controller_Handle_Keys_By_Page(Ui_Page page,
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

    /* 上移键：增加频率或向上移动光标。 */
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                /* 无故障时菜单在第0至3项循环，有故障时扩展到第4项。 */
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

    /* 下移键：降低频率或向下移动光标。 */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                /* 非故障状态不允许停留在灰色故障项，越界后回到第一项。 */
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

    /* 确定键：确认选项或控制PWM启停。 */
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
                                Ui_Controller_Reset_EMA();
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
                        s_setting_cursor = UI_SETTING_ITEM_LANGUAGE;
                        break;
                    default: break;  /* 第4项仅在故障状态下可用。 */
                }
                break;

            case UI_PAGE_MONITOR_SUB_MENU:
                switch (s_menu_cursor) {
                    case 0: s_page = UI_PAGE_MONITOR_SUMMARY; Ui_Controller_Reset_EMA(); break;
                    case 1: s_page = UI_PAGE_MONITOR_FREQ;    Ui_Controller_Reset_EMA(); break;
                    case 2: s_page = UI_PAGE_MONITOR_VOLT;    Ui_Controller_Reset_EMA(); break;
                    case 3: s_page = UI_PAGE_MONITOR_CURR;    Ui_Controller_Reset_EMA(); break;
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
                            Ui_Controller_Reset_EMA();
                        }
                    }
                }
                break;

            case UI_PAGE_WIFI_SETUP: {
                uint8_t cs = App_Network_Get_Connect_Status();
                if (cs == APP_NETWORK_CONN_ONLINE || App_Network_Is_Connecting()) {
                    App_Network_Manual_Disconnect(); s_no_wifi_mode = 1;  /* 在线状态切换为主动离线。 */
                } else if (cs == APP_NETWORK_CONN_OFFLINE_ACTIVE) {
                    s_no_wifi_mode = 0; App_Network_Manual_Connect();    /* 主动离线状态开始重连。 */
                } else if (cs == APP_NETWORK_CONN_OFFLINE_PASSIVE) {
                    s_no_wifi_mode = 0; App_Network_Resume_From_Offline(); /* 被动离线状态恢复连接嗅探。 */
                } else {
                    s_no_wifi_mode = 0; App_Network_Start_Connect();     /* 空闲状态启动完整连接流程。 */
                }
                break;
            }

            case UI_PAGE_FAULT:
                if (Sys_Core_Reset_Fault() == SYS_CONTROL_RESULT_OK) {
                    s_page = UI_PAGE_MAIN_MENU;
                    s_menu_cursor = 0;
                    s_was_fault_state = 0;
                    Ui_Controller_Reset_EMA();
                }
                break;

            default: break;
        }
    }

    /* 返回键双击时直接跳转主菜单。 */
    if (k1 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_page = UI_PAGE_MAIN_MENU;
        s_menu_cursor = 0;
        s_setting_cursor = 0;
        /* 双击和单击事件互斥，因此继续执行时不会再次进入单击分支。 */
    }
    /* 返回键单击时回到上一页面。
       设置子页通常由专用处理函数拦截，此处保留统一兜底行为。 */
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

/* ==============================================================
 *  设置页面
 * ============================================================== */

/* 设置菜单文字，字符间距选项替代旧字体大小选项。 */
static const char* Ui_Controller_Get_Menu_Setting_Text(uint8_t idx)
{
    switch (idx) {
        case UI_SETTING_ITEM_LANGUAGE: return Ui_Controller_Pick_CN_EN("1. \xe8\xaf\xad\xe8\xa8\x80", "1. Language");
        case UI_SETTING_ITEM_FREQUENCY: return Ui_Controller_Pick_CN_EN("2. \xe5\x90\xaf\xe5\x8a\xa8\xe9\xa2\x91\xe7\x8e\x87", "2. Startup Freq");
        case UI_SETTING_ITEM_SPACING: return Ui_Controller_Pick_CN_EN("3. \xe5\xad\x97\xe9\x97\xb4\xe8\xb7\x9d", "3. Spacing");
        case UI_SETTING_ITEM_ICONS: return Ui_Controller_Pick_CN_EN("4. \xe5\x85\x89\xe6\xa0\x87\xe5\x9b\xbe\xe6\xa0\x87", "4. Cursor Icon");
        case UI_SETTING_ITEM_COLOR: return Ui_Controller_Pick_CN_EN("5. \xe9\xa2\x9c\xe8\x89\xb2", "5. Color");
        default: return "";
    }
}

static void Ui_Controller_Save_Settings(void)
{
    App_Storage_Request_Save_Settings(s_language, 0U, 100U,
                                      s_letter_spacing, sc_preset,
                                      s_color_fg, s_color_bg,
                                      s_startup_low_freq_hz,
                                      s_startup_high_freq_hz,
                                      s_startup_freq_band,
                                      s_menu_cursor_icon);
    Inverter_Control_Configure_Startup(
        (s_startup_freq_band == APP_STORAGE_FREQ_BAND_LOW) ?
            INVERTER_CONTROL_STARTUP_LOW :
            INVERTER_CONTROL_STARTUP_HIGH,
        s_startup_low_freq_hz, s_startup_high_freq_hz);
    Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2U));
    s_settings_dirty = 0U;
    s_settings_saved_until_ms = Sys_Timer_Get_Tick() + UI_SETTINGS_SAVED_MS;
}

/* ==============================================================
 *  设置一：设置主菜单
 * ============================================================== */
static void Ui_Controller_Draw_Setting_Full(void)
{
    uint8_t i;
    uint8_t enabled;
    const char* text;

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_SETTINGS_CN, S_SETTINGS_EN));
    Ui_Controller_Draw_Divider(1);

    /* 第2至6行固定显示五项设置。 */
    for (i = 0U; i < UI_SETTING_ITEM_COUNT; i++) {
        text = Ui_Controller_Get_Menu_Setting_Text(i);
        enabled = 1U;
        Ui_Controller_Erase_Line(2 + i);
        Ui_Controller_Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    Ui_Controller_Draw_Cursor(2 + s_setting_cursor);
    Ui_Controller_Erase_Line(7);
    if ((uint32_t)(s_settings_saved_until_ms - Sys_Timer_Get_Tick()) < UI_SETTINGS_SAVED_MS) {
        Tft_Driver_Show_CN_String(7, 2,
            Ui_Controller_Pick_CN_EN("\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98", "Saved"),
            Uc_Ok(), Ui_Controller_Get_Background_Color());
    }
}

static void Ui_Controller_Handle_Setting_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                                 Key_Driver_Event k3, Key_Driver_Event k4)
{
    /* 返回主菜单前，如设置已变化则请求后台写入外部存储器。 */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (s_settings_dirty) {
            Ui_Controller_Save_Settings();
        }
        s_page = UI_PAGE_MAIN_MENU; s_menu_cursor = 3; s_page_drawn = 0; return;
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor == UI_SETTING_ITEM_LANGUAGE) s_setting_cursor = UI_SETTING_ITEM_COUNT - 1U;
        else s_setting_cursor--;
    }
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor >= UI_SETTING_ITEM_COUNT - 1U) s_setting_cursor = UI_SETTING_ITEM_LANGUAGE;
        else s_setting_cursor++;
    }
    if (k4 == KEY_DRIVER_EVENT_CLICK) {
        switch (s_setting_cursor) {
            case UI_SETTING_ITEM_LANGUAGE: s_page = UI_PAGE_SETTING_LANG;
                    s_preview_choice = s_language;  /* 用已保存语言初始化预览光标。 */
                    break;
            case UI_SETTING_ITEM_FREQUENCY: s_page = UI_PAGE_SETTING_FREQUENCY;
                    s_frequency_editing = 0U;
                    s_frequency_edit_band = s_startup_freq_band;
                    break;
            case UI_SETTING_ITEM_SPACING: s_page = UI_PAGE_SETTING_SPACING;
                    s_preview_choice = s_letter_spacing;
                    break;
            case UI_SETTING_ITEM_ICONS: s_page = UI_PAGE_SETTING_ICONS;
                    s_icon_cursor = s_menu_cursor_icon; break;
            case UI_SETTING_ITEM_COLOR: s_page = UI_PAGE_SETTING_COLOR;
                    s_preview_choice = sc_preset;  /* 用已保存配色初始化预览光标。 */
                    break;
        }
    }
}

/* ==============================================================
 *  设置二：语言选择，先预览再确认
 *  上下键移动预览光标，底部显示即时示例；
 *  确定键保存选择，返回键取消并恢复原值。
 * ============================================================== */
static void Ui_Controller_Draw_Lang_Full(void)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_TITLE_LANG_CN, S_TITLE_LANG_EN));

    Ui_Controller_Erase_Line(3);
    Tft_Driver_Show_CN_String(3, 2,
        Ui_Controller_Pick_CN_EN("\xe4\xb8\xad\xe6\x96\x87", "Chinese"),
        (s_preview_choice == 0 && flash_ok) ? Ui_Controller_Get_Value_Color() : Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
    Ui_Controller_Erase_Line(4);
    Tft_Driver_Show_CN_String(4, 2,
        Ui_Controller_Pick_CN_EN("\xe8\x8b\xb1\xe6\x96\x87", "English"),
        (s_preview_choice == 1 || !flash_ok) ? Ui_Controller_Get_Value_Color() : Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());

    Ui_Controller_Draw_Cursor(s_preview_choice == 0 ? 3 : 4);

    /* 底部区域显示即时语言预览和外部字库诊断信息。 */
    Ui_Controller_Erase_Line(6);
    if (flash_ok)
        Tft_Driver_Show_String(6, 0, " Flash:OK  Chinese OK", Uc_Dim(), Ui_Controller_Get_Background_Color());
    else
        Tft_Driver_Show_String(6, 0, " Flash:--  EN only", 0xE8E4U, Ui_Controller_Get_Background_Color());
    Ui_Controller_Erase_Line(7);
    if (s_preview_choice == 0 && flash_ok)
        Tft_Driver_Show_CN_String(7, Ui_Controller_Center_Text("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"), "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c", Uc_Dim(), Ui_Controller_Get_Background_Color());
    else
        Tft_Driver_Show_String(7, Ui_Controller_Center_Text("Hello World"), "Hello World", Uc_Dim(), Ui_Controller_Get_Background_Color());
}

static void Ui_Controller_Handle_Lang_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                              Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t up   = (k2 == KEY_DRIVER_EVENT_CLICK);
    uint8_t down = (k3 == KEY_DRIVER_EVENT_CLICK);
    uint8_t back = (k1 == KEY_DRIVER_EVENT_CLICK);
    uint8_t ok   = (k4 == KEY_DRIVER_EVENT_CLICK);

    /* 返回键取消预览，恢复原值且不保存。 */
    if (back) {
        s_preview_choice = s_language;
        s_page = UI_PAGE_SETTING; s_setting_cursor = UI_SETTING_ITEM_LANGUAGE; s_page_drawn = 0; return;
    }

    /* 上下键循环移动预览光标。 */
    if (up)   { s_preview_choice = (s_preview_choice == 0) ? 1 : 0; s_page_drawn = 0; }
    if (down) { s_preview_choice = (s_preview_choice == 0) ? 1 : 0; s_page_drawn = 0; }

    /* 确定键提交当前预览选项。 */
    if (ok && s_preview_choice != s_language) {
        s_language  = s_preview_choice;
        s_settings_dirty = 1;
        s_page_drawn = 0;
    }
}

/* ==============================================================
 *  设置三：双档启动频率。总览只选择档位，进入编辑后使用副本，
 *  返回键丢弃副本，确认键才提交到持久化配置和下一轮软启动配置。
 * ============================================================== */
static void Ui_Controller_Draw_Frequency_Full(void)
{
    char line_buf[24];
    uint8_t low_selected = (s_frequency_edit_band == APP_STORAGE_FREQ_BAND_LOW);

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN("\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa2\x91\xe7\x8e\x87", "Startup Freq"));
    if (s_frequency_editing != 0U) {
        snprintf(line_buf, sizeof(line_buf), "%s %s",
                 Ui_Controller_Pick_CN_EN("编辑", "Edit"),
                 low_selected ? Ui_Controller_Pick_CN_EN("低档", "Low") : Ui_Controller_Pick_CN_EN("高档", "High"));
        Ui_Controller_Draw_Menu_Text(2, 2, line_buf, 1U);
        if (low_selected) {
            snprintf(line_buf, sizeof(line_buf), "%.1fkHz",
                     (float)s_frequency_edit_value / 1000.0f);
        } else {
            snprintf(line_buf, sizeof(line_buf), "%lukHz",
                     (unsigned long)(s_frequency_edit_value / 1000U));
        }
        Ui_Controller_Draw_Menu_Text(4, 2, line_buf, 1U);
        Ui_Controller_Draw_Menu_Text(6, 2,
            Ui_Controller_Pick_CN_EN("返回取消  确定保存", "Back Cancel  OK Save"), 1U);
        Ui_Controller_Draw_Menu_Cursor(4U, 1U);
        return;
    }

    snprintf(line_buf, sizeof(line_buf), "Low  %.1fkHz",
             (float)s_startup_low_freq_hz / 1000.0f);
    Ui_Controller_Draw_Menu_Text(2, 2, line_buf, 1U);
    snprintf(line_buf, sizeof(line_buf), "High %lukHz",
             (unsigned long)(s_startup_high_freq_hz / 1000U));
    Ui_Controller_Draw_Menu_Text(3, 2, line_buf, 1U);
    Ui_Controller_Draw_Menu_Text(5, 2,
        low_selected ? "Active: Low" : "Active: High", 1U);
    if ((uint32_t)(s_settings_saved_until_ms - Sys_Timer_Get_Tick()) < UI_SETTINGS_SAVED_MS) {
        Ui_Controller_Draw_Menu_Text(6, 2,
            Ui_Controller_Pick_CN_EN("\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98", "Saved"), 1U);
    }
    Ui_Controller_Draw_Menu_Text(7, 2,
        Ui_Controller_Pick_CN_EN("确定编辑", "OK Edit"), 1U);
    Ui_Controller_Draw_Menu_Cursor(low_selected ? 2U : 3U, 1U);
}

static void Ui_Controller_Handle_Frequency_Keys(Key_Driver_Event k1,
    Key_Driver_Event k2, Key_Driver_Event k3, Key_Driver_Event k4)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (s_frequency_editing != 0U) {
            s_frequency_editing = 0U;
        }
        s_page = UI_PAGE_SETTING;
        s_setting_cursor = UI_SETTING_ITEM_FREQUENCY;
        s_page_drawn = 0U;
        return;
    }

    if (s_frequency_editing == 0U) {
        if ((k2 == KEY_DRIVER_EVENT_CLICK) || (k3 == KEY_DRIVER_EVENT_CLICK)) {
            s_frequency_edit_band = (s_frequency_edit_band == APP_STORAGE_FREQ_BAND_LOW) ?
                APP_STORAGE_FREQ_BAND_HIGH : APP_STORAGE_FREQ_BAND_LOW;
            s_page_drawn = 0U;
        }
        if (k4 == KEY_DRIVER_EVENT_CLICK) {
            s_frequency_edit_value = (s_frequency_edit_band == APP_STORAGE_FREQ_BAND_LOW) ?
                s_startup_low_freq_hz : s_startup_high_freq_hz;
            s_frequency_editing = 1U;
            s_page_drawn = 0U;
        }
        return;
    }

    if (s_frequency_edit_band == APP_STORAGE_FREQ_BAND_LOW) {
        if (k2 == KEY_DRIVER_EVENT_CLICK &&
            s_frequency_edit_value <= UI_FREQUENCY_LOW_MAX_HZ - UI_FREQUENCY_LOW_STEP_HZ) {
            s_frequency_edit_value += UI_FREQUENCY_LOW_STEP_HZ;
        }
        if (k3 == KEY_DRIVER_EVENT_CLICK &&
            s_frequency_edit_value >= UI_FREQUENCY_LOW_MIN_HZ + UI_FREQUENCY_LOW_STEP_HZ) {
            s_frequency_edit_value -= UI_FREQUENCY_LOW_STEP_HZ;
        }
    } else {
        if (k2 == KEY_DRIVER_EVENT_CLICK &&
            s_frequency_edit_value <= UI_FREQUENCY_HIGH_MAX_HZ - UI_FREQUENCY_HIGH_STEP_HZ) {
            s_frequency_edit_value += UI_FREQUENCY_HIGH_STEP_HZ;
        }
        if (k3 == KEY_DRIVER_EVENT_CLICK &&
            s_frequency_edit_value >= UI_FREQUENCY_HIGH_MIN_HZ + UI_FREQUENCY_HIGH_STEP_HZ) {
            s_frequency_edit_value -= UI_FREQUENCY_HIGH_STEP_HZ;
        }
    }
    if ((k2 == KEY_DRIVER_EVENT_CLICK) || (k3 == KEY_DRIVER_EVENT_CLICK)) {
        s_page_drawn = 0U;
    }
    if (k4 == KEY_DRIVER_EVENT_CLICK) {
        if (s_frequency_edit_band == APP_STORAGE_FREQ_BAND_LOW) {
            s_startup_low_freq_hz = s_frequency_edit_value;
        } else {
            s_startup_high_freq_hz = s_frequency_edit_value;
        }
        s_startup_freq_band = s_frequency_edit_band;
        s_settings_dirty = 1U;
        Ui_Controller_Save_Settings();
        s_frequency_editing = 0U;
        s_page_drawn = 0U;
    }
}

/* ==============================================================
 *  设置四：八项前导光标图标选择。
 * ============================================================== */
/* 八项候选图标的中英文名称表。 */
static const char* Ui_Controller_Get_Icon_Name(uint8_t icon_id)
{
    switch (icon_id) {
        case 0:  return Ui_Controller_Pick_CN_EN("WIFI_SIG",    "WIFI_SIG");
        case 1:  return Ui_Controller_Pick_CN_EN("WIFI_CONN",   "WIFI_CONN");
        case 2:  return Ui_Controller_Pick_CN_EN("WIFI_OFF",    "WIFI_OFF");
        case 3:  return Ui_Controller_Pick_CN_EN("WIFI_RMV",    "WIFI_RMV");
        case 4:  return Ui_Controller_Pick_CN_EN("MQTT",        "MQTT");
        case 5:  return Ui_Controller_Pick_CN_EN("MQTT_YES",    "MQTT_YES");
        case 6:  return Ui_Controller_Pick_CN_EN("MQTT_NO",     "MQTT_NO");
        case 7:  return Ui_Controller_Pick_CN_EN("MQTT_ANIM",   "MQTT_ANIM");
        case 8:  return Ui_Controller_Pick_CN_EN("\xe6\x98\x9f\xe6\xa0\x87",  "STAR");
        case 9:  return Ui_Controller_Pick_CN_EN("\xe5\x85\x89\xe6\xa0\x87\xe5\x8a\xa8", "STAR_CUR");
        case 10: return Ui_Controller_Pick_CN_EN("\xe7\x81\xab\xe7\xae\xad",  "ROCKET");
        case 11: return Ui_Controller_Pick_CN_EN("\xe7\x94\xb5\xe6\xb1\xa0",  "BATTERY");
        case 12: return Ui_Controller_Pick_CN_EN("\xe8\xad\xa6\xe5\x91\x8a",  "WARNING");
        case 13: return Ui_Controller_Pick_CN_EN("\xe5\x8b\xbe",    "CHECK");
        case 14: return Ui_Controller_Pick_CN_EN("\xe5\x8f\x89",    "CROSS");
        case 15: return Ui_Controller_Pick_CN_EN("\xe7\x94\xb5\xe6\xba\x90",  "POWER");
        case 16: return Ui_Controller_Pick_CN_EN("\xe9\x97\xaa\xe7\x94\xb5",  "LIGHTNING");
        case 17: return Ui_Controller_Pick_CN_EN("\xe6\xb8\xa9\xe5\xba\xa6",  "TEMP");
        case 18: return Ui_Controller_Pick_CN_EN("\xe9\xa3\x8e\xe6\x89\x87",  "FAN");
        case 19: return Ui_Controller_Pick_CN_EN("\xe9\x94\x81",    "LOCK");
        case 20: return Ui_Controller_Pick_CN_EN("\xe4\xb8\xbb\xe9\xa1\xb5",  "HOME");
        case 21: return Ui_Controller_Pick_CN_EN("\xe8\xae\xbe\xe7\xbd\xae",  "GEAR");
        case 22: return Ui_Controller_Pick_CN_EN("\xe5\x88\xb7\xe6\x96\xb0",  "REFRESH");
        case 23: return Ui_Controller_Pick_CN_EN("\xe4\xb8\x8a\xe7\xae\xad",  "ARROW_UP");
        case 24: return Ui_Controller_Pick_CN_EN("\xe4\xb8\x8b\xe7\xae\xad",  "ARROW_DN");
        case 25: return Ui_Controller_Pick_CN_EN("\xe5\xb7\xa6\xe7\xae\xad",  "ARROW_LT");
        case 26: return Ui_Controller_Pick_CN_EN("\xe5\x8f\xb3\xe7\xae\xad",  "ARROW_RT");
        case 27: return Ui_Controller_Pick_CN_EN("\xe4\xbf\xa1\xe5\x8f\xb7",  "SIGNAL");
        case 28: return Ui_Controller_Pick_CN_EN("\xe5\x85\xa8\xe7\x90\x83",  "GLOBE");
        case 29: return Ui_Controller_Pick_CN_EN("\xe5\x9b\xbe\xe8\xa1\xa8",  "CHART");
        case 30: return Ui_Controller_Pick_CN_EN("\xe6\x97\xb6\xe9\x92\x9f",  "CLOCK");
        case 31: return Ui_Controller_Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "1", "EXTRA1");
        case 32: return Ui_Controller_Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "2", "EXTRA2");
        case 33: return Ui_Controller_Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "3", "EXTRA3");
        case 34: return Ui_Controller_Pick_CN_EN("\xe6\x89\xa9\xe5\xb1\x95" "4", "EXTRA4");
        default: return "?";
    }
}

static void Ui_Controller_Draw_Icons_Full(void)
{
    uint8_t i;
    uint8_t icon_id;
    uint16_t x;
    uint16_t y;

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN("\xe5\x85\x89\xe6\xa0\x87\xe5\x9b\xbe\xe6\xa0\x87", "Cursor Icon"));
    for (i = 0U; i < UI_CURSOR_ICON_COUNT; i++) {
        x = (uint16_t)(24U + (uint16_t)(i % 4U) * 32U);
        y = (uint16_t)(28U + (uint16_t)(i / 4U) * 36U);
        icon_id = s_cursor_icon_ids[i];
        if (i == s_icon_cursor) {
            Tft_Driver_Fill_Rect(x - 2U, y - 2U, 20U, 20U,
                                 Ui_Controller_Get_Value_Color());
        }
        if (Tft_Driver_Draw_Icon_By_Id(x, y, icon_id, 0U,
            Ui_Controller_Get_Text_Color(),
            (i == s_icon_cursor) ? Ui_Controller_Get_Value_Color() :
                Ui_Controller_Get_Background_Color()) == 0U) {
            Tft_Driver_Draw_Icon_By_Id(x, y, ICON_ID_STAR, 0U,
                Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
        }
    }
    Ui_Controller_Draw_Menu_Text(6, 2,
        Ui_Controller_Get_Icon_Name(s_cursor_icon_ids[s_icon_cursor]), 1U);
    Ui_Controller_Draw_Menu_Text(7, 2,
        Ui_Controller_Pick_CN_EN("确定保存光标", "OK Save Cursor"), 1U);
}

static void Ui_Controller_Handle_Icons_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                               Key_Driver_Event k3, Key_Driver_Event k4)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        s_page = UI_PAGE_SETTING;
        s_setting_cursor = UI_SETTING_ITEM_ICONS;
        s_page_drawn = 0U;
        return;
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        s_icon_cursor = (s_icon_cursor == 0U) ? UI_CURSOR_ICON_COUNT - 1U :
            s_icon_cursor - 1U;
        s_page_drawn = 0U;
    }
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        s_icon_cursor = (s_icon_cursor >= UI_CURSOR_ICON_COUNT - 1U) ? 0U :
            s_icon_cursor + 1U;
        s_page_drawn = 0U;
    }
    if (k4 == KEY_DRIVER_EVENT_CLICK) {
        s_menu_cursor_icon = s_icon_cursor;
        s_settings_dirty = 1U;
        Ui_Controller_Save_Settings();
        s_page = UI_PAGE_SETTING;
        s_setting_cursor = UI_SETTING_ITEM_ICONS;
        s_page_drawn = 0U;
    }
}

/* ==============================================================
 *  设置五：字符间距，替代旧字体大小选项
 *  上下键在0至3之间移动预览光标，底部即时显示间距效果；
 *  确定键保存，返回键取消。
 * ============================================================== */
#define S_SPACING_TITLE_CN "\xe5\xad\x97\xe9\x97\xb4\xe8\xb7\x9d"
#define S_SPACING_TITLE_EN "Spacing"

    /* 标签本身带有不同间距，直接展示预览效果。
     * 字符串均使用已验证的UTF-8编码，兼容ARMCC V5。 */
static const char* Ui_Controller_Get_Spacing_Label(uint8_t v)
{
    switch (v) {
        case 0: return Ui_Controller_Pick_CN_EN("  \xe6\x97\xa0 (0px)", "  None (0)");
        case 1: return Ui_Controller_Pick_CN_EN("  \xe5\xb0\x8f (2px)", "  Small (2)");
        case 2: return Ui_Controller_Pick_CN_EN("  \xe4\xb8\xad (4px)", "  Medium (4)");
        case 3: return Ui_Controller_Pick_CN_EN("  \xe5\xa4\xa7 (6px)", "  Large (6)");
        default: return "";
    }
}

static void Ui_Controller_Draw_Spacing_Full(void)
{
    uint8_t i;
    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_SPACING_TITLE_CN, S_SPACING_TITLE_EN));

    for (i = 0; i < 4; i++) {
        Ui_Controller_Erase_Line(3 + i);
        /* 仅在绘制标签期间临时应用预览间距，绘制后立即恢复。 */
        {
            uint8_t saved = s_letter_spacing;
            Tft_Driver_Set_Letter_Spacing((uint8_t)(i * 2));
            Tft_Driver_Show_CN_String(3 + i, 2,
                Ui_Controller_Get_Spacing_Label(i),
                (s_preview_choice == i) ? Ui_Controller_Get_Value_Color() : Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
            Tft_Driver_Set_Letter_Spacing((uint8_t)(saved * 2));
        }
    }

    Ui_Controller_Draw_Cursor(3 + s_preview_choice);

    /* 第7行用示例文字即时展示字符间距。 */
    Ui_Controller_Erase_Line(7);
    {
        char preview_buf[21] = "";
        /* 运行时选择示例字符串，避免ARMCC对多字节字符串发出误报。 */
        snprintf(preview_buf, 21, "%s", Ui_Controller_Pick_CN_EN("Aa\xe4\xb8\xad\xe6\x96\x87\xe6\xa8\xa1\xe5\xbc\x8f",
                                                    "Aa Zh Demo"));
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_preview_choice * 2));
        Tft_Driver_Show_CN_String(7, Ui_Controller_Center_Text(preview_buf), preview_buf, Uc_Dim(), Ui_Controller_Get_Background_Color());
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));  /* 恢复已保存的实际间距。 */
    }
}

static void Ui_Controller_Handle_Spacing_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                                 Key_Driver_Event k3, Key_Driver_Event k4)
{
    uint8_t back = (k1 == KEY_DRIVER_EVENT_CLICK);
    uint8_t ok   = (k4 == KEY_DRIVER_EVENT_CLICK);
    uint8_t up   = (k2 == KEY_DRIVER_EVENT_CLICK);
    uint8_t down = (k3 == KEY_DRIVER_EVENT_CLICK);

    if (back) {
        s_preview_choice = s_letter_spacing;
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));
        s_page = UI_PAGE_SETTING; s_setting_cursor = UI_SETTING_ITEM_SPACING; s_page_drawn = 0; return;
    }

    /* 上下键在0至3之间循环移动预览光标。 */
    if (up)   { s_preview_choice = (s_preview_choice == 0) ? 3 : s_preview_choice - 1; s_page_drawn = 0; }
    if (down) { s_preview_choice = (s_preview_choice >= 3) ? 0 : s_preview_choice + 1; s_page_drawn = 0; }

    /* 确定键提交选项，并换算为实际像素间距。 */
    if (ok && s_preview_choice != s_letter_spacing) {
        s_letter_spacing = s_preview_choice;
        /* 四个选项分别映射为0、2、4、6像素。 */
        Tft_Driver_Set_Letter_Spacing((uint8_t)(s_preview_choice * 2));
        s_settings_dirty = 1;
        s_page_drawn = 0;
    }
}

/* ==============================================================
 *  设置六：配色方案，先预览再确认
 *  上下键移动预览光标，底部显示三种颜色样块；
 *  确定键保存并触发整页重绘，返回键取消并恢复原方案。
 * ============================================================== */
static void Ui_Controller_Apply_Color_Preset(uint8_t preset_idx)
{
    const ColorPreset* p = &COLOR_PRESETS[preset_idx];
    s_color_fg      = p->fg;
    s_color_bg      = p->bg;
    s_color_accent  = p->accent;
    sc_preset = preset_idx;
}
static void Ui_Controller_Draw_Color_Full(void)
{
    uint8_t i;

    Ui_Controller_Draw_Header(Ui_Controller_Pick_CN_EN(S_TITLE_COLOR_CN, S_TITLE_COLOR_EN));

    for (i = 0; i < 6; i++) {
        char buf[24];
        const char* name = Ui_Controller_Pick_CN_EN(COLOR_PRESETS[i].name_cn, COLOR_PRESETS[i].name_en);
        Ui_Controller_Erase_Line(2 + i);
        /* 已生效方案保留勾号，行选择由统一前导光标表示。 */
        {
            const char* prefix = (sc_preset == i) ? "\xe2\x9c\x93 " : "  ";
            snprintf(buf, 24, "%s%s", prefix, name);
            Tft_Driver_Show_CN_String(2 + i, 2, buf,
                (s_preview_choice == i) ? Ui_Controller_Get_Value_Color() : Ui_Controller_Get_Text_Color(), Ui_Controller_Get_Background_Color());
        }
    }

    Ui_Controller_Draw_Cursor(2 + s_preview_choice);

    /* 第7行使用当前预览方案绘制三色预览条。 */
    Ui_Controller_Erase_Line(7);
    {
        const ColorPreset* p = &COLOR_PRESETS[s_preview_choice];
        uint16_t bar_y = 7 * TFT_FONT_HEIGHT;
        uint16_t bw = 53;
        /* 三个等宽色块依次表示背景色、前景色和强调色。 */
        Tft_Driver_Fill_Rect(0,  bar_y, bw, TFT_FONT_HEIGHT, p->bg);
        Tft_Driver_Fill_Rect(bw, bar_y, bw, TFT_FONT_HEIGHT, p->fg);
        Tft_Driver_Fill_Rect((uint16_t)(bw * 2), bar_y, bw, TFT_FONT_HEIGHT, p->accent);
        /* 为三个色块绘制简短标签。 */
        {
            uint16_t bg_label_fg = (p->bg == 0x0000 || p->bg < 0x2104) ? Ui_Controller_Get_Text_Color() : TFT_COLOR_BLACK;
            Tft_Driver_Show_String(7, 0,  "B", bg_label_fg, p->bg);
            Tft_Driver_Show_String(7, 7,  "F", (p->fg < 0x8410) ? Ui_Controller_Get_Text_Color() : TFT_COLOR_BLACK, p->fg);
            Tft_Driver_Show_String(7, 14, "A", (p->accent < 0x8410) ? Ui_Controller_Get_Text_Color() : TFT_COLOR_BLACK, p->accent);
        }
    }

}

static void Ui_Controller_Handle_Color_Keys(Key_Driver_Event k1, Key_Driver_Event k2,
                               Key_Driver_Event k3, Key_Driver_Event k4)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        s_preview_choice = sc_preset;  /* 恢复已保存方案。 */
        s_page = UI_PAGE_SETTING; s_setting_cursor = UI_SETTING_ITEM_COLOR; s_page_drawn = 0; return;
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

    /* 确定键应用新配色，并让第七阶段执行一次整页重绘。 */
    if (k4 == KEY_DRIVER_EVENT_CLICK && s_preview_choice != sc_preset) {
        Ui_Controller_Apply_Color_Preset(s_preview_choice);
        s_settings_dirty = 1;
        s_page_drawn = 0;  /* 标记页面失效，使用新背景色整页重绘。 */
    }
}

/* ==============================================================
 *  设置页面按键分发
 * ============================================================== */
static uint8_t Ui_Controller_Handle_Settings_Keys(Ui_Page page,
    Key_Driver_Event k1, Key_Driver_Event k2,
    Key_Driver_Event k3, Key_Driver_Event k4)
{
    if (k1 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_frequency_editing = 0U;
        if (s_settings_dirty) {
            Ui_Controller_Save_Settings();
        }
        s_page = UI_PAGE_MAIN_MENU;
        s_menu_cursor = 3U;
        s_setting_cursor = UI_SETTING_ITEM_LANGUAGE;
        s_page_drawn = 0U;
        return 1U;
    }
    switch (page) {
        case UI_PAGE_SETTING:           Ui_Controller_Handle_Setting_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_LANG:      Ui_Controller_Handle_Lang_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_FREQUENCY: Ui_Controller_Handle_Frequency_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_SPACING:   Ui_Controller_Handle_Spacing_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_ICONS:     Ui_Controller_Handle_Icons_Keys(k1,k2,k3,k4); return 1;
        case UI_PAGE_SETTING_COLOR:     Ui_Controller_Handle_Color_Keys(k1,k2,k3,k4); return 1;
        default: return 0;
    }
}

/* ================================================================
 *  主调度器，采用分阶段增量刷新架构
 *
 *  第零阶段：自动同步右上角连接状态图标。
 *  第一阶段：检测故障跳变，必要时切换故障页面。
 *  第二阶段：检测扫频完成，必要时跳转综合监测页面。
 *  第三阶段：读取并分发按键事件。
 *  第四阶段：检测页面切换并清除全部刷新缓存。
 *  第五阶段：每200ms增量更新动态数值。
 *  第六阶段：钳位页面光标，防止越界。
 *  第七阶段：按页面失效标志执行整页或局部绘制。
 * ================================================================ */
void Ui_Controller_Task(
    const Key_Driver_Event events[KEY_DRIVER_COUNT])
{
    static uint32_t s_last_ui_ms = 0;
    uint8_t old_cursor;
    uint8_t cursor_changed = 0;
    uint8_t tick_200ms = 0;

    Tft_Driver_Begin_Draw_Cycle();

    /* 第零阶段：持续同步右上角连接状态图标。 */
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
            /* 网络状态、无线开关、动画帧或页面任一变化时刷新图标。 */
            if ((s_icon_cache_valid == 0U) ||
                cs != s_last_icon_cs ||
                s_no_wifi_mode != s_last_icon_mode ||
                wifi_frame != s_last_wifi_frame ||
                mqtt_frame != s_last_mqtt_frame ||
                rssi_frame != s_last_rssi_frame ||
                (uint8_t)s_page != s_last_icon_page ||
                Ui_Controller_Get_Background_Color() != s_last_icon_bg ||
                Ui_Controller_Get_Text_Color() != s_last_icon_fg) {

                /* 直接局部更新图标，不重复绘制页面标题。 */
                Ui_Controller_Draw_TopRight_Icons();
            }
        } else {
            /* 页面跳转瞬间，强制失效状态，以便进入新页后图标能立刻跟进刷新 */
            s_icon_cache_valid = 0U;
        }
    }

    /* 第一阶段：检测系统故障跳变。 */
    {
        uint8_t sys_fault = (Sys_Core_Get_State() == SYS_STATE_FAULT);
        if (sys_fault != s_was_fault_state) {
            s_was_fault_state = sys_fault;
            s_last_is_fault_menu = 0xFF;  /* 强制主菜单重新判断故障项。 */
        }
    }

    /* 第二阶段：扫频完成后自动进入综合监测页面。 */
    if (s_page == UI_PAGE_SWEEP) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
            s_page = UI_PAGE_MONITOR_SUMMARY;
            s_page_drawn = 0;
            Ui_Controller_Reset_EMA();
            s_user_target_synced = 0;
        }
    }

    /* 第三阶段：在单次临界区内批量读取并分发按键事件。 */
    old_cursor = s_menu_cursor;
    {
        uint8_t old_setting_cur = s_setting_cursor;
        /* 设置页面使用独立按键分发，并包含返回导航。 */
        if (!Ui_Controller_Handle_Settings_Keys(s_page,
                                   events[KEY_DRIVER_ID_BACK],
                                   events[KEY_DRIVER_ID_UP],
                                   events[KEY_DRIVER_ID_DOWN],
                                   events[KEY_DRIVER_ID_CONFIRM])) {
            Ui_Controller_Handle_Keys_By_Page(s_page,
                                events[KEY_DRIVER_ID_BACK],
                                events[KEY_DRIVER_ID_UP],
                                events[KEY_DRIVER_ID_DOWN],
                                events[KEY_DRIVER_ID_CONFIRM]);
        }
        if (s_setting_cursor != old_setting_cur) {
            s_last_setting_cursor = old_setting_cur;
            cursor_changed = 2;  /* 数值2表示设置主菜单光标发生移动。 */
        }
    }
    if (s_menu_cursor != old_cursor) cursor_changed = 1;

    /* 第四阶段：检测页面切换。 */
    if ((uint8_t)s_page != s_last_page) {
        s_last_page  = (uint8_t)s_page;
        s_page_drawn = 0;
        /* 页面切换后清除全部增量刷新跟踪状态。 */
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

    /* 第五阶段：生成200ms动态刷新节拍。 */
    if (Sys_Timer_Get_Tick() - s_last_ui_ms >= UI_REFRESH_MS) {
        s_last_ui_ms = Sys_Timer_Get_Tick();
        tick_200ms = 1;
    }
    if (s_settings_saved_until_ms != 0U &&
        (uint32_t)(Sys_Timer_Get_Tick() - s_settings_saved_until_ms) < 0x80000000U) {
        s_settings_saved_until_ms = 0U;
        if (s_page == UI_PAGE_SETTING || s_page == UI_PAGE_SETTING_FREQUENCY) {
            s_page_drawn = 0U;
        }
    }

    /* 第六阶段：钳位光标边界。 */
    if (s_page == UI_PAGE_MAIN_MENU) {
        /* 非故障时第5项不可达, 光标最多到3 */
        uint8_t max_cursor = (Sys_Core_Get_State() == SYS_STATE_FAULT) ? 4 : 3;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    if (s_page == UI_PAGE_MONITOR_SUB_MENU) {
        if (s_menu_cursor > 4) s_menu_cursor = 0;
    }
    if (s_page == UI_PAGE_SETTING) {
        if (s_setting_cursor >= UI_SETTING_ITEM_COUNT) {
            s_setting_cursor = UI_SETTING_ITEM_LANGUAGE;
        }
    }

    /* ============================================================
     *  第七阶段：执行整页绘制或增量绘制
     * ============================================================ */

    if (!s_page_drawn) {
        /* 整页绘制前先使用当前背景色清屏。 */
        Tft_Driver_Clear(Ui_Controller_Get_Background_Color());
        switch (s_page) {
            case UI_PAGE_MAIN_MENU:        Ui_Controller_Draw_Main_Menu_Full();   break;
            case UI_PAGE_MONITOR_SUB_MENU: Ui_Controller_Draw_Sub_Menu_Full();    break;
            case UI_PAGE_SWEEP:            Ui_Controller_Draw_Sweep_Full();       break;
            case UI_PAGE_MONITOR_SUMMARY:  Ui_Controller_Draw_Summary_Full();     break;
            case UI_PAGE_MONITOR_FREQ:     Ui_Controller_Draw_Freq_Full();        break;
            case UI_PAGE_MONITOR_VOLT:     Ui_Controller_Draw_Volt_Full();        break;
            case UI_PAGE_MONITOR_CURR:     Ui_Controller_Draw_Curr_Full();        break;
            case UI_PAGE_WIFI_SETUP:       Ui_Controller_Draw_WiFi_Full();        break;
            case UI_PAGE_FAULT:            Ui_Controller_Draw_Fault_Full();       break;
            case UI_PAGE_SETTING:          Ui_Controller_Draw_Setting_Full();     break;
            case UI_PAGE_SETTING_LANG:      Ui_Controller_Draw_Lang_Full();        break;
            case UI_PAGE_SETTING_FREQUENCY: Ui_Controller_Draw_Frequency_Full();   break;
            case UI_PAGE_SETTING_SPACING:   Ui_Controller_Draw_Spacing_Full();     break;
            case UI_PAGE_SETTING_ICONS:     Ui_Controller_Draw_Icons_Full();       break;
            case UI_PAGE_SETTING_COLOR:     Ui_Controller_Draw_Color_Full();       break;
        }
        Ui_Controller_Update_Leds();
        if (!Tft_Driver_Is_Draw_Blocked()) {
            s_page_drawn = 1;
            cursor_changed = 0;
        }
    } else {
        /* 增量绘制只修改已经发生变化的像素区域。 */

        if (cursor_changed) {
            if (cursor_changed == 2) {
                /* 设置主菜单只更新光标；其余设置页使用预览光标并触发整页重绘。 */
                if (s_page == UI_PAGE_SETTING) {
                    Ui_Controller_Erase_Cursor(2 + s_last_setting_cursor);
                    Ui_Controller_Draw_Cursor(2 + s_setting_cursor);
                }
            } else {
                switch (s_page) {
                    case UI_PAGE_MAIN_MENU:
                        Ui_Controller_Main_Menu_Cursor_Update(old_cursor);
                        break;
                    case UI_PAGE_MONITOR_SUB_MENU:
                        Ui_Controller_Sub_Menu_Cursor_Update(old_cursor);
                        break;
                    default:
                        s_page_drawn = 0;
                        break;
                }
            }
        }

        if (tick_200ms) {
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:        Ui_Controller_Main_Menu_Dynamic_Update(); break;
                case UI_PAGE_MONITOR_SUB_MENU: /* 静态页面 */               break;
                case UI_PAGE_SWEEP:            Ui_Controller_Sweep_Dynamic_Update();    break;
                case UI_PAGE_MONITOR_SUMMARY:  Ui_Controller_Summary_Dynamic_Update();  break;
                case UI_PAGE_MONITOR_FREQ:     Ui_Controller_Freq_Dynamic_Update();     break;
                case UI_PAGE_MONITOR_VOLT:     Ui_Controller_Volt_Dynamic_Update();     break;
                case UI_PAGE_MONITOR_CURR:     Ui_Controller_Curr_Dynamic_Update();     break;
                case UI_PAGE_WIFI_SETUP:       Ui_Controller_WiFi_Dynamic_Update();     break;
                case UI_PAGE_FAULT:            /* 静态页面 */               break;
                case UI_PAGE_SETTING:          /* 静态页面 */               break;
                case UI_PAGE_SETTING_LANG:      /* 静态页面 */               break;
                case UI_PAGE_SETTING_FREQUENCY: /* 静态页面 */               break;
                case UI_PAGE_SETTING_SPACING:   /* 静态页面 */               break;
                case UI_PAGE_SETTING_ICONS:     /* 静态页面 */               break;
                case UI_PAGE_SETTING_COLOR:     /* 静态页面 */               break;
            }
            Ui_Controller_Update_Leds();
        }
        if (Tft_Driver_Is_Draw_Blocked()) {
            s_page_drawn = 0;
        }
    }
}

/* ================================================================
 *  公开接口
 * ================================================================ */
uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }

/**
 * @brief  外部强制跳转到目标页面并重置菜单光标
 * @note   远程启停命令使用此接口；除页面跳转外还会把主菜单光标复位到首项。
 *         防止远端操作后本地菜单光标停留在已失效的旧菜单项上
 */
void Ui_Controller_Force_Page_And_Reset(Ui_Page page)
{
    s_page        = page;
    s_menu_cursor = 0;
    s_page_drawn  = 0;
}

/**
 * @brief  从应用存储层加载设置参数，并应用到界面和显示驱动
 * @note   必须在应用存储层初始化之后调用。
 *         旧背光亮度字段仅为兼容历史配置而保留；PA12现在只支持亮灭控制。
 */
void Ui_Controller_Apply_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                   uint8_t spacing, uint8_t preset,
                                   uint16_t fg, uint16_t bg,
                                   uint32_t startup_freq_low_hz,
                                   uint32_t startup_freq_high_hz,
                                   uint8_t startup_freq_band,
                                   uint8_t cursor_icon)
{
    s_language        = lang;
    s_letter_spacing  = spacing;
    s_startup_low_freq_hz = startup_freq_low_hz;
    s_startup_high_freq_hz = startup_freq_high_hz;
    s_startup_freq_band = startup_freq_band;
    s_menu_cursor_icon = cursor_icon;
    if (s_startup_low_freq_hz < UI_FREQUENCY_LOW_MIN_HZ ||
        s_startup_low_freq_hz > UI_FREQUENCY_LOW_MAX_HZ ||
        (s_startup_low_freq_hz % UI_FREQUENCY_LOW_STEP_HZ) != 0U) {
        s_startup_low_freq_hz = UI_FREQUENCY_LOW_MIN_HZ;
    }
    if (s_startup_high_freq_hz < UI_FREQUENCY_HIGH_MIN_HZ ||
        s_startup_high_freq_hz > UI_FREQUENCY_HIGH_MAX_HZ ||
        (s_startup_high_freq_hz % UI_FREQUENCY_HIGH_STEP_HZ) != 0U) {
        s_startup_high_freq_hz = UI_FREQUENCY_HIGH_MIN_HZ;
    }
    if (s_startup_freq_band != APP_STORAGE_FREQ_BAND_LOW &&
        s_startup_freq_band != APP_STORAGE_FREQ_BAND_HIGH) {
        s_startup_freq_band = APP_STORAGE_FREQ_BAND_HIGH;
    }
    if (s_menu_cursor_icon >= UI_CURSOR_ICON_COUNT) {
        s_menu_cursor_icon = 0U;
    }
    (void)font;  /* 字体大小已由字符间距取代，该字段仅用于兼容旧配置。 */
    (void)bl;
    Tft_Driver_Set_Letter_Spacing((uint8_t)(s_letter_spacing * 2));  /* 选项0至3映射为0、2、4、6像素。 */
    if (preset < 6) {
        Ui_Controller_Apply_Color_Preset(preset);
    } else {
        s_color_fg     = fg;
        s_color_bg     = bg;
        sc_preset = 255;
    }
    /* 使用已保存设置初始化各预览光标。 */
    s_preview_choice = sc_preset;
}
