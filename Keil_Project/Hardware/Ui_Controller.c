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
#include "Sys_Core.h"
#include "Tft_Driver.h"
#include "TFT_Font_Data.h"
#include "Key_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Esp8266_Driver.h"
#include "App_Network.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Sys_Timer.h"

/* ── Energy Bar color table + draw logic (merged from Energy_Bar.c) ── */
static const uint16_t EB_COLOR_TABLE[8] = {
    0x07E0, 0x2FE0, 0x5FE0, 0x87E0, 0xFF80, 0xFD00, 0xF900, 0xF800  /* 绿→黄→红 RGB565 */
};

static void Ui_Ui_Energy_Bar_Draw(uint16_t x, uint16_t y, uint16_t max_w, uint16_t h,
                                float value, float min_val, float max_val, uint16_t bg_color)
{
    uint16_t total_w;
    uint8_t  seg_count, i;
    uint16_t seg_w, seg_x;

    {
        float range = max_val - min_val;
        float ratio;
        if (range <= 0.0f) { Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color); return; }
        ratio = (value - min_val) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        total_w = (uint16_t)(ratio * (float)max_w);
    }

    Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color);
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
#include <stdio.h>
#include <string.h>
#include <math.h>

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
#define S_ON_DISCONNECT   "ON:\xe6\x96\xad\xe5\xbc\x80WIFI"
#define S_ON_CONNECT      "ON:\xe8\xbf\x9e\xe6\x8e\xa5WIFI"
#define S_LONG_CLEAR      "\xe9\x95\xbf\xe6\x8c\x89ON:" S_CLEAR_WIFI

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

