/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   Ui Controller V11 — incremental refresh, zero-flicker
 * @note    TFT 8x20 cols, 160x128 landscape, 4 keys: F+/F-/KEY0/PAGE
 *          Architecture: static text drawn ONCE on page entry,
 *          cursor changes update only 2 lines (erase old ▶ + draw new ▶),
 *          200ms cycle only updates changing values (F/V/I/bar/status).
 *          Menu pages idle at 0% SPI activity — no writes unless state changes.
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

#define S_PAUSE     "\xe5\xb7\xb2\xe6\x9a\x82\xe5\x81\x9c"           /* 已暂停 */
#define S_OVERCUR   "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4" /* 过流保护 */
#define S_PWM_OFF   "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"       /* PWM已关断 */
#define S_FAULT_TITLE "!!!\xe6\x95\x85\xe9\x9a\x9c!!!"               /* !!!故障!!! */
#define S_RESET_HINT "\xe6\x8c\x89" "KEY0" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf"
#define S_BOTTOM_CONFIRM  "ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e"
#define S_BOTTOM_STOP     "ON:\xe5\x81\x9c\xe6\xad\xa2 PAGE:\xe8\xbf\x94\xe5\x9b\x9e"
#define S_BOTTOM_CONT     "ON:\xe7\xbb\xa7\xe7\xbb\xad PAGE:\xe8\xbf\x94\xe5\x9b\x9e"
#define S_BOTTOM_BACK     "PAGE:\xe8\xbf\x94\xe5\x9b\x9e"
#define S_BOTTOM_TUNE     "F+/F-:\xe8\xb0\x83\xe9\xa2\x91 PAGE:\xe8\xbf\x94\xe5\x9b\x9e"
#define S_BOTTOM_SWITCH   "\xe5\x8f\x8c\xe5\x87\xbb" "PAGE" "\xe5\x88\x87" "\xe9\xa1\xb5" /* 双击PAGE切页 */
#define S_ON_DISCONNECT   "ON:\xe6\x96\xad\xe5\xbc\x80WIFI"
#define S_ON_CONNECT      "ON:\xe8\xbf\x9e\xe6\x8e\xa5WIFI"
#define S_LONG_CLEAR      "\xe9\x95\xbf\xe6\x8c\x89ON:" S_CLEAR_WIFI

/* -------- Page state variables -------- */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;
static uint8_t  s_last_page       = 0xFF;

/* EMA smoothing */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

/* User freq stepping */
static uint32_t s_user_target_hz = 100000;
static uint8_t  s_user_target_synced = 0;

/* ── Incremental refresh state (V11) ── */
static uint8_t s_page_drawn         = 0;    /* 0=need full redraw, 1=static content present */
static uint8_t s_last_cursor_idx    = 0;    /* previous cursor, for 2-line cursor update */
static uint8_t s_last_is_running    = 0xFF; /* tracked PWM running state */
static uint8_t s_last_is_fault_menu = 0xFF; /* tracked FAULT state for menu item 3 */
static uint8_t s_last_sub_visible   = 0;    /* tracked sub-menu visible_top */
static uint8_t s_last_sweep_stopped = 0xFF; /* tracked sweep pause state */
static uint8_t s_last_wifi_cs       = 0xFF; /* tracked WiFi connection status */
static uint8_t s_last_retry         = 0xFF; /* tracked WiFi retry count */
static int8_t  s_last_rssi          = -128; /* tracked RSSI */

/* Cached last formatted value strings — avoid redrawing unchanged values */
static char    s_last_f_str[21];
static char    s_last_v_str[21];
static char    s_last_i_str[21];
static char    s_last_status_buf[42];
static char    s_last_retry_buf[16];

static void Reset_EMA(void) { s_ema_ok = 0; }

/* ================================================================
 *  Helpers: Center / Right / Fmt_V / Fmt_I / Fmt_F
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
 * ================================================================ */
