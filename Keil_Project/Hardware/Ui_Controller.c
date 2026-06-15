/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   Ui Controller V11 — incremental refresh, zero-flicker, ICON_STAR
 * @note    TFT 8x20 cols, 160x128 landscape, 4 keys: F+/F-/KEY0/PAGE
 *          Architecture: static text drawn ONCE on page entry,
 *          cursor changes update only 2 lines (erase old ★ + draw new ★),
 *          200ms cycle only updates changing values (F/V/I/bar/status).
 *          Menu pages idle at 0% SPI activity — no writes unless state changes.
 *          Every _Full() covers all 8 rows → zero cross-page pixel residue.
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
#include <string.h>

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
#define S_BOTTOM_L_CONFIRM "ON:\xe7\xa1\xae\xe5\xae\x9a"           /* ON:确定 */
#define S_BOTTOM_L_STOP    "ON:\xe5\x81\x9c\xe6\xad\xa2"           /* ON:停止 */
#define S_BOTTOM_L_CONT    "ON:\xe7\xbb\xa7\xe7\xbb\xad"           /* ON:继续 */
#define S_BOTTOM_L_TUNE    "F+/F-:\xe8\xb0\x83\xe9\xa2\x91"        /* F+/F-:调频 */
#define S_BOTTOM_R         "PAGE:\xe8\xbf\x94\xe5\x9b\x9e"         /* PAGE:返回 */
#define S_HUD_L            ">>> "
#define S_HUD_R            " <<<"
#define S_BADGE_OK         "[OK]"
#define S_BADGE_WARN       "[WARN]"
#define S_BADGE_HI         "[HI]"
#define S_SWEEP_BADGE      "[SWEEP]"
#define S_DONE_BADGE       "[DONE]"
#define S_IDLE_BADGE       "[IDLE]"
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
static uint8_t s_last_is_running    = 0xFF; /* tracked PWM running state */
static uint8_t s_last_is_fault_menu = 0xFF; /* tracked FAULT state for menu item 3 */
static uint8_t s_last_sub_visible   = 0;    /* tracked sub-menu visible_top */
static uint8_t s_last_sweep_stopped = 0xFF; /* tracked sweep pause state */
static uint8_t s_last_wifi_cs       = 0xFF; /* tracked WiFi connection status */
static uint8_t s_last_retry         = 0xFF; /* tracked WiFi retry count */

/* Cached last formatted value strings — avoid redrawing unchanged values */
static char    s_last_f_str[21];
static char    s_last_v_str[21];
static char    s_last_i_str[21];
static char    s_last_status_buf[42];
static char    s_last_retry_buf[16];

/* Gauge badge tracking — only redraw badge when level changes */
static uint8_t s_last_volt_badge = 0xFF;  /* 0=OK, 1=WARN, 2=HI */
static uint8_t s_last_curr_badge = 0xFF;  /* 0=OK, 1=WARN */
static uint8_t s_last_freq_badge = 0xFF;  /* 0=SWEEP, 1=DONE, 2=IDLE */

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
 *  Cursor: ICON_STAR at pixel x=0 — draw/erase (16x16 pixel update)
 * ================================================================ */
static void Draw_Cursor(uint8_t line)
{
    /* ICON_STAR (diamond) at left edge — black star on cyan bg for selected row */
    Tft_Driver_Draw_Single_Icon(0, (uint16_t)line * TFT_FONT_HEIGHT,
                                ICON_STAR, UI_COLOR_BG, UI_COLOR_VALUE);
}

