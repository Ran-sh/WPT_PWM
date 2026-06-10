/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 — 实现 (V6.2 TFT 彩屏中文版)
 * @note    TFT 8行×20列 16px, 中文占 2 列宽, 横屏 160×128 RGB565
 *          4键: KEY0=启停(PB9), KEY1=F+(PB8), KEY2=F-(PB7), KEY3=切页(PB5)
 *
 *          配色: 黑底 + 黄标题 + 白正文 + 青数值 + 红报警 + 绿正常
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

/* ── 配色方案 ── */
#define COLOR_BG          TFT_COLOR_BLACK
#define COLOR_TITLE       TFT_COLOR_YELLOW
#define COLOR_TEXT        TFT_COLOR_WHITE
#define COLOR_VALUE       TFT_COLOR_CYAN
#define COLOR_ALARM       TFT_COLOR_RED
#define COLOR_OK          TFT_COLOR_GREEN

#define TFT_REFRESH_MS     200

/* ── 过流保护阈值 ── */
#define UI_CONTROLLER_OVERCURRENT_THRESHOLD_A  5.0f

/* ── 中文串宏 (UTF-8 hex escapes) ── */
#define STR_INIT_TITLE          "\xe6\x8e\xa7\xe5\x88\xb6\xe9\x9d\xa2\xe6\x9d\xbf"           /* 控制面板 */
#define STR_CN_MODE             "\xe7\x9b\x91\xe6\xb5\x8b\xe6\xa8\xa1\xe5\xbc\x8f"           /* 监测模式 */
#define STR_CONNECTING          "\xe6\xad\xa3\xe5\x9c\xa8\xe8\xbf\x9e\xe6\x8e\xa5..."       /* 正在连接... */
#define STR_ESP_INIT            "ESP\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe4\xb8\xad"       /* ESP初始化中 */
#define STR_PLEASE_WAIT         "\xe8\xaf\xb7\xe7\xad\x89\xe5\xbe\x85..."                   /* 请等待... */
#define STR_SWEEPING            "\xe6\x89\xab\xe9\xa2\x91\xe4\xb8\xad..."                   /* 扫频中... */
#define STR_RESONANT            "\xe8\xb0\x90\xe6\x8c\xaf\xe6\xa8\xa1\xe5\xbc\x8f"          /* 谐振模式 */
#define STR_OVERFLOW            "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4"          /* 过流保护 */
#define STR_PWM_OFF             "PWM\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"                    /* PWM已关断 */
#define STR_FAULT_BANNER        "!!!\xe6\x95\x85\xe9\x9a\x9c!!!"                              /* !!!故障!!! */
#define STR_WELD_CLR            "WiFi\xe5\xb7\xb2\xe6\xb8\x85\xe9\x99\xa4"                    /* WiFi已清除 */
#define STR_REPAIR              "\xe8\xaf\xb7\xe9\x87\x8d\xe6\x96\xb0\xe9\x85\x8d\xe7\xbd\x91"  /* 请重新配网 */
#define STR_WIRELESS_CHG        "\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5"          /* 无线充电 */
#define STR_BOOTING             "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad..."                   /* 启动中... */

/* ── 模块状态 ── */
static uint8_t             s_page       = 0;
static Ui_Controller_State s_ui_state   = UI_CONTROLLER_STATE_INIT;

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
 *  各页面绘制 (160x128 宽松排版)
 * ═══════════════════════════════════════════════════════════════ */

static void Draw_Init(void)
{
    Tft_Driver_Show_CN_String(0, 0, STR_INIT_TITLE, COLOR_TITLE, COLOR_BG);
    Tft_Driver_Show_CN_String(2, 0, "WiFi:\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5", COLOR_TEXT, COLOR_BG);       /* WiFi:未连接 */
    Tft_Driver_Show_CN_String(4, 0, "\xe6\x8c\x89" "K0" "\xe8\xbf\x9e\xe6\x8e\xa5" "WiFi", COLOR_VALUE, COLOR_BG); /* 按K0连接WiFi */
    Tft_Driver_Show_CN_String(6, 0, "[K3]\xe5\x88\x87\xe6\x8d\xa2\xe9\xa1\xb5\xe9\x9d\xa2", COLOR_TEXT, COLOR_BG);  /* [K3]切换页面 */
}

static void Draw_Connecting(uint8_t retry, uint8_t max_retry)
{
    char buf[21];
    Tft_Driver_Show_CN_String(0, 0, STR_CONNECTING, COLOR_TITLE, COLOR_BG);
    Tft_Driver_Show_CN_String(2, 0, STR_ESP_INIT, COLOR_TEXT, COLOR_BG);
    snprintf(buf, sizeof(buf), "\xe9\x87\x8d\xe8\xaf\x95:%d/%d", retry, max_retry);          /* 重试: */
    Tft_Driver_Show_CN_String(4, 0, buf, COLOR_VALUE, COLOR_BG);
    Tft_Driver_Show_CN_String(6, 0, STR_PLEASE_WAIT, COLOR_TEXT, COLOR_BG);
}

static void Draw_Ready(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_CN_String(0, 0, STR_INIT_TITLE, COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, "WiFi:\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5", COLOR_OK, COLOR_BG);       /* WiFi:已连接 */
        Tft_Driver_Show_CN_String(3, 0, "\xe7\x8a\xb6\xe6\x80\x81:\xe5\xb0\xb1\xe7\xbb\xaa", COLOR_OK, COLOR_BG); /* 状态:就绪 */
        Tft_Driver_Show_CN_String(5, 0, "\xe6\x8c\x89" "K0" "\xe5\x90\xaf\xe5\x8a\xa8", COLOR_VALUE, COLOR_BG);   /* 按K0启动 */
    } else {
        Tft_Driver_Show_CN_String(0, 0, STR_CN_MODE, COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, "\xe7\x8a\xb6\xe6\x80\x81:\xe5\xb0\xb1\xe7\xbb\xaa", COLOR_OK, COLOR_BG); /* 状态:就绪 */
        Tft_Driver_Show_CN_String(4, 0, "\xe7\xad\x89\xe5\xbe\x85\xe5\x90\xaf\xe5\x8a\xa8", COLOR_TEXT, COLOR_BG); /* 等待启动 */
    }
}