static void Draw_Header(const char* title)
{
    #define MQTT_ICON_X  128
    #define WIFI_ICON_X  144

    Tft_Driver_Fill_Rect(0, 0, TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(0, 0, title, UI_COLOR_TITLE, UI_COLOR_BG);

    /* MQTT cloud icon */
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

    /* WIFI icon */
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
            s_last_rssi = r;
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

/* ================================================================
 *  Cursor: ▶ at col 0 — draw/erase (minimal pixel update)
 * ================================================================ */
static void Draw_Cursor(uint8_t line)
{
    Tft_Driver_Show_Char(line, 0, '>', UI_COLOR_VALUE, UI_COLOR_BG);
}

static void Erase_Cursor(uint8_t line)
{
    Tft_Driver_Show_Char(line, 0, ' ', UI_COLOR_BG, UI_COLOR_BG);
}

/* ================================================================
 *  Line primitives
 * ================================================================ */
static void Erase_Line(uint8_t line)
{
    Tft_Driver_Fill_Rect(0, (uint16_t)line * TFT_FONT_HEIGHT,
                        TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
}

static void Draw_Divider(uint8_t line)
{
    Tft_Driver_Show_String(line, 0, S_DIV, UI_COLOR_DIM, UI_COLOR_BG);
}

/* ── Draw menu text at line,col (erases whole line first) ── */
static void Draw_Menu_Text(uint8_t line, uint8_t col, const char* text, uint8_t enabled)
{
    uint16_t color = enabled ? UI_COLOR_TEXT : UI_COLOR_DIM;
    Erase_Line(line);
    Tft_Driver_Show_CN_String(line, col, text, color, UI_COLOR_BG);
}

/* ================================================================
 *  Page draw: MAIN_MENU (static frame — called ONCE on entry)
 * ================================================================ */
static void Draw_Main_Menu_Full(void)
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

    for (i = 0; i < 4; i++) {
        const char* text;
        uint8_t enabled = 1;
        switch (i) {
            case 0:
                text = is_running
                    ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"    /* 1.停止PWM */
                    : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";    /* 1.启动PWM */
                break;
            case 1: text = "2. " S_MONITOR; break;
            case 2: text = "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91"; break;
            case 3:
                text = "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4";
                enabled = is_fault ? 1 : 0;
                break;
            default: text = ""; break;
        }
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    /* Cursor on current item */
    Draw_Cursor(2 + s_menu_cursor);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right(S_BOTTOM_CONFIRM),
        S_BOTTOM_CONFIRM, UI_COLOR_TEXT, UI_COLOR_BG);

    /* Track state for incremental updates */
    s_last_is_running    = is_running;
    s_last_is_fault_menu = is_fault;
    s_last_cursor_idx    = s_menu_cursor;
}

/* ── MAIN_MENU cursor move: erase old ▶ + draw new ▶ (2 chars total) ── */
static void Main_Menu_Cursor_Update(uint8_t old_cursor)
{
    Erase_Cursor(2 + old_cursor);
    Draw_Cursor(2 + s_menu_cursor);
    s_last_cursor_idx = s_menu_cursor;
}

/* ── MAIN_MENU 200ms dynamic: only redraw if PWM/fault state changed ── */
static void Main_Menu_Dynamic_Update(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }

    /* Item 0: text changes when PWM starts/stops */
    if (is_running != s_last_is_running) {
        const char* text = is_running
            ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"
            : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";
        Draw_Menu_Text(2, 2, text, 1);
        if (s_menu_cursor == 0) Draw_Cursor(2);  /* re-draw cursor if it's on this line */
        s_last_is_running = is_running;
    }

    /* Item 3: enabled/disabled changes with fault state */
    if (is_fault != s_last_is_fault_menu) {
        const char* text = "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4";
        uint8_t enabled = is_fault ? 1 : 0;
        Draw_Menu_Text(5, 2, text, enabled);
        if (s_menu_cursor == 3) Draw_Cursor(5);
        s_last_is_fault_menu = is_fault;
    }
}

/* ================================================================
 *  Page draw: MONITOR_SUB_MENU (5 items, 4-row scroll window)
 * ================================================================ */
static const char* Sub_Item_Name(uint8_t idx)
{
    switch (idx) {
        case 0: return S_SUMMARY;
        case 1: return S_MON_FREQ;
        case 2: return S_MON_VOLT;
        case 3: return S_MON_CURR;
        case 4: return S_BACK;
        default: return "";
    }
}

static void Draw_Sub_Menu_Full(void)
{
    uint8_t visible_top = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;
    uint8_t i, line;

    Draw_Header(S_MONITOR);
    Draw_Divider(1);

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

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("\xe8\xbf\x94\xe5\x9b\x9e"),
        "\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);

    s_last_cursor_idx  = s_menu_cursor;
    s_last_sub_visible = visible_top;
}

/* ── Sub-menu cursor: 2-line update (or 4-line if scroll window changed) ── */
static void Sub_Menu_Cursor_Update(uint8_t old_cursor)
{
    uint8_t old_visible = s_last_sub_visible;
    uint8_t new_visible = (s_menu_cursor >= 3) ? (s_menu_cursor - 2) : 0;

    if (new_visible != old_visible) {
        /* Scroll happened — redraw all 4 visible lines */
        uint8_t i, line;
        Erase_Cursor(2 + (old_cursor - old_visible));  /* erase old cursor first */

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
        Draw_Cursor(2 + (s_menu_cursor - new_visible));
    } else {
        /* Simple cursor move within same window — 2 char updates */
        uint8_t old_line = 2 + (old_cursor - old_visible);
        uint8_t new_line = 2 + (s_menu_cursor - new_visible);
        Erase_Cursor(old_line);
        Draw_Cursor(new_line);
    }

    s_last_cursor_idx  = s_menu_cursor;
    s_last_sub_visible = new_visible;
}

/* ================================================================
 *  Page draw: SWEEP (fully dynamic — redrawn every 200ms)
 *  Static frame drawn once, values updated incrementally
 * ================================================================ */
static void Draw_Sweep_Full(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    Draw_Header(S_SWEEP);
    Draw_Divider(1);

    /* Frequency label + first value */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* Voltage / Current — initial draw */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    /* Progress bar area */
    {
        uint32_t progress;
        if (is_stopped) {
            progress = 0;
        } else {
            progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                     / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
            if (progress > 10) progress = 10;
        }
        Tft_Driver_Fill_Rect(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8, UI_COLOR_BG);
        if (!is_stopped) {
            Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                           14 * TFT_FONT_WIDTH, 8,
                           (float)progress, 0.0f, 10.0f,
                           ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
            snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
            if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        } else {
            Tft_Driver_Show_CN_String(3, 5, S_PAUSE, UI_COLOR_ALARM, UI_COLOR_BG);
        }
    }

    Draw_Divider(6);
    {
        const char* hint = is_stopped ? S_BOTTOM_CONT : S_BOTTOM_STOP;
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    s_last_sweep_stopped = is_stopped;
}

/* ── SWEEP 200ms: update F value + progress bar + V/I ── */
static void Sweep_Dynamic_Update(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    uint8_t is_stopped = (ss == INVERTER_CONTROL_SS_STATE_IDLE);
    char buf[21];

    /* Frequency — only if changed */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Progress bar */
    {
        uint32_t progress;
        Tft_Driver_Fill_Rect(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8, UI_COLOR_BG);
        if (!is_stopped) {
            progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                     / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
            if (progress > 10) progress = 10;
            Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                           14 * TFT_FONT_WIDTH, 8,
                           (float)progress, 0.0f, 10.0f,
                           ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
            snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
            if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        } else {
            Tft_Driver_Show_CN_String(3, 5, S_PAUSE, UI_COLOR_ALARM, UI_COLOR_BG);
        }
    }

    /* Voltage */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, Adc_Driver_Get_Current());
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* Bottom hint — if stopped state changed */
    if (is_stopped != s_last_sweep_stopped) {
        const char* hint = is_stopped ? S_BOTTOM_CONT : S_BOTTOM_STOP;
        Erase_Line(7);
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
        s_last_sweep_stopped = is_stopped;
    }
}

/* ================================================================
 *  Page draw: MONITOR_SUMMARY (F/V/I on lines 2/3/4)
 * ================================================================ */
static void Draw_Summary_Full(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    Draw_Header(S_SUMMARY);
    Draw_Divider(1);

    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    Draw_Divider(6);
    {
        const char* hint = is_running ? S_BOTTOM_TUNE : S_BOTTOM_CONFIRM;
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    s_last_is_running = is_running;
}

/* ── Summary 200ms: update F/V/I + bottom hint if is_running changed ── */
static void Summary_Dynamic_Update(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    /* Frequency */
    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Voltage */
    Fmt_V(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* Bottom hint */
    if (is_running != s_last_is_running) {
        const char* hint = is_running ? S_BOTTOM_TUNE : S_BOTTOM_CONFIRM;
        Erase_Line(7);
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
        s_last_is_running = is_running;
    }
}

/* ================================================================
 *  Page draw: MONITOR_FREQ (gauge)
 * ================================================================ */
static void Draw_Freq_Full(void)
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
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   is_running ? s_ema_f : 0.0f, 95.0f, 150.0f,
                   ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "95", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 17, "150", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    {
        const char* hint = is_running ? S_BOTTOM_TUNE : S_BOTTOM_CONFIRM;
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    s_last_is_running = is_running;
}

static void Freq_Dynamic_Update(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   is_running ? s_ema_f : 0.0f, 95.0f, 150.0f,
                   ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);

    if (is_running != s_last_is_running) {
        const char* hint = is_running ? S_BOTTOM_TUNE : S_BOTTOM_CONFIRM;
        Erase_Line(7);
        Tft_Driver_Show_CN_String(7, Right(hint), hint, UI_COLOR_TEXT, UI_COLOR_BG);
        s_last_is_running = is_running;
    }
}

/* ================================================================
 *  Page draw: MONITOR_VOLT (gauge)
 * ================================================================ */
static void Draw_Volt_Full(void)
{
    char buf[21];
    Update_EMA();
    Draw_Header(S_MON_VOLT);
    Draw_Divider(1);

    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_v, 0.0f, 48.0f,
                   ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 17, "48", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right(S_BOTTOM_BACK),
        S_BOTTOM_BACK, UI_COLOR_TEXT, UI_COLOR_BG);
}

static void Volt_Dynamic_Update(void)
{
    char buf[21];
    Update_EMA();
    Fmt_V(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }
    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_v, 0.0f, 48.0f,
                   ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
}

/* ================================================================
 *  Page draw: MONITOR_CURR (gauge)
 * ================================================================ */
static void Draw_Curr_Full(void)
{
    char buf[21];
    Update_EMA();
    Draw_Header(S_MON_CURR);
    Draw_Divider(1);

    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_i, 0.0f, 3.0f,
                   ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 18, "3", UI_COLOR_TITLE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right(S_BOTTOM_BACK),
        S_BOTTOM_BACK, UI_COLOR_TEXT, UI_COLOR_BG);
}

