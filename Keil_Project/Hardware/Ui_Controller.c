/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 — 实现 (V6.2 TFT 彩屏中文版)
 * @note    TFT 8行×20列 16px, 中文占 2 列宽, 横屏 160×128 RGB565
 *          4键: K0=启停(PB9), K1=F+(PB8), K2=F-(PB7), K3=切页(PB5)
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

#include <stdlib.h>

#define UI_CONTROLLER_COLOR_BG          TFT_COLOR_BLACK
#define UI_CONTROLLER_COLOR_TITLE       TFT_COLOR_YELLOW
#define UI_CONTROLLER_COLOR_TEXT        TFT_COLOR_WHITE
#define UI_CONTROLLER_COLOR_VALUE       TFT_COLOR_CYAN
#define UI_CONTROLLER_COLOR_ALARM       TFT_COLOR_RED
#define UI_CONTROLLER_COLOR_OK          TFT_COLOR_GREEN

#define UI_CONTROLLER_REFRESH_MS        200
#define UI_CONTROLLER_OVERCURRENT_THRESHOLD_A  5.0f
#define UI_CONTROLLER_POWER_VOLTAGE_THRESHOLD_V  12.0f  /* POWER 灯亮起最低电压 */

/*
 * 中文串宏: UTF-8 hex escape (ARMCC V5 汇编器不支持 multibyte_chars)
 * 字库为 60 字宋体 LSB 优先, 缺少"在/制/板/振"等字已替换
 */
#define UI_STR_TITLE       "\xe6\x8e\xa7\xe5\x88\xb6\xe9\x9d\xa2\xe6\x9d\xbf" /* 控制面板 — 字库46=控56=制28=面2=板 ✓ */
#define UI_STR_MONITOR     "\xe7\x9b\x91\xe6\xb5\x8b"                 /* 监测 — 19=监4=测 ✓ */
#define UI_STR_CONNECTING  "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad"    /* 连接中 — 25=连21=接57=中 ✓ */
#define UI_STR_ESP_INIT    "ESP" "\xe5\x88\x9d\xe5\xa7\x8b"           /* ESP初始 — 7=初37=始 ✓ */
#define UI_STR_PLEASE_WAIT "\xe8\xaf\xb7\xe7\xad\x89\xe5\xbe\x85"     /* 请等待 — 35=请8=待 ✓ */
#define UI_STR_SWEEPING    "\xe6\x89\xab\xe9\xa2\x91\xe4\xb8\xad"     /* 扫频中 — 36=扫31=频57=中 ✓ */
#define UI_STR_RESONANT    "\xe8\xb0\x90\xe6\x8c\xaf"                 /* 谐振 — 47=谐60=振 ✓ */
#define UI_STR_OVERFLOW    "\xe8\xbf\x87\xe6\xb5\x81\xe4\xbf\x9d\xe6\x8a\xa4" /* 过流保护 — 15=过26=流, 保=3护=17 ✓ */
#define UI_STR_PWM_OFF     "PWM" "\xe5\xb7\xb2\xe5\x85\xb3\xe6\x96\xad"       /* PWM已关断 — 52=已14=关11=断 ✓ */
#define UI_STR_FAULT_BANNER "!!!" "\xe6\x95\x85\xe9\x9a\x9c" "!!!"    /* !!!故障!!! — 13=故53=障 ✓ */
#define UI_STR_WELD_CLR    "WiFi" "\xe5\xb7\xb2\xe6\xb8\x85\xe9\x99\xa4" /* WiFi已清除 — 52=已34=清6=除 ✓ */
#define UI_STR_REPAIR       "\xe8\xaf\xb7" "\xe9\x87\x8d" "\xe6\x96\xb0" "\xe9\x85\x8d" "\xe7\xbd\x91"  /* 请重新配网 — 35=请58=重48=新30=配42=网 */
#define UI_STR_WIRELESS_CHG "\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5"            /* 无线充电 — 45=无46=线5=充9=电 */
#define UI_STR_BOOTING      "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad"                         /* 启动中 — 32=启10=动57=中 */
#define UI_STR_STATE_READY  "\xe5\xb0\xb1\xe7\xbb\xaa"                                     /* 就绪 — 22=就49=绪 */
#define UI_STR_WIFI_LINKED   "WiFi:" "\xe5\xb7\xb2\xe8\xbf\x9e"                             /* WiFi:已连 — 52=已25=连 */
#define UI_STR_WIFI_UNLINK   "WiFi:" "\xe6\x9c\xaa\xe8\xbf\x9e"                             /* WiFi:未连 — 44=未25=连 */
#define UI_STR_PRESS_K0      "\xe6\x8c\x89" "K0"                                     /* 按K0 — 0=按 */
#define UI_STR_PRESS_K0_ON    "\xe6\x8c\x89" "K0" "\xe5\x90\xaf\xe5\x8a\xa8"          /* 按K0启动 */
#define UI_STR_PRESS_K0_WIFI  "\xe6\x8c\x89" "K0" "\xe8\xbf\x9e" "WiFi"               /* 按K0连WiFi */
#define UI_STR_START_PAGE  "\xe5\x90\xaf\xe5\x8a\xa8\xe9\xa1\xb5"     /* 启动页 — 32=启10=动51=页 ✓ */
#define UI_STR_FREQ         "\xe9\xa2\x91\xe7\x8e\x87"                               /* 频率 */
#define UI_STR_VOLTAGE      "\xe7\x94\xb5\xe5\x8e\x8b"                               /* 电压 */
#define UI_STR_CURRENT      "\xe7\x94\xb5\xe6\xb5\x81"                               /* 电流 */
#define UI_STR_STOP         "\xe5\x81\x9c\xe6\xad\xa2"                               /* 停止 */
#define UI_STR_RESET        "\xe5\xa4\x8d\xe4\xbd\x8d"                               /* 复位 */
#define UI_STR_WAIT_START     "\xe7\xad\x89\xe5\xbe\x85\xe5\x90\xaf\xe5\x8a\xa8"       /* 等待启动 */
#define UI_STR_RETRY_PREFIX   "\xe9\x87\x8d\xe8\xaf\x95:"                              /* 重试: */

/* ── 模块状态 ── */
static uint8_t             s_page       = 0;
static Ui_Controller_State s_ui_state   = UI_CONTROLLER_STATE_INIT;

/* EMA 显示平滑 */
static float   s_disp_v     = 0.0f;
static float   s_disp_i     = 0.0f;
static float   s_disp_f_khz = 0.0f;
static uint8_t s_disp_init  = 0;

static void Reset_Display_EMA(void) { s_disp_init = 0; }

static void Update_Leds(Ui_Controller_State ui_state)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    /* WiFi 灯 (绿): 未连接→慢闪, 连接中→快闪, 在线→常亮 */
    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);

    /* PWM 灯 (翠绿): 扫频中→快闪, 其余→灭 */
    if (ui_state == UI_CONTROLLER_STATE_SWEEPING)
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_FAST);
    else
        Led_Driver_Set_Pwm(LED_DRIVER_STATE_OFF);

    /* POWER 灯 (绿): 输入端电压>阈值→亮, 否则→灭 */
    Led_Driver_Set_Power(
        Adc_Driver_Get_Voltage() > UI_CONTROLLER_POWER_VOLTAGE_THRESHOLD_V
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);

    /* TEMP 灯 (翠绿): 温度功能启用后→超阈值慢闪/正常常亮, 未启用→灭 */
    Led_Driver_Set_Temp(LED_DRIVER_STATE_OFF);  /* 暂未启用 */

    /* COM 灯: MQTT 在线→常亮, 离线→灭 */
    Led_Driver_Set_Com(
        cs == APP_NETWORK_CONN_ONLINE
        ? LED_DRIVER_STATE_ON : LED_DRIVER_STATE_OFF);
}

/* ═══════════════════════════════════════════════════════════════
 *  各页面绘制 (160x128)
 * ═══════════════════════════════════════════════════════════════ */

static void Draw_Init(void)
{
    if (s_page == 0) {
        /* 行0: 启动页 (3CN=6col, col=(20-6)/2=7) */
        Tft_Driver_Show_CN_String(0, 7, UI_STR_START_PAGE, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        /* 行1: 空 */
        /* 行2: 空 */
        /* 行3: WiFi:未连 (WiFi:=5+未连=2CN→4+4=9col, col=(20-9)/2=5) */
        Tft_Driver_Show_CN_String(3, 5, UI_STR_WIFI_UNLINK, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        /* 行4: Vin: xx.xV (居中, ~12chars, col=4) */
        {
            char buf[21];
            float v = Adc_Driver_Get_Voltage();
            int32_t vi = (int32_t)(v * 10.0f + 0.5f);
            snprintf(buf, sizeof(buf), "Vin:%2d.%1dV", vi/10, abs(vi)%10);
            Tft_Driver_Show_String(4, 6, buf, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        }
        /* 行5: Iin: x.xxA (居中, ~12chars, col=4) */
        {
            char buf[21];
            float c = Adc_Driver_Get_Current();
            int32_t ci = (int32_t)(c * 100.0f + 0.5f);
            snprintf(buf, sizeof(buf), "Iin:%1d.%2dA", ci/100, abs(ci)%100);
            Tft_Driver_Show_String(5, 6, buf, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        }
        /* 行6: 空 */
        /* 行7: 按K0连WiFi + [PAGE]切页 (左下+右下) */
        Tft_Driver_Show_CN_String(7, 0,  UI_STR_PRESS_K0_WIFI,          UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(7, 11, "[PAGE]" "\xe5\x88\x87\xe9\xa1\xb5", UI_CONTROLLER_COLOR_TEXT,  UI_CONTROLLER_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_TITLE, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_WIFI_UNLINK, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, UI_STR_PRESS_K0_WIFI, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
    }
}

static void Draw_Connecting(uint8_t retry, uint8_t max_retry)
{
    char buf[21];
    Tft_Driver_Show_CN_String(0, 0, UI_STR_CONNECTING, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
    Tft_Driver_Show_CN_String(2, 0, UI_STR_ESP_INIT, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
    snprintf(buf, sizeof(buf), UI_STR_RETRY_PREFIX "%d/%d", retry, max_retry);
    Tft_Driver_Show_CN_String(4, 0, buf, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
    Tft_Driver_Show_CN_String(6, 0, UI_STR_PLEASE_WAIT, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
}

static void Draw_Ready(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_TITLE, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_WIFI_LINKED, UI_CONTROLLER_COLOR_OK, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(3, 0, UI_STR_STATE_READY, UI_CONTROLLER_COLOR_OK, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(5, 0, UI_STR_PRESS_K0_ON, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_MONITOR, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_STATE_READY, UI_CONTROLLER_COLOR_OK, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, UI_STR_WAIT_START, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
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
        Tft_Driver_Show_CN_String(0, 0, UI_STR_SWEEPING, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);

        snprintf(fline, sizeof(fline), UI_STR_FREQ ": %3lu.%1lukHz",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
        Tft_Driver_Show_CN_String(2, 0, fline, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);

        fline[0] = '[';
        for (j = 0; j < 10; j++) fline[1 + j] = (j < (int)progress) ? '#' : ' ';
        fline[11] = ']';
        fline[12] = '\0';
        Tft_Driver_Show_CN_String(4, 0, fline, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(6, 0, "K0/K1" UI_STR_STOP, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_MONITOR, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_SWEEPING, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        snprintf(fline, sizeof(fline), UI_STR_FREQ ": %lukHz", (unsigned long)(f / 1000));
        Tft_Driver_Show_CN_String(4, 0, fline, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
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
        Tft_Driver_Show_CN_String(0, 0, UI_STR_RESONANT, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);

        snprintf(buf, sizeof(buf), "F = %3lukHz", (unsigned long)(s_disp_f_khz + 0.5f));
        Tft_Driver_Show_CN_String(2, 0, buf, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(4, 0, "Vin:", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_Float(4, 5, s_disp_v, 2, 1, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(4, 12, "V", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(5, 0, "Iin:", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_Float(5, 5, s_disp_i, 1, 2, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(5, 12, "A", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(7, 0, "K0" UI_STR_STOP " K1/2:+/-\xe9\xa2\x91\xe7\x8e\x87",
            UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_MONITOR, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(2, 0, UI_STR_VOLTAGE ":", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_Float(2, 7, s_disp_v, 2, 1, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 14, "V", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(3, 0, UI_STR_CURRENT ":", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_Float(3, 7, s_disp_i, 1, 2, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(3, 14, "A", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);

        Tft_Driver_Show_CN_String(5, 0, UI_STR_FREQ ":", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_Num(5, 7, (uint32_t)(s_disp_f_khz + 0.5f), 3,
            UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(5, 10, "kHz", UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
    }
}

static void Draw_Fault(void)
{
    if (s_page == 0) {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_FAULT_BANNER, UI_CONTROLLER_COLOR_ALARM, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_OVERFLOW, UI_CONTROLLER_COLOR_ALARM, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, UI_STR_PWM_OFF, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(6, 0, "\xe6\x8c\x89" "K0/K1" UI_STR_RESET,
            UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
    } else {
        Tft_Driver_Show_CN_String(0, 0, UI_STR_MONITOR, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(2, 0, UI_STR_FAULT_BANNER, UI_CONTROLLER_COLOR_ALARM, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(4, 0, UI_STR_OVERFLOW, UI_CONTROLLER_COLOR_ALARM, UI_CONTROLLER_COLOR_BG);
        Tft_Driver_Show_CN_String(6, 0, "\xe6\x8c\x89" "K0/K1" UI_STR_RESET,
            UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发
 * ═══════════════════════════════════════════════════════════════ */

static void Handle_Keys(Ui_Controller_State ui_state,
                        Key_Driver_Event k0, Key_Driver_Event k1,
                        Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* PAGE 按键单击切页 — 所有界面通用 */
    if (k3 == KEY_DRIVER_EVENT_CLICK) {
        s_page = !s_page;
        Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
        return;
    }

    /* K0 单击 — 按状态分发 */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (ui_state) {
            case UI_CONTROLLER_STATE_INIT:
                App_Network_Start_Connect();
                Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
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
        switch (ui_state) {
            case UI_CONTROLLER_STATE_SWEEPING:
                Inverter_Control_Soft_Start_Stop();
                break;
            case UI_CONTROLLER_STATE_RUNNING: {
                uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
                if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
                break;
            }
            case UI_CONTROLLER_STATE_FAULT:
                Inverter_Control_Soft_Start_Stop();
                break;
            default: break;
        }
        return;
    }

    /* K2 单击 — 仅 RUNNING 有效 */
    if (k2 == KEY_DRIVER_EVENT_CLICK && ui_state == UI_CONTROLLER_STATE_RUNNING) {
        uint32_t f = Pwm_Driver_Get_Frequency();
        if (f >= PWM_DRIVER_FREQ_MIN_HZ + 1000) Pwm_Driver_Set_Frequency(f - 1000);
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
    static uint32_t s_last_refresh = 0;
    static uint8_t  s_last_state   = 0xFF;
    static uint8_t  s_last_page    = 0xFF;
    uint8_t         need_refresh = 0;

    if (Sys_Timer_Get_Tick() - s_last_refresh >= UI_CONTROLLER_REFRESH_MS) {
        s_last_refresh = Sys_Timer_Get_Tick();
        need_refresh = 1;
    }

    Ui_Controller_State ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != s_last_state || s_page != s_last_page) {
        s_last_state = (uint8_t)ui_state;
        s_last_page  = s_page;
        Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
        Reset_Display_EMA();
        need_refresh = 1;
    }

    Key_Driver_Event k0 = Key_Driver_Get_Event(KEY_DRIVER_ID_ON_OFF);
    Key_Driver_Event k1 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_UP);
    Key_Driver_Event k2 = Key_Driver_Get_Event(KEY_DRIVER_ID_FREQ_DOWN);
    Key_Driver_Event k3 = Key_Driver_Get_Event(KEY_DRIVER_ID_PAGE);

    /* KEY_ON_OFF 长按 → 清除 WiFi */
    if (k0 == KEY_DRIVER_EVENT_LONG_PRESS) {
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
            Tft_Driver_Show_CN_String(0, 0, UI_STR_TITLE, UI_CONTROLLER_COLOR_TITLE, UI_CONTROLLER_COLOR_BG);
            Tft_Driver_Show_CN_String(2, 0, UI_STR_WELD_CLR, UI_CONTROLLER_COLOR_VALUE, UI_CONTROLLER_COLOR_BG);
            Tft_Driver_Show_CN_String(4, 0, UI_STR_REPAIR, UI_CONTROLLER_COLOR_TEXT, UI_CONTROLLER_COLOR_BG);
            s_last_state = (uint8_t)UI_CONTROLLER_STATE_CONNECTING;
            s_ui_state   = UI_CONTROLLER_STATE_CONNECTING;
            Led_Driver_Task();
        }
        return;
    }

    Handle_Keys(ui_state, k0, k1, k2, k3);

    ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != s_last_state || s_page != s_last_page) {
        s_last_state = (uint8_t)ui_state;
        s_last_page  = s_page;
        Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
        Reset_Display_EMA();
        need_refresh = 1;
    }

    s_ui_state = ui_state;

    /* ── 过流保护 ── */
    if (ui_state == UI_CONTROLLER_STATE_SWEEPING ||
        ui_state == UI_CONTROLLER_STATE_RUNNING) {
        if (Adc_Driver_Get_Current() > UI_CONTROLLER_OVERCURRENT_THRESHOLD_A) {
            Inverter_Control_Soft_Start_Fault();
            Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
            ui_state    = UI_CONTROLLER_STATE_FAULT;
            s_last_state = (uint8_t)UI_CONTROLLER_STATE_FAULT;
            s_ui_state  = UI_CONTROLLER_STATE_FAULT;
            Tft_Driver_Clear(UI_CONTROLLER_COLOR_BG);
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
        case UI_CONTROLLER_STATE_CONNECTING: Draw_Connecting(
            App_Network_Get_Retry_Count() + 1, 3);                             break;
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
