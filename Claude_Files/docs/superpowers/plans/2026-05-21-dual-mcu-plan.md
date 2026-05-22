# Dual-MCU Architecture Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor STM32 firmware to pure JSON serial passthrough (removing all AT command handling), and create ESP8266 Arduino MQTT firmware.

**Architecture:** STM32 handles PWM/ADC/UI only; ESP8266 runs independent Arduino firmware for WiFi+MQTT. Communication: USART2 115200bps, pure text JSON — no AT commands.

**Tech Stack:** STM32 SPL V3.5.0 (Keil MDK), ESP8266 Arduino (ESP8266WiFi + PubSubClient)

---

### Task 1: Simplify ESP8266_Init() — Remove AT+RST and "+++"

**Files:**
- Modify: `PWM/Hardware/ESP8266.c:120-156`

- [ ] **Step 1: Delete the "+++ exit transparent mode" + AT+RST block**

Remove lines 126-156 (`/* ── 8. 透传退出 + AT+RST 双软件复位 ── */` through the end of the function), keeping only the `ESP8266_ClearRxBuffer()` call before that block. The function now ends after `ESP8266_ClearRxBuffer()` on line 123.

**Before (lines 120-156):**
```c
    /* ── 7. 使能 USART2 ── */
    USART_Cmd(USART2, ENABLE);

    ESP8266_ClearRxBuffer();

    /*
     * ── 8. 透传退出 + AT+RST 双软件复位 ──
     * CH_PD 硬件复位后, 部分模块仍可能卡在透传模式 (ESP8266
     * 通过 IO 引脚漏电维持部分电路)。两步软件复位确保干净起点:
     *   8a. 发送 "+++" 退出透传 (须前后各 1s 静默, 不加 \r\n)
     *   8b. 发送 AT+RST 软复位固件, 等待 "ready"
     */
    SysTimer_DelayMs(1000);                  /* +++ 前静默 1s */
    ESP8266_SendString("+++");              /* 退出透传, 不带 \r\n */
    SysTimer_DelayMs(1000);                  /* +++ 后静默 1s */
    ESP8266_ClearRxBuffer();

    ESP8266_SendString("AT+RST\r\n");
    {
        uint32_t rst_elapsed = 0;
        uint8_t  rst_ok = 0;
        while (rst_elapsed < 5000)   /* 软件复位最多等 5s */
        {
            if (s_FrameReady)
            {
                USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
                s_FrameReady = 0;
                rst_ok = (strstr(s_RxBuf, "ready") != NULL);
                USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
                if (rst_ok) break;
            }
            SysTimer_DelayMs(10);
            rst_elapsed += 10;
        }
    }
    ESP8266_ClearRxBuffer();  /* 清除复位过程中的垃圾数据 */
}
```

**After:**
```c
    /* ── 7. 使能 USART2 ── */
    USART_Cmd(USART2, ENABLE);

    ESP8266_ClearRxBuffer();
}
```

Actual edit in ESP8266.c:

- [ ] **Step 2: Commit**

```bash
git add PWM/Hardware/ESP8266.c
git commit -m "refactor: remove AT+RST and +++ from ESP8266_Init for Dual-MCU arch"
```

---

### Task 2: Rewrite App_Net.h — Remove Non-Blocking APIs

**Files:**
- Modify: `PWM/User/App_Net.h`

- [ ] **Step 1: Replace entire file content**

```c
/**
 ******************************************************************************
 * @file    User/App_Net.h
 * @brief   双脑架构网络应用层 —— 公开接口
 * @note    V4.0: Dual-MCU — STM32 只做 JSON 串口透传, ESP8266 Arduino 固件负责连云
 *          通信协议: STM32→ESP8266 发送 JSON, ESP8266→STM32 发送 CMD:ON/OFF
 ******************************************************************************
 */

#ifndef __APP_NET_H
#define __APP_NET_H

#include "stm32f10x.h"

/* ═══════════════════════════════════════════════════════════════
 *          配置宏 (WiFi 信息已移至 ESP8266 Arduino 固件,
 *          此处保留服务器参数仅用于文档参考)
 * ═══════════════════════════════════════════════════════════════ */

uint8_t    App_Net_Init(void);            /* 初始化串口 + 标记就绪, 始终返回 0 */
void       App_Net_Task(void);            /* JSON 遥测发送 + CMD:ON/OFF 接收 */
uint8_t    App_Net_IsConnected(void);     /* 网络已就绪? (硬件已初始化) */

#endif
```

- [ ] **Step 2: Commit**

```bash
git add PWM/User/App_Net.h
git commit -m "refactor: simplify App_Net.h for Dual-MCU — remove non-blocking APIs"
```

---

### Task 3: Rewrite App_Net.c — Pure JSON Passthrough

**Files:**
- Modify: `PWM/User/App_Net.c`

- [ ] **Step 1: Replace entire file content**