static void Erase_Cursor(uint8_t line)
{
    /* Erase the 16x16 icon area with black */
    Tft_Driver_Fill_Rect(0, (uint16_t)line * TFT_FONT_HEIGHT,
                         16, 16, UI_COLOR_BG);
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

/* ── Bottom bar (row 7): left_text (col 0, left-aligned) + S_BOTTOM_R (right-aligned) ── */
static void Draw_Bottom_Bar(const char* left_text)
{
    Erase_Line(7);
    Tft_Driver_Show_CN_String(7, 0, left_text, UI_COLOR_TEXT, UI_COLOR_BG);
    Tft_Driver_Show_CN_String(7, Right(S_BOTTOM_R),
        S_BOTTOM_R, UI_COLOR_TEXT, UI_COLOR_BG);
}

/* ── HUD value on row 2: >>> {formatted_value} <<<  centered ── */
static void Draw_HUD_Value(uint8_t line, const char* formatted_buf)
{
    /* Erase full line first */
    Erase_Line(line);

    /* Draw left chevron at col 0 */
    Tft_Driver_Show_String(line, 0, S_HUD_L, UI_COLOR_DIM, UI_COLOR_BG);

    /* Draw value centered (it contains the leading Chinese label like "电压V:xx.xxV") */
    Tft_Driver_Show_CN_String(line, Center(formatted_buf), formatted_buf, UI_COLOR_VALUE, UI_COLOR_BG);

    /* Draw right chevron at col 16 (16*8=128px, leaves 2 cols on right = 16px) */
    Tft_Driver_Show_String(line, 16, S_HUD_R, UI_COLOR_DIM, UI_COLOR_BG);
}

/* ── Gauge trough (dark background behind energy bar) ── */
static void Draw_Gauge_Trough(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    Tft_Driver_Fill_Rect(x, y, w, h, 0x2104);  /* dark grey-blue */
}

/* ── 5 tick marks above the energy bar ── */
static void Draw_Gauge_Ticks(uint16_t x, uint16_t y, uint16_t w)
{
    uint8_t ti;
    for (ti = 0; ti < 5; ti++) {
        /* tick at x + (w/4)*ti, centered */
        uint16_t tx = x + (uint16_t)(((uint32_t)w * ti) / 4);
        if (tx > x + w) tx = x + w;
        Tft_Driver_Fill_Rect(tx, y, 2, 3, UI_COLOR_DIM);
    }
}

/* ── Draw menu text at line,col (erases whole line first, text at col≥2 for star) ── */
static void Draw_Menu_Text(uint8_t line, uint8_t col, const char* text, uint8_t enabled)
{
    uint16_t color = enabled ? UI_COLOR_TEXT : UI_COLOR_DIM;
    Erase_Line(line);
    Tft_Driver_Show_CN_String(line, col, text, color, UI_COLOR_BG);
}

/* ================================================================
 *  Page draw: MAIN_MENU (4 items) — covers all 8 rows
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

    Draw_Header(S_WPT_PWM);       /* row 0 */
    Draw_Divider(1);              /* row 1 */

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
        Draw_Menu_Text(2 + i, 2, text, enabled);    /* text at col=2, leaves col-0/1 for star */
    }

    Draw_Cursor(2 + s_menu_cursor);   /* rows 2-5 */

    Draw_Divider(6);              /* row 6 */
    Draw_Bottom_Bar(S_BOTTOM_L_CONFIRM);

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
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }

    if (is_running != s_last_is_running) {
        const char* text = is_running
            ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"
            : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";
        Draw_Menu_Text(2, 2, text, 1);
        if (s_menu_cursor == 0) Draw_Cursor(2);
        s_last_is_running = is_running;
    }

    if (is_fault != s_last_is_fault_menu) {
        const char* text = "4. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4";
        uint8_t enabled = is_fault ? 1 : 0;
        Draw_Menu_Text(5, 2, text, enabled);
        if (s_menu_cursor == 3) Draw_Cursor(5);
        s_last_is_fault_menu = is_fault;
    }
}

