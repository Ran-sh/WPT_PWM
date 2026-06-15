/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   Ui Controller V10 - two-level menu architecture (9 pages)
 * @note    TFT 8x20 cols, 160x128 landscape, 4 keys: F+/F-/KEY0/PAGE
 *          Colors: BK/YE/WH/CY/RD/GN/GY
 *          Dual icons (MQTT+WIFI) at top-right, all pages shared
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

/* -------- Chinese strings (UTF-8 hex) -------- */
#define S_WPT_PWM   "WPT-PWM"
#define S_SWEEP     "\xe6\x89\xab\xe9\xa2\x91\xe9\xa1\xb5"           /* sweep page */
#define S_MONITOR   "\xe7\x8a\xb6\xe6\x80\x81\xe7\x9b\x91\xe6\xb5\x8b" /* status monitor */
#define S_MON_FREQ  "\xe7\x9b\x91\xe6\xb5\x8b\xe9\xa2\x91\xe7\x8e\x87" /* monitor freq */
#define S_MON_VOLT  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe5\x8e\x8b" /* monitor volt */
#define S_MON_CURR  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe6\xb5\x81" /* monitor curr */
#define S_LAUNCH    "\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa1\xb5"           /* launch page */
#define S_FREQ      "\xe9\xa2\x91\xe7\x8e\x87"                       /* freq */
#define S_VOLTAGE   "\xe7\x94\xb5\xe5\x8e\x8b"                       /* voltage */
#define S_CURRENT   "\xe7\x94\xb5\xe6\xb5\x81"                       /* current */
#define S_STOP      "\xe5\x81\x9c\xe6\xad\xa2"                       /* stop */
#define S_CLEAR_WIFI "\xe6\xb8\x85\xe9\x99\xa4WIFI"                  /* clear WIFI */
#define S_WIFI_ONLINE  "\xe8\xbf\x9e\xe6\x8e\xa5" "\xe6\x88\x90" "\xe5\x8a\x9f" /* success */
#define S_WIFI_FAILED  "\xe8\xbf\x9e\xe6\x8e\xa5" "\xe5\xa4\xb1" "\xe8\xb4\xa5" /* failed */
#define S_WIFI_CONN    "\xe8\xbf\x9e\xe6\x8e\xa5" "\xe4\xb8\xad"               /* connecting */
#define S_WIFI_IDLE    "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5"                   /* idle */
#define S_WIFI_FORMAT  "\xe6\x97\xa0\xe7\xba\xbf\xe7\x8a\xb6\xe6\x80\x81" /* 无线状态 */
#define S_SUMMARY   "\xe7\xbb\xbc\xe5\x90\x88\xe7\x9b\x91\xe6\xb5\x8b" /* summary */
#define S_BACK      "\xe8\xbf\x94\xe5\x9b\x9e\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95" /* back to main */
#define S_DIV       "--------------------"           /* divider */

/* -------- Page state variables -------- */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;    /* 0=auto-connect at boot, 1=WiFi cleared by user */
static uint8_t  s_last_page       = 0xFF;
static uint8_t  s_last_cursor     = 0xFF;
static uint8_t  s_last_page_cleared = 0xFF;

/* EMA smoothing */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

/* User freq stepping — based on target, not actual (avoid integer-division drift) */
static uint32_t s_user_target_hz = 100000;
static uint8_t  s_user_target_synced = 0;

static void Reset_EMA(void) { s_ema_ok = 0; }

/* ================================================================
 *  Helpers: Center / Right / Fmt_V / Fmt_I / Fmt_F (preserved)
 * ================================================================ */

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

/* ================================================================
 *  Draw_Header: line0 title(left) + MQTTcloud(x=128) + WIFI(x=144)
 *  All pages call this. Preserved from V9.
 * ================================================================ */
