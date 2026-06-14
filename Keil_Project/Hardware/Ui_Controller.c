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
#define S_SUMMARY   "\xe7\xbb\xbc\xe5\x90\x88\xe7\x9b\x91\xe6\xb5\x8b" /* summary */
#define S_BACK      "\xe8\xbf\x94\xe5\x9b\x9e\xe4\xb8\xbb\xe8\x8f\x9c\xe5\x8d\x95" /* back to main */
#define S_OK        "\xe7\xa1\xae\xe5\xae\x9a"                     /* 确定 */
#define S_CANCEL    "\xe5\x8f\x96\xe6\xb6\x88"                     /* 取消 */
#define S_RETURN    "\xe8\xbf\x94\xe5\x9b\x9e"                     /* 返回 */
#define S_DIV       "--------------------"           /* divider */

/* -------- Page state variables -------- */
static Ui_Page  s_page            = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor     = 0;
static uint8_t  s_was_fault_state = 0;
static uint8_t  s_no_wifi_mode    = 0;    /* 0=auto-connect at boot, 1=WiFi cleared by user */
static uint8_t  s_last_page       = 0xFF;
static uint8_t  s_last_cursor     = 0xFF;

/* EMA smoothing */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

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
    uint8_t col_start = 0;  /* text start column after prefix */

    if (!enabled) {
        color = UI_COLOR_DIM;
    }

    if (cursor == idx) {
        /* Selected: fill entire row with cyan, draw black text over it */
        Tft_Driver_Fill_Rect(0, (uint16_t)line * TFT_FONT_HEIGHT,
                            TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_VALUE);
        Tft_Driver_Show_CN_String(line, 0, "\xe2\x96\xb6", UI_COLOR_BG, UI_COLOR_VALUE);
        col_start = 1;
        color = UI_COLOR_BG;
    } else {
        Tft_Driver_Show_String(line, 0, "  ", UI_COLOR_TEXT, UI_COLOR_BG);
        col_start = 1;
    }

    Tft_Driver_Show_CN_String(line, col_start, text, color,
        (cursor == idx) ? UI_COLOR_VALUE : UI_COLOR_BG);
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
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }

    Draw_Header(S_WPT_PWM);
    Draw_Divider(1);

    /* Item 1: Start PWM / Stop PWM (dynamic text) */
    {
        /* "1. \xe5\x90\xaf\xe5\x8a\xa8PWM" = 1.启动PWM, "1. \xe5\x81\x9c\xe6\xad\xa2PWM" = 1.停止PWM */
        const char* t1 = is_running
            ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"
            : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";
        Draw_Menu_Item(2, s_menu_cursor, 0, t1, 1);
    }

    /* Item 2: Status Monitor */
    Draw_Menu_Item(3, s_menu_cursor, 1, "2. " S_MONITOR, 1);

    /* Item 3: WiFi Setup */
    Draw_Menu_Item(4, s_menu_cursor, 2, "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91", 1);

    /* Item 4: Fault Clear (only when faulted) */
    Draw_Menu_Item(5, s_menu_cursor, 3, "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4", is_fault ? 1 : 0);

    Draw_Divider(6);

    Tft_Driver_Show_CN_String(7, Right("ON:" S_OK " PAGE:" S_CANCEL),
        "ON:" S_OK " PAGE:" S_CANCEL, UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- Monitor Sub-Menu (5 items) -------- */
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

/* -------- Sweep Page -------- */
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

    /* Frequency */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Energy bar + percentage */
    Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                   14 * TFT_FONT_WIDTH, 8,
                   (float)progress, 0.0f, 10.0f,
                   ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
    snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(progress * 10));
    if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);

    /* Voltage / Current */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);

    Draw_Divider(6);
    Tft_Driver_Show_CN_String(7, Right("ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "ON:\xe7\xa1\xae\xe5\xae\x9a PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* -------- Monitor Summary (dual mode: idle / running) -------- */
static void Draw_Monitor_Summary(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    char buf[21];

    Update_EMA();

    Draw_Header(S_SUMMARY);
    Draw_Divider(1);

    /* Frequency */
    if (is_running) {
        Fmt_F(buf, s_ema_f);
    } else {
        snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz");
    }
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Voltage */
    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Current */
    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Energy bar (only when running) */
    if (is_running) {
        Energy_Bar_Draw(0, 5 * TFT_FONT_HEIGHT, TFT_WIDTH, 12,
                       s_ema_v, 0.0f, 48.0f,
                       ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
    }

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
    char buf[21];

    if (cs == APP_NETWORK_CONN_ONLINE)
        status_text = "\xe5\xb7\xb2\xe8\xbf\x9e\xe7\xba\xbf\xe4\xb8\x8a\xe7\xba\xbf";  /* online */
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = "\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5";              /* failed */
    else if (App_Network_Is_Connecting())
        status_text = "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad";                          /* connecting */
    else
        status_text = "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5";                          /* disconnected */

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
    Tft_Driver_Show_CN_String(7, Right("PAGE:\xe8\xbf\x94\xe5\x9b\x9e"),
        "PAGE:\xe8\xbf\x94\xe5\x9b\x9e", UI_COLOR_TEXT, UI_COLOR_BG);
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

    /* -------- F_UP (k1): cursor up OR freq +1kHz -------- */
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

    /* -------- F_DOWN (k2): cursor down OR freq -1kHz -------- */
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
                Inverter_Control_Soft_Start_Stop();
                break;

            case UI_PAGE_WIFI_SETUP: {
                /* ON in WiFi page: if online -> disconnect (no-WiFi mode);
                 * if not online -> reconnect + clear WiFi for re-config.
                 * WiFiManager remembers config, so CLEAR forces hotspot mode. */
                uint8_t cs = App_Network_Get_Connect_Status();
                if (cs == APP_NETWORK_CONN_ONLINE) {
                    /* Connected: disconnect and enter no-WiFi mode */
                    App_Network_Soft_Reset();
                    s_no_wifi_mode = 1;
                    s_page = UI_PAGE_MAIN_MENU;
                    s_menu_cursor = 0;
                } else {
                    /* Not connected: clear old config and reconnect */
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

    /* -------- KEY0 long-press: clear WiFi (any page) -------- */
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

    /* -------- PAGE (k3): go back to main menu -------- */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        s_page = UI_PAGE_MAIN_MENU;
        s_menu_cursor = 0;
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
        }
    }

    /* -- 3. Key scan (every frame) -- */
    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);

    Handle_Keys_by_Page(s_page, k0, k1, k2, k3);

    /* -- 4. Page change detection (after key handling) -- */
    /*     Only full-clear on PAGE CHANGE, not cursor move.            */
    /*     Cursor moves use incremental redraw (old line + new line).  */
    if ((uint8_t)s_page != s_last_page) {
        s_last_page   = (uint8_t)s_page;
        s_last_cursor = s_menu_cursor;
        Tft_Driver_Clear(UI_COLOR_BG);
        Reset_EMA();
        need_draw = 1;
    } else if (s_menu_cursor != s_last_cursor) {
        /* Cursor moved: erase old row + redraw menu, no full clear */
        uint8_t old_cursor = s_last_cursor;
        s_last_cursor = s_menu_cursor;
        /* Erase old selected row (clear to BG) */
        if (old_cursor < 8) {
            /* line offset: main menu items start at line 2, sub-menu at line 2 */
            uint8_t line_old = (s_page == UI_PAGE_MAIN_MENU) ? (2 + old_cursor)
                              : (s_page == UI_PAGE_MONITOR_SUB_MENU && old_cursor < 4) ? (2 + old_cursor)
                              : (s_page == UI_PAGE_MONITOR_SUB_MENU && old_cursor == 4) ? 7
                              : 0xFF;
            if (line_old != 0xFF) {
                Tft_Driver_Fill_Rect(0, (uint16_t)line_old * TFT_FONT_HEIGHT,
                                    TFT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
            }
        }
        need_draw = 1;
    }

    /* -- 5. 200ms periodic refresh -- */
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
            s_last_page = 0xFF;   /* force clear+redraw */
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