/* ================================================================
 *  Page draw: MONITOR_SUB_MENU (5 items, 4-row window) — covers all 8 rows
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

    Draw_Header(S_MONITOR);       /* row 0 */
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

    Draw_Cursor(2 + (s_menu_cursor - visible_top));   /* rows 2-5 */

    Draw_Divider(6);              /* row 6 */
    Draw_Bottom_Bar("");                                      /* only PAGE:返回 on right */

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

    Draw_Header(S_SWEEP);         /* row 0 */
    Draw_Divider(1);              /* row 1 */

    /* row 2: Frequency */
    snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
             (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
    Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* row 3: Progress bar area */
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

    /* row 4: Voltage */
    Fmt_V(buf, Adc_Driver_Get_Voltage());
    Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* row 5: Current */
    Fmt_I(buf, Adc_Driver_Get_Current());
    Tft_Driver_Show_CN_String(5, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    Draw_Divider(6);              /* row 6 */
    Draw_Bottom_Bar(is_stopped ? S_BOTTOM_L_CONT : S_BOTTOM_L_STOP);  /* row 7 */

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

    /* Bottom hint */
    if (is_stopped != s_last_sweep_stopped) {
        Draw_Bottom_Bar(is_stopped ? S_BOTTOM_L_CONT : S_BOTTOM_L_STOP);
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

    Draw_Header(S_SUMMARY);       /* row 0 */
    Draw_Divider(1);              /* row 1 */

    /* row 2: Freq */
    if (is_running) { Fmt_F(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz"); }
    Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* row 3: Voltage */
    Fmt_V(buf, s_ema_v);
    Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* row 4: Current */
    Fmt_I(buf, s_ema_i);
    Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    /* row 5: blank — erase any residue from previous page */
    Erase_Line(5);

    Draw_Divider(6);              /* row 6 */
    {
        const char* hint = is_running ? S_BOTTOM_L_TUNE : S_BOTTOM_L_CONFIRM;
        Draw_Bottom_Bar(hint);     /* row 7 */
    }

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
    else            { snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz"); }
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
        const char* hint = is_running ? S_BOTTOM_L_TUNE : S_BOTTOM_L_CONFIRM;
        Draw_Bottom_Bar(hint);
        s_last_is_running = is_running;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  MONITOR_FREQ (gauge) — HUD industrial style, covers all 8 rows
 *  Row 2: >>> 频率F:xxx.xkHz <<<  (HUD chevrons)
 *  Row 3: tick marks above bar
 *  Row 4: energy bar (dark trough + bar + ticks)
 *  Row 5: 95  [SWEEP/DONE/IDLE]  150
 *  Row 6: divider
 *  Row 7: bottom bar
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Freq_Full(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    Inverter_Control_Soft_Start_State sw_state = Inverter_Control_Soft_Start_Get_State();
    uint8_t badge;
    const char* badge_text;
    char buf[21];

    Update_EMA();
    Draw_Header(S_MON_FREQ);       /* row 0 */
    Draw_Divider(1);               /* row 1 */

    /* row 2: HUD chevron-wrapped freq value */
    if (is_running) { Fmt_F(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz"); }
    Draw_HUD_Value(2, buf);
    strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
    s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';

    /* row 3: Tick marks (5 ticks above the energy bar on row 4) */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;         /* x=32 */
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;        /* w=96 */
        Erase_Line(3);
        Draw_Gauge_Ticks(bar_x, 3 * TFT_FONT_HEIGHT + 12, bar_w); /* ticks at bottom of row 3 */
    }

    /* row 4: Energy bar with dark trough */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Draw_Gauge_Trough(bar_x, bar_y, bar_w, bar_h);
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       is_running ? s_ema_f : 0.0f, 95.0f, 150.0f,
                       ENERGY_BAR_METRIC_FREQ, 0x2104);
    }

    /* row 5: range labels + status badge */
    {
        /* Determine freq status */
        if      (sw_state == INVERTER_CONTROL_SS_STATE_SWEEP) { badge = 0; badge_text = S_SWEEP_BADGE; }
        else if (sw_state == INVERTER_CONTROL_SS_STATE_DONE)  { badge = 1; badge_text = S_DONE_BADGE;  }
        else                                                   { badge = 2; badge_text = S_IDLE_BADGE;  }

        Erase_Line(5);
        Tft_Driver_Show_String(5, 4, "95", UI_COLOR_TITLE, UI_COLOR_BG);
        {
            uint16_t badge_color;
            if      (badge == 0) badge_color = UI_COLOR_VALUE;  /* SWEEP=cyan */
            else if (badge == 1) badge_color = UI_COLOR_OK;     /* DONE=green */
            else                 badge_color = UI_COLOR_DIM;    /* IDLE=gray */
            Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                                   badge_text, badge_color, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 17, "150", UI_COLOR_TITLE, UI_COLOR_BG);
        s_last_freq_badge = badge;
    }

    Draw_Divider(6);               /* row 6 */
    Draw_Bottom_Bar(is_running ? S_BOTTOM_L_TUNE : S_BOTTOM_L_CONFIRM);  /* row 7 */

    s_last_is_running = is_running;
}

static void Freq_Dynamic_Update(void)
{
    uint8_t is_running = (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE);
    Inverter_Control_Soft_Start_State sw_state = Inverter_Control_Soft_Start_Get_State();
    uint8_t badge;
    const char* badge_text;
    char buf[21];

    Update_EMA();

    /* Frequency value (HUD row 2) */
    if (is_running) { Fmt_F(buf, s_ema_f); }
    else            { snprintf(buf, sizeof(buf), S_FREQ "F:---.-kHz"); }
    if (strncmp(buf, s_last_f_str, sizeof(s_last_f_str)) != 0) {
        Draw_HUD_Value(2, buf);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Energy bar */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       is_running ? s_ema_f : 0.0f, 95.0f, 150.0f,
                       ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
    }

    /* Status badge (row 5 center) */
    if      (sw_state == INVERTER_CONTROL_SS_STATE_SWEEP) { badge = 0; badge_text = S_SWEEP_BADGE; }
    else if (sw_state == INVERTER_CONTROL_SS_STATE_DONE)  { badge = 1; badge_text = S_DONE_BADGE;  }
    else                                                   { badge = 2; badge_text = S_IDLE_BADGE;  }

    if (badge != s_last_freq_badge) {
        uint16_t badge_color;
        if      (badge == 0) badge_color = UI_COLOR_VALUE;
        else if (badge == 1) badge_color = UI_COLOR_OK;
        else                 badge_color = UI_COLOR_DIM;
        /* Erase center of row 5 (~col 7 to 13) */
        Tft_Driver_Fill_Rect(7 * TFT_FONT_WIDTH, 5 * TFT_FONT_HEIGHT,
                             6 * TFT_FONT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
        Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                               badge_text, badge_color, UI_COLOR_BG);
        s_last_freq_badge = badge;
    }

    /* Bottom bar */
    if (is_running != s_last_is_running) {
        Draw_Bottom_Bar(is_running ? S_BOTTOM_L_TUNE : S_BOTTOM_L_CONFIRM);
        s_last_is_running = is_running;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  MONITOR_VOLT (gauge) — HUD industrial style, covers all 8 rows
 *  Row 2: >>> 电压V:xx.xxV <<<
 *  Row 3: tick marks
 *  Row 4: energy bar
 *  Row 5: 0  [OK/WARN/HI]  48
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Volt_Full(void)
{
    char buf[21];
    uint8_t badge;           /* 0=OK, 1=WARN, 2=HI */
    const char* badge_text;
    uint16_t badge_color;

    Update_EMA();
    Draw_Header(S_MON_VOLT);       /* row 0 */
    Draw_Divider(1);               /* row 1 */

    /* row 2: HUD chevron-wrapped voltage value */
    Fmt_V(buf, s_ema_v);
    Draw_HUD_Value(2, buf);
    strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
    s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';

    /* row 3: Tick marks */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        Erase_Line(3);
        Draw_Gauge_Ticks(bar_x, 3 * TFT_FONT_HEIGHT + 12, bar_w);
    }

    /* row 4: Energy bar with dark trough */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Draw_Gauge_Trough(bar_x, bar_y, bar_w, bar_h);
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       s_ema_v, 0.0f, 48.0f,
                       ENERGY_BAR_METRIC_VOLT, 0x2104);
    }

    /* row 5: range labels + status badge */
    if      (s_ema_v > 40.0f) { badge = 2; badge_text = S_BADGE_HI;   badge_color = UI_COLOR_ALARM; }
    else if (s_ema_v > 36.0f) { badge = 1; badge_text = S_BADGE_WARN; badge_color = UI_COLOR_VALUE; }
    else                       { badge = 0; badge_text = S_BADGE_OK;   badge_color = UI_COLOR_OK;    }

    Erase_Line(5);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                           badge_text, badge_color, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 17, "48", UI_COLOR_TITLE, UI_COLOR_BG);
    s_last_volt_badge = badge;

    Draw_Divider(6);               /* row 6 */
    Draw_Bottom_Bar("");                                       /* only PAGE:返回 on right */
}

static void Volt_Dynamic_Update(void)
{
    char buf[21];
    uint8_t badge;
    const char* badge_text;
    uint16_t badge_color;

    Update_EMA();

    /* Voltage value (HUD row 2) */
    Fmt_V(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Draw_HUD_Value(2, buf);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Energy bar */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       s_ema_v, 0.0f, 48.0f,
                       ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
    }

    /* Status badge (row 5 center) — dirty-checked */
    if      (s_ema_v > 40.0f) { badge = 2; badge_text = S_BADGE_HI;   badge_color = UI_COLOR_ALARM; }
    else if (s_ema_v > 36.0f) { badge = 1; badge_text = S_BADGE_WARN; badge_color = UI_COLOR_VALUE; }
    else                       { badge = 0; badge_text = S_BADGE_OK;   badge_color = UI_COLOR_OK;    }

    if (badge != s_last_volt_badge) {
        /* Erase center of row 5 */
        Tft_Driver_Fill_Rect(7 * TFT_FONT_WIDTH, 5 * TFT_FONT_HEIGHT,
                             6 * TFT_FONT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
        Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                               badge_text, badge_color, UI_COLOR_BG);
        s_last_volt_badge = badge;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  MONITOR_CURR (gauge) — HUD industrial style, covers all 8 rows
 *  Row 2: >>> 电流I:+x.xxxA <<<
 *  Row 3: tick marks
 *  Row 4: energy bar
 *  Row 5: 0  [OK/WARN]  3
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Curr_Full(void)
{
    char buf[21];
    uint8_t badge;
    const char* badge_text;
    uint16_t badge_color;

    Update_EMA();
    Draw_Header(S_MON_CURR);       /* row 0 */
    Draw_Divider(1);               /* row 1 */

    /* row 2: HUD chevron-wrapped current value */
    Fmt_I(buf, s_ema_i);
    Draw_HUD_Value(2, buf);
    strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
    s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';

    /* row 3: Tick marks */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        Erase_Line(3);
        Draw_Gauge_Ticks(bar_x, 3 * TFT_FONT_HEIGHT + 12, bar_w);
    }

    /* row 4: Energy bar with dark trough */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Draw_Gauge_Trough(bar_x, bar_y, bar_w, bar_h);
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       s_ema_i, 0.0f, 3.0f,
                       ENERGY_BAR_METRIC_CURR, 0x2104);
    }

    /* row 5: range labels + status badge */
    if      (s_ema_i > 2.5f) { badge = 1; badge_text = S_BADGE_WARN; badge_color = UI_COLOR_VALUE; }
    else                      { badge = 0; badge_text = S_BADGE_OK;   badge_color = UI_COLOR_OK;    }

    Erase_Line(5);
    Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
    Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                           badge_text, badge_color, UI_COLOR_BG);
    Tft_Driver_Show_String(5, 18, "3", UI_COLOR_TITLE, UI_COLOR_BG);
    s_last_curr_badge = badge;

    Draw_Divider(6);               /* row 6 */
    Draw_Bottom_Bar("");                                       /* only PAGE:返回 on right */
}

static void Curr_Dynamic_Update(void)
{
    char buf[21];
    uint8_t badge;
    const char* badge_text;
    uint16_t badge_color;

    Update_EMA();

    /* Current value (HUD row 2) */
    Fmt_I(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Draw_HUD_Value(2, buf);
        strncpy(s_last_i_str, buf, sizeof(s_last_i_str));
        s_last_i_str[sizeof(s_last_i_str) - 1] = '\0';
    }

    /* Energy bar */
    {
        uint16_t bar_x = 4 * TFT_FONT_WIDTH;
        uint16_t bar_y = 4 * TFT_FONT_HEIGHT + 2;
        uint16_t bar_w = 12 * TFT_FONT_WIDTH;
        uint16_t bar_h = 8;
        Energy_Bar_Draw(bar_x, bar_y, bar_w, bar_h,
                       s_ema_i, 0.0f, 3.0f,
                       ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
    }

    /* Status badge (row 5 center) — dirty-checked */
    if      (s_ema_i > 2.5f) { badge = 1; badge_text = S_BADGE_WARN; badge_color = UI_COLOR_VALUE; }
    else                      { badge = 0; badge_text = S_BADGE_OK;   badge_color = UI_COLOR_OK;    }

    if (badge != s_last_curr_badge) {
        Tft_Driver_Fill_Rect(7 * TFT_FONT_WIDTH, 5 * TFT_FONT_HEIGHT,
                             6 * TFT_FONT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);
        Tft_Driver_Show_String(5, (uint8_t)(10 - (uint8_t)(strlen(badge_text) / 2)),
                               badge_text, badge_color, UI_COLOR_BG);
        s_last_curr_badge = badge;
    }
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
        status_text = S_WIFI_ONLINE;
    else if (cs == APP_NETWORK_CONN_FAILED)
        status_text = S_WIFI_FAILED;
    else if (App_Network_Is_Connecting())
        status_text = S_WIFI_CONN;
    else
        status_text = S_WIFI_IDLE;

    hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? S_ON_DISCONNECT : S_ON_CONNECT;

    Draw_Header(S_LAUNCH);         /* row 0 */
    Draw_Divider(1);               /* row 1 */

    /* row 2: Status */
    {
        char buf[42];
        snprintf(buf, sizeof(buf), S_WIFI_FORMAT ": %s", status_text);
        Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        strncpy(s_last_status_buf, buf, sizeof(s_last_status_buf));
        s_last_status_buf[sizeof(s_last_status_buf) - 1] = '\0';
    }

    /* row 3: Retry count (or blank) */
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

    /* row 4: blank — spacer between info and action hints */
    Erase_Line(4);

    /* row 5: ON action hint */
    Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, UI_COLOR_TEXT, UI_COLOR_BG);
    /* row 6: Long-press clear hint */
    Tft_Driver_Show_CN_String(6, Right(S_LONG_CLEAR), S_LONG_CLEAR, UI_COLOR_ALARM, UI_COLOR_BG);
    Draw_Divider(7);               /* row 7 */

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

    if (need_hint_update) {
        hint_text = (cs == APP_NETWORK_CONN_ONLINE) ? S_ON_DISCONNECT : S_ON_CONNECT;
        Tft_Driver_Show_CN_String(5, Right(hint_text), hint_text, UI_COLOR_TEXT, UI_COLOR_BG);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  FAULT — fully static, covers all 8 rows
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Fault_Full(void)
{
    Draw_Header(S_FAULT_TITLE);     /* row 0 */
    Draw_Divider(1);                /* row 1 */

    Tft_Driver_Show_CN_String(2, Center(S_OVERCUR),
        S_OVERCUR, UI_COLOR_ALARM, UI_COLOR_BG);      /* row 2 */
    Tft_Driver_Show_CN_String(3, Center(S_PWM_OFF),
        S_PWM_OFF, UI_COLOR_TEXT, UI_COLOR_BG);        /* row 3 */

    Erase_Line(4);                  /* row 4: blank spacer */

    Tft_Driver_Show_CN_String(5, Center(S_RESET_HINT),
        S_RESET_HINT, UI_COLOR_VALUE, UI_COLOR_BG);    /* row 5 */

    Draw_Divider(6);                /* row 6 */
    Draw_Bottom_Bar("");                                          /* only PAGE:返回 on right */
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
 *  Phase 4: Page change → s_page_drawn=0; all tracking invalidated
 *  Phase 5: 200ms tick → dynamic incremental update (values only)
 *  Phase 6: Cursor boundary clamp
 *  Phase 7: PB10 PowerContrl
 *  Phase 8: Overcurrent protection
 *  Phase 9: Draw — full page only when s_page_drawn==0
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
        Tft_Driver_Clear(UI_COLOR_BG);  /* erase all previous-page residue */
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
        cursor_changed = 0;
    } else {
        /* ── Incremental updates — only touch changed pixels ── */

        if (cursor_changed) {
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
