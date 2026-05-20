/**
 ******************************************************************************
 * @file    Hardware/UI.c
 * @brief   人机交互界面 —— OLED 显示 + 按键事件分发 + LED 状态驱动
 * @note    存放路径: 项目根目录\Hardware\
 *
 *          双页面架构:
 *            页面 0: 控制面板 (电压/电流/频率/软启动状态, 按键操作)
 *            页面 1: 锁屏监控 (只读, 屏蔽按键)
 *
 *          按键 (V3.1):
 *            KEY0 单击 : 触发联网 (未联网时) / 触发软启动 (SS_IDLE 时) / 关断 (SS_DONE)
 *            KEY0 双击 : 切页 (控制面板 ↔ 锁屏监控)
 *            KEY1 单击 : 关断 (SS_SWEEP) / 微调频率+1kHz (SS_DONE, 循环100k~150k)
 *
 *          LED 状态 (每周期刷新, 不受页面影响):
 *            PB3 WiFi: 待联网慢闪 / 联网中常亮 / 成功常亮2s→灭
 *            PB4 PWM:  扫频/运行慢闪 / 关断灭
 *            PB5 Ready: IDLE/DONE 亮 / 忙灭
 *
 *          每次切页调用 OLED_Clear() 防残影
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

static uint8_t  UI_Page            = 0;  /* 0: 控制面板, 1: 锁屏监控 */
static uint8_t  wifi_connected     = 0;  /* 0: 未联网, 1: 已联网 */

static void UI_ClearAllLines(void)
{
    OLED_ShowString(1, 1, "                ");
    OLED_ShowString(2, 1, "                ");
    OLED_ShowString(3, 1, "                ");
    OLED_ShowString(4, 1, "                ");
}

void UI_Task(void)
{
    static uint32_t   last_oled       = 0;
    static uint8_t    last_ss_state   = 0xFF;
    uint8_t           need_refresh    = 0;

    if (SysTimer_GetTick() - last_oled >= 200) {
        last_oled = SysTimer_GetTick();
        need_refresh = 1;
    }

    SoftStart_State_t ss = Inverter_SoftStart_GetState();
    uint8_t key0_event = KEY_Get_Event(0);  /* KEY0 (PB12) */
    uint8_t key1_event = KEY_Get_Event(1);  /* KEY1 (PB13) */

    /*
     * 状态迁移: 全屏清零。状态切换罕见 (IDLE→SWEEP→DONE),
     * OLED_Clear ~100ms 可接受, 保证无残影。日常 200ms 刷新不清屏。
     */
    if (ss != last_ss_state) {
        last_ss_state = ss;
        OLED_Clear();
        need_refresh = 1;
        last_oled = SysTimer_GetTick();
    }

    /*
     * ═══════════════════════════════════════════════════════════
     *  LED 状态更新 — 每 200ms 刷新, 不受页面切换影响
     * ═══════════════════════════════════════════════════════════
     */
    if (need_refresh) {
        /* PB3 WiFi */
        if (!wifi_connected) {
            LED_Update_WiFi(LED_SLOW);           /* 待联网: 慢闪 */
        } else {
            LED_Update_WiFi(LED_OFF);            /* 已联网: 灭 */
        }

        /* PB4 PWM: 扫频时快闪, 运行时慢闪 */
        if (ss == SS_SWEEP) {
            LED_Update_PWM(LED_FAST);            /* 扫频中: 快闪 5Hz */
        } else if (ss == SS_DONE) {
            LED_Update_PWM(LED_SLOW);            /* 运行中: 慢闪 1Hz */
        } else {
            LED_Update_PWM(LED_OFF);
        }

        /* PB5 Ready: 控制面板 + IDLE/DONE + 已联网 → 可操作 */
        LED_Update_Ready((UI_Page == 0) && wifi_connected
            && (ss == SS_IDLE || ss == SS_DONE));
    }
    /* LED_Status_Task 每周期调用, 驱动闪烁时间戳 */
    LED_Status_Task();

    /* ── KEY0 双击: 全局切页 ── */
    if (key0_event == 2) {
        UI_Page = !UI_Page;
        UI_ClearAllLines();
        need_refresh = 1;
        last_oled = SysTimer_GetTick();
    }

    /* ── 页面路由 ── */
    switch (UI_Page)
    {
        case 0: /* 控制面板 */
            /* ── 未联网: KEY0 触发联网 (阻塞) ── */
            if (!wifi_connected) {
                if (key0_event == 1) {
                    uint8_t ret;
                    /* 行覆盖替代 OLED_Clear, 避免软件 I2C 阻塞 ~100ms */
                    OLED_ShowString(1, 1, "[Control Mode] ");
                    OLED_ShowString(2, 1, "WiFi Connecting ");
                    OLED_ShowString(3, 1, "Please wait...  ");
                    OLED_ShowString(4, 1, "                ");

                    /*
                     * 联网前: 关逆变器 → LED 闪 + 点跳动循环 → 常亮 → 阻塞联网
                     * 点循环: 1→2→3→4→5→1→2→... 每 150ms 一跳
                     */
                    Inverter_SoftStart_Stop();
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
                    if (ret == 0) {
                        wifi_connected = 1;
                        LED_Update_WiFi(LED_SOLID);     /* PB3 常亮: 联网成功指示 */
                    } else {
                        LED_Update_WiFi(LED_OFF);        /* 失败立即灭 PB3 */
                    }
                    UI_ClearAllLines();
                    need_refresh = 0;
                    break;
                }
                if (need_refresh) {
                    OLED_ShowString(1, 1, "[Control Mode] ");
                    OLED_ShowString(2, 1, "WiFi: DISCONN  ");
                    OLED_ShowString(3, 1, "Press KEY0 WiFi");
                    OLED_ShowString(4, 1, "F:  --.- kHz    ");
                }
                break;
            }

            /* ── 已联网: 按键事件分发 (按状态路由) ── */
            if (key0_event == 1) {
                if (ss == SS_IDLE) {
                    Inverter_SoftStart_Trigger();
                } else if (ss == SS_DONE) {
                    Inverter_SoftStart_Stop();
                }
            }
            if (key1_event == 1) {
                if (ss == SS_SWEEP) {
                    Inverter_SoftStart_Stop();
                } else if (ss == SS_DONE) {
                    uint32_t new_f = PWM_GetFrequency() + 1000;
                    if (new_f > 150000) new_f = 100000;
                    PWM_SetFrequency(new_f);
                }
            }

            if (need_refresh) {
                switch (ss) {
                    case SS_IDLE:
                        OLED_ShowString(1, 1, "[Control Mode] ");
                        OLED_ShowString(2, 1, "State: IDLE   ");
                        OLED_ShowString(3, 1, "Press KEY0 start");
                        OLED_ShowString(4, 1, "F:  --.- kHz    ");
                        break;

                    case SS_SWEEP:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        uint32_t progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                                          / (SOFTSTART_START_FREQ_HZ - 100000UL);
                        char bar[17];
                        uint8_t j;

                        OLED_ShowString(1, 1, "[Sweeping...]  ");
                        /* 频率行: snprintf 合并为单次写入, 无残影 */
                        {
                            char fline[17];
                            snprintf(fline, sizeof(fline), "Freq:%3lu.%1lukHz ",
                                     (unsigned long)(f / 1000),
                                     (unsigned long)((f % 1000) / 100));
                            fline[16] = '\0';
                            OLED_ShowString(2, 1, fline);
                        }
                        bar[0] = '[';
                        for (j = 0; j < 10; j++)
                            bar[1 + j] = (j < progress) ? '#' : ' ';
                        bar[11] = ']';
                        for (j = 12; j < 16; j++)
                            bar[j] = ' ';
                        bar[16] = '\0';
                        OLED_ShowString(3, 1, bar);
                        break;
                    }

                    case SS_DONE:
                    {
                        uint32_t f = PWM_GetFrequency();
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
            }
            break;

        case 1: /* 锁屏监控 (只读, 屏蔽按键) */
            if (need_refresh) {
                switch (ss) {
                    case SS_IDLE:
                        OLED_ShowString(1, 1, "- Monitor Only -");
                        OLED_ShowString(2, 1, "State: IDLE    ");
                        OLED_ShowString(3, 1, "Waiting trigger ");
                        break;
                    case SS_SWEEP:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        OLED_ShowString(1, 1, "- Monitor Only -");
                        OLED_ShowString(2, 1, "Sweeping...    ");
                        OLED_ShowString(3, 1, "F:");
                        OLED_ShowNum(3, 3, f / 1000, 3);
                        OLED_ShowString(3, 6, "kHz");
                        break;
                    }
                    case SS_DONE:
                    {
                        uint32_t f = PWM_GetFrequency();
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
            }
            break;
    }
}

void UI_SetBridgeState(uint8_t on_off)
{
    (void)on_off;  /* V3.1: 状态由 Inverter_SoftStart_GetState 推导, 无需手动同步 */
}

uint8_t UI_GetBridgeState(void)
{
    SoftStart_State_t s = Inverter_SoftStart_GetState();
    return (s == SS_SWEEP || s == SS_DONE);
}

void UI_SetWiFiConnected(uint8_t on_off)
{
    wifi_connected = on_off;
}