static void Draw_Sweeping(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char fline[21];
    uint8_t j;

    progress = (SOFTSTART_START_FREQ_HZ - f) * 10
             / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
    if (progress > 10) progress = 10;

    if (s_page == 0) {
        Tft_Driver_Show_CN_String(0, 0, STR_SWEEPING, COLOR_TITLE, COLOR_BG);

        snprintf(fline, sizeof(fline), "\xe9\xa2\x91\xe7\x8e\x87: %3lu.%1lukHz",              /* 频率: */
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
        Tft_Driver_Show_CN_String(2, 0, fline, COLOR_VALUE, COLOR_BG);

        /* 进度条 */
        fline[0] = '[';
        for (j = 0; j < 10; j++) fline[1 + j] = (j < (int)progress) ? '#' : ' ';
        fline[11] = ']';
        fline[12] = '\0';
        Tft_Driver_Show_CN_String(4, 0, fline, COLOR_TEXT, COLOR_BG);

        Tft_Driver_Show_CN_String(6, 0, "K0/K1\xe5\x81\x9c\xe6\xad\xa2", COLOR_TEXT, COLOR_BG); /* K0/K1停止 */
    } else {
        Tft_Driver_Show_CN_String(0, 0, STR_CN_MODE, COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, STR_SWEEPING, COLOR_VALUE, COLOR_BG);
        snprintf(fline, sizeof(fline), "\xe9\xa2\x91\xe7\x8e\x87: %lukHz", (unsigned long)(f / 1000)); /* 频率: */
        Tft_Driver_Show_CN_String(4, 0, fline, COLOR_TEXT, COLOR_BG);
    }
}

static void Draw_Running(void)
{
    char buf[21];

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
        /* 主面板: 宽松排版 */
        Tft_Driver_Show_CN_String(0, 0, STR_RESONANT, COLOR_TITLE, COLOR_BG);

        /* 行2: F=xxxkHz   */
        snprintf(buf, sizeof(buf), "F = %3lukHz", (unsigned long)(s_disp_f_khz + 0.5f));
        Tft_Driver_Show_CN_String(2, 0, buf, COLOR_VALUE, COLOR_BG);

        /* 行4: Vin=xx.xV   */
        Tft_Driver_Show_CN_String(4, 0, "Vin:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(4, 5, s_disp_v, 2, 1, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_CN_String(4, 12, "V", COLOR_TEXT, COLOR_BG);

        /* 行5: Iin=x.xA    */
        Tft_Driver_Show_CN_String(5, 0, "Iin:", COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_Float(5, 5, s_disp_i, 1, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_CN_String(5, 12, "A", COLOR_TEXT, COLOR_BG);

        /* 行7: 操作提示 */
        Tft_Driver_Show_CN_String(7, 0, "K0\xe5\x81\x9c K1/2:+/-\xe9\xa2\x91\xe7\x8e\x87", COLOR_TEXT, COLOR_BG);  /* K0停 K1/2:+/-频率 */
    } else {
        /* 监测模式: 上下两列 */
        Tft_Driver_Show_CN_String(0, 0, STR_CN_MODE, COLOR_TITLE, COLOR_BG);

        Tft_Driver_Show_CN_String(2, 0, "\xe7\x94\xb5\xe5\x8e\x8b:", COLOR_TEXT, COLOR_BG);    /* 电压: */
        Tft_Driver_Show_Float(2, 7, s_disp_v, 2, 1, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 14, "V", COLOR_TEXT, COLOR_BG);

        Tft_Driver_Show_CN_String(3, 0, "\xe7\x94\xb5\xe6\xb5\x81:", COLOR_TEXT, COLOR_BG);    /* 电流: */
        Tft_Driver_Show_Float(3, 7, s_disp_i, 1, 2, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_CN_String(3, 14, "A", COLOR_TEXT, COLOR_BG);

        Tft_Driver_Show_CN_String(5, 0, "\xe9\xa2\x91\xe7\x8e\x87:", COLOR_TEXT, COLOR_BG);    /* 频率: */
        Tft_Driver_Show_Num(5, 7, (uint32_t)(s_disp_f_khz + 0.5f), 3, COLOR_VALUE, COLOR_BG);
        Tft_Driver_Show_CN_String(5, 10, "kHz", COLOR_TEXT, COLOR_BG);
    }
}

static void Draw_Fault(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_CN_String(0, 0, STR_FAULT_BANNER, COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, STR_OVERFLOW, COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, STR_PWM_OFF, COLOR_TEXT, COLOR_BG);
        Tft_Driver_Show_CN_String(6, 0, "\xe6\x8c\x89" "K0/K1" "\xe5\xa4\x8d\xe4\xbd\x8d", COLOR_VALUE, COLOR_BG); /* 按K0/K1复位 */
    } else {
        Tft_Driver_Show_CN_String(0, 0, STR_CN_MODE, COLOR_TITLE, COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, STR_FAULT_BANNER, COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, STR_OVERFLOW, COLOR_ALARM, COLOR_BG);
        Tft_Driver_Show_CN_String(6, 0, "\xe6\x8c\x89" "K0/K1" "\xe5\xa4\x8d\xe4\xbd\x8d", COLOR_TEXT, COLOR_BG);   /* 按K0/K1复位 */
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发
 * ═══════════════════════════════════════════════════════════════ */

static void Handle_Keys(Ui_Controller_State ui_state,
                        Key_Driver_Event k0, Key_Driver_Event k1,
                        Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* PAGE 双击切页 */
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
            Tft_Driver_Show_CN_String(0, 0, STR_INIT_TITLE, COLOR_TITLE, COLOR_BG);
            Tft_Driver_Show_CN_String(2, 0, STR_WELD_CLR, COLOR_VALUE, COLOR_BG);
            Tft_Driver_Show_CN_String(4, 0, STR_REPAIR, COLOR_TEXT, COLOR_BG);
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

    if (ui_state != UI_CONTROLLER_STATE_FAULT)
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);

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
