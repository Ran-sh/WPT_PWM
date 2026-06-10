/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 V7 — 多页仪表盘 + 无WIFI本地模式
 * @note    TFT 8行×20列, 横屏 160×128, 4键: ON/OFF/F+/F-/PAGE
 *          配色: 黑底/黄标题/白正文/青数值/红报警/绿正常
 ******************************************************************************
 */

#include "Ui_Controller.h"
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
#include <stdio.h>

#define UI_COLOR_BG      TFT_COLOR_BLACK
#define UI_COLOR_TITLE   TFT_COLOR_YELLOW
#define UI_COLOR_TEXT    TFT_COLOR_WHITE
#define UI_COLOR_VALUE   TFT_COLOR_CYAN
#define UI_COLOR_ALARM   TFT_COLOR_RED
#define UI_COLOR_OK      TFT_COLOR_GREEN

#define UI_REFRESH_MS              200
#define UI_OVERCURRENT_THRESHOLD_A 5.0f
#define UI_POWER_V_THRESHOLD_V     12.0f

/* ── 中文串 (UTF-8 hex, 字库73字) ── */
#define S_LAUNCH    "\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa1\xb5"           /* 启动页 */
#define S_SWEEP     "\xe6\x89\xab\xe9\xa2\x91\xe9\xa1\xb5"           /* 扫频页 */
#define S_MONITOR   "\xe7\x9b\x91\xe6\xb5\x8b\xe6\xa8\xa1\xe5\xbc\x8f" /* 监测模式 */
#define S_MON_FREQ  "\xe7\x9b\x91\xe6\xb5\x8b\xe9\xa2\x91\xe7\x8e\x87" /* 监测频率 */
#define S_MON_VOLT  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe5\x8e\x8b" /* 监测电压 */
#define S_MON_CURR  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe6\xb5\x81" /* 监测电流 */
#define S_FAILED    "\xe5\xa4\xb1\xe8\xb4\xa5"                       /* 失败 */
#define S_CONN_DOTS "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad"           /* 连接中 */
#define S_CONN_OK   "\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x88\x90\xe5\x8a\x9f" /* 连接成功 */
#define S_NO_WIFI   "\xe6\x97\xa0WIFI"                                /* 无WIFI */
#define S_WIFI      "WIFI:"
#define S_WIFI_OK   "WIFI:" "\xe5\xb7\xb2\xe8\xbf\x9e"               /* WIFI:已连 */
#define S_WIFI_NO   "WIFI:" "\xe6\x9c\xaa\xe8\xbf\x9e"               /* WIFI:未连 */
#define S_FREQ      "\xe9\xa2\x91\xe7\x8e\x87"                       /* 频率 */
#define S_VOLTAGE   "\xe7\x94\xb5\xe5\x8e\x8b"                       /* 电压 */
#define S_CURRENT   "\xe7\x94\xb5\xe6\xb5\x81"                       /* 电流 */
#define S_STOP      "\xe5\x81\x9c\xe6\xad\xa2"                       /* 停止 */
#define S_RESET     "\xe5\xa4\x8d\xe4\xbd\x8d"                       /* 复位 */
#define S_CLEAR_WIFI "\xe6\xb8\x85\xe9\x99\xa4WIFI"                  /* 清除WIFI */
#define S_RECONN     "\xe9\x87\x8d\xe8\xbf\x9eWIFI"                  /* 重连WIFI */
#define S_SWEEP_STOP "\xe5\x81\x9c\xe6\xad\xa2\xe6\x89\xab\xe9\xa2\x91" /* 停止扫频 */
#define S_SWEEP_START "\xe5\x90\xaf\xe5\x8a\xa8\xe6\x89\xab\xe9\xa2\x91" /* 启动扫频 */
#define S_PAGE_F     "\xe5\x88\x87\xe9\xa1\xb5F"                     /* 切页F */
#define S_PAGE_V     "\xe5\x88\x87\xe9\xa1\xb5V"                     /* 切页V */
#define S_PAGE_I     "\xe5\x88\x87\xe9\xa1\xb5I"                     /* 切页I */
#define S_PAGE       "\xe5\x88\x87\xe9\xa1\xb5"                       /* 切页 */
#define S_BACK_DBL   "\xe5\x8f\x8c\xe5\x87\xbbBack"                  /* 双击Back */
#define S_NO_WIFI_MODE "\xe5\x88\x87\xe6\x8d\xa2\xe6\x97\xa0WIFI"    /* 切换无WIFI */
#define S_LONG_CLEAR "\xe9\x95\xbf\xe6\x8c\x89ON:" S_CLEAR_WIFI      /* 长按ON:清除WIFI */

