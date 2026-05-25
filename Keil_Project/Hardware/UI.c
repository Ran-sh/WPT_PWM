/**
 ******************************************************************************
 * @file    Hardware/UI.c
 * @brief   人机交互界面 — 7 界面状态机 + OLED 显示 + 按键分发 + LED 驱动
 * @note    V4.0: 上电自动连WiFi → 3次重试 → 失败回初始界面
 *          界面1(初始) → 界面2(连接中) → 界面3(已连接) → 界面4(扫频)
 *          → 界面5(运行) 双击切界面6/7(控制面板/监测模式)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "UI.h"
#include "OLED.h"
#include "KEY.h"
#include "PWM.h"
#include "ADC.h"
#include "ESP8266.h"
#include "App_Net.h"
#include "SysTimer.h"
#include "LED.h"
#include <stdio.h>

static uint8_t    UI_Page = 0;        /* 0:控制面板, 1:监测模式 (双击切换) */
static UI_State_t s_ui_state = UI_STATE_INIT;  /* 当前 UI 状态, 供 App_Net 查询 */
static char       s_error_line[17];   /* 界面1 底部错误信息 */
static uint8_t    s_has_error = 0;

static void UI_ClearError(void)
{
    s_has_error = 0;
}

static void UI_SetError(const char* msg)
{
    uint8_t i;
    if (msg == NULL) { UI_ClearError(); return; }
    for (i = 0; i < 16 && msg[i]; i++) s_error_line[i] = msg[i];
    for (; i < 16; i++) s_error_line[i] = ' ';
    s_error_line[16] = '\0';
    s_has_error = 1;
}

/* ── LED 状态更新 ── */
static void UI_UpdateLEDs(UI_State_t ui_state)
{
    uint8_t cs = App_Net_GetConnectStatus();

    /* PB3 WiFi: 等待慢闪 → 连接中快闪 → 成功常亮 */
    if (cs == 2)
        LED_Update_WiFi(LED_SOLID);
    else if (cs == 1)
        LED_Update_WiFi(LED_FAST);
    else
        LED_Update_WiFi(LED_SLOW);

    /* PB4: 系统运行时亮 (SWEEPING/RUNNING), 待机灭 */
    if (ui_state == UI_STATE_SWEEPING || ui_state == UI_STATE_RUNNING)
        LED_Update_PWM(LED_SOLID);
    else
        LED_Update_PWM(LED_OFF);

    /* PB5: KEY1 +1kHz/复位可操作时亮 (RUNNING / FAULT), SWEEPING 只能停不能+1k */
    if (ui_state == UI_STATE_RUNNING || ui_state == UI_STATE_FAULT)
        LED_Update_Ready(1);
    else
        LED_Update_Ready(0);
}

/* ═══════════════════════════════════════════════════════════════
 *  各界面绘制函数
 * ═══════════════════════════════════════════════════════════════ */

/* 界面1: 初始 — 按KEY0连接, 底部无频率, 可选错误信息 */
static void UI_DrawInit(void)
{
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi: DISCONNED");
    OLED_ShowString(3, 1, "Press KEY0 WiFi ");
    if (s_has_error) {
        OLED_ShowString(4, 1, s_error_line);
    } else {
        OLED_ShowString(4, 1, "                ");
    }
}

/* 界面2: 连接中 */
static void UI_DrawConnecting(uint8_t retry, uint8_t max_retry)
{
    char buf[17];
    OLED_ShowString(1, 1, "[Connecting...] ");
    OLED_ShowString(2, 1, "ESP WiFi Init.. ");
    snprintf(buf, sizeof(buf), "Retry: %d/%d      ", retry, max_retry);
    buf[16] = '\0';
    OLED_ShowString(3, 1, buf);
    OLED_ShowString(4, 1, "Please wait...  ");
}

/* 界面3: 已连接 — KEY0 Start (底部无频率) */
static void UI_DrawReady(void)
{
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi: CONNECTED ");
    OLED_ShowString(3, 1, "Press KEY0 Start");
    OLED_ShowString(4, 1, "F:  --.- kHz    ");
}

/* 界面4: 扫频中 */
static void UI_DrawSweeping(void)
{
    uint32_t f, progress;
    char fline[17], bar[17];
    uint8_t j;

    if (UI_Page == 0) {
        f = Inverter_SoftStart_GetCurrentFreq();
        progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                 / (SOFTSTART_START_FREQ_HZ - 100000UL);
        if (progress > 10) progress = 10;

        OLED_ShowString(1, 1, "[Sweeping...]  ");
        snprintf(fline, sizeof(fline), "Freq:%3lu.%1lukHz ",
                 (unsigned long)(f / 1000),
                 (unsigned long)((f % 1000) / 100));
        fline[16] = '\0';
        OLED_ShowString(2, 1, fline);

        bar[0] = '[';
        for (j = 0; j < 10; j++) bar[1 + j] = (j < (int)progress) ? '#' : ' ';
        bar[11] = ']';
        for (j = 12; j < 16; j++) bar[j] = ' ';
        bar[16] = '\0';
        OLED_ShowString(3, 1, bar);
        OLED_ShowString(4, 1, "                ");
    } else {
        f = Inverter_SoftStart_GetCurrentFreq();
        OLED_ShowString(1, 1, "- Monitor Only -");
        OLED_ShowString(2, 1, "Sweeping...    ");
        OLED_ShowString(3, 1, "F:");
        OLED_ShowNum(3, 3, f / 1000, 3);
        OLED_ShowString(3, 6, "kHz            ");
        OLED_ShowString(4, 1, "                ");
    }
}

/* 界面5: 运行中 */
static void UI_DrawRunning(void)
{
    uint32_t f = PWM_GetFrequency();

    if (UI_Page == 0) {
        OLED_ShowString(1, 1, "[Resonant Mode] ");
        OLED_ShowString(2, 1, "F:");
        OLED_ShowNum(2, 3, f / 1000, 3);
        OLED_ShowString(2, 6, "kHz  ");
        OLED_ShowString(3, 1, "V:");
        OLED_ShowFloatNum(3, 3, Get_Real_Voltage(), 2, 1);
        OLED_ShowString(3, 9, "I:");
        OLED_ShowFloatNum(3, 11, Get_Real_Current(), 1, 2);
        OLED_ShowString(4, 1, "K0:Stop K1:+1k ");
    } else {
        OLED_ShowString(1, 1, "- Monitor Only -");
        OLED_ShowString(2, 1, "Freq: ");
        OLED_ShowNum(2, 7, f / 1000, 3);
        OLED_ShowString(2, 10, "kHz");
        OLED_ShowString(3, 1, "Volt: ");
        OLED_ShowFloatNum(3, 7, Get_Real_Voltage(), 2, 2);
        OLED_ShowString(4, 1, "Curr: ");
        OLED_ShowFloatNum(4, 7, Get_Real_Current(), 2, 2);
    }
}

/* 界面5(故障态): 过流保护 */
static void UI_DrawFault(void)
{
    if (UI_Page == 0) {
        OLED_ShowString(1, 1, "!!! FAULT !!!   ");
        OLED_ShowString(2, 1, "Over Current    ");
        OLED_ShowString(3, 1, "PWM Disabled    ");
        OLED_ShowString(4, 1, "K0/K1: Reset    ");
    } else {
        OLED_ShowString(1, 1, "- Monitor Only -");
        OLED_ShowString(2, 1, "!!! FAULT !!!   ");
        OLED_ShowString(3, 1, "Over Current    ");
        OLED_ShowString(4, 1, "Reset: K0/K1    ");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  按键分发 (各界面不同)
 * ═══════════════════════════════════════════════════════════════ */

static void UI_HandleKeys(UI_State_t ui_state, uint8_t key0, uint8_t key1)
{
    /* KEY0 双击: 切换控制面板/监测模式 — 所有界面通用 */
    if (key0 == 2) {
        UI_Page = !UI_Page;
        OLED_Clear();
        UI_SetError(NULL);
        return;
    }

    switch (ui_state) {
        case UI_STATE_INIT:
            /* KEY0 单击: 开始连接WiFi */
            if (key0 == 1) {
                App_Net_StartConnect();
                UI_ClearError();
                OLED_Clear();
            }
            break;

        case UI_STATE_CONNECTING:
            /* 连接中无按键操作 */
            break;

        case UI_STATE_READY:
            if (key0 == 1) {
                Inverter_SoftStart_Trigger();
            }
            if (key1 == 1) {
                /* 界面3 KEY1 无操作 */
            }
            break;

        case UI_STATE_SWEEPING:
            if (key1 == 1) {
                Inverter_SoftStart_Stop();
            }
            if (key0 == 1) {
                Inverter_SoftStart_Stop();
            }
            break;

        case UI_STATE_RUNNING:
            if (key0 == 1) {
                Inverter_SoftStart_Stop();
            }
            if (key1 == 1) {
                uint32_t f = PWM_GetFrequency() + 1000;
                if (f <= 150000) PWM_SetFrequency(f);
            }
            break;

        case UI_STATE_FAULT:
            if (key0 == 1 || key1 == 1) {
                Inverter_SoftStart_Stop();  /* 复位故障 */
            }
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  UI_Task — 主调度器 (每 200ms 执行一次显示刷新)
 * ═══════════════════════════════════════════════════════════════ */

void UI_Task(void)
{
    static uint32_t last_oled = 0;
    static uint8_t last_ui_state = 0xFF;
    static uint8_t last_page = 0xFF;
    uint8_t need_refresh = 0;

    if (SysTimer_GetTick() - last_oled >= 200) {
        last_oled = SysTimer_GetTick();
        need_refresh = 1;
    }

    /* ── 计算当前 UI 状态 ── */
    UI_State_t ui_state;
    SoftStart_State_t ss = Inverter_SoftStart_GetState();

    /* 故障状态优先 */
    if (ss == SS_FAULT) {
        ui_state = UI_STATE_FAULT;
    } else if (!ESP8266_IsReady()) {
        /* ESP8266 硬件未就绪 → 初始界面 */
        ui_state = UI_STATE_INIT;
    } else {
        /* ESP8266 就绪 → 检查联网状态 */
        uint8_t cs = App_Net_GetConnectStatus();

        if (cs == 3) {
            /* 3次重试耗尽 → 初始界面 + 错误 */
            ui_state = UI_STATE_INIT;
            if (need_refresh && !s_has_error) {
                UI_SetError("WiFi Failed x3  ");
            }
        } else if (cs == 2) {
            /* 已联网 → 根据软启动状态分 */
            if (ss == SS_IDLE)
                ui_state = UI_STATE_READY;
            else if (ss == SS_SWEEP)
                ui_state = UI_STATE_SWEEPING;
            else /* SS_DONE */
                ui_state = UI_STATE_RUNNING;
        } else if (cs == 1) {
            /* 连接中 (含重试) */
            ui_state = UI_STATE_CONNECTING;
        } else {
            /* cs==0(未启动) → 初始界面 */
            ui_state = UI_STATE_INIT;
        }
    }

    /* ── 状态迁移 → 全屏清零 ── */
    if (ui_state != last_ui_state || UI_Page != last_page) {
        last_ui_state = (uint8_t)ui_state;
        last_page = UI_Page;
        OLED_Clear();
        need_refresh = 1;
    }

    /* ── 读按键 ── */
    uint8_t key0 = KEY_Get_Event(0);
    uint8_t key1 = KEY_Get_Event(1);

    /* KEY0 长按(>3s): 清除WiFi配网 — 所有界面通用, 早退避免被覆盖 */
    if (key0 == 3) {
        if (ESP8266_IsReady()) {
            ESP8266_SendString("CMD:CLEAR\n");
            App_Net_SoftReset();
            OLED_Clear();
            OLED_ShowString(1, 1, "[Control Mode] ");
            OLED_ShowString(2, 1, "WiFi Cleared... ");
            OLED_ShowString(3, 1, "Please reconfi-");
            OLED_ShowString(4, 1, "gure WiFi      ");
            last_ui_state = (uint8_t)UI_STATE_CONNECTING;
            s_ui_state = UI_STATE_CONNECTING;
            LED_Status_Task();
        }
        return;
    }

    /* ── 按键分发 ── */
    UI_HandleKeys(ui_state, key0, key1);

    /* ── 重新计算状态 (按键可能改变了软启动状态) ── */
    ss = Inverter_SoftStart_GetState();
    if (ss == SS_FAULT) {
        ui_state = UI_STATE_FAULT;
    } else if (ui_state >= UI_STATE_READY) {
        if (ss == SS_IDLE)
            ui_state = UI_STATE_READY;
        else if (ss == SS_SWEEP)
            ui_state = UI_STATE_SWEEPING;
        else if (ss == SS_DONE)
            ui_state = UI_STATE_RUNNING;
    }

    /* ── 再次同步状态迁移 (按键后可能变了) ── */
    if (ui_state != last_ui_state || UI_Page != last_page) {
        last_ui_state = (uint8_t)ui_state;
        last_page = UI_Page;
        OLED_Clear();
        need_refresh = 1;
    }

    /* ── 更新全局 UI 状态 ── */
    s_ui_state = ui_state;

    /* ── LED 更新 ── */
    if (need_refresh) UI_UpdateLEDs(ui_state);
    LED_Status_Task();

    /* ── 绘制 ── */
    if (!need_refresh) return;

    switch (ui_state) {
        case UI_STATE_INIT:
            UI_DrawInit();
            break;
        case UI_STATE_CONNECTING:
            UI_DrawConnecting(App_Net_GetRetryCount() + 1, 3);
            break;
        case UI_STATE_READY:
            UI_DrawReady();
            break;
        case UI_STATE_SWEEPING:
            UI_DrawSweeping();
            break;
        case UI_STATE_RUNNING:
            UI_DrawRunning();
            break;
        case UI_STATE_FAULT:
            UI_DrawFault();
            break;
    }
}

UI_State_t UI_GetState(void)
{
    return s_ui_state;
}

uint8_t UI_GetBridgeState(void)
{
    SoftStart_State_t s = Inverter_SoftStart_GetState();
    return (s == SS_SWEEP || s == SS_DONE);
}
