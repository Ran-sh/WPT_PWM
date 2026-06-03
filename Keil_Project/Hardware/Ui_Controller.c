/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 — 实现 (V6.2 TFT 彩屏版)
 * @note    TFT 8行×20列 16px 字体, 横屏 160×128 RGB565
 *          4键: KEY0=ON/OFF(PB9), KEY1=F_UP(PB8), KEY2=F_DOWN(PB7), KEY3=PAGE(PB5)
 *
 *          配色方案: 深蓝背景 + 亮白前景 + 青色高亮 + 红色告警
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

/* ── 配色 ── */
#define COLOR_BG          TFT_COLOR_BLACK       /* 背景 */
#define COLOR_TITLE       TFT_COLOR_YELLOW      /* 标题 */
#define COLOR_TEXT        TFT_COLOR_WHITE       /* 正文 */
#define COLOR_VALUE       TFT_COLOR_CYAN        /* 数值 */
#define COLOR_ALARM       TFT_COLOR_RED         /* 告警 */
#define COLOR_OK          TFT_COLOR_GREEN       /* 正常 */
#define COLOR_BAR_BG      TFT_COLOR_GRAY        /* 进度条背景 */

#define TFT_REFRESH_MS     200

/* ── 过流保护阈值 ── */
#define UI_CONTROLLER_OVERCURRENT_THRESHOLD_A  5.0f

/* ── 模块状态 ── */
static uint8_t             s_page       = 0;
static Ui_Controller_State s_ui_state   = UI_CONTROLLER_STATE_INIT;
static uint8_t             s_has_error  = 0;

/* EMA 显示平滑 */
static float   s_disp_v     = 0.0f;
static float   s_disp_i     = 0.0f;
static float   s_disp_f_khz = 0.0f;
static uint8_t s_disp_init  = 0;

static void Reset_Display_EMA(void) { s_disp_init = 0; }

/* ── LED 更新 ── */
static void Update_Leds(Ui_Controller_State ui_state)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);

    if (ui_state == UI_CONTROLLER_STATE_SWEEPING || ui_state == UI_CONTROLLER_STATE_RUNNING)
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_ON);
    else
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Power(LED_DRIVER_STATE_ON);

    if (ui_state == UI_CONTROLLER_STATE_FAULT)
        Led_Driver_Set_Temp(LED_DRIVER_STATE_ON);
    else
        Led_Driver_Set_Temp(LED_DRIVER_STATE_OFF);

    Led_Driver_Set_Com((ui_state >= UI_CONTROLLER_STATE_READY) ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);
}

/* ═══════════════════════════════════════════════════════════════
 *  各页面绘制 (TFT 彩屏版)
 * ═══════════════════════════════════════════════════════════════ */

static void Draw_Init(void)
{
    Tft_Driver_Show_String(1, 0, "[Control Mode]", COLOR_TITLE, COLOR_BG);
    Tft_Driver_Show_String(3, 0, "WiFi: DISCONN", COLOR_TEXT, COLOR_BG);
    Tft_Driver_Show_String(5, 0, "Press KEY0 WiFi", COLOR_VALUE, COLOR_BG);
}

static void Draw_Connecting(uint8_t retry, uint8_t max_retry)
{
    char buf[21];
    Tft_Driver_Show_String(1, 0, "[Connecting...]", COLOR_TITLE, COLOR_BG);
    Tft_Driver_Show_String(3, 0, "ESP WiFi Init", COLOR_TEXT, COLOR_BG);
    snprintf(buf, sizeof(buf), "Retry: %d/%d", retry, max_retry);
    Tft_Driver_Show_String(5, 0, buf, COLOR_VALUE, COLOR_BG);
    Tft_Driver_Show_String(7, 0, "Please wait...", COLOR_TEXT, COLOR_BG);
}

static void Draw_Ready(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_String(1, 0, "[Control Mode]", COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "WiFi: CONNECTED", COLOR_OK, COLOR_BG);
        Tft_Driver_Show_String(5, 0, "Press KEY0 Start", COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(7, 0, "F:  --.- kHz", COLOR_TEXT, COLOR_BG);
    } else {
        Tft_Driver_Show_String(1, 0, "- Monitor Only -", COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "State: READY", COLOR_OK, COLOR_BG);
        Tft_Driver_Show_String(5, 0, "Awaiting Start", COLOR_TEXT, COLOR_BG);
    }
}