static void Curr_Dynamic_Update(void)
{
    char buf[21];
    Update_EMA();
    Fmt_I(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }
    Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                   12 * TFT_FONT_WIDTH, 12,
                   s_ema_i, 0.0f, 3.0f,
                   ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
}

/* ================================================================
 *  Page draw: WIFI_SETUP
 * ================================================================ */
static void Draw_WiFi_Full(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    const char* status_text;
    const char* hint_text;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = S_WIFI_ONLINE;
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = S_WIFI_FAILED;
    else if (App_Network_Is_Connecting())
        status_text = S_WIFI_CONN;
    else
        status_text = S_WIFI_IDLE;

    hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? S_ON_DISCONNECT : S_ON_CONNECT;

    Draw_Header(S_LAUNCH);
    Draw_Divider(1);

    {
        char buf[42];
        snprintf(buf, sizeof(buf), S_WIFI_FORMAT ": %s", status_text);
        Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
        s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
    }

    if (App_Network_Is_Connecting()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\xe9\x87\x8d\xe8\xaf\x95 %d/%d",
                 App_Network_Get_Retry_Count() + 1, 3);
        Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_retry_buf, buf, sizeof(s_last_retry_buf));
        s_last_retry_buf[sizeof(s_last_retry_buf) - 1] = '\0';
    } else {
        Erase_Line(3);
        s_last_retry_buf[0] = '\0';
    }

    Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, UI_COLOR_TEXT, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(6, Right(S_LONG_CLEAR), S_LONG_CLEAR, UI_COLOR_ALARM, UI_COLOR_BG);
    Draw_Divider(7);

    s_last_wifi_cs = cs;
    s_last_retry   = App_Network_Get_Retry_Count();
}