/* Gauge value & status cache (for diff-based incremental refresh) */
static char    s_gauge_val_str[24] = "";
static char    s_gauge_status_buf[24] = "";

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
    snprintf(buf, 21, "\xe9\xa2\x91\xe7\x8e\x87" "F:%3d.%01dkHz", (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

/* ================================================================
 *  Draw_Header: line0 title(left) — icons via Draw_TopRight_Icons
 * ================================================================ */
static void Draw_Header(const char* title)
{
    Tft_Driver_Erase_Pixel_Area(0, 0, TFT_WIDTH, TFT_FONT_HEIGHT);
    Tft_Driver_Show_CN_String(0, 0, title, UI_COLOR_TITLE, UI_COLOR_BG);
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

/* ── Draw menu text at line,col (precise erase excluding cursor zone, text at col≥2 for star) ── */
static void Draw_Menu_Text(uint8_t line, uint8_t col, const char* text, uint8_t enabled)
{
    uint16_t color = enabled ? UI_COLOR_TEXT : UI_COLOR_DIM;
    Tft_Driver_Erase_Pixel_Area(col * 8, (uint16_t)line * TFT_FONT_HEIGHT, TFT_WIDTH - col * 8, TFT_FONT_HEIGHT);
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
        Erase_Line(2);
        Tft_Driver_Show_CN_String(2, 0, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Progress bar */
    {
        uint32_t progress;
        Tft_Driver_Erase_Pixel_Area(0, 3 * TFT_FONT_HEIGHT, TFT_WIDTH, TFT_FONT_HEIGHT + 8);
        if (!is_stopped) {
            progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                     / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
            if (progress > 10) progress = 10;
            Ui_Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
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
        Erase_Line(4);
        Tft_Driver_Show_CN_String(4, 0, buf, UI_COLOR_DATA, UI_COLOR_BG);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, Adc_Driver_Get_Current());
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Erase_Line(5);
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
        Erase_Line(2);
        Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_f_str, buf, sizeof(s_last_f_str));
        s_last_f_str[sizeof(s_last_f_str) - 1] = '\0';
    }

    /* Voltage */
    Fmt_V(buf, s_ema_v);
    if (strncmp(buf, s_last_v_str, sizeof(s_last_v_str)) != 0) {
        Erase_Line(3);
        Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        strncpy(s_last_v_str, buf, sizeof(s_last_v_str));
        s_last_v_str[sizeof(s_last_v_str) - 1] = '\0';
    }

    /* Current */
    Fmt_I(buf, s_ema_i);
    if (strncmp(buf, s_last_i_str, sizeof(s_last_i_str)) != 0) {
        Erase_Line(4);
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
    static const uint16_t blue_grad[6] = {0x0018,0x001B,0x001F,0x07FF,0x07BF,0x07FF};
    static const uint16_t rainbow[6] = {0xF800,0xFD20,0xFFE0,0x07E0,0x07FF,0x001F};

    /* ── WIFI icon (x=128) ── */
    if (s_no_wifi_mode) {
        Tft_Driver_Draw_Single_Icon(WX, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    } else if (!Esp8266_Driver_Is_Ready()) {
        icon_frame = (uint8_t)(Sys_Timer_Get_Tick()/150) % 6;
        Tft_Driver_Draw_Single_Icon(WX, 0, WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
    } else if (cs == APP_NETWORK_CONN_ONLINE) {
        int8_t r = App_Network_Get_RSSI();
        if (r >= -50) icon_frame=3; else if (r >= -60) icon_frame=2; else if (r >= -70) icon_frame=1; else icon_frame=0;
        Tft_Driver_Draw_WiFi_Icon(WX, 0, icon_frame, UI_COLOR_OK, UI_COLOR_BG);
    } else if (App_Network_Is_Connecting()) {
        icon_frame = (uint8_t)(Sys_Timer_Get_Tick()/150) % 6;
        Tft_Driver_Draw_Single_Icon(WX, 0, WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
    } else if (cs == APP_NETWORK_CONN_FAILED) {
        Tft_Driver_Draw_Single_Icon(WX, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    } else {
        Tft_Driver_Draw_Single_Icon(WX, 0, WIFI_REMOVE_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    }

    /* ── MQTT cloud (x=144) ── */
    if (cs == APP_NETWORK_CONN_ONLINE) {
        Tft_Driver_Draw_Single_Icon(MX, 0, MQTT_YES_ICON, UI_COLOR_OK, UI_COLOR_BG);
    } else if (App_Network_Is_Connecting()) {
        uint8_t mqtt_frame = (uint8_t)(Sys_Timer_Get_Tick()/200) % 6;
        Tft_Driver_Draw_Single_Icon(MX, 0, MQTT_ANIM[mqtt_frame], rainbow[mqtt_frame], UI_COLOR_BG);
    } else {
        Tft_Driver_Draw_Single_Icon(MX, 0, MQTT_NO_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
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

    /* ── 1. Global physical clear — pure full-screen gauge, no header/divider ── */
    Tft_Driver_Clear(UI_COLOR_BG);

    /* ── 2. Compute needle angle (0=left, 180=right) ── */
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
                color = is_red ? UI_COLOR_ALARM : UI_COLOR_TEXT;
            else
                color = UI_COLOR_DIM;
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
            uint16_t color = (v >= cfg->red_start) ? UI_COLOR_ALARM : UI_COLOR_TEXT;

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
                                              nb, color, UI_COLOR_BG);
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
                    { status_text = "SWP"; status_color = UI_COLOR_VALUE; }
                else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                    { status_text = "DON"; status_color = UI_COLOR_OK; }
                else
                    { status_text = "IDL"; status_color = UI_COLOR_DIM; }
            } else {
                float thr_warn = (cfg->label == 'V') ? 36.0f : 1.2f;
                if (val >= cfg->red_start)
                    { status_text = "HI"; status_color = UI_COLOR_ALARM; }
                else if (val >= thr_warn)
                    { status_text = "WRN"; status_color = UI_COLOR_VALUE; }
                else
                    { status_text = "OK"; status_color = UI_COLOR_OK; }
            }
            Tft_Driver_Show_CN_String(4, Center(status_text), status_text,
                                      status_color, UI_COLOR_BG);
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
                ? UI_COLOR_DIM : TFT_COLOR_YELLOW;

            if (cfg->label == 'C')
                snprintf(buf, sizeof(buf), "%.3f", (double)val);
            else if (cfg->label == 'F' && !is_running_f)
                snprintf(buf, sizeof(buf), "0");
            else
                snprintf(buf, sizeof(buf), "%.2f", (double)val);
            Tft_Driver_Show_CN_String(5, Center(buf), buf, num_color, UI_COLOR_BG);
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }

        /* -- Row 6 (Y=96): metric label with unit suffix, center-aligned ── */
        if (cfg->label == 'F') {
            Tft_Driver_Show_CN_String(6, Center(S_FREQ " kHz"),
                S_FREQ " kHz", UI_COLOR_VALUE, UI_COLOR_BG);
        } else if (cfg->label == 'V') {
            Tft_Driver_Show_CN_String(6, Center(S_VOLTAGE " V"),
                S_VOLTAGE " V", UI_COLOR_VALUE, UI_COLOR_BG);
        } else {
            Tft_Driver_Show_CN_String(6, Center(S_CURRENT " A"),
                S_CURRENT " A", UI_COLOR_VALUE, UI_COLOR_BG);
        }
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
                        color = is_red ? UI_COLOR_ALARM : UI_COLOR_TEXT;
                    else
                        color = UI_COLOR_DIM;
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
                { status_text = "SWP"; status_color = UI_COLOR_VALUE; }
            else if (st == INVERTER_CONTROL_SS_STATE_DONE)
                { status_text = "DON"; status_color = UI_COLOR_OK; }
            else
                { status_text = "IDL"; status_color = UI_COLOR_DIM; }
        } else {
            float thr_warn = (cfg->label == 'V') ? 36.0f : 2.5f;
            if (val >= cfg->red_start)
                { status_text = "HI"; status_color = UI_COLOR_ALARM; }
            else if (val >= thr_warn)
                { status_text = "WRN"; status_color = UI_COLOR_VALUE; }
            else
                { status_text = "OK"; status_color = UI_COLOR_OK; }
        }

        if (strncmp(status_text, s_gauge_status_buf, sizeof(s_gauge_status_buf)) != 0) {
            Tft_Driver_Erase_Pixel_Area(24, 64, 112, 16);
            Tft_Driver_Show_CN_String(4, Center(status_text), status_text,
                                      status_color, UI_COLOR_BG);
            strncpy(s_gauge_status_buf, status_text, sizeof(s_gauge_status_buf));
            s_gauge_status_buf[sizeof(s_gauge_status_buf) - 1] = '\0';
        }
    }

    /* -- Row 5 (Y=80): numeric value (3 decimal for current, 2 otherwise) -- */
    {
        if (cfg->label == 'C')
            snprintf(buf, sizeof(buf), "%.3f", (double)val);
        else
            snprintf(buf, sizeof(buf), "%.2f", (double)val);
        if (strncmp(buf, s_gauge_val_str, sizeof(s_gauge_val_str)) != 0) {
            Tft_Driver_Erase_Pixel_Area(24, 80, 112, 16);
            Tft_Driver_Show_CN_String(5, Center(buf), buf, TFT_COLOR_YELLOW, UI_COLOR_BG);
            strncpy(s_gauge_val_str, buf, sizeof(s_gauge_val_str));
            s_gauge_val_str[sizeof(s_gauge_val_str) - 1] = '\0';
        }
    }

    /* -- Row 6 (Y=96): metric label with unit suffix -- */
    {
        const char* label_text;
        if (cfg->label == 'F')
            label_text = "\xe9\xa2\x91\xe7\x8e\x87"
            " kHz";
        else if (cfg->label == 'V')
            label_text = "\xe7\x94\xb5\xe5\x8e\x8b"
            " V";
        else
            label_text = "\xe7\x94\xb5\xe6\xb5\x81"
            " A";
        static const char* s_last_gauge_label = NULL;
        if (label_text != s_last_gauge_label) {
            s_last_gauge_label = label_text;
            Tft_Driver_Erase_Pixel_Area(24, 96, 112, 16);
            Tft_Driver_Show_CN_String(6, Center(label_text), label_text, UI_COLOR_VALUE, UI_COLOR_BG);
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
    /* PWM 未运行时数值显示为 0 (频率实际未输出) */
    if (!is_running) {
        Tft_Driver_Erase_Pixel_Area(24, 80, 112, 16);
        Tft_Driver_Show_CN_String(5, Center("0"), "0", UI_COLOR_DIM, UI_COLOR_BG);
        strncpy(s_gauge_val_str, "0", sizeof(s_gauge_val_str));
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
        s_last_val_f = s_ema_f;
        s_user_target_synced = 0;
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
            Erase_Line(2);  /* clear old text before writing shorter/equal text */
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
                uint8_t max_cursor = (g_sys_state == SYS_STATE_FAULT) ? 3 : 2;
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
                uint8_t max_cursor = (g_sys_state == SYS_STATE_FAULT) ? 3 : 2;
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
                            g_sys_state = SYS_STATE_IDLE;
                        } else {
                            Inverter_Control_Soft_Start_Trigger();
                            g_sys_state = SYS_STATE_SWEEP;
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
                        g_sys_state = SYS_STATE_IDLE;
                    } else if (ss == INVERTER_CONTROL_SS_STATE_IDLE) {
                        Inverter_Control_Soft_Start_Trigger();
                        g_sys_state = SYS_STATE_SWEEP;
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
                g_sys_state = SYS_STATE_IDLE;
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
 *  Phase 0: Global Top-Right Icons Manager (Always On, Auto-Sync)
 *  Phase 1: Fault edge detection → may set s_page, s_page_drawn=0
 *  Phase 2: Sweep complete detection → may set s_page
 *  Phase 3: Key scan + dispatch → may set s_page or s_menu_cursor
 *  Phase 4: Page change → s_page_drawn=0; all tracking invalidated
 *  Phase 5: 200ms tick → dynamic incremental update (values only)
 *  Phase 6: Cursor boundary clamp
 *  Phase 7: Draw — full page only when s_page_drawn==0
 * ================================================================ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last_ui_ms = 0;
    uint8_t old_cursor;
    uint8_t cursor_changed = 0;
    uint8_t tick_200ms = 0;

    /* ── Phase 0: Global Top-Right Icons Manager (Always On, Auto-Sync) ── */
    {
        uint8_t cs = App_Network_Get_Connect_Status();
        uint8_t wifi_frame = (App_Network_Is_Connecting() || !Esp8266_Driver_Is_Ready())
            ? (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6 : 0xFF;
        uint8_t mqtt_frame = (App_Network_Is_Connecting())
            ? (uint8_t)(Sys_Timer_Get_Tick() / 200) % 6 : 0xFF;

        static uint8_t s_last_icon_cs    = 0xFF;
        static uint8_t s_last_icon_mode  = 0xFF;
        static uint8_t s_last_wifi_frame = 0xFF;
        static uint8_t s_last_mqtt_frame = 0xFF;
        static uint8_t s_last_icon_page  = 0xFF;

        if (s_page_drawn) {
            /* 触发条件：网络状态改变 OR WiFi开关改变 OR 动画帧跳动 OR 刚发生过切页 */
            if (cs != s_last_icon_cs ||
                s_no_wifi_mode != s_last_icon_mode ||
                wifi_frame != s_last_wifi_frame ||
                mqtt_frame != s_last_mqtt_frame ||
                s_page != s_last_icon_page) {

                s_last_icon_cs    = cs;
                s_last_icon_mode  = s_no_wifi_mode;
                s_last_wifi_frame = wifi_frame;
                s_last_mqtt_frame = mqtt_frame;
                s_last_icon_page  = s_page;

                /* 直接局部泵送图标，不再重绘 Header */
                Draw_TopRight_Icons();
            }
        } else {
            /* 页面跳转瞬间，强制失效状态，以便进入新页后图标能立刻跟进刷新 */
            s_last_icon_cs   = 0xFF;
            s_last_icon_page = 0xFF;
        }
    }

    /* ── Phase 1: System Fault detection (listen to g_sys_state, stripped from HW access) ── */
    {
        uint8_t sys_fault = (g_sys_state == SYS_STATE_FAULT);
        if (sys_fault && !s_was_fault_state) {
            s_page = UI_PAGE_FAULT;
            s_was_fault_state = 1;
            s_page_drawn = 0;
        }
        if (!sys_fault) {
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
        uint8_t max_cursor = (g_sys_state == SYS_STATE_FAULT) ? 3 : 2;
        if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
    }
    if (s_page == UI_PAGE_MONITOR_SUB_MENU) {
        if (s_menu_cursor > 4) s_menu_cursor = 0;
    }

    /* ════════════════════════════════════════════════════════════
     *  Phase 7: Draw — full or incremental
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

/**
 * @brief  外部强制跳转到目标页面 (物理级)
 * @note   远程指令触发或系统状态迁移时, 调用此函数同步 UI 页面, 防止 UI 展示不同步
 */
void Ui_Controller_Force_Page(Ui_Page page)
{
    s_page = page;
    s_page_drawn = 0;  /* 强制全量重绘 */
}
