/**
 ******************************************************************************
 * @file    User/App_Net.c
 * @brief   双脑架构网络应用层 —— 实现
 * @note    V4.0: Dual-MCU — 纯 JSON 串口透传, 零 AT 指令
 *
 *          模块职责:
 *            1. App_Net_Init() — 初始化 ESP8266 硬件 (仅串口 + CH_PD 复位)
 *            2. App_Net_Task() — 非阻塞周期任务
 *               - 每 2000ms: 采集电压/电流/频率 → JSON → USART2 直发
 *               - 实时轮询: strstr CMD:ON / CMD:OFF → 控制逆变器
 *
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":12.50,"I":1.23,"F":100000}\n
 *            ESP8266 → STM32:  CMD:ON\n  或  CMD:OFF\n
 *
 *          依赖: Hardware/ESP8266, Hardware/ADC, Hardware/PWM, Hardware/UI,
 *               Hardware/OLED, System/SysTimer, Hardware/LED
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ESP8266.h"
#include "ADC.h"
#include "PWM.h"
#include "UI.h"
#include "OLED.h"
#include "SysTimer.h"
#include "LED.h"
#include "App_Net.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════
 *                    本地状态
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t s_WiFiConnected = 0;  /* 硬件初始化完成即置 1 */

/* ═══════════════════════════════════════════════════════════════
 *                    公开接口实现
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  网络应用层初始化 (阻塞约 3s, 仅为 CH_PD 硬件复位延时)
 * @retval 始终返回 0
 */
uint8_t App_Net_Init(void)
{
    ESP8266_Init();                        /* CH_PD 硬件复位 + USART2 初始化 (~3s) */
    s_WiFiConnected = 1;
    LED_Update_WiFi(LED_SOLID);            /* 常亮 = 硬件就绪 */
    return 0;
}

/**
 * @brief  网络应用层周期任务 (非阻塞)
 * @note   发送: 每 2000ms JSON 遥测
 *         接收: 实时轮询 CMD:ON / CMD:OFF
 */
void App_Net_Task(void)
{
    if (!s_WiFiConnected) return;

    /* ── 子功能 1: JSON 遥测 (每 2000ms) ── */
    {
        static uint32_t last_telemetry = 0;

        if (SysTimer_GetTick() - last_telemetry >= 2000)
        {
            last_telemetry = SysTimer_GetTick();

            if (Inverter_SoftStart_GetState() != SS_SWEEP)
            {
                char jsonBuf[80];
                snprintf(jsonBuf, sizeof(jsonBuf),
                         "{\"V\":%.2f,\"I\":%.2f,\"F\":%lu}\n",
                         Get_Real_Voltage(),
                         Get_Real_Current(),
                         (unsigned long)PWM_GetFrequency());
                ESP8266_SendString(jsonBuf);
            }
        }
    }

    /* ── 子功能 2: 指令接收 (实时轮询) ── */
    if (ESP8266_GetRxFlag())
    {
        char localBuf[64];

        ESP8266_CopyRxFrame(localBuf, sizeof(localBuf));

        if (strstr(localBuf, "CMD:ON"))
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
    }
}

uint8_t App_Net_IsConnected(void)
{
    return s_WiFiConnected;
}