static void WiFi_Dynamic_Update(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();
    uint8_t retry = App_Network_Get_Retry_Count();
    const char* status_text;
    const char* hint_text;
    uint8_t need_hint_update = 0;

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = S_WIFI_ONLINE;
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = S_WIFI_FAILED;
    else if (App_Network_Is_Connecting())
        status_text = S_WIFI_CONN;
    else
        status_text = S_WIFI_IDLE;

    /* Status line — only if changed */
    if (cs != s_last_wifi_cs) {
        char buf[42];
        snprintf(buf, sizeof(buf), S_WIFI_FORMAT ": %s", status_text);
        if (strncmp(buf, s_last_status_buf, sizeof(s_last_status_buf)) != 0) {
            Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
            s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
        }
        need_hint_update = 1;
        s_last_wifi_cs = cs;
    }

    /* Retry line */
    if (App_Network_Is_Connecting()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\xe9\x87\x8d\xe8\xaf\x95 %d/%d", retry + 1, 3);
        if (retry != s_last_retry || strncmp(buf, s_last_retry_buf, sizeof(s_last_retry_buf)) != 0) {
            Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
            strncpy(s_last_retry_buf, buf, sizeof(s_last_retry_buf));
            s_last_retry_buf[sizeof(s_last_retry_buf) - 1] = '\0';
        }
    } else if (s_last_retry != 0xFF || s_last_retry_buf[0] != '\0') {
        Erase_Line(3);
        s_last_retry_buf[0] = '\0';
    }
    s_last_retry = retry;

    /* Hint text — only if connection state changed */
    if (need_hint_update) {
        hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? S_ON_DISCONNECT : S_ON_CONNECT;
        Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

/* ================================================================
 *  Page draw: FAULT (fully static after first draw)
 * ================================================================ */
static void Draw_Fault_Full(void)
{
    Draw_Header(S_FAULT_TITLE);
    Draw_Divider(1);

    Tft_Driver_Show_CN_String(2, Center(S_OVERCUR),
        S_OVERCUR, UI_COLOR_ALARM, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(3, Center(S_PWM_OFF),
        S_PWM_OFF, UI_COLOR_TEXT, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(5, Center(S_RESET_HINT),
        S_RESET_HINT, UI_COLOR_VALUE, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right(S_BOTTOM_BACK),
        S_BOTTOM_BACK, UI_COLOR_TEXT, UI_COLOR_BG);
}

/* ================================================================
 *  LED Update
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
 *  Key Dispatch
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

    /* F_UP (k1) */
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

    /* F_DOWN (k2) */
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

    /* KEY0 (k0) */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            case UI_PAGE_MAIN_MENU:
                switch (s_menu_cursor) {
                    case 0:
                        if (is_running) {
                            Inverter_Control_Soft_Start_Stop();
                        } else {
                            Inverter_Control_Soft_Start_Trigger();
                            s_page = UI_PAGE_SWEEP;
                            Reset_EMA();
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
                {
                    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
                    if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) {
                        Inverter_Control_Soft_Start_Stop();
                    } else if (ss == INVERTER_CONTROL_SS_STATE_IDLE) {
                        Inverter_Control_Soft_Start_Trigger();
                        Reset_EMA();
                    }
                }
                break;

            case UI_PAGE_WIFI_SETUP: {
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

    /* KEY0 long-press: clear WiFi */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE) {
            return;
        }
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            s_no_wifi_mode = 1;
            if (s_page != UI_PAGE_WIFI_SETUP && s_page != UI_PAGE_FAULT) {
                s_page = UI_PAGE_WIFI_SETUP;
            }
            Reset_EMA();
        }
    }

    /* PAGE (k3): go back */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
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
        }
    }
}