```c
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
                UI_SetBridgeState(1);
            }
            OLED_ShowString(4, 1, "CMD: Remote ON  ");
        }
        else if (strstr(localBuf, "CMD:OFF"))
        {
            Inverter_SoftStart_Stop();
            UI_SetBridgeState(0);
            OLED_ShowString(4, 1, "CMD: Remote OFF ");
        }
    }
}

uint8_t App_Net_IsConnected(void)
{
    return s_WiFiConnected;
}
```

- [ ] **Step 2: Commit**

```bash
git add PWM/User/App_Net.c
git commit -m "refactor: rewrite App_Net.c for Dual-MCU — pure JSON passthrough, zero AT"
```

---

### Task 4: Clean up main.c — Remove App_Net_Connect_Task() Call

**Files:**
- Modify: `PWM/User/main.c:61`

- [ ] **Step 1: Delete line 61**

Remove:
```c
        App_Net_Connect_Task();      /* 非阻塞联网步进 */
```

The while loop becomes:
```c
    while (1)
    {
        KEY_Task();
        ADC_Filter_Task();
        UI_Task();
        App_Net_Task();
        Inverter_SoftStart_Task();
        LED_Task();
    }
```

- [ ] **Step 2: Commit**

```bash
git add PWM/User/main.c
git commit -m "refactor: remove App_Net_Connect_Task call from main loop"
```

---

### Task 5: Simplify UI.c — Remove Non-Blocking Networking References

**Files:**
- Modify: `PWM/Hardware/UI.c`

- [ ] **Step 1: Simplify UI_UpdateLEDs — remove NetState_t check**

Replace lines 33-41 (the `UI_UpdateLEDs` function body):

**Before:**
```c
    /* PB3 WiFi: 联网中保持快闪, 不被 UpdateLEDs 覆盖 */
    {
        NetState_t ns = App_Net_GetConnectState();
        if (ns > NET_IDLE && ns < NET_SUCCESS)
            LED_Update_WiFi(LED_FAST);       /* 联网进行中: 快闪 */
        else
            LED_Update_WiFi(App_Net_IsConnected() ? LED_OFF : LED_SLOW);
    }
```

**After:**
```c
    /* PB3 WiFi: 由 App_Net_IsConnected() 权威持有 */
    LED_Update_WiFi(App_Net_IsConnected() ? LED_SOLID : LED_SLOW);
```

- [ ] **Step 2: Rewrite UI_TryConnectWiFi — call App_Net_Init directly**

Replace lines 59-68:

**Before:**
```c
static uint8_t UI_TryConnectWiFi(void)
{
    LED_Update_WiFi(LED_FAST);
    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi Connecting ");
    App_Net_Connect_Trigger();
    return 1;
}
```

**After:**
```c
static uint8_t UI_TryConnectWiFi(void)
{
    LED_Update_WiFi(LED_FAST);
    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "HW Init...     ");
    App_Net_Init();                        /* 阻塞 ~3s: CH_PD 复位 + USART2 初始化 */
    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi: READY    ");
    return 1;
}
```

- [ ] **Step 3: Simplify UI_Task page 0 "not connected" branch**

Replace lines 242-271:

**Before:**
```c
        if (!App_Net_IsConnected()) {
            NetState_t ns = App_Net_GetConnectState();

            if (ns > NET_IDLE && ns < NET_SUCCESS) {
                /* ── 联网进行中 ── */
                if (key1 == 1) App_Net_Connect_Cancel();
                if (need_refresh) {
                    OLED_ShowString(1, 1, "[Control Mode] ");
                    OLED_ShowString(2, 1, "WiFi Connecting ");
                    OLED_ShowString(4, 1, "KEY1: Cancel    ");
                }
            } else if (ns == NET_FAIL) {
                /* NET_FAIL: UI 负责显示错误码, 3s 后自动恢复 */
                if (need_refresh) {
                    uint8_t err = App_Net_GetErrorCode();
                    OLED_ShowString(1, 1, "!!! WiFi Error !!!");
                    OLED_ShowString(2, 1, "Err Code:       ");
                    OLED_ShowNum(2, 11, err, 1);
                    OLED_ShowString(3, 1, "Retry in 3s...  ");
                }
            } else {
                /* NET_IDLE: 等待触发 */
                if (key0 == 1) UI_TryConnectWiFi();
                else if (need_refresh) {
                    OLED_ShowString(1, 1, "[Control Mode] ");
                    OLED_ShowString(2, 1, "WiFi: DISCONN  ");
                    OLED_ShowString(3, 1, "Press KEY0 WiFi");
                    OLED_ShowString(4, 1, "F:  --.- kHz    ");
                }
            }
        } else {
```

**After:**
```c
        if (!App_Net_IsConnected()) {
            /* 硬件未初始化: 等待 KEY0 触发 */
            if (key0 == 1) UI_TryConnectWiFi();
            else if (need_refresh) {
                OLED_ShowString(1, 1, "[Control Mode] ");
                OLED_ShowString(2, 1, "WiFi: DISCONN  ");
                OLED_ShowString(3, 1, "Press KEY0 WiFi");
                OLED_ShowString(4, 1, "F:  --.- kHz    ");
            }
        } else {
```

