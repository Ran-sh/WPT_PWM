/**
 ******************************************************************************
 * @file    Hardware/Ui_Controller.c
 * @brief   人机界面控制器 — 实现
 ******************************************************************************
 */

#include "Ui_Controller.h"
#include "Oled_Driver.h"
#include "Key_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Esp8266_Driver.h"
#include "App_Network.h"
#include "Led_Driver.h"
#include "Sys_Timer.h"
#include <stdio.h>

/* ── 显示字符串常量 ── */
#define STR_CONTROL_MODE    "[Control Mode] "
#define STR_MONITOR_ONLY    "- Monitor Only -"
#define STR_WIFI_DISCONN    "WiFi: DISCONNED"
#define STR_WIFI_CONNECTED  "WiFi: CONNECTED "
#define STR_PRESS_KEY0_WIFI "Press KEY0 WiFi "
#define STR_PRESS_KEY0_START "Press KEY0 Start"
#define STR_FREQ_NONE       "F:  --.- kHz    "
#define STR_CONNECTING      "[Connecting...] "
#define STR_ESP_INIT        "ESP WiFi Init.. "
#define STR_PLEASE_WAIT     "Please wait...  "
#define STR_SWEEPING        "[Sweeping...]  "
#define STR_RESONANT_MODE   "[Resonant Mode] "
#define STR_FAULT           "!!! FAULT !!!   "
#define STR_OVER_CURRENT    "Over Current    "
#define STR_PWM_DISABLED    "PWM Disabled    "
#define STR_RESET_KEYS      "K0/K1: Reset    "
#define STR_K0_STOP_K1_ADD  "K0:Stop K1:+1k "
#define STR_STATE_READY     "State: READY    "
#define STR_AWAIT_START     "Awaiting Start  "

#define OLED_REFRESH_MS     200

/* ── 模块状态 ── */
static uint8_t             s_page       = 0;
static Ui_Controller_State s_ui_state   = UI_STATE_INIT;
static char                s_error_line[17];
static uint8_t             s_has_error  = 0;

static void Clear_Error(void) { s_has_error = 0; }

static void Set_Error(const char* msg)
{
    uint8_t i;
    if (!msg) { Clear_Error(); return; }
    for (i = 0; i < 16 && msg[i]; i++) s_error_line[i] = msg[i];
    for (; i < 16; i++) s_error_line[i] = ' ';
    s_error_line[16] = '\0';
    s_has_error = 1;
}

/* ── LED 更新 ── */
static void Update_Leds(Ui_Controller_State ui_state)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == 2)
        Led_Driver_Set_WiFi(LED_STATE_ON);
    else if (cs == 1)
        Led_Driver_Set_WiFi(LED_STATE_FAST);
    else
        Led_Driver_Set_WiFi(LED_STATE_SLOW);

    if (ui_state == UI_STATE_SWEEPING || ui_state == UI_STATE_RUNNING)
        Led_Driver_Set_Pwm(LED_STATE_ON);
    else
        Led_Driver_Set_Pwm(LED_STATE_OFF);

    Led_Driver_Set_Ready((ui_state == UI_STATE_RUNNING || ui_state == UI_STATE_FAULT));
}

/* ═══════════════════════════════════════════════════════════════
 *  各页面绘制
 * ═══════════════════════════════════════════════════════════════ */

static void Draw_Init(void)
{
    Oled_Driver_Show_String(1, 1, STR_CONTROL_MODE);
    Oled_Driver_Show_String(2, 1, STR_WIFI_DISCONN);
    Oled_Driver_Show_String(3, 1, STR_PRESS_KEY0_WIFI);
    if (s_has_error)
        Oled_Driver_Show_String(4, 1, s_error_line);
    else
        Oled_Driver_Show_String(4, 1, "                ");
}

static void Draw_Connecting(uint8_t retry, uint8_t max_retry)
{
    char buf[17];
    Oled_Driver_Show_String(1, 1, STR_CONNECTING);
    Oled_Driver_Show_String(2, 1, STR_ESP_INIT);
    snprintf(buf, sizeof(buf), "Retry: %d/%d      ", retry, max_retry);
    buf[16] = '\0';
    Oled_Driver_Show_String(3, 1, buf);
    Oled_Driver_Show_String(4, 1, STR_PLEASE_WAIT);
}

static void Draw_Ready(void)
{
    if (s_page == 0) {
        Oled_Driver_Show_String(1, 1, STR_CONTROL_MODE);
        Oled_Driver_Show_String(2, 1, STR_WIFI_CONNECTED);
        Oled_Driver_Show_String(3, 1, STR_PRESS_KEY0_START);
        Oled_Driver_Show_String(4, 1, STR_FREQ_NONE);
    } else {
        Oled_Driver_Show_String(1, 1, STR_MONITOR_ONLY);
        Oled_Driver_Show_String(2, 1, STR_STATE_READY);
        Oled_Driver_Show_String(3, 1, STR_AWAIT_START);
        Oled_Driver_Show_String(4, 1, "                ");
    }
}

static void Draw_Sweeping(void)
{
    uint32_t f = Inverter_Control_Soft_Start_Get_Current_Freq();
    uint32_t progress;
    char fline[17], bar[17];
    uint8_t j;

    if (s_page == 0) {
        progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                 / (SOFTSTART_START_FREQ_HZ - SOFTSTART_TARGET_FREQ_HZ);
        if (progress > 10) progress = 10;

        Oled_Driver_Show_String(1, 1, STR_SWEEPING);
        snprintf(fline, sizeof(fline), "Freq:%3lu.%1lukHz ",
                 (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 100));
        fline[16] = '\0';
        Oled_Driver_Show_String(2, 1, fline);

        bar[0] = '[';
        for (j = 0; j < 10; j++) bar[1 + j] = (j < (int)progress) ? '#' : ' ';
        bar[11] = ']';
        for (j = 12; j < 16; j++) bar[j] = ' ';
        bar[16] = '\0';
        Oled_Driver_Show_String(3, 1, bar);
        Oled_Driver_Show_String(4, 1, "                ");
    } else {
        Oled_Driver_Show_String(1, 1, STR_MONITOR_ONLY);
        Oled_Driver_Show_String(2, 1, "Sweeping...    ");
        Oled_Driver_Show_String(3, 1, "F:");
        Oled_Driver_Show_Num(3, 3, f / 1000, 3);
        Oled_Driver_Show_String(3, 6, "kHz            ");
        Oled_Driver_Show_String(4, 1, "                ");
    }
}

static void Draw_Running(void)
{
    uint32_t f = Pwm_Driver_Get_Frequency();

    if (s_page == 0) {
        Oled_Driver_Show_String(1, 1, STR_RESONANT_MODE);
        Oled_Driver_Show_String(2, 1, "F:");
        Oled_Driver_Show_Num(2, 3, f / 1000, 3);
        Oled_Driver_Show_String(2, 6, "kHz  ");
        Oled_Driver_Show_String(3, 1, "V:");
        Oled_Driver_Show_Float(3, 3, Adc_Driver_Get_Voltage(), 2, 1);
        Oled_Driver_Show_String(3, 9, "I:");
        Oled_Driver_Show_Float(3, 11, Adc_Driver_Get_Current(), 1, 2);
        Oled_Driver_Show_String(4, 1, STR_K0_STOP_K1_ADD);
    } else {
        Oled_Driver_Show_String(1, 1, STR_MONITOR_ONLY);
        Oled_Driver_Show_String(2, 1, "Freq: ");
        Oled_Driver_Show_Num(2, 7, f / 1000, 3);
        Oled_Driver_Show_String(2, 10, "kHz");
        Oled_Driver_Show_String(3, 1, "Volt: ");
        Oled_Driver_Show_Float(3, 7, Adc_Driver_Get_Voltage(), 2, 2);
        Oled_Driver_Show_String(4, 1, "Curr: ");
        Oled_Driver_Show_Float(4, 7, Adc_Driver_Get_Current(), 2, 2);
    }
}

static void Draw_Fault(void)
{
    if (s_page == 0) {
        Oled_Driver_Show_String(1, 1, STR_FAULT);
        Oled_Driver_Show_String(2, 1, STR_OVER_CURRENT);
        Oled_Driver_Show_String(3, 1, STR_PWM_DISABLED);
        Oled_Driver_Show_String(4, 1, STR_RESET_KEYS);
    } else {
        Oled_Driver_Show_String(1, 1, STR_MONITOR_ONLY);
        Oled_Driver_Show_String(2, 1, STR_FAULT);
        Oled_Driver_Show_String(3, 1, STR_OVER_CURRENT);
        Oled_Driver_Show_String(4, 1, "Reset: K0/K1    ");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发
 * ═══════════════════════════════════════════════════════════════ */

static void Handle_Keys(Ui_Controller_State ui_state, Key_Driver_Event key0, Key_Driver_Event key1)
{
    /* 双击切页 — 所有界面通用, 监测模式下唯一有效操作 */
    if (key0 == KEY_EVENT_DOUBLE_CLICK) {
        s_page = !s_page;
        Oled_Driver_Clear();
        Clear_Error();
        return;
    }

    /* 监测模式: 仅双击有效 */
    if (s_page == 1) return;

    switch (ui_state) {
        case UI_STATE_INIT:
            if (key0 == KEY_EVENT_CLICK) {
                App_Network_Start_Connect();
                Clear_Error();
                Oled_Driver_Clear();
            }
            break;

        case UI_STATE_CONNECTING:
            break;

        case UI_STATE_READY:
            if (key0 == KEY_EVENT_CLICK)
                Inverter_Control_Soft_Start_Trigger();
            break;

        case UI_STATE_SWEEPING:
            if (key0 == KEY_EVENT_CLICK || key1 == KEY_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            break;

        case UI_STATE_RUNNING:
            if (key0 == KEY_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            if (key1 == KEY_EVENT_CLICK) {
                uint32_t f = Pwm_Driver_Get_Frequency() + 1000;
                if (f <= PWM_DRIVER_FREQ_MAX_HZ) Pwm_Driver_Set_Frequency(f);
            }
            break;

        case UI_STATE_FAULT:
            if (key0 == KEY_EVENT_CLICK || key1 == KEY_EVENT_CLICK)
                Inverter_Control_Soft_Start_Stop();
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  UI 主调度
 * ═══════════════════════════════════════════════════════════════ */

/*
 * UI 状态计算 — 独立纯函数, 消除原始代码中的双重计算
 */
static Ui_Controller_State Calc_Ui_State(void)
{
    Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();

    if (ss == SS_STATE_FAULT)          return UI_STATE_FAULT;
    if (!Esp8266_Driver_Is_Ready())    return UI_STATE_INIT;

    uint8_t cs = App_Network_Get_Connect_Status();

    switch (cs) {
        case 0:  return UI_STATE_INIT;
        case 1:  return UI_STATE_CONNECTING;
        case 3:
            if (!s_has_error) Set_Error("WiFi Failed x3  ");
            return UI_STATE_INIT;
        case 2:
            if (ss == SS_STATE_IDLE)   return UI_STATE_READY;
            if (ss == SS_STATE_SWEEP)  return UI_STATE_SWEEPING;
            return UI_STATE_RUNNING;   /* SS_DONE */
        default: return UI_STATE_INIT;
    }
}

void Ui_Controller_Task(void)
{
    static uint32_t last_oled    = 0;
    static uint8_t  last_state   = 0xFF;
    static uint8_t  last_page    = 0xFF;
    uint8_t         need_refresh = 0;

    if (Sys_Timer_Get_Tick() - last_oled >= OLED_REFRESH_MS) {
        last_oled = Sys_Timer_Get_Tick();
        need_refresh = 1;
    }

    Ui_Controller_State ui_state = Calc_Ui_State();

    /* 状态/页面迁移 → 全屏清零 */
    if ((uint8_t)ui_state != last_state || s_page != last_page) {
        last_state = (uint8_t)ui_state;
        last_page  = s_page;
        Oled_Driver_Clear();
        need_refresh = 1;
    }

    /* 按键 */
    Key_Driver_Event key0 = Key_Driver_Get_Event(0);
    Key_Driver_Event key1 = Key_Driver_Get_Event(1);

    /* KEY0 长按 → 清除 WiFi */
    if (key0 == KEY_EVENT_LONG_PRESS) {
        if (Esp8266_Driver_Is_Ready()) {
            Esp8266_Driver_Send_String("CMD:CLEAR\n");
            App_Network_Soft_Reset();
            Oled_Driver_Clear();
            Oled_Driver_Show_String(1, 1, STR_CONTROL_MODE);
            Oled_Driver_Show_String(2, 1, "WiFi Cleared... ");
            Oled_Driver_Show_String(3, 1, "Please reconfi-");
            Oled_Driver_Show_String(4, 1, "gure WiFi      ");
            last_state = (uint8_t)UI_STATE_CONNECTING;
            s_ui_state = UI_STATE_CONNECTING;
            Led_Driver_Task();
        }
        return;
    }

    Handle_Keys(ui_state, key0, key1);

    /* 按键可能改变逆变器状态, 重新计算 */
    ui_state = Calc_Ui_State();

    if ((uint8_t)ui_state != last_state || s_page != last_page) {
        last_state = (uint8_t)ui_state;
        last_page  = s_page;
        Oled_Driver_Clear();
        need_refresh = 1;
    }

    s_ui_state = ui_state;

    /* READY 状态持续追踪电流零点 */
    if (ui_state == UI_STATE_READY &&
        Inverter_Control_Soft_Start_Get_State() == SS_STATE_IDLE) {
        Adc_Driver_Calibrate_Offset();
    }

    if (need_refresh) Update_Leds(ui_state);
    Led_Driver_Task();

    if (!need_refresh) return;

    switch (ui_state) {
        case UI_STATE_INIT:       Draw_Init();                      break;
        case UI_STATE_CONNECTING: Draw_Connecting(App_Network_Get_Retry_Count() + 1, 3); break;
        case UI_STATE_READY:      Draw_Ready();                     break;
        case UI_STATE_SWEEPING:   Draw_Sweeping();                  break;
        case UI_STATE_RUNNING:    Draw_Running();                   break;
        case UI_STATE_FAULT:      Draw_Fault();                     break;
    }
}

Ui_Controller_State Ui_Controller_Get_State(void) { return s_ui_state; }

uint8_t Ui_Controller_Get_Bridge_State(void)
{
    Inverter_Control_Soft_Start_State s = Inverter_Control_Soft_Start_Get_State();
    return (s == SS_STATE_SWEEP || s == SS_STATE_DONE);
}
