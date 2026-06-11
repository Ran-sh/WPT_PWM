/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 V9 — WIFI角标统一右上角 + 智能双切换
 * @note    TFT 8行×20列, 横屏 160×128, 4键: ON/OFF/F+/F-/PAGE
 *          配色: 黑底/黄标题/白正文/青数值/红报警/绿正常
 *          6 态: INIT → FAILED → READY → SWEEPING → RUNNING → FAULT
 *          WIFI 角标统一在第0行右上角, 所有界面通用
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
#include "Energy_Bar.h"
#include <stdio.h>

#define UI_COLOR_BG      TFT_COLOR_BLACK   /* 背景色: 黑 */
#define UI_COLOR_TITLE   TFT_COLOR_YELLOW  /* 标题: 黄 */
#define UI_COLOR_TEXT    TFT_COLOR_WHITE   /* 正文: 白 */
#define UI_COLOR_VALUE   TFT_COLOR_CYAN    /* 数值(仪表): 青 */
#define UI_COLOR_DATA    TFT_COLOR_BLUE    /* 数据(电压/电流): 蓝 */
#define UI_COLOR_ALARM   TFT_COLOR_RED     /* 报警/故障: 红 */
#define UI_COLOR_OK      TFT_COLOR_GREEN   /* 正常/在线: 绿 */

#define UI_REFRESH_MS              200   /* UI 刷新周期 (ms) */
#define UI_OVERCURRENT_THRESHOLD_A 5.0f  /* 过流保护阈值 (A) */
#define UI_POWER_V_THRESHOLD_V     12.0f /* 12V 动力电源开启阈值 (V) */

/* ── 中文串 (UTF-8 hex, 字库73字) ── */
#define S_LAUNCH    "\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa1\xb5"           /* 启动页 */
#define S_SWEEP     "\xe6\x89\xab\xe9\xa2\x91\xe9\xa1\xb5"           /* 扫频页 */
#define S_MONITOR   "\xe7\x9b\x91\xe6\xb5\x8b\xe6\xa8\xa1\xe5\xbc\x8f" /* 监测模式 */
#define S_MON_FREQ  "\xe7\x9b\x91\xe6\xb5\x8b\xe9\xa2\x91\xe7\x8e\x87" /* 监测频率 */
#define S_MON_VOLT  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe5\x8e\x8b" /* 监测电压 */
#define S_MON_CURR  "\xe7\x9b\x91\xe6\xb5\x8b\xe7\x94\xb5\xe6\xb5\x81" /* 监测电流 */
#define S_NO_WIFI   "\xe6\x97\xa0WIFI"                                /* 无WIFI */
#define S_FREQ      "\xe9\xa2\x91\xe7\x8e\x87"                       /* 频率 */
#define S_VOLTAGE   "\xe7\x94\xb5\xe5\x8e\x8b"                       /* 电压 */
#define S_CURRENT   "\xe7\x94\xb5\xe6\xb5\x81"                       /* 电流 */
#define S_STOP      "\xe5\x81\x9c\xe6\xad\xa2"                       /* 停止 */
#define S_CLEAR_WIFI "\xe6\xb8\x85\xe9\x99\xa4WIFI"                  /* 清除WIFI */
#define S_RECONN     "\xe9\x87\x8d\xe8\xbf\x9eWIFI"                  /* 重连WIFI */
#define S_SWEEP_STOP "\xe5\x81\x9c\xe6\xad\xa2\xe6\x89\xab\xe9\xa2\x91" /* 停止扫频 */
#define S_SWEEP_START "\xe5\x90\xaf\xe5\x8a\xa8\xe6\x89\xab\xe9\xa2\x91" /* 启动扫频 */
#define SKIP_F       "\xe5\x88\x87\xe9\xa1\xb5" "F"   /* 切页F */
#define SKIP_V       "\xe5\x88\x87\xe9\xa1\xb5" "V"   /* 切页V */
#define SKIP_I       "\xe5\x88\x87\xe9\xa1\xb5" "I"   /* 切页I */
#define SKIP         "\xe5\x88\x87\xe9\xa1\xb5"     /* 切页 */

#define S_LONG_CLEAR "\xe9\x95\xbf\xe6\x8c\x89ON:" S_CLEAR_WIFI      /* 长按ON:清除WIFI */

/* WIFI 角标动画: W→WI→WIF→WIFI 循环, 周期 600ms/帧 */
#define WIFI_ANIM_FRAME_MS 600

/* ── 模块状态 ── */
static uint8_t s_page          = 0;  /* 子页: 0=综合 1=频率 2=电压 3=电流 */
static uint8_t s_no_wifi_mode  = 1;  /* 开机默认无WIFI模式 */
static uint8_t s_wifi_anim_idx = 0;  /* WIFI角标动画帧: 0=W,1=WI,2=WIF,3=WIFI */
static uint32_t s_wifi_anim_last = 0;
static Ui_Controller_State s_ui_state = UI_CONTROLLER_STATE_INIT;

/* EMA 平滑 */
static float   s_ema_v = 0.0f, s_ema_i = 0.0f, s_ema_f = 0.0f;
static uint8_t s_ema_ok = 0;

/* 重置 EMA, 下次 Update_EMA 将以当前 ADC 值重新初始化 */
static void Reset_EMA(void) { s_ema_ok = 0; }

/* ═══════════════════════════════════════════════════════════════
 *  辅助函数
 * ═══════════════════════════════════════════════════════════════ */

/* 计算中英文混合字符串居中起始列号: 串宽=ASCII长度+CN长度×2 */
static uint8_t Center(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }  /* 防截断UTF-8 */
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : (20 - w) / 2;
}

/* 计算中英文混合字符串右对齐起始列号 */
static uint8_t Right(const char* s)
{
    uint8_t w = 0;
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0xE0 && c <= 0xEF) { w += 2; s += 3; if (!*s) break; }  /* 防截断UTF-8 */
        else { w++; s++; }
    }
    return (w >= 20) ? 0 : 20 - w;
}

/* ═══════════════════════════════════════════════════════════════
 *  第0行右上角 WIFI 角标: 左侧标题, 右侧WIFI实时状态
 *  绿色=在线 蓝色逐字闪烁=连接中 红色=离线/无WIFI
 *  所有界面统一调用, 不占额外行
 * ═══════════════════════════════════════════════════════════════ */
static void Draw_Header(const char* title)
{
    /* 左侧: 标题 */
    Tft_Driver_Show_CN_String(0, 0, title, UI_COLOR_TITLE, UI_COLOR_BG);

    /* 右侧: WIFI 角标 (固定4字符区域, 先用背景色擦除再画) */
    {
        uint16_t color;
        const char* text;
        uint8_t col;
        uint8_t cs = App_Network_Get_Connect_Status();

        /* 固定位置: 右对齐从 col=16 开始, 宽度4字符=64像素 */
        #define WIFI_CORNER_COL  16   /* 右对齐: 20-4=16 */

        /* 擦除角标区域 (4字符×16行, 即 64×16 像素) */
        Tft_Driver_Fill_Rect(WIFI_CORNER_COL * TFT_FONT_WIDTH, 0,
                             4 * TFT_FONT_WIDTH, TFT_FONT_HEIGHT, UI_COLOR_BG);

        if (s_no_wifi_mode) {
            text = S_NO_WIFI; color = UI_COLOR_ALARM;
            col = Right(text);
            Tft_Driver_Show_CN_String(0, col, text, color, UI_COLOR_BG);
        } else if (!Esp8266_Driver_Is_Ready()) {
            text = "WIFI"; color = UI_COLOR_ALARM;
            col = Right(text);
            Tft_Driver_Show_String(0, col, text, color, UI_COLOR_BG);
        } else if (cs == APP_NETWORK_CONN_ONLINE) {
            text = "WIFI"; color = UI_COLOR_OK;
            col = Right(text);
            Tft_Driver_Show_String(0, col, text, color, UI_COLOR_BG);
        } else if (cs == APP_NETWORK_CONN_WIFI) {
            /* 连接中: W→WI→WIF→WIFI 蓝色逐字闪烁, 右对齐确保不跳动 */
            static const char* const frames[] = { "W", "WI", "WIF", "WIFI" };
            text = frames[s_wifi_anim_idx & 3];
            color = UI_COLOR_VALUE;
            col = Right(text);
            Tft_Driver_Show_String(0, col, text, color, UI_COLOR_BG);
        } else {
            text = "WIFI"; color = UI_COLOR_ALARM;
            col = Right(text);
            Tft_Driver_Show_String(0, col, text, color, UI_COLOR_BG);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  判断当前WiFi是否在线
 * ═══════════════════════════════════════════════════════════════ */
static uint8_t Is_WiFi_Online(void)
{
    if (s_no_wifi_mode) return 0;
    if (!Esp8266_Driver_Is_Ready()) return 0;
    return (App_Network_Get_Connect_Status() == APP_NETWORK_CONN_ONLINE);
}

/* EMA 指数移动平均: 首次直接赋值, 后续 α=0.25 (τ≈800ms) */
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

/* 格式化电压: "电压V:xx.xxV", 自动取绝对值 */
static void Fmt_V(char* buf, float v)
{
    int32_t x = (int32_t)(v * 100.0f + 0.5f);
    if (x < 0) x = -x;
    snprintf(buf, 21, S_VOLTAGE "V:%2d.%02dV", (int)(x/100), (int)(x%100));
}
/* 格式化电流: "电流I:+x.xxA", 自动判定正负号 */
static void Fmt_I(char* buf, float c)
{
    int32_t x = (int32_t)(c * 100.0f + 0.5f);
    char sign = (c < 0) ? '-' : '+';
    x = (x < 0) ? -x : x;
    snprintf(buf, 21, S_CURRENT "I:%c%1d.%02dA", sign, (int)(x/100), (int)(x%100));
}
/* 格式化频率: "频率F:xxx.xkHz" */
static void Fmt_F(char* buf, float f)
{
    snprintf(buf, 21, S_FREQ "F:%3d.%01dkHz", (int)f, (int)((f-(int)f)*10+0.5f)%10);
}

/* ═══════════════════════════════════════════════════════════════
 *  绘制函数 — 所有界面顶部统一调用 Draw_Header
 * ═══════════════════════════════════════════════════════════════ */

/* 扫频态4子页绘制: 0=进度 1=频率仪表盘 2=电压仪表盘 3=电流仪表盘 */
static void Draw_Sweep_Main(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char buf[21];

    progress = (SOFTSTART_START_FREQ_HZ - f) * 10
             / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
    if (progress > 10) progress = 10;

    switch (s_page) {
    case 0: /* 扫频进度页 */
        Draw_Header(S_SWEEP);
        snprintf(buf, sizeof(buf), S_FREQ "F:%3lu.%1lukHz",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
        Tft_Driver_Show_String(2, 1, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        {
            /* 扫频进度能量条: 10段像素宽条, 显示扫频百分比 */
            Energy_Bar_Draw(3 * TFT_FONT_WIDTH, 3 * TFT_FONT_HEIGHT + 4,
                           14 * TFT_FONT_WIDTH, 8,
                           (float)progress, 0.0f, 10.0f,
                           ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
            snprintf(buf, sizeof(buf), "%d%%", progress*10);
            /* 百分比数值居中显示在能量条下方 */
            if (buf[0]) Tft_Driver_Show_String(3, 8, buf, UI_COLOR_TEXT, UI_COLOR_BG);
        }
        Fmt_V(buf, Adc_Driver_Get_Voltage());
        Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);
        Fmt_I(buf, Adc_Driver_Get_Current());
        Tft_Driver_Show_CN_String(5, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right("OFF:" S_SWEEP_STOP), "OFF:" S_SWEEP_STOP, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 1: /* 频率仪表盘 */
        Update_EMA();
        Draw_Header(S_MON_FREQ);
        {
            char fline[21];
            int f_int = (int)(s_ema_f + 0.5f);
            int f_dec = (int)((s_ema_f - f_int + 0.5f) * 10);
            snprintf(fline, sizeof(fline), S_FREQ "F:%3d.%01dkHz", f_int, f_dec);
            Tft_Driver_Show_CN_String(2, Center(fline), fline, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            /* 频率能量条: 全宽像素条 */
            Energy_Bar_Draw(0, 4 * TFT_FONT_HEIGHT + 2,
                           TFT_WIDTH, 12,
                           s_ema_f, 95.0f, 150.0f,
                           ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 0, "95", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 18, "150", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_F), SKIP_F, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 2: /* 电压仪表盘 */
        Update_EMA();
        Draw_Header(S_MON_VOLT);
        {
            char vbuf[21];
            int v_int = (int)s_ema_v;
            int v_dec = (int)((s_ema_v - v_int + 0.5f) * 100);
            snprintf(vbuf, sizeof(vbuf), S_VOLTAGE "V:%2d.%02dV", v_int, v_dec);
            Tft_Driver_Show_CN_String(2, Center(vbuf), vbuf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            /* 电压能量条: 居中像素条 */
            Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                           12 * TFT_FONT_WIDTH, 12,
                           s_ema_v, 0.0f, 48.0f,
                           ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 17, "48", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_V), SKIP_V, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 3: /* 电流仪表盘 */
        Update_EMA();
        Draw_Header(S_MON_CURR);
        {
            char ibuf[21];
            int i_int = (int)s_ema_i;
            int i_dec = (int)((s_ema_i - i_int + 0.5f) * 100);
            snprintf(ibuf, sizeof(ibuf), S_CURRENT "I:%1d.%02dA", i_int, i_dec);
            Tft_Driver_Show_CN_String(2, Center(ibuf), ibuf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            /* 电流能量条: 居中像素条 */
            Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                           12 * TFT_FONT_WIDTH, 12,
                           s_ema_i, 0.0f, 3.0f,
                           ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 18, "3", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_I), SKIP_I, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    }
}

/* 运行态4子页绘制: 0=综合监测 1=频率表 2=电压表 3=电流表 */
static void Draw_Run_Main(void)
{
    Update_EMA();
    switch (s_page) {
    case 0: /* 综合监测 */
        Draw_Header(S_MONITOR);
        {
            char buf[21];
            Fmt_F(buf, s_ema_f);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_V(buf, s_ema_v);
            Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_TEXT, UI_COLOR_BG);
            Fmt_I(buf, s_ema_i);
            Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_TEXT, UI_COLOR_BG);
        }
        Tft_Driver_Show_CN_String(6, Right("OFF:" S_STOP), "OFF:" S_STOP, UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_F), SKIP_F, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 1: /* 频率表 */
        Draw_Header(S_MON_FREQ);
        {
            char buf[21];
            Fmt_F(buf, s_ema_f);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
        {
            /* 频率能量条: 居中像素条 */
            Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                           12 * TFT_FONT_WIDTH, 12,
                           s_ema_f, 95.0f, 150.0f,
                           ENERGY_BAR_METRIC_FREQ, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 4, "95", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 17, "150", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, 0,  "\xe5\x8f\x8c\xe5\x87\xbb" "Back",     UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_F), SKIP_F, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 2: /* 电压表 */
        Draw_Header(S_MON_VOLT);
        {
            char buf[21];
            Fmt_V(buf, s_ema_v);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            /* 电压能量条: 居中像素条 */
            Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                           12 * TFT_FONT_WIDTH, 12,
                           s_ema_v, 0.0f, 48.0f,
                           ENERGY_BAR_METRIC_VOLT, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 17, "48", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, 0,  "\xe5\x8f\x8c\xe5\x87\xbb" "Back",     UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_V), SKIP_V, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    case 3: /* 电流表 */
        Draw_Header(S_MON_CURR);
        {
            char buf[21];
            Fmt_I(buf, s_ema_i);
            Tft_Driver_Show_CN_String(2, Center(buf), buf, UI_COLOR_VALUE, UI_COLOR_BG);
            /* 电流能量条: 居中像素条 */
            Energy_Bar_Draw(4 * TFT_FONT_WIDTH, 4 * TFT_FONT_HEIGHT + 2,
                           12 * TFT_FONT_WIDTH, 12,
                           s_ema_i, 0.0f, 3.0f,
                           ENERGY_BAR_METRIC_CURR, UI_COLOR_BG);
        }
        Tft_Driver_Show_String(5, 4, "0", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_String(5, 18, "3", UI_COLOR_TITLE, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, 0,  "\xe5\x8f\x8c\xe5\x87\xbb" "Back",     UI_COLOR_TEXT, UI_COLOR_BG);
        Tft_Driver_Show_CN_String(7, Right(SKIP_I), SKIP_I, UI_COLOR_TEXT, UI_COLOR_BG);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  LED 更新 — 根据 UI 状态 + 网络状态同步 6 路 LED
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

    /* POWER LED: 仅受电压控制, 电压>12V亮, 与PB10同步 */
    Led_Driver_Set_Power(
        Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Temp(LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Com(
        cs == APP_NETWORK_CONN_ONLINE
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发 — k0=ON/OFF, k1=F+, k2=F-, k3=PAGE
 * ═══════════════════════════════════════════════════════════════ */
static void Handle_Keys(Ui_Controller_State ui_state,
                        Key_Driver_Event k0, Key_Driver_Event k1,
                        Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* K0双击 = 智能WIFI切换: 在线→断开, 离线→连接 */
    if (k0 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        if (Is_WiFi_Online()) {
            /* 已连接 → 断开WIFI */
            s_no_wifi_mode = 1;
            App_Network_Soft_Reset();
        } else {
            /* 未连接 → 连接WIFI */
            s_no_wifi_mode = 0;
            App_Network_Start_Connect();
        }
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        /* PAGE单击 = 切子页 或 FAILED/READY→进入无WIFI模式 */
        if (ui_state == UI_CONTROLLER_STATE_SWEEPING || ui_state == UI_CONTROLLER_STATE_RUNNING) {
            s_page = (s_page + 1) % 4;
        } else if (ui_state == UI_CONTROLLER_STATE_FAILED || ui_state == UI_CONTROLLER_STATE_READY) {
            s_no_wifi_mode = 1;
            App_Network_Soft_Reset();
        }
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }
    /* PAGE双击 → 综合监测 */
    if (k3 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_page = 0;
        Tft_Driver_Clear(UI_COLOR_BG);
        return;
    }
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (ui_state) {
            case UI_CONTROLLER_STATE_INIT:
            case UI_CONTROLLER_STATE_FAILED:
                Tft_Driver_Clear(UI_COLOR_BG);
                return;
            case UI_CONTROLLER_STATE_FAULT:
                Inverter_Control_Soft_Start_Reset();
                return;
            case UI_CONTROLLER_STATE_READY:
                Inverter_Control_Soft_Start_Trigger();
                return;
            case UI_CONTROLLER_STATE_SWEEPING:
            case UI_CONTROLLER_STATE_RUNNING:
                Inverter_Control_Soft_Start_Stop();
                return;
            default: return;
        }
    }
    if (k1 == KEY_DRIVER_EVENT_CLICK && ui_state == UI_CONTROLLER_STATE_RUNNING) {
        uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
        if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK && ui_state == UI_CONTROLLER_STATE_RUNNING) {
        uint32_t f = Pwm_Driver_Get_Frequency();
        if (f >= PWM_DRIVER_FREQ_MIN_HZ + 1000) Pwm_Driver_Set_Frequency(f - 1000);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  状态机 — 由逆变器软启状态 + ESP 状态 + s_no_wifi_mode 计算当前 UI 状态
 * ═══════════════════════════════════════════════════════════════ */
static Ui_Controller_State Calc_Ui_State(void)
{
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
    if (ss == INVERTER_CONTROL_SS_STATE_FAULT) {
        return UI_CONTROLLER_STATE_FAULT;
    }

    /* ESP 未就绪 → 根据连接结果回 INIT 或 FAILED */
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
        case APP_NETWORK_CONN_IDLE:
        case APP_NETWORK_CONN_WIFI:
        case APP_NETWORK_CONN_MQTT:   return UI_CONTROLLER_STATE_INIT;
        case APP_NETWORK_CONN_FAILED: return UI_CONTROLLER_STATE_FAILED;
        case APP_NETWORK_CONN_ONLINE:
            if (ss == INVERTER_CONTROL_SS_STATE_IDLE)  return UI_CONTROLLER_STATE_READY;
            if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) return UI_CONTROLLER_STATE_SWEEPING;
            return UI_CONTROLLER_STATE_RUNNING;
        default: return UI_CONTROLLER_STATE_INIT;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  主调度 — 200ms 周期: 状态计算 → 按键分发 → PB10 控制 → 过流检测 → 绘制
 * ═══════════════════════════════════════════════════════════════ */
void Ui_Controller_Task(void)
{
    static uint32_t s_last = 0;
    static uint8_t  s_last_state = 0xFF;
    static uint8_t  s_last_page  = 0xFF;
    static uint8_t  s_last_wifi_anim = 0;     /* 上一帧WIFI动画索引 */
    uint8_t need = 0;

    /* WIFI 动画计时: 每帧仅刷新第0行角标，不全屏重绘 */
    if (Sys_Timer_Get_Tick() - s_wifi_anim_last >= WIFI_ANIM_FRAME_MS) {
        s_wifi_anim_last = Sys_Timer_Get_Tick();
        s_wifi_anim_idx++;
    }
    /* 仅角标变化时刷新第0行，不触发全屏重绘 */
    if (s_wifi_anim_idx != s_last_wifi_anim) {
        s_last_wifi_anim = s_wifi_anim_idx;
        /* 快速刷新角标：根据当前状态绘制第0行标题+WIFI */
        {
            Ui_Controller_State st = Calc_Ui_State();
            switch (st) {
                case UI_CONTROLLER_STATE_INIT: case UI_CONTROLLER_STATE_FAILED:
                    Draw_Header(S_LAUNCH); break;
                case UI_CONTROLLER_STATE_READY:
                    Draw_Header(S_LAUNCH); break;
                case UI_CONTROLLER_STATE_SWEEPING:
                    if (s_page == 0) Draw_Header(S_SWEEP);
                    else if (s_page == 1) Draw_Header(S_MON_FREQ);
                    else if (s_page == 2) Draw_Header(S_MON_VOLT);
                    else Draw_Header(S_MON_CURR);
                    break;
                case UI_CONTROLLER_STATE_RUNNING:
                    if (s_page == 0) Draw_Header(S_MONITOR);
                    else if (s_page == 1) Draw_Header(S_MON_FREQ);
                    else if (s_page == 2) Draw_Header(S_MON_VOLT);
                    else Draw_Header(S_MON_CURR);
                    break;
                case UI_CONTROLLER_STATE_FAULT:
                    Draw_Header("!!!\xe6\x95\x85\xe9\x9a\x9c!!!"); break;
                default: break;
            }
        }
    }

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
            s_last_page  = 0xFF;
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

    /* PB10 PowerContrl: 仅受电压控制, 低=使能12V, 高=关断. 与PWM开关独立 */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > UI_POWER_V_THRESHOLD_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_ResetBits(GPIOB, GPIO_Pin_10);  /* 电压>12V, 拉低=开12V */
            else        GPIO_SetBits(GPIOB, GPIO_Pin_10);     /* 电压≤12V, 拉高=关12V */
        }
    }

    /* 过流保护 (使用EMA平滑值, 避免ADC噪声尖峰误触发) */
    if (ui_state == UI_CONTROLLER_STATE_SWEEPING ||
        ui_state == UI_CONTROLLER_STATE_RUNNING) {
        Update_EMA();
        if (s_ema_i > UI_OVERCURRENT_THRESHOLD_A) {
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
    if (!need) return;

    switch (ui_state) {
        case UI_CONTROLLER_STATE_INIT: {
            char buf[21];
            Draw_Header(S_LAUNCH);
            {
                uint8_t cs = App_Network_Get_Connect_Status();
                uint8_t retry = App_Network_Get_Retry_Count();
                if (cs == APP_NETWORK_CONN_WIFI)
                    snprintf(buf, sizeof(buf), "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad" " %d/3", retry > 0 ? retry : 1);
                else
                    buf[0] = '\0';
                if (buf[0]) Tft_Driver_Show_CN_String(1, Right(buf), buf, UI_COLOR_TEXT, UI_COLOR_BG);
            }
            Fmt_V(buf, Adc_Driver_Get_Voltage());
            Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);
            Fmt_I(buf, Adc_Driver_Get_Current());
            Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);

            if (Is_WiFi_Online()) {
                Tft_Driver_Show_CN_String(6, Right("\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x96\xad\xe5\xbc\x80WIFI"),
                    "\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x96\xad\xe5\xbc\x80WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            } else {
                Tft_Driver_Show_CN_String(6, Right("\xe5\x8f\x8c\xe5\x87\xbbON" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI"),
                    "\xe5\x8f\x8c\xe5\x87\xbbON" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            }
            Tft_Driver_Show_CN_String(7, Right("PAGE:" "\xe6\x97\xa0WIFI" "\xe6\xa8\xa1\xe5\xbc\x8f"),
                "PAGE:" "\xe6\x97\xa0WIFI" "\xe6\xa8\xa1\xe5\xbc\x8f", UI_COLOR_TEXT, UI_COLOR_BG);
            break;
        }
        case UI_CONTROLLER_STATE_FAILED: {
            char buf[21];
            Draw_Header(S_LAUNCH);
            Tft_Driver_Show_CN_String(1, Right("\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5"), "\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5", UI_COLOR_ALARM, UI_COLOR_BG);
            Fmt_V(buf, Adc_Driver_Get_Voltage());
            Tft_Driver_Show_CN_String(3, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);
            Fmt_I(buf, Adc_Driver_Get_Current());
            Tft_Driver_Show_CN_String(4, Center(buf), buf, UI_COLOR_DATA, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("\xe5\x8f\x8c\xe5\x87\xbbON" S_RECONN), "\xe5\x8f\x8c\xe5\x87\xbbON" S_RECONN, UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right("PAGE:" "\xe6\x97\xa0WIFI" "\xe6\xa8\xa1\xe5\xbc\x8f"),
                "PAGE:" "\xe6\x97\xa0WIFI" "\xe6\xa8\xa1\xe5\xbc\x8f", UI_COLOR_TEXT, UI_COLOR_BG);
            break;
        }
        case UI_CONTROLLER_STATE_READY:
            Draw_Header(S_LAUNCH);
            Tft_Driver_Show_CN_String(3, Center("ON:" S_SWEEP_START),
                "ON:" S_SWEEP_START, UI_COLOR_OK, UI_COLOR_BG);
            if (Is_WiFi_Online()) {
                Tft_Driver_Show_CN_String(7, Center("\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x96\xad\xe5\xbc\x80WIFI"),
                    "\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x96\xad\xe5\xbc\x80WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            } else {
                Tft_Driver_Show_CN_String(7, Center("\xe5\x8f\x8c\xe5\x87\xbbON" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI"),
                    "\xe5\x8f\x8c\xe5\x87\xbbON" "\xe8\xbf\x9e\xe6\x8e\xa5WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            }
            break;
        case UI_CONTROLLER_STATE_SWEEPING:
            Draw_Sweep_Main();
            break;
        case UI_CONTROLLER_STATE_RUNNING:
            Draw_Run_Main();
            break;
        case UI_CONTROLLER_STATE_FAULT:
            Draw_Header("!!!\xe6\x95\x85\xe9\x9a\x9c!!!");  /* !!!故障!!! */
            Tft_Driver_Show_CN_String(2, Center("\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4"), "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4", UI_COLOR_ALARM, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(4, Center("PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"), "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad", UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(6, Right("ON:" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf"),   /* ON:复位重启 */
                "ON:" "\xe5\xa4\x8d\xe4\xbd\x8d" "\xe9\x87\x8d\xe5\x90\xaf", UI_COLOR_TEXT, UI_COLOR_BG);
            Tft_Driver_Show_CN_String(7, Right("\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x97\xa0WIFI"), "\xe5\x8f\x8c\xe5\x87\xbbON" "\xe6\x97\xa0WIFI", UI_COLOR_TEXT, UI_COLOR_BG);
            break;
    }
}

Ui_Controller_State Ui_Controller_Get_State(void) { return s_ui_state; }

uint8_t Ui_Controller_Is_No_WiFi_Mode(void) { return s_no_wifi_mode; }

uint8_t Ui_Controller_Get_Bridge_State(void)
{
    Inverter_Control_Soft_Start_State s = Inverter_Control_Soft_Start_Get_State();
    return (s == INVERTER_CONTROL_SS_STATE_SWEEP || s == INVERTER_CONTROL_SS_STATE_DONE);
}