- [ ] **Step 4: Commit**

```bash
git add PWM/Hardware/UI.c
git commit -m "refactor: simplify UI.c for Dual-MCU — remove non-blocking networking logic"
```

---

### Task 6: Create ESP8266 Arduino MQTT Firmware

**Files:**
- Create: `ArduinoProject/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`

- [ ] **Step 1: Create directory and file**

```bash
mkdir -p ArduinoProject/ESP8266_MQTT_Firmware
```

- [ ] **Step 2: Write the firmware**

```cpp
/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V4.0: 双脑架构 — ESP8266 独立负责 WiFi + MQTT 连云
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":xx,"I":xx,"F":xx}\n  → MQTT publish
 *            MQTT → ESP8266 → STM32:  CMD:ON\n 或 CMD:OFF\n
 *
 *          依赖: ESP8266WiFi.h + PubSubClient.h
 *          烧录: Arduino IDE → 选择 "Generic ESP8266 Module" → 115200
 ******************************************************************************
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* ═══════════════════════════════════════════════════════════════
 *          用户配置宏 (须根据实际环境修改)
 * ═══════════════════════════════════════════════════════════════ */

#define WIFI_SSID       "Rss"
#define WIFI_PASSWORD   "123456789"

/* OneNET MQTT 服务器 (mqtts.heclouds.com 需 SSL, 此处用非加密端口) */
#define MQTT_SERVER     "mqtt.heclouds.com"
#define MQTT_PORT       6002

#define MQTT_CLIENT_ID  "WPT_PWM_001"
#define MQTT_USERNAME   "your_onenet_product_id"
#define MQTT_PASSWORD   "your_onenet_access_key"

/* OneNET 主题: $dp = 数据上报, CMD = 指令下发 */
#define MQTT_TOPIC_PUB  "$dp"
#define MQTT_TOPIC_SUB  "CMD"

/* ═══════════════════════════════════════════════════════════════
 *                    全局对象
 * ═══════════════════════════════════════════════════════════════ */

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

/* 串口接收行缓冲 */
static String serialLine = "";

/* ═══════════════════════════════════════════════════════════════
 *                    MQTT 回调
 * ═══════════════════════════════════════════════════════════════ */

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    /* 将 payload 转为字符串 */
    char msg[64];
    unsigned int len = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
    memcpy(msg, payload, len);
    msg[len] = '\0';

    /* 匹配开/关指令 → 透传给 STM32 */
    if (strstr(msg, "ON") || strstr(msg, "on") || strstr(msg, "1"))
    {
        Serial.print("CMD:ON\n");
    }
    else if (strstr(msg, "OFF") || strstr(msg, "off") || strstr(msg, "0"))
    {
        Serial.print("CMD:OFF\n");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *                    WiFi 连接
 * ═══════════════════════════════════════════════════════════════ */

void connectWiFi()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *                    MQTT 连接
 * ═══════════════════════════════════════════════════════════════ */

void connectMQTT()
{
    while (!mqttClient.connected())
    {
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD))
        {
            mqttClient.subscribe(MQTT_TOPIC_SUB);
        }
        else
        {
            delay(5000);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *                    setup / loop
 * ═══════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(115200);

    connectWiFi();

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    connectMQTT();
}

void loop()
{
    /* 1. MQTT 心跳维持 */
    if (!mqttClient.connected())
    {
        connectMQTT();
    }
    mqttClient.loop();

    /* 2. 串口 → MQTT: 读到 \n 结尾的行即转发 */
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();
        if (c == '\n')
        {
            if (serialLine.length() > 0)
            {
                mqttClient.publish(MQTT_TOPIC_PUB, serialLine.c_str());
                serialLine = "";
            }
        }
        else if (c == '\r')
        {
            /* 忽略 \r */
        }
        else
        {
            serialLine += c;
            /* 防止行过长 */
            if (serialLine.length() >= 128)
            {
                serialLine = "";
            }
        }
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add ArduinoProject/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino
git commit -m "feat: add ESP8266 Arduino MQTT firmware for Dual-MCU architecture"
```

---

### Task 7: Final Integration Verification

- [ ] **Step 1: Review all changes**

```bash
git diff HEAD~6..HEAD --stat
```

- [ ] **Step 2: Verify no broken references**

```bash
# Check no remaining references to deleted symbols
grep -r "App_Net_Connect_Trigger\|App_Net_Connect_Task\|App_Net_Connect_Cancel\|App_Net_GetConnectState\|App_Net_GetErrorCode\|NetState_t\|NET_STEP_AT\|NET_FAIL\|NET_SUCCESS\|NET_IDLE" PWM/ --include="*.c" --include="*.h"
```

Expected: no matches (all clean).

- [ ] **Step 3: Commit verification result**

No code changes — just confirming everything is clean.