static void Draw_Sweeping(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char fline[21];
    uint8_t j;

    if (s_page == 0) {
        progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                 / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
        if (progress > 10) progress = 10;

        Tft_Driver_Show_String(1, 0, "[Sweeping...]", COLOR_TITLE, COLOR_BG);

        snprintf(fline, sizeof(fline), "Freq: %3lu.%1lukHz",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
        Tft_Driver_Show_String(3, 0, fline, COLOR_VALUE, COLOR_BG);

        /* 进度条 */
        fline[0] = '[';
        for (j = 0; j < 10; j++) fline[1 + j] = (j < (int)progress) ? '#' : ' ';
        fline[11] = ']';
        fline[12] = '\0';
        Tft_Driver_Show_String(5, 0, fline, COLOR_TEXT, COLOR_BG);
    } else {
        Tft_Driver_Show_String(1, 0, "- Monitor Only -", COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "Sweeping...", COLOR_VALUE, COLOR_BG);
        snprintf(fline, sizeof(fline), "F: %lukHz", (unsigned long)(f / 1000));
        Tft_Driver_Show_String(5, 0, fline, COLOR_TEXT, COLOR_BG);
    }
}

static void Draw_Running(void)
{
    if (!s_disp_init) {
        s_disp_v     = Adc_Driver_Get_Voltage();
        s_disp_i     = Adc_Driver_Get_Current();
        s_disp_f_khz = (float)Pwm_Driver_Get_Frequency() / 1000.0f;
        s_disp_init  = 1;
    } else {
        s_disp_v     = s_disp_v     * 0.75f + Adc_Driver_Get_Voltage()              * 0.25f;
        s_disp_i     = s_disp_i     * 0.75f + Adc_Driver_Get_Current()              * 0.25f;
        s_disp_f_khz = s_disp_f_khz * 0.75f + (float)Pwm_Driver_Get_Frequency() / 1000.0f * 0.25f;
    }

    if (s_page == 0) {
        char buf[21];
        Tft_Driver_Show_String(1, 0, "[Resonant Mode]", COLOR_TITLE, COLOR_BG);

        snprintf(buf, sizeof(buf), "F:%3lukHz", (unsigned long)(s_disp_f_khz + 0.5f));
        Tft_Driver_Show_String(3, 0, buf, COLOR_VALUE, COLOR_BG);

        Tft_Driver_Show_String(5, 0, "V:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(5, 2, s_disp_v, 2, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(5, 9, "V", COLOR_TEXT, COLOR_BG);

        Tft_Driver_Show_String(5, 11, "I:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(5, 13, s_disp_i, 1, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(5, 18, "A", COLOR_TEXT, COLOR_BG);

        Tft_Driver_Show_String(7, 0, "K0:Stop K1/2:f+/", COLOR_TEXT, COLOR_BG);
    } else {
        Tft_Driver_Show_String(1, 0, "- Monitor Only -", COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "Freq:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Num(3, 6, (uint32_t)(s_disp_f_khz + 0.5f), 3, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(3, 9, "kHz", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_String(5, 0, "Volt:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(5, 6, s_disp_v, 2, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(5, 12, "V", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_String(7, 0, "Curr:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(7, 6, s_disp_i, 2, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_String(7, 12, "A", COLOR_TEXT, COLOR_BG);
    }
}

static void Draw_Fault(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_String(1, 0, "!!! FAULT !!!", COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "Over Current", COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_String(5, 0, "PWM Disabled", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_String(7, 0, "K0/K1: Reset", COLOR_VALUE, COLOR_BG);
    } else {
        Tft_Driver_Show_String(1, 0, "- Monitor Only -", COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_String(3, 0, "!!! FAULT !!!", COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_String(5, 0, "Over Current", COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_String(7, 0, "Reset: K0/K1", COLOR_TEXT, COLOR_BG);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发 (V6.2 4键版)
 * ═══════════════════════════════════════════════════════════════ */

static void Handle_Keys(Ui_Controller_State ui_state,
                        Key_Driver_Event k0, Key_Driver_Event k1,
                        Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* PAGE 双击切页 — 所有界面通用 */
    if (k3 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_page = !s_page;
        Tft_Driver_Clear(COLOR_BG);
        return;
    }

    /* 监测模式: 仅 PAGE 双击有效 */
    if (s_page == 1) return;

    switch (ui_state) {
        case UI_CONTROLLER_STATE_INIT:
            if (k0 == KEY_DRIVER_EVENT_CLICK) {
                App_Network_Start_Connect();
                Tft_Driver_Clear(COLOR_BG);
            }
            break;

        case UI_CONTROLLER_STATE_CONNECTING:
            break;

        case UI_CONTROLLER_STATE_READY:
            if (k0 == KEY_DRIVER_EVENT_CLICK)
                Inverter_Control_Soft_Start_Trigger();
            break;

        case UI_CONTROLLER_STATE_SWEEPING:
            if (k0 == KEY_DRIVER_EVENT_CLICK || k1 == KEY_DRIVER_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            break;

        case UI_CONTROLLER_STATE_RUNNING:
            if (k0 == KEY_DRIVER_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            if (k1 == KEY_DRIVER_EVENT_CLICK) {
                uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
                if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
            }
            if (k2 == KEY_DRIVER_EVENT_CLICK) {
                uint32_t f = Pwm_Driver_Get_Frequency();
                if (f >= PWM_DRIVER_FREQ_MIN_HZ + 1000) Pwm_Driver_Set_Frequency(f - 1000);
            }
            break;

        case UI_CONTROLLER_STATE_FAULT:
            if (k0 == KEY_DRIVER_EVENT_CLICK || k1 == KEY_DRIVER_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  UI 主调度
 * ═══════════════════════════════════════════════════════════════ */

static Ui_Controller_State Calc_Ui_State(void)
{
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();

    if (ss == INVERTER_CONTROL_SS_STATE_FAULT) return UI_CONTROLLER_STATE_FAULT;
    if (!Esp8266_Driver_Is_Ready())           return UI_CONTROLLER_STATE_INIT;

    uint8_t cs = App_Network_Get_Connect_Status();

    switch (cs) {
        case APP_NETWORK_CONN_IDLE:   return UI_CONTROLLER_STATE_INIT;
        case APP_NETWORK_CONN_WIFI:   return UI_CONTROLLER_STATE_CONNECTING;
        case APP_NETWORK_CONN_FAILED: return UI_CONTROLLER_STATE_INIT;
        case APP_NETWORK_CONN_ONLINE:
            if (ss == INVERTER_CONTROL_SS_STATE_IDLE)  return UI_CONTROLLER_STATE_READY;
            if (ss == INVERTER_CONTROL_SS_STATE_SWEEP) return UI_CONTROLLER_STATE_SWEEPING;
            return UI_CONTROLLER_STATE_RUNNING;
        default: return UI_CONTROLLER_STATE_INIT;
    }
}

void Ui_Controller_Task(void)
{
    static uint32_t last_refresh = 0;
    static uint8_t  last_state   = 0xFF;
    static uint8_t  last_page    = 0xFF;
    uint8_t         need_refresh = 0;

    if (Sys_Timer_Get_Tick() - last_refresh >= TFT_REFRESH_MS) {
        last_refresh = Sys_Timer_Get_Tick();
        need_refresh = 1;
    }

    Ui_Controller_State ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != last_state || s_page != last_page) {
        last_state = (uint8_t)ui_state;
        last_page  = s_page;
        Tft_Driver_Clear(COLOR_BG);
        Reset_Display_EMA();
        need_refresh = 1;
    }

    /* 按键: 0=ON/OFF, 1=F_UP, 2=F_DOWN, 3=PAGE */
    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_ID_ONOFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_ID_F_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_ID_F_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_ID_PAGE);

    /* KEY0 长按 → 清除 WiFi */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            Tft_Driver_Clear(COLOR_BG);
            Tft_Driver_Show_String(1, 0, "[Control Mode]", COLOR_TITLE, COLOR_BG);
            Tft_Driver_Show_String(3, 0, "WiFi Cleared...", COLOR_VALUE, COLOR_BG);
            Tft_Driver_Show_String(5, 0, "Reconfigure WiFi", COLOR_TEXT, COLOR_BG);
            last_state = (uint8_t)UI_CONTROLLER_STATE_CONNECTING;
            s_ui_state = UI_CONTROLLER_STATE_CONNECTING;
            Led_Driver_Task();
        }
        return;
    }

    Handle_Keys(ui_state, k0, k1, k2, k3);

    ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != last_state || s_page != last_page) {
        last_state = (uint8_t)ui_state;
        last_page  = s_page;
        Tft_Driver_Clear(COLOR_BG);
        Reset_Display_EMA();
        need_refresh = 1;
    }

    s_ui_state = ui_state;

    /* ── 过流保护 ── */
    if (ui_state == UI_CONTROLLER_STATE_SWEEPING || ui_state == UI_CONTROLLER_STATE_RUNNING) {
        if (Adc_Driver_Get_Current() > UI_CONTROLLER_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            ui_state = UI_CONTROLLER_STATE_FAULT;
            last_state = (uint8_t)UI_CONTROLLER_STATE_FAULT;
            s_ui_state = UI_CONTROLLER_STATE_FAULT;
            Tft_Driver_Clear(COLOR_BG);
            need_refresh = 1;
        }
    }

    /* FAULT 复位时关闭蜂鸣器 */
    if (ui_state != UI_CONTROLLER_STATE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

    /* READY 状态持续追踪电流零点 */
    if (ui_state == UI_CONTROLLER_STATE_READY &&
        Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_IDLE) {
        Adc_Driver_Calibrate_Offset();
    }

    if (need_refresh) Update_Leds(ui_state);
    Led_Driver_Task();
    Buzzer_Driver_Task();

    if (!need_refresh) return;

    switch (ui_state) {
        case UI_CONTROLLER_STATE_INIT:       Draw_Init();                      break;
        case UI_CONTROLLER_STATE_CONNECTING: Draw_Connecting(App_Network_Get_Retry_Count() + 1, 3); break;
        case UI_CONTROLLER_STATE_READY:      Draw_Ready();                     break;
        case UI_CONTROLLER_STATE_SWEEPING:   Draw_Sweeping();                  break;
        case UI_CONTROLLER_STATE_RUNNING:    Draw_Running();                   break;
        case UI_CONTROLLER_STATE_FAULT:      Draw_Fault();                     break;
    }
}

Ui_Controller_State Ui_Controller_Get_State(void) { return s_ui_state; }

uint8_t Ui_Controller_Get_Bridge_State(void)
{
    Inverter_Control_Soft_Start_State s = Inverter_Control_Soft_Start_Get_State();
    return (s == INVERTER_CONTROL_SS_STATE_SWEEP || s == INVERTER_CONTROL_SS_STATE_DONE);
}
