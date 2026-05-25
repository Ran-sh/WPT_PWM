/**
 ******************************************************************************
 * @file    User/App_Net.c
 * @brief   双脑架构网络应用层 — 实现
 * @note    V5.0: 上电自动连WiFi + 3次重试 + UI状态门控遥测
 *
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":12.50,"I":1.23,"F":100000,"S":2}\n
 *            ESP8266 → STM32:  CMD:ON / CMD:OFF / CMD:SETFREQ:<Hz>\n
 *            ESP8266 → STM32:  STATUS:ONLINE\n  (WiFi+MQTT连接成功, 上升沿)
 *            STM32 → ESP8266:  CMD:CLEAR\n       (清除WiFi配网凭据)
 *
 *          依赖: Hardware/ESP8266, Hardware/ADC, Hardware/PWM,
 *               Hardware/OLED, System/SysTimer, Hardware/LED, Hardware/UI
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ESP8266.h"
#include "ADC.h"
#include "PWM.h"
#include "OLED.h"
#include "SysTimer.h"
#include "LED.h"
#include "UI.h"
#include "App_Net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CONNECT_TIMEOUT_MS  15000   /* 单次连接超时 15s */
#define MAX_RETRIES          3      /* 最多重试 3 次 */

static uint8_t  s_network_online = 0;
static uint8_t  s_connecting     = 0;
static uint8_t  s_retry_count    = 0;
static uint32_t s_connect_start  = 0;

/* ═══════════════════════════════════════════════════════════════
 *                    公开接口实现
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  启动联网流程 (上电自动调用 / KEY0 手动触发)
 * @note   阻塞 ~3s (CH_PD 硬件复位), 重置重试计数, 开启连接超时窗口
 */
uint8_t App_Net_StartConnect(void)
{
    s_network_online = 0;
    s_connecting     = 1;
    s_retry_count    = 0;
    s_connect_start  = SysTimer_GetTick();
    ESP8266_Init();                        /* CH_PD 硬件复位 + USART2 (~3s) */
    LED_Update_WiFi(LED_SLOW);            /* 慢闪 = 等待 WiFi 连接 */
    return 0;
}

/**
 * @brief  软复位联网状态 (CMD:CLEAR 后调用)
 * @note   不触发 CH_PD 硬件复位 (ESP8266 已自行重启)
 *         重置状态机, 等待 ESP8266 重启后发送 STATUS:ONLINE
 */
uint8_t App_Net_SoftReset(void)
{
    s_network_online = 0;
    s_connecting     = 1;
    s_retry_count    = 0;
    s_connect_start  = SysTimer_GetTick();
    LED_Update_WiFi(LED_SLOW);
    return 0;
}

/**
 * @brief  查询联网状态 (无副作用, 只读)
 * @retval 0=未开始/空闲  1=连接中(含重试)  2=已连接  3=失败(3次耗尽)
 */
uint8_t App_Net_GetConnectStatus(void)
{
    if (!s_connecting) {
        return s_network_online ? 2 : 0;
    }
    if (s_network_online) {
        s_connecting = 0;
        return 2;
    }
    if (s_retry_count >= MAX_RETRIES) {
        s_connecting = 0;
        return 3;
    }
    return 1;
}

uint8_t App_Net_GetRetryCount(void)
{
    return s_retry_count;
}

uint8_t App_Net_IsConnected(void)
{
    return ESP8266_IsReady() && s_network_online;
}

/* ═══════════════════════════════════════════════════════════════
 *              内部: 重试逻辑 (含阻塞复位)
 * ═══════════════════════════════════════════════════════════════ */

static void App_Net_CheckRetry(void)
{
    if (!s_connecting) return;
    if (s_network_online) return;
    if (s_retry_count >= MAX_RETRIES) return;

    if (SysTimer_GetTick() - s_connect_start >= CONNECT_TIMEOUT_MS)
    {
        s_retry_count++;
        if (s_retry_count < MAX_RETRIES)
        {
            s_connect_start = SysTimer_GetTick();
            ESP8266_Init();                /* 硬件复位重试 (~3s 阻塞) */
            LED_Update_WiFi(LED_SLOW);
        }
    }
}

/**
 * @brief  网络应用层周期任务 (非阻塞)
 * @note   连接失败时不发送遥测 (OneNET 设备保持离线)
 *         UI 到达界面3(READY)以上才发送遥测, 保证云平台设备在线=用户可操作
 */
void App_Net_Task(void)
{
    if (!ESP8266_IsReady()) return;

    /* ── 子功能 0: 重试检查 ── */
    App_Net_CheckRetry();

    /* ── 指令接收 (实时轮询, 不依赖 UI 状态) ── */
    if (ESP8266_GetRxFlag())
    {
        char localBuf[64];
        ESP8266_CopyRxFrame(localBuf, sizeof(localBuf));

        if (strstr(localBuf, "STATUS:ONLINE"))
        {
            s_network_online = 1;
            s_connecting     = 0;
            LED_Update_WiFi(LED_SOLID);
        }
        else if (strstr(localBuf, "CMD:ON"))
        {
            if (Inverter_SoftStart_GetState() == SS_IDLE) {
                Inverter_SoftStart_Trigger();
            }
            OLED_ShowString(4, 1, "CMD: Remote ON  ");
        }
        else if (strstr(localBuf, "CMD:OFF"))
        {
            Inverter_SoftStart_Stop();
            OLED_ShowString(4, 1, "CMD: Remote OFF ");
        }
        else if (strstr(localBuf, "CMD:SETFREQ:"))
        {
            if (Inverter_SoftStart_GetState() == SS_DONE)
            {
                long f = atol(localBuf + 12);
                if (f >= 95000 && f <= 150000)
                {
                    Inverter_FreqRamp_Trigger((uint32_t)f);
                    char disp[17];
                    snprintf(disp, sizeof(disp), "CMD: Ramp %lukHz",
                             (unsigned long)(f / 1000));
                    OLED_ShowString(4, 1, disp);
                }
            }
        }
    }

    /* ── 子功能 1: JSON 遥测 (仅 UI>=界面3 时发送) ── */
    {
        static uint32_t last_telemetry = 0;

        if (SysTimer_GetTick() - last_telemetry >= 500)
        {
            last_telemetry = SysTimer_GetTick();

            /* 遥测门控: 仅已联网 + UI 界面3+ 才发送 */
            if (!s_network_online) return;
            if (UI_GetState() < UI_STATE_READY) return;

            SoftStart_State_t ss = Inverter_SoftStart_GetState();
            if (ss != SS_SWEEP)
            {
                char jsonBuf[80];

                if (ss == SS_DONE)
                {
                    snprintf(jsonBuf, sizeof(jsonBuf),
                             "{\"V\":%.2f,\"I\":%.2f,\"F\":%lu,\"S\":%d}\n",
                             Get_Real_Voltage(),
                             Get_Real_Current(),
                             (unsigned long)PWM_GetFrequency(),
                             (int)ss);
                }
                else
                {
                    snprintf(jsonBuf, sizeof(jsonBuf),
                             "{\"V\":0.00,\"I\":0.00,\"F\":%lu,\"S\":%d}\n",
                             (unsigned long)PWM_GetFrequency(),
                             (int)ss);
                }
                ESP8266_SendString(jsonBuf);
            }
        }
    }
}