static void Draw_Header(const char* title)
{
    #define MQTT_ICON_X  128
    #define WIFI_ICON_X  144

    /* Fill entire header row bg first — prevents residual pixels from prev page */
    Tft_Driver_Fill_Rect(0, 0, TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);

    Tft_Driver_Show_CN_String(0, 0, title, UI_COLOR_TITLE, UI_COLOR_BG);

    /* -------- MQTT cloud icon (x=128) -------- */
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

    /* -------- WIFI icon (x=144) -------- */
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

/* -------- Draw a menu item at given line with cursor highlight -------- */
static void Draw_Menu_Item(uint8_t line, uint8_t cursor, uint8_t idx, const char* text, uint8_t enabled)
{
    uint16_t color = UI_COLOR_TEXT;
    uint16_t bg;
    uint8_t is_selected = (cursor == idx);

    if (!enabled) {
        color = UI_COLOR_DIM;
    }

    if (is_selected) {
        /* Selected row: cyan BG + black text + star icon */
        bg    = UI_COLOR_VALUE;
        color = UI_COLOR_BG;
    } else {
        bg = UI_COLOR_BG;
    }

    Tft_Driver_Fill_Rect(0, (uint16_t)line * TFT_FONT_HEIGHT,
                        TFT_WIDTH, TFT_FONT_HEIGHT, bg);

    if (is_selected) {
        /* Star icon at column-0, only when selected */
        Tft_Driver_Draw_Single_Icon(0, (uint16_t)line * TFT_FONT_HEIGHT, ICON_STAR,
                                     UI_COLOR_BG, UI_COLOR_VALUE);
    }

    /* Text always at column 2, aligned regardless of star */
    Tft_Driver_Show_CN_String(line, 2, text, color, bg);
}

/* -------- Divider line at given row -------- */
static void Draw_Divider(uint8_t line)
{
    Tft_Driver_Show_String(line, 0, S_DIV, UI_COLOR_DIM, UI_COLOR_BG);
}

/* ================================================================
 *  Page draw functions -- 9 pages total
 * ================================================================ */

/* -------- Main Menu (4 items) -------- */
static void Draw_Main_Menu(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    uint8_t i;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }

    Draw_Header(S_WPT_PWM);
    Draw_Divider(1);

    /* 4 menu items with dynamic text */
    for (i = 0; i < 4; i++) {
        const char* text;
        uint8_t enabled = 1;
        switch (i) {
            case 0: /* StartPWM / StopPWM */
                text = is_running
                    ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"    /* 1.停止PWM */
                    : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";    /* 1.启动PWM */
                break;
            case 1: /* Status Monitor */
                text = "2. " S_MONITOR;
                break;
            case 2: /* WiFi Setup */
                text = "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91";  /* 无线配网 */
                break;
            case 3: /* Fault Clear */
                text = "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4";  /* 故障清除 */
                enabled = is_fault ? 1 : 0;
                break;
            default: text = ""; break;
        }
        Draw_Menu_Item(2 + i, s_menu_cursor, i, text, enabled);
    }

    Draw_Divider(6);

    Tft_Driver_Show_CN_String(7, Right("ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- Monitor Sub-Menu (5 items) -------- */
static void Draw_Monitor_Sub_Menu(void)
{
    uint8_t visible_top = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t i, line;

    Draw_Header(S_MONITOR);
    Draw_Divider(1);

    /* Fill lines 2-5 (4-row window) with items OR fill bg for empty rows */
    for (line = 2; line <= 5; line++) {
        i = visible_top + (line - 2);
        if (i < 5) {
            char item_buf[22];
            const char* name;
            switch (i) {
                case 0: name = S_SUMMARY;  break;
                case 1: name = S_MON_FREQ; break;
                case 2: name = S_MON_VOLT; break;
                case 3: name = S_MON_CURR; break;
                case 4: name = S_BACK;     break;
                default: name = ""; break;
            }
            snprintf(item_buf, sizeof(item_buf), "%d. %s", i + 1, name);
            Draw_Menu_Item(line, s_menu_cursor, i, item_buf, 1);
        } else {
            /* Empty row: fill with BG to clear any old content */
            Tft_Driver_Fill_Rect(0, (uint16_t)line * TFT_FONT_HEIGHT,
                                TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
        }
    }

    Draw_Divider(6);

    /* Bottom line: simple back hint */
    Tft_Driver_Show_CN_String(7, Right("\xe8\xbf\x94\xe5\x9b\x9e"),
        "\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- Sweep Page -------- */
static void Draw_Sweep_Page(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    uint32_t progress;
    char buf[21];

    if (is_stopped) {
        progress = 0;
    } else {
        progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                 / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
        if (progress > 10) progress = 10;
    }

    Draw_Header(S_SWEEP);
    Draw_Divider(1);

    /* Frequency */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Energy bar + percentage */
    /* Erase entire bar area (including text row below) before redrawing */
    Tft_Driver_Fill_Rect(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8, UI_COLOR_BG);
    if (!is_stopped) {
        Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                       14 * TFT_FONT_WIDTH, 8,
                       (float)progress, 0.0f, 10.0f,
                       ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
        snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
        if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(3, 5, "\xe5\xb7\xb2\xe6\x9a\x82\xe5\x81\x9c", UI_COLOR_ALARM, UI_COLOR_BG); /* 已暂停 */
    }

    /* Voltage / Current */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);

    Draw_Divider(6);
    if (is_stopped) {
        Tft_Driver_Show_CN_String(7, Right("ON:\xe7\xbb\xa7\xe7\xbb\xad PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "ON:\xe7\xbb\xa7\xe7\xbb\xad PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(7, Right("ON:\xe5\x81\x9c\xe6\xad\xa2 PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "ON:\xe5\x81\x9c\xe6\xad\xa2 PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

/* -------- Monitor Summary (dual mode: idle / running) -------- */
static void Draw_Monitor_Summary(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    Draw_Header(S_SUMMARY);
    Draw_Divider(1);

    /* Frequency, Voltage, Current — centered on lines 2/3/4 */
    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Voltage */
    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Current */
    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);

    Draw_Divider(6);

    if (is_running) {
        Tft_Driver_Show_CN_String(7, Right("F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(7, Right("ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

/* -------- Monitor Freq (gauge page) -------- */
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
        Tft_Driver_Show_CN_String(7, Right("F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(7, Right("ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
            "ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

/* -------- Monitor Volt (gauge page) -------- */
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
    Tft_Driver_Show_CN_String(7, Right("PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- Monitor Curr (gauge page) -------- */
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
    Tft_Driver_Show_CN_String(7, Right("PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- WiFi Setup Page -------- */
static void Draw_WiFi_Setup(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    const char* status_text;
    const char* hint_text;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = S_WIFI_ONLINE;   /* online */
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = S_WIFI_FAILED;   /* failed */
    else if (App_Network_Is_Connecting())
        status_text = S_WIFI_CONN;     /* connecting */
    else
        status_text = S_WIFI_IDLE;     /* disconnected */

    /* Dynamic hint based on state */
    if (cs == APP_NETWORK_CONN_ONLINE)
        hint_text = "ON:\xe6\x96\xad\xe5\xbc\x80WIFI";       /* ON:断开WIFI */
    else
        hint_text = "ON:\xe8\xbf\x9e\xe6\x8e\xa5WIFI";       /* ON:连接WIFI */

    Draw_Header(S_LAUNCH);
    Draw_Divider(1);

    {
        char buf[42];
        snprintf(buf, sizeof(buf), S_WIFI_FORMAT ": %s", status_text);
        Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    /* Retry count: only when connecting */
    if (App_Network_Is_Connecting()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\xe9\x87\x8d\xe8\xaf\x95 %d/%d", App_Network_Get_Retry_Count() + 1, 3);
        Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
    } else {
        /* Erase retry line when not connecting */
        Tft_Driver_Fill_Rect(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
    }

    /* ON action hint, right-aligned */
    Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, UI_COLOR_TEXT, UI_COLOR_BG);

    /* Long-press clear WiFi, right-aligned */
    {
        const char* clear_text = "\xe9\x95\xbf\xe6\x8c\x89ON:" S_CLEAR_WIFI;
        Tft_Driver_Show_CN_String(6, Right(clear_text), clear_text, UI_COLOR_ALARM, UI_COLOR_BG);
    }

    Draw_Divider(7);
}

/* -------- Fault Page -------- */
static void Draw_Fault_Page(void)
{
    Draw_Header("!!!\xe6\x95\x85\xe9\x9a\x9c!!!");  /* !!!FAULT!!! */
    Draw_Divider(1);

    Tft_Driver_Show_CN_String(2, Center("\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4"),
        "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4", UI_COLOR_ALARM, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(3, Center("PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"),
        "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad", UI_COLOR_TEXT, UI_COLOR_BG);

    Tft_Driver_Show_CN_String(5, Center("\xe6\x8c\x89" "KEY0" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf"),
        "\xe6\x8c\x89" "KEY0" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf", UI_COLOR_VALUE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* ================================================================
 *  LED Update -- sync 6 LEDs based on current page + network
 * ================================================================ */
static void Update_Leds(Ui_Page page)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI || cs == APP_NETWORK_CONN_MQTT)
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

/* ================================================================
 *  Key Dispatch -- final key bus mapping
 * ================================================================ */
static void Handle_Keys_by_Page(Ui_Page page,
                                Key_Driver_Event k0, Key_Driver_Event k1,
                                Key_Driver_Event k2, Key_Driver_Event k3)
{
    uint8_t is_running = 0;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
    }

    /* -------- F_UP (k1): cursor up (wrap-around) OR freq +1kHz -------- */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU: {
                uint8_t is_fault = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT);
                uint8_t max_cursor = is_fault ? 3 : 2;
                if (s_menu_cursor == 0) s_menu_cursor = max_cursor;
                else s_menu_cursor--;
                break;
            }
            case UI_PAGE_MONITOR_SUB_MENU:
                if (s_menu_cursor == 0) s_menu_cursor = 4;
                else s_menu_cursor--;
                break;
            case UI_PAGE_MONITOR_SUMMARY:
            case UI_PAGE_MONITOR_FREQ:
                if (is_running) {
                    if (!s_user_target_synced) {
                        /* First keypress: sync target to nearest kHz from actual freq */
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

    /* -------- F_DOWN (k2): cursor down (wrap-around) OR freq -1kHz -------- */
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU: {
                uint8_t is_fault = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT);
                uint8_t max_cursor = is_fault ? 3 : 2;
                if (s_menu_cursor >= max_cursor) s_menu_cursor = 0;
                else s_menu_cursor++;
                break;
            }
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

    /* -------- KEY0 (k0): confirm / action -------- */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                switch (s_menu_cursor) {
                    case 0: /* StartPWM / StopPWM */
                        if (is_running) {
                            Inverter_Control_Soft_Start_Stop();
                        } else {
                            Inverter_Control_Soft_Start_Trigger();
                            s_page = UI_PAGE_SWEEP;
                            Reset_EMA();
                        }
                        break;
                    case 1: /* Status Monitor */
                        s_page = UI_PAGE_MONITOR_SUB_MENU;
                        s_menu_cursor = 0;
                        break;
                    case 2: /* WiFi Setup */
                        s_page = UI_PAGE_WIFI_SETUP;
                        break;
                    case 3: /* Fault Clear */
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
                /* ON during sweep: toggle stop/restart */
                {
                    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
                    if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) {
                        Inverter_Control_Soft_Start_Stop();
                    } else if (ss == INVERTER_CONTROL_SS_STATE_IDLE) {
                        /* Was stopped mid-sweep, restart from current frequency */
                        Inverter_Control_Soft_Start_Trigger();
                        Reset_EMA();
                    }
                }
                break;

            case UI_PAGE_WIFI_SETUP: {
                /* ON in WiFi page: if online -> disconnect (no-WiFi mode), stay on page;
                 * if not online -> clear old config and reconnect, stay on page. */
                uint8_t cs = App_Network_Get_Connect_Status();
                if (cs == APP_NETWORK_CONN_ONLINE) {
                    App_Network_Soft_Reset();
                    s_no_wifi_mode = 1;
                } else {
                    if (Esp8266_Driver_Is_Ready()) {
                        Esp8266_Driver_Send_String("CMD:CLEAR\n");
                    }
                    App_Network_Soft_Reset();
                    s_no_wifi_mode = 0;
                    App_Network_Start_Connect();
                }
                break;
            }

            case UI_PAGE_FAULT:
                Inverter_Control_Soft_Start_Reset();
                s_page = UI_PAGE_MAIN_MENU;
                s_menu_cursor = 0;
                s_was_fault_state = 0;
                Reset_EMA();
                break;

            default: break;
        }
    }

    /* -------- KEY0 long-press: clear WiFi (any page, gate on PWM inactive) -------- */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE) {
            /* PWM active: ignore long-press to protect hardware */
            return;
        }
        if (Esp8266_Driver_Is_Ready()) {
            /* Only clear WiFi after ensuring ESP idle; send CLEAR then reconnect */
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            s_no_wifi_mode = 1;
            /* If not already on WiFi page, go there to show status */
            if (s_page != UI_PAGE_WIFI_SETUP && s_page != UI_PAGE_FAULT) {
                s_page = UI_PAGE_WIFI_SETUP;
            }
            /* Stay on WiFi page to show new state, reset EMA */
            Reset_EMA();
        }
    }

    /* -------- PAGE (k3): go back one level -------- */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                break;  /* already at top, PAGE does nothing */
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
        }
    }
}

/* ================================================================
 *  Main Scheduler -- 200ms cycle
 * ================================================================ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last_ui_ms = 0;
    static uint8_t  s_last_wifi_frame = 0xFF;
    static uint8_t  s_last_mqtt_frame = 0xFF;
    uint8_t need_draw = 0;

    /* -- 0. WIFI + MQTT icon animation: per-frame partial refresh of line 0 -- */
    /*     WIFI: 150ms/6fr, MQTT: 200ms/6fr. Both sampled independently.    */
    /*     Only refresh header when actually in connecting state.           */
    {
        uint8_t wifi_frame = (App_Network_Is_Connecting() || !Esp8266_Driver_Is_Ready())
            ? (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6
            : 0xFF;
        uint8_t mqtt_frame = (App_Network_Is_Connecting())
            ? (uint8_t)(Sys_Timer_Get_Tick() / 200) % 6
            : 0xFF;
        if (wifi_frame != s_last_wifi_frame || mqtt_frame != s_last_mqtt_frame) {
            s_last_wifi_frame = wifi_frame;
            s_last_mqtt_frame = mqtt_frame;
            /* Redraw only the header row */
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:        Draw_Header(S_WPT_PWM);                      break;
                case UI_PAGE_MONITOR_SUB_MENU: Draw_Header(S_MONITOR);                      break;
                case UI_PAGE_SWEEP:            Draw_Header(S_SWEEP);                        break;
                case UI_PAGE_MONITOR_SUMMARY:  Draw_Header(S_SUMMARY);                      break;
                case UI_PAGE_MONITOR_FREQ:     Draw_Header(S_MON_FREQ);                     break;
                case UI_PAGE_MONITOR_VOLT:     Draw_Header(S_MON_VOLT);                     break;
                case UI_PAGE_MONITOR_CURR:     Draw_Header(S_MON_CURR);                     break;
                case UI_PAGE_WIFI_SETUP:       Draw_Header(S_LAUNCH);                       break;
                case UI_PAGE_FAULT:            Draw_Header("!!!\xe6\x95\x85\xe9\x9a\x9c!!!"); break;
            }
        }
    }

    /* -- 1. Fault edge detection (every frame) -- */
    {
        uint8_t current_fault = (Inverter_Control_Soft_Start_Get_State()
                                 == INVERTER_CONTROL_SS_STATE_FAULT);
        if (current_fault && !s_was_fault_state) {
            /* Rising edge 0->1: force jump to fault page */
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
        }
        if (!current_fault) {
            s_was_fault_state = 0;
        }
    }

    /* -- 2. Sweep complete detection: SWEEP -> auto-jump to SUMMARY -- */
    if (s_page == UI_PAGE_SWEEP) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
            s_page = UI_PAGE_MONITOR_SUMMARY;
            Reset_EMA();
            s_user_target_synced = 0;
        }
    }

    /* -- 3. Key scan (every frame) -- */
    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);

    Handle_Keys_by_Page(s_page, k0, k1, k2, k3);

    /* -- 4. Page/cursor change detection -- */
    if ((uint8_t)s_page != s_last_page || s_menu_cursor != s_last_cursor) {
        s_last_page   = (uint8_t)s_page;
        s_last_cursor = s_menu_cursor;
        /* Page change: skip clear, force immediate redraw to avoid black flicker */
        if ((uint8_t)s_page != s_last_page_cleared) {
            s_last_page_cleared = (uint8_t)s_page;
            need_draw = 1;
            s_last_ui_ms = Sys_Timer_Get_Tick();   /* reset throttle, draw this frame */
        }
        Reset_EMA();
    }

    /* -- 5. 200ms periodic throttle -- */
    if (Sys_Timer_Get_Tick() - s_last_ui_ms >= UI_REFRESH_MS) {
        s_last_ui_ms = Sys_Timer_Get_Tick();
        need_draw = 1;
    }

    /* -- 6. Menu cursor boundary clamp (before draw, prevent overrun) -- */
    if (s_page == UI_PAGE_MAIN_MENU) {
        uint8_t is_fault = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT);
        uint8_t max_cursor = is_fault ? 3 : 2;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    if (s_page == UI_PAGE_MONITOR_SUB_MENU) {
        if (s_menu_cursor > 4) s_menu_cursor = 0;
    }

    /* -- 7. PB10 PowerContrl -- */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* -- 8. Overcurrent protection -- */
    if (s_page == UI_PAGE_SWEEP ||
        s_page == UI_PAGE_MONITOR_SUMMARY ||
        s_page == UI_PAGE_MONITOR_FREQ ||
        s_page == UI_PAGE_MONITOR_VOLT ||
        s_page == UI_PAGE_MONITOR_CURR) {
        Update_EMA();
        if (s_ema_i > UI_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            /* Immediate jump: set page + dirty the state tracker so same-frame draw picks it up */
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
            s_last_page = 0xFF;     /* force clear+redraw */
            s_last_page_cleared = 0xFF;
            need_draw = 1;          /* force immediate redraw, skip throttle */
            s_last_ui_ms = Sys_Timer_Get_Tick();
        }
    }

    if (s_page != UI_PAGE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

    /* -- 9. Draw -- */
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

/* ================================================================
 *  Public Interface
 * ================================================================ */
Ui_Page Ui_Controller_Get_Page(void)      { return s_page; }
uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }
