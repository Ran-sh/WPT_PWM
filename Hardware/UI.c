/**
 ******************************************************************************
 * @file    Hardware/UI.c
 * @brief   人机交互界面 —— OLED 显示 + 按键事件分发 + LED 状态驱动
 * @note    V3.1: 拆分为 UI_UpdateLEDs / UI_TryConnectWiFi / UI_HandleKeys
 *          UI_DrawPage0 / UI_DrawPage1, 每函数 ≤ 2 层嵌套
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "UI.h"
#include "OLED.h"
#include "KEY.h"
#include "PWM.h"
#include "ADC.h"
#include "App_Net.h"
#include "SysTimer.h"
#include "LED.h"
#include <stdio.h>

static uint8_t UI_Page = 0;   /* 0: 控制面板, 1: 锁屏监控 */

static void UI_ClearAllLines(void)
{
    OLED_ShowString(1, 1, "                ");
    OLED_ShowString(2, 1, "                ");
    OLED_ShowString(3, 1, "                ");
    OLED_ShowString(4, 1, "                ");
}

/* ── LED 状态更新 (每 200ms) ── */
static void UI_UpdateLEDs(SoftStart_State_t ss)
{
    /* PB3 WiFi */
    LED_Update_WiFi(App_Net_IsConnected() ? LED_OFF : LED_SLOW);

    /* PB4 PWM */
    if (ss == SS_SWEEP)
        LED_Update_PWM(LED_FAST);
    else if (ss == SS_DONE)
        LED_Update_PWM(LED_SLOW);
    else
        LED_Update_PWM(LED_OFF);

    /* PB5 Ready */
    LED_Update_Ready((UI_Page == 0) && App_Net_IsConnected()
        && (ss == SS_IDLE || ss == SS_DONE));
}

/* ── 联网触发 (返回 1=已触发 0=未触发) ── */
static uint8_t UI_TryConnectWiFi(void)
{
    uint8_t ret;

    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi Connecting ");
    OLED_ShowString(3, 1, "Please wait...  ");
    OLED_ShowString(4, 1, "                ");

    Inverter_SoftStart_Stop();

    /* 点跳动 + LED 闪 (150ms/帧) */
    LED_WiFi_ON();  OLED_ShowString(3, 1, "Please wait.    "); SysTimer_DelayMs(150);
    LED_WiFi_OFF(); OLED_ShowString(3, 1, "Please wait..   "); SysTimer_DelayMs(150);
    LED_WiFi_ON();  OLED_ShowString(3, 1, "Please wait...  "); SysTimer_DelayMs(150);
    LED_WiFi_OFF(); OLED_ShowString(3, 1, "Please wait.... "); SysTimer_DelayMs(150);
    LED_WiFi_ON();  OLED_ShowString(3, 1, "Please wait..... "); SysTimer_DelayMs(150);
    LED_WiFi_OFF(); OLED_ShowString(3, 1, "Please wait.    "); SysTimer_DelayMs(150);
    LED_WiFi_ON();  OLED_ShowString(3, 1, "Please wait..   "); SysTimer_DelayMs(150);
    LED_Update_WiFi(LED_SOLID);

    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi Connecting ");
    ret = App_Net_Init();

    LED_Update_WiFi((ret == 0) ? LED_SOLID : LED_OFF);
    UI_ClearAllLines();
    return 1;
}

/* ── 已联网按键分发 ── */
static void UI_HandleKeys(uint8_t key0, uint8_t key1, SoftStart_State_t ss)
{
    if (key0 == 1) {
        if (ss == SS_IDLE)
            Inverter_SoftStart_Trigger();
        else if (ss == SS_DONE)
            Inverter_SoftStart_Stop();
    }
    if (key1 == 1) {
        if (ss == SS_SWEEP)
            Inverter_SoftStart_Stop();
        else if (ss == SS_DONE) {
            uint32_t f = PWM_GetFrequency() + 1000;
            PWM_SetFrequency((f > 150000) ? 100000 : f);
        }
    }
}

/* ── 页面 0: 控制面板 ── */
static void UI_DrawPage0(SoftStart_State_t ss)
{
    uint32_t f;
    uint32_t progress;
    char bar[17];
    uint8_t j;
    char fline[17];

    switch (ss) {
        case SS_IDLE:
            OLED_ShowString(1, 1, "[Control Mode] ");
            OLED_ShowString(2, 1, "State: IDLE   ");
            OLED_ShowString(3, 1, "Press KEY0 start");
            OLED_ShowString(4, 1, "F:  --.- kHz    ");
            break;

        case SS_SWEEP:
            f = Inverter_SoftStart_GetCurrentFreq();
            progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                     / (SOFTSTART_START_FREQ_HZ - 100000UL);
            OLED_ShowString(1, 1, "[Sweeping...]  ");
            snprintf(fline, sizeof(fline), "Freq:%3lu.%1lukHz ",
                     (unsigned long)(f / 1000),
                     (unsigned long)((f % 1000) / 100));
            fline[16] = '\0';
            OLED_ShowString(2, 1, fline);
            bar[0] = '[';
            for (j = 0; j < 10; j++) bar[1 + j] = (j < progress) ? '#' : ' ';
            bar[11] = ']';
            for (j = 12; j < 16; j++) bar[j] = ' ';
            bar[16] = '\0';
            OLED_ShowString(3, 1, bar);
            break;

        case SS_DONE:
            f = PWM_GetFrequency();
            OLED_ShowString(1, 1, "[Resonant Mode] ");
            OLED_ShowString(2, 1, "F:");
            OLED_ShowNum(2, 3, f / 1000, 3);
            OLED_ShowString(2, 6, "kHz  ");
            OLED_ShowString(3, 1, "V:");
            OLED_ShowFloatNum(3, 3, Get_Real_Voltage(), 2, 1);
            OLED_ShowString(3, 9, "I:");
            OLED_ShowFloatNum(3, 11, Get_Real_Current(), 1, 2);
            OLED_ShowString(4, 1, "K0:Stop K1:+1k ");
            break;
    }
}

/* ── 页面 1: 锁屏监控 ── */
static void UI_DrawPage1(SoftStart_State_t ss)
{
    uint32_t f;

    switch (ss) {
        case SS_IDLE:
            OLED_ShowString(1, 1, "- Monitor Only -");
            OLED_ShowString(2, 1, "State: IDLE    ");
            OLED_ShowString(3, 1, "Waiting trigger ");
            break;

        case SS_SWEEP:
            f = Inverter_SoftStart_GetCurrentFreq();
            OLED_ShowString(1, 1, "- Monitor Only -");
            OLED_ShowString(2, 1, "Sweeping...    ");
            OLED_ShowString(3, 1, "F:");
            OLED_ShowNum(3, 3, f / 1000, 3);
            OLED_ShowString(3, 6, "kHz");
            break;

        case SS_DONE:
            f = PWM_GetFrequency();
            OLED_ShowString(1, 1, "- Monitor Only -");
            OLED_ShowString(2, 1, "Freq: ");
            OLED_ShowNum(2, 7, f / 1000, 3);
            OLED_ShowString(2, 10, "kHz");
            OLED_ShowString(3, 1, "Volt: ");
            OLED_ShowFloatNum(3, 7, Get_Real_Voltage(), 2, 2);
            OLED_ShowString(4, 1, "Curr: ");
            OLED_ShowFloatNum(4, 7, Get_Real_Current(), 2, 2);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  UI_Task — 主调度器
 * ═══════════════════════════════════════════════════════════════ */

void UI_Task(void)
{
    static uint32_t last_oled     = 0;
    static uint8_t  last_ss_state = 0xFF;
    uint8_t         need_refresh  = 0;

    if (SysTimer_GetTick() - last_oled >= 200) {
        last_oled = SysTimer_GetTick();
        need_refresh = 1;
    }

    SoftStart_State_t ss = Inverter_SoftStart_GetState();
    uint8_t key0 = KEY_Get_Event(0);
    uint8_t key1 = KEY_Get_Event(1);

    /* 状态迁移 → 全屏清零 */
    if (ss != last_ss_state) {
        last_ss_state = ss;
        OLED_Clear();
        need_refresh = 1;
        last_oled = SysTimer_GetTick();
    }

    /* LED 更新 */
    if (need_refresh) UI_UpdateLEDs(ss);
    LED_Status_Task();

    /* KEY0 双击: 切页 */
    if (key0 == 2) {
        UI_Page = !UI_Page;
        UI_ClearAllLines();
        need_refresh = 1;
        last_oled = SysTimer_GetTick();
    }

    /* 页面路由 */
    if (UI_Page == 0) {
        /* 控制面板 */
        if (!App_Net_IsConnected()) {
            if (key0 == 1) { UI_TryConnectWiFi(); need_refresh = 0; }
            else if (need_refresh) {
                OLED_ShowString(1, 1, "[Control Mode] ");
                OLED_ShowString(2, 1, "WiFi: DISCONN  ");
                OLED_ShowString(3, 1, "Press KEY0 WiFi");
                OLED_ShowString(4, 1, "F:  --.- kHz    ");
            }
        } else {
            UI_HandleKeys(key0, key1, ss);
            if (need_refresh) UI_DrawPage0(ss);
        }
    } else {
        /* 锁屏监控 */
        if (need_refresh) UI_DrawPage1(ss);
    }
}

void UI_SetBridgeState(uint8_t on_off)
{
    (void)on_off;  /* V3.1: 由 Inverter_SoftStart_GetState 推导 */
}

uint8_t UI_GetBridgeState(void)
{
    SoftStart_State_t s = Inverter_SoftStart_GetState();
    return (s == SS_SWEEP || s == SS_DONE);
}

void UI_SetWiFiConnected(uint8_t on_off)
{
    (void)on_off;  /* V3.1: 由 App_Net_IsConnected() 权威持有 */
}