/* ── 模块状态 ── */
static uint8_t s_page          = 0;  /* 监测子页: 0=频率 1=电压 2=电流 */
static uint8_t s_no_wifi_mode  = 0;  /* 无WIFI模式标志 */
static Ui_Controller_State s_ui_state = UI_CONTROLLER_STATE_INIT;

/* EMA 平滑 */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

static void Reset_EMA(void) { s_ema_ok = 0; }

/* ═══════════════════════════════════════════════════════════════
 *  辅助函数
 * ═══════════════════════════════════════════════════════════════ */

/* 居中 col: 串宽 = ASCII长度 + CN长度*2 */
static uint8_t Center(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; }
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : (20 - w) / 2;
}

/* 右对齐 col */
static uint8_t Right(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; }
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : 20 - w;
}

/* 写点填充: 在串后填 '.' 到 20 列 */
static void Show_Fill(uint8_t line, uint8_t col, const char* s, uint16_t fg, uint16_t bg)
{
    char buf[21];
    uint8_t i, w = 0;
    const char* p = s;
    while (*p) {
        uint8_t c = (uint8_t)*p;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; p += 3; }
        else { w++; p++; }
    }
    for (i = 0; i < 20 && s[i]; i++) buf[i] = s[i];
    for (; i < 20; i++) buf[i] = '.';
    buf[20] = '\0';
    Tft_Driver_Show_CN_String(line, col, buf, fg, bg);
}

/* WiFi 状态字符串 */
static const char* Get_WiFi_Str(void)
{
    if (s_no_wifi_mode) return S_NO_WIFI;
    uint8_t cs = App_Network_Get_Connect_Status();
    if (cs == APP_NETWORK_CONN_ONLINE) return S_WIFI_OK;
    return S_WIFI_NO;
}

static uint16_t Get_WiFi_Color(void)
{
    if (s_no_wifi_mode) return UI_COLOR_ALARM;
    uint8_t cs = App_Network_Get_Connect_Status();
    if (cs == APP_NETWORK_CONN_ONLINE) return UI_COLOR_OK;
    return UI_COLOR_ALARM;
}

/* EMA 更新 */
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

/* 电压/电流/频率 格式化 (避免负数取模) */
static void Fmt_V(char* buf, float v)
{
    int32_t x = (int32_t)(v * 100.0f + 0.5f);
    if (x < 0) x = -x;
    snprintf(buf, 21, S_VOLTAGE "V:%d.%02dV", (int)(x/100), (int)(x%100));
}
static void Fmt_I(char* buf, float c)
{
    int32_t x = (int32_t)(c * 100.0f + 0.5f);
    if (x < 0) x = -x;
    snprintf(buf, 21, S_CURRENT "I:%d.%02dA", (int)(x/100), (int)(x%100));
}
static void Fmt_F(char* buf, float f)
{
    snprintf(buf, 21, S_FREQ "F:%3d.%01dkHz", (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

/* ═══════════════════════════════════════════════════════════════
 *  绘制函数
 * ═══════════════════════════════════════════════════════════════ */

/* 扫频/运行子页面: 0=扫频进度/综合 1=频率表 2=电压表 3=电流表 */

static void Draw_Sweep_Main(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char buf[21];
    uint8_t j;

    progress = (SOFTSTART_START_FREQ_HZ - f) * 10
             / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
    if (progress > 10) progress = 10;

    switch (s_page) {
    case 0: /* 扫频进度页 */
        Tft_Driver_Show_CN_String(0, Center(S_SWEEP), S_SWEEP, UI_COLOR_TITLE, UI_COLOR_BG);
        snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz %d/100",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100), progress*10);
        Tft_Driver_Show_String(2, 1, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        /* 进度条 */
        {
            char bar[14];
            bar[0] = '[';
            for (j = 0; j < 10; j++) bar[1+j] = (j < (int)progress) ? '#' : ' ';
            bar[11] = ']';
            bar[12] = '\0';
            snprintf(buf, sizeof(buf), "%s %d%%", bar, progress*10);
            Tft_Driver_Show_String(3, 3, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        }
        Fmt_V(buf, Adc_Driver_Get_Voltage());
        Tft_Driver_Show_CN_String(4, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        Fmt_I(buf, Adc_Driver_Get_Current());
        Tft_Driver_Show_CN_String(5, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right("OFF:" S_SWEEP_STOP), "OFF:" S_SWEEP_STOP, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 1: /* 频率仪表盘 */
        Update_EMA();
        Tft_Driver_Show_CN_String(0, Center(S_MON_FREQ), S_MON_FREQ, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char fline[21];
            int f_int = (int)(s_ema_f + 0.5f);
            int f_dec = (int)((s_ema_f - f_int + 0.5f) * 10);
            snprintf(fline, sizeof(fline), S_FREQ "F:%3d.%01dkHz", f_int, f_dec);
            Tft_Driver_Show_CN_String(2, Center(fline), fline, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        /* 简易仪表盘: 频率刻度条 + 数值 */
        {
            char bar[21];
            int bar_w = (int)(s_ema_f * 10 / 150 + 0.5f); /* 0~10 for 0~150kHz */
            if (bar_w > 10) bar_w = 10;
            for (j = 0; j < 20; j++) bar[j] = (j < bar_w) ? '#' : ' ';
            bar[19] = '\0'; /* reserve last char */
            Tft_Driver_Show_String(4, 0, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_F), S_PAGE_F, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 2: /* 电压仪表盘 */
        Update_EMA();
        Tft_Driver_Show_CN_String(0, Center(S_MON_VOLT), S_MON_VOLT, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char vbuf[21];
            int v_int = (int)s_ema_v;
            int v_dec = (int)((s_ema_v - v_int + 0.5f) * 100);
            snprintf(vbuf, sizeof(vbuf), S_VOLTAGE "V:%2d.%02dV", v_int, v_dec);
            Tft_Driver_Show_CN_String(2, Center(vbuf), vbuf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            int bar_w = (int)(s_ema_v * 10 / 30 + 0.5f); /* 0~10 for 0~30V */
            if (bar_w > 10) bar_w = 10;
            char bar[12];
            for (j = 0; j < 11; j++) bar[j] = (j < (int)bar_w) ? '#' : ' ';
            bar[11] = '\0';
            Tft_Driver_Show_String(4, 4, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_V), S_PAGE_V, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 3: /* 电流仪表盘 */
        Update_EMA();
        Tft_Driver_Show_CN_String(0, Center(S_MON_CURR), S_MON_CURR, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char ibuf[21];
            int i_int = (int)s_ema_i;
            int i_dec = (int)((s_ema_i - i_int + 0.5f) * 100);
            snprintf(ibuf, sizeof(ibuf), S_CURRENT "I:%1d.%02dA", i_int, i_dec);
            Tft_Driver_Show_CN_String(2, Center(ibuf), ibuf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            int bar_w = (int)(s_ema_i * 10 / 5 + 0.5f); /* 0~10 for 0~5A */
            if (bar_w > 10) bar_w = 10;
            char bar[12];
            for (j = 0; j < 11; j++) bar[j] = (j < (int)bar_w) ? '#' : ' ';
            bar[11] = '\0';
            Tft_Driver_Show_String(4, 4, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_I), S_PAGE_I, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    }
}

static void Draw_Run_Main(void)
{
    Update_EMA();
    switch (s_page) {
    case 0: /* 频率表 */
        Tft_Driver_Show_CN_String(0, Center(S_MON_FREQ), S_MON_FREQ, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char buf[21];
            Fmt_F(buf, s_ema_f);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            Fmt_V(buf, s_ema_v);
            Tft_Driver_Show_CN_String(3, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_I(buf, s_ema_i);
            Tft_Driver_Show_CN_String(4, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        }
        {
            char bar[12]; uint8_t j;
            int bar_w = (int)(s_ema_f * 10 / 150 + 0.5f);
            if (bar_w > 10) bar_w = 10;
            for (j = 0; j < 11; j++) bar[j] = (j < bar_w) ? '#' : ' ';
            bar[11] = '\0';
            Tft_Driver_Show_String(5, 4, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_F), S_PAGE_F, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 1: /* 电压表 */
        Tft_Driver_Show_CN_String(0, Center(S_MON_VOLT), S_MON_VOLT, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char buf[21]; uint8_t j;
            Fmt_V(buf, s_ema_v);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            int bar_w = (int)(s_ema_v * 10 / 30 + 0.5f);
            if (bar_w > 10) bar_w = 10;
            char bar[12];
            for (j = 0; j < 11; j++) bar[j] = (j < bar_w) ? '#' : ' ';
            bar[11] = '\0';
            Tft_Driver_Show_String(4, 4, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_V), S_PAGE_V, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 2: /* 电流表 */
        Tft_Driver_Show_CN_String(0, Center(S_MON_CURR), S_MON_CURR, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char buf[21]; uint8_t j;
            Fmt_I(buf, s_ema_i);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            int bar_w = (int)(s_ema_i * 10 / 5 + 0.5f);
            if (bar_w > 10) bar_w = 10;
            char bar[12];
            for (j = 0; j < 11; j++) bar[j] = (j < bar_w) ? '#' : ' ';
            bar[11] = '\0';
            Tft_Driver_Show_String(4, 4, bar, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,        UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE_I), S_PAGE_I, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 3: /* 综合监测 */
        Tft_Driver_Show_CN_String(0, Center(S_MONITOR), S_MONITOR, UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(1, Center(Get_WiFi_Str()), Get_WiFi_Str(), Get_WiFi_Color(), UI_COLOR_BG);
        {
            char buf[21];
            Fmt_F(buf, s_ema_f);
            Tft_Driver_Show_CN_String(2, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_V(buf, s_ema_v);
            Tft_Driver_Show_CN_String(3, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_I(buf, s_ema_i);
            Tft_Driver_Show_CN_String(4, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(6, Right("OFF:" S_STOP), "OFF:" S_STOP, UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, 0,  S_BACK_DBL,     UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(S_PAGE),      "\xe5\x88\x87\xe9\xa1\xb5V",    UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  LED 更新
 * ═══════════════════════════════════════════════════════════════ */
static void Update_Leds(Ui_Controller_State ui_state)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);

    if (ui_state == UI_CONTROLLER_STATE_SWEEPING)
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

/* ═══════════════════════════════════════════════════════════════
 *  按键分发
 * ═══════════════════════════════════════════════════════════════ */
static void Handle_Keys(Ui_Controller_State ui_state,
                        Key_Driver_Event k0, Key_Driver_Event k1,
                        Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* K0 双击 = 无WIFI模式 (所有界面通用) */
    if (k0 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_no_wifi_mode = !s_no_wifi_mode;
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }

    /* PAGE 单击 = 子页切换 (扫频/运行状态) */
    if (k3 == KEY_DRIVER_EVENT_CLICK && (ui_state == UI_CONTROLLER_STATE_SWEEPING || ui_state == UI_CONTROLLER_STATE_RUNNING)) {
        s_page = (s_page + 1) % 4;
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }

    /* PAGE 双击 = 无WIFI模式 (所有界面通用) */
    if (k3 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_no_wifi_mode = !s_no_wifi_mode;
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }

    /* K0 单击 */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (ui_state) {
            case UI_CONTROLLER_STATE_INIT:
                if (!s_no_wifi_mode) { App_Network_Start_Connect(); }
                Tft_Driver_Clear(UI_COLOR_BG);
                break;
            case UI_CONTROLLER_STATE_FAILED:
                if (!s_no_wifi_mode) { App_Network_Start_Connect(); }
                Tft_Driver_Clear(UI_COLOR_BG);
                break;
            case UI_CONTROLLER_STATE_READY:
                Inverter_Control_Soft_Start_Trigger();
                break;
            case UI_CONTROLLER_STATE_SWEEPING:
                Inverter_Control_Soft_Start_Stop();
                break;
            case UI_CONTROLLER_STATE_RUNNING:
                Inverter_Control_Soft_Start_Stop();
                break;
            default: break;
        }
        return;
    }

    /* K1 单击 */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (ui_state == UI_CONTROLLER_STATE_SWEEPING)
            Inverter_Control_Soft_Start_Stop();
        else if (ui_state == UI_CONTROLLER_STATE_RUNNING) {
            uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
            if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
        }
        return;
    }

    /* K2 单击 */
    if (k2 == KEY_DRIVER_EVENT_CLICK && ui_state == UI_CONTROLLER_STATE_RUNNING) {
        uint32_t f = Pwm_Driver_Get_Frequency();
        if (f >= PWM_DRIVER_FREQ_MIN_HZ + 1000) Pwm_Driver_Set_Frequency(f - 1000);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  状态机
 * ═══════════════════════════════════════════════════════════════ */
static Ui_Controller_State Calc_Ui_State(void)
{
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    if (ss == INVERTER_CONTROL_SS_STATE_FAULT) {
        return UI_CONTROLLER_STATE_FAULT;
    }

    /* ESP 未就绪: INIT 或 FAILED */
    if (!Esp8266_Driver_Is_Ready() && !s_no_wifi_mode) {
        uint8_t cs = App_Network_Get_Connect_Status();
        if (cs == APP_NETWORK_CONN_FAILED) return UI_CONTROLLER_STATE_FAILED;
        return UI_CONTROLLER_STATE_INIT;
    }
    uint8_t cs = App_Network_Get_Connect_Status();
    if (s_no_wifi_mode) {
        /* 无WIFI模式: 跳过网络状态, 直接根据逆变器状态决定 */
        if (ss == INVERTER_CONTROL_SS_STATE_IDLE)  return UI_CONTROLLER_STATE_READY;
        if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) return UI_CONTROLLER_STATE_SWEEPING;
        return UI_CONTROLLER_STATE_RUNNING;
    }
    switch (cs) {
        case APP_NETWORK_CONN_IDLE:   return UI_CONTROLLER_STATE_INIT;
        case APP_NETWORK_CONN_WIFI:   return UI_CONTROLLER_STATE_CONNECTING;
        case APP_NETWORK_CONN_FAILED: return UI_CONTROLLER_STATE_FAILED;
        case APP_NETWORK_CONN_ONLINE:
            if (ss == INVERTER_CONTROL_SS_STATE_IDLE)  return UI_CONTROLLER_STATE_READY;
            if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) return UI_CONTROLLER_STATE_SWEEPING;
            return UI_CONTROLLER_STATE_RUNNING;
        default: return UI_CONTROLLER_STATE_INIT;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  主调度
 * ═══════════════════════════════════════════════════════════════ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last = 0;
    static uint8_t  s_last_state = 0xFF;
    static uint8_t  s_last_page  = 0xFF;
    uint8_t need = 0;

    if (Sys_Timer_Get_Tick() - s_last >= UI_REFRESH_MS) {
        s_last = Sys_Timer_Get_Tick();
        need = 1;
    }

    Ui_Controller_State ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != s_last_state || s_page != s_last_page) {
        s_last_state = (uint8_t)ui_state;
        s_last_page  = s_page;
        Tft_Driver_Clear(UI_COLOR_BG);
        Reset_EMA();
        need = 1;
    }

    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);

    /* K0 长按清除 WiFi */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            s_no_wifi_mode = 1;
            Tft_Driver_Clear(UI_COLOR_BG);
            s_last_state = 0xFF;
            Led_Driver_Task();
        }
        return;
    }

    Handle_Keys(ui_state, k0, k1, k2, k3);
    ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != s_last_state || s_page != s_last_page) {
        s_last_state = (uint8_t)ui_state;
        s_last_page  = s_page;
        Tft_Driver_Clear(UI_COLOR_BG);
        Reset_EMA();
        need = 1;
    }

    s_ui_state = ui_state;

    /* 过流保护 */
    if (ui_state == UI_CONTROLLER_STATE_SWEEPING ||
        ui_state == UI_CONTROLLER_STATE_RUNNING) {
        if (Adc_Driver_Get_Current() > UI_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            ui_state = UI_CONTROLLER_STATE_FAULT;
            s_last_state = (uint8_t)UI_CONTROLLER_STATE_FAULT;
            s_ui_state = UI_CONTROLLER_STATE_FAULT;
            Tft_Driver_Clear(UI_COLOR_BG);
            need = 1;
        }
    }

    if (ui_state != UI_CONTROLLER_STATE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

    if (need) Update_Leds(ui_state);
    Led_Driver_Task();
    Buzzer_Driver_Task();
    if (!need) return;

    switch (ui_state) {
        case UI_CONTROLLER_STATE_INIT: {
            char buf[21];
            Tft_Driver_Show_CN_String(0, Center(S_LAUNCH), S_LAUNCH, UI_COLOR_TITLE, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(2, Center(S_WIFI_NO), S_WIFI_NO, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_V(buf, Adc_Driver_Get_Voltage());
            Tft_Driver_Show_CN_String(3, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_I(buf, Adc_Driver_Get_Current());
            Tft_Driver_Show_CN_String(4, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("ON:" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI"),
                "ON:" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right(S_NO_WIFI_MODE),
                S_NO_WIFI_MODE, UI_COLOR_TEXT, UI_COLOR_BG);
            break;
        }
        case UI_CONTROLLER_STATE_CONNECTING:
            Show_Fill(3, 0, "WIFI" S_CONN_DOTS, UI_COLOR_TITLE, UI_COLOR_BG);
            {
                char buf[21];
                snprintf(buf, sizeof(buf), S_FAILED "\xe6\xac\xa1\xe6\x95\xb0:%d/%d",
                    App_Network_Get_Retry_Count(), 3);
                Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            }
            break;
        case UI_CONTROLLER_STATE_FAILED: {
            char buf[21];
            Tft_Driver_Show_CN_String(0, Center(S_LAUNCH), S_LAUNCH, UI_COLOR_TITLE, UI_COLOR_BG);
            snprintf(buf, sizeof(buf), "FAILED[x%d]", App_Network_Get_Retry_Count());
            Tft_Driver_Show_CN_String(1, Center(buf), buf, UI_COLOR_ALARM, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(2, Center(S_WIFI_NO), S_WIFI_NO, UI_COLOR_ALARM, UI_COLOR_BG);
            Fmt_V(buf, Adc_Driver_Get_Voltage());
            Tft_Driver_Show_CN_String(3, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_I(buf, Adc_Driver_Get_Current());
            Tft_Driver_Show_CN_String(4, 1, buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(5, Right(S_LONG_CLEAR), S_LONG_CLEAR, UI_COLOR_OK, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("ON:" S_RECONN), "ON:" S_RECONN, UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right(S_NO_WIFI_MODE), S_NO_WIFI_MODE, UI_COLOR_TEXT, UI_COLOR_BG);
            break;
        }
        case UI_CONTROLLER_STATE_READY:
            Show_Fill(3, 0, "WIFI" S_CONN_OK, UI_COLOR_OK, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("ON:" S_SWEEP_START),
                "ON:" S_SWEEP_START, UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right(S_NO_WIFI_MODE),
                S_NO_WIFI_MODE, UI_COLOR_TEXT, UI_COLOR_BG);
            break;
        case UI_CONTROLLER_STATE_SWEEPING:
            Draw_Sweep_Main();
            break;
        case UI_CONTROLLER_STATE_RUNNING:
            Draw_Run_Main();
            break;
        case UI_CONTROLLER_STATE_FAULT:
            Tft_Driver_Show_CN_String(0, Center("!!!\xe6\x95\x85\xe9\x9a\x9c!!!"), "!!!\xe6\x95\x85\xe9\x9a\x9c!!!", UI_COLOR_ALARM, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(2, Center("\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4"), "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4", UI_COLOR_ALARM, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(4, Center("PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"), "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad", UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("\xe6\x8c\x89K0/K1\xe5\xa4\x8d\xe4\xbd\x8d"), "\xe6\x8c\x89K0/K1\xe5\xa4\x8d\xe4\xbd\x8d", UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right(S_NO_WIFI_MODE), S_NO_WIFI_MODE, UI_COLOR_TEXT, UI_COLOR_BG);
            break;
    }
}

Ui_Controller_State Ui_Controller_Get_State(void) { return s_ui_state; }

uint8_t Ui_Controller_Get_Bridge_State(void)
{
    Inverter_Control_Soft_Start_State s = Inverter_Control_Soft_Start_Get_State();
    return (s == INVERTER_CONTROL_SS_STATE_SWEEP || s == INVERTER_CONTROL_SS_STATE_DONE);
}