/* ================================================================
 *  Main Scheduler — V11 incremental refresh architecture
 *
 *  Phase 0: Header animation (per-frame, WIFI/MQTT icons only)
 *  Phase 1: Fault edge detection → may set s_page, s_page_drawn=0
 *  Phase 2: Sweep complete detection → may set s_page
 *  Phase 3: Key scan + dispatch → may set s_page or s_menu_cursor
 *  Phase 4: Page change → s_page_drawn=0; cursor change → cursor_update
 *  Phase 5: 200ms tick → dynamic incremental update (values only)
 *  Phase 6: PB10 + Overcurrent protection
 *  Phase 7: Draw — full page only when s_page_drawn==0
 *
 *  Menu pages (MAIN_MENU, SUB_MENU, FAULT): static text drawn once.
 *    200ms only checks for PWM/fault state transitions, zero SPI if idle.
 *  Dynamic pages (SWEEP, MONITOR_*): 200ms updates F/V/I values + energy bar.
 *    Only redraws lines whose content actually changed (string compare).
 *  Cursor: ▶ at col 0. On move → erase old ▶ + draw new ▶ (2 chars, ~0.5ms).
 * ================================================================ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last_ui_ms = 0;
    static uint8_t  s_last_wifi_frame = 0xFF;
    static uint8_t  s_last_mqtt_frame = 0xFF;
    uint8_t old_cursor;
    uint8_t cursor_changed = 0;
    uint8_t tick_200ms = 0;

    /* ── Phase 0: Header WIFI+MQTT animation ── */
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
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:        Draw_Header(S_WPT_PWM);                      break;
                case UI_PAGE_MONITOR_SUB_MENU: Draw_Header(S_MONITOR);                      break;
                case UI_PAGE_SWEEP:            Draw_Header(S_SWEEP);                        break;
                case UI_PAGE_MONITOR_SUMMARY:  Draw_Header(S_SUMMARY);                      break;
                case UI_PAGE_MONITOR_FREQ:     Draw_Header(S_MON_FREQ);                     break;
                case UI_PAGE_MONITOR_VOLT:     Draw_Header(S_MON_VOLT);                     break;
                case UI_PAGE_MONITOR_CURR:     Draw_Header(S_MON_CURR);                     break;
                case UI_PAGE_WIFI_SETUP:       Draw_Header(S_LAUNCH);                       break;
                case UI_PAGE_FAULT:            Draw_Header(S_FAULT_TITLE);                  break;
            }
        }
    }

    /* ── Phase 1: Fault edge detection ── */
    {
        uint8_t current_fault = (Inverter_Control_Soft_Start_Get_State()
                                 == INVERTER_CONTROL_SS_STATE_FAULT);
        if (current_fault && !s_was_fault_state) {
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
            s_page_drawn = 0;
        }
        if (!current_fault) {
            s_was_fault_state = 0;
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

    /* ── Phase 3: Key scan + dispatch ── */
    old_cursor = s_menu_cursor;
    {
        Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
        Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
        Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
        Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);
        Handle_Keys_by_Page(s_page, k0, k1, k2, k3);
    }
    if (s_menu_cursor != old_cursor) cursor_changed = 1;

    /* ── Phase 4: Page change detection ── */
    if ((uint8_t)s_page != s_last_page) {
        s_last_page  = (uint8_t)s_page;
        s_page_drawn = 0;
        s_last_cursor_idx = s_menu_cursor;
        /* Invalidate all tracking state */
        s_last_is_running    = 0xFF;
        s_last_is_fault_menu = 0xFF;
        s_last_sweep_stopped = 0xFF;
        s_last_wifi_cs       = 0xFF;
        s_last_retry         = 0xFF;
        s_last_sub_visible   = 0;
        s_last_f_str[0] = '\0';
        s_last_v_str[0] = '\0';
        s_last_i_str[0] = '\0';
        s_last_status_buf[0] = '\0';
        s_last_retry_buf[0]  = '\0';
    }

    /* ── Phase 5: 200ms tick ── */
    if (Sys_Timer_Get_Tick() - s_last_ui_ms >= UI_REFRESH_MS) {
        s_last_ui_ms = Sys_Timer_Get_Tick();
        tick_200ms = 1;
    }

    /* ── Phase 6: Cursor boundary clamp ── */
    if (s_page == UI_PAGE_MAIN_MENU) {
        uint8_t is_fault = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT);
        uint8_t max_cursor = is_fault ? 3 : 2;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    if (s_page == UI_PAGE_MONITOR_SUB_MENU) {
        if (s_menu_cursor > 4) s_menu_cursor = 0;
    }

    /* ── Phase 7: PB10 PowerContrl ── */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* ── Phase 8: Overcurrent protection ── */
    if (s_page == UI_PAGE_SWEEP ||
        s_page == UI_PAGE_MONITOR_SUMMARY ||
        s_page == UI_PAGE_MONITOR_FREQ ||
        s_page == UI_PAGE_MONITOR_VOLT ||
        s_page == UI_PAGE_MONITOR_CURR) {
        Update_EMA();
        if (s_ema_i > UI_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
            s_page_drawn = 0;
            s_last_page  = 0xFF;  /* force full redraw */
        }
    }
    if (s_page != UI_PAGE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

    /* ════════════════════════════════════════════════════════════
     *  Phase 9: Draw — full or incremental
     * ════════════════════════════════════════════════════════════ */

    if (!s_page_drawn) {
        /* ── Full page draw (page entry) ── */
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
        }
        Update_Leds(s_page);
        s_page_drawn = 1;
        cursor_changed = 0;  /* consumed by full draw */
    } else {
        /* ── Incremental updates — only touch changed pixels ── */

        /* Cursor move: 2-line update for menu pages */
        if (cursor_changed) {
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:
                    Main_Menu_Cursor_Update(old_cursor);
                    break;
                case UI_PAGE_MONITOR_SUB_MENU:
                    Sub_Menu_Cursor_Update(old_cursor);
                    break;
                default:
                    /* Non-menu pages: cursor change triggers full redraw
                     * (only MONITOR_SUMMARY/FREQ use cursor for freq stepping,
                     *  and they redraw completely every 200ms anyway) */
                    s_page_drawn = 0;
                    break;
            }
        }

        /* 200ms dynamic value update */
        if (tick_200ms) {
            switch (s_page) {
                case UI_PAGE_MAIN_MENU:
                    Main_Menu_Dynamic_Update();
                    break;
                case UI_PAGE_MONITOR_SUB_MENU:
                    /* Static page — nothing to update */
                    break;
                case UI_PAGE_SWEEP:
                    Sweep_Dynamic_Update();
                    break;
                case UI_PAGE_MONITOR_SUMMARY:
                    Summary_Dynamic_Update();
                    break;
                case UI_PAGE_MONITOR_FREQ:
                    Freq_Dynamic_Update();
                    break;
                case UI_PAGE_MONITOR_VOLT:
                    Volt_Dynamic_Update();
                    break;
                case UI_PAGE_MONITOR_CURR:
                    Curr_Dynamic_Update();
                    break;
                case UI_PAGE_WIFI_SETUP:
                    WiFi_Dynamic_Update();
                    break;
                case UI_PAGE_FAULT:
                    /* Static page — nothing to update */
                    break;
            }
            Update_Leds(s_page);
        }
    }
}

/* ================================================================
 *  Public Interface
 * ================================================================ */
Ui_Page Ui_Controller_Get_Page(void)      { return s_page; }
uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }
