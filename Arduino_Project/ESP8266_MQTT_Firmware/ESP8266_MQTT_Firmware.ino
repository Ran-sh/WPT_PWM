/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V4.0: 双脑架构 — ESP8266 独立负责 WiFi + MQTT 连云 (OneNET)
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":xx,"I":xx,"F":xx}\n  → OneNET 物模型上报
 *            OneNET → ESP8266 → STM32:  CMD:ON\n 或 CMD:OFF\n  或  CMD:SETFREQ:100000\n
 *
 *          依赖: ESP8266WiFi.h + PubSubClient.h + ArduinoJson.h (v7) + WiFiManager.h (tzapu)
 *          烧录: Arduino IDE → 选择 "Generic ESP8266 Module" → 115200 上传
 *
 *          ⚠️ 请在 Arduino IDE 库管理器中安装:
 *             - ArduinoJson (by Benoit Blanchon) — v7
 *             - PubSubClient (by Nick O'Leary)
 *             - WiFiManager (by tzapu) — Web 网页配网
 ******************************************************************************
 */

// #define DEBUG  /* Uncomment to enable debug serial output */

#define SERIAL_LINE_MAX       128
#define RECONNECT_INTERVAL_MS 5000
#define WIFI_AP_NAME          "STM32_WPT_Config"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

/* ═══════════════════════════════════════════════════════════════
 *          WiFi 配网: 首次上电或找不到已存网络时,
 *          ESP8266 会开启 "STM32_WPT_Config" 无密码热点,
 *          手机连接后浏览器自动弹出配网页, 选择路由器并输入密码即可.
 *          WiFiManager 会将凭据存入闪存, 后续自动连接.
 * ═══════════════════════════════════════════════════════════════ */

/* OneNET MQTT 服务器 */
#define MQTT_SERVER     "mqtts.heclouds.com"
#define MQTT_PORT       1883

/* OneNET 设备凭证 */
#define ONENET_PRODUCT_ID   "1iS397oJFL"
#define ONENET_DEVICE_NAME  "20260001"
#define ONENET_TOKEN        "version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D"

/* OneNET 物模型主题 */
#define MQTT_TOPIC_PROPERTY_POST      "$sys/1iS397oJFL/20260001/thing/property/post"
#define MQTT_TOPIC_PROPERTY_SET       "$sys/1iS397oJFL/20260001/thing/property/set"
#define MQTT_TOPIC_PROPERTY_SET_REPLY "$sys/1iS397oJFL/20260001/thing/property/set_reply"

/* 公共 Broker (Web 端数据读取, 支持 WebSocket) */
#define PUBLIC_MQTT_SERVER  "broker.emqx.io"
#define PUBLIC_MQTT_PORT    1883
#define PUBLIC_TOPIC_DATA   "wpt/20260001/data"

/* ═══════════════════════════════════════════════════════════════
 *                    全局对象
 * ═══════════════════════════════════════════════════════════════ */

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

WiFiClient    publicEspClient;
PubSubClient  publicMqttClient(publicEspClient);

static char     serialBuf[128];          /* 串口行缓冲 */
static uint8_t  serialLen = 0;           /* 缓冲有效字节数 */
static unsigned long   lastReconnectAttempt = 0;  /* 重连间隔控制 */
static uint32_t s_lastSetFreq = 100000;  /* 上次指令目标频率，遥测不覆盖 */
static uint8_t  s_skipSwitch  = 0;      /* 收到ON/OFF后跳过1次Switch遥测, 防覆盖 */

/* ═══════════════════════════════════════════════════════════════
 *                    MQTT 回调
 * ═══════════════════════════════════════════════════════════════ */

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    /*
     * 解析 OneNET 属性设置下发 JSON, 支持两种格式:
     *   格式 A (布尔值): {"id":"123","version":"1.0","params":{"Switch":true}}
     *   格式 B (对象值): {"id":"123","version":"1.0","params":{"Switch":{"value":1}}}
     *   SetFreq (整数): {"params":{"SetFreq":{"value":100000}}}  — 95000~150000 Hz
     */
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, (const char*)payload, length);

    int8_t    cmd      = 0;
    uint32_t  freqVal  = 0;

    if (!err)
    {
        JsonObject params = doc["params"];

        /* ── Switch ── */
        if (params.containsKey("Switch"))
        {
            JsonVariant sw = params["Switch"];
            int val = 0;

            if (sw.is<bool>())
                val = sw.as<bool>() ? 1 : 0;
            else if (sw.containsKey("value"))
                val = (sw["value"].as<int>() != 0) ? 1 : 0;
            else
                val = sw.as<int>() ? 1 : 0;

            cmd = val ? 1 : -1;
        }

        /* ── SetFreq (直接设置频率) ── */
        if (params.containsKey("SetFreq"))
        {
            JsonVariant sf = params["SetFreq"];
            int val = 0;

            if (sf.is<int>())
                val = sf.as<int>();
            else if (sf.containsKey("value"))
                val = sf["value"].as<int>();

            if (val >= 95000 && val <= 150000)
            {
                freqVal = (uint32_t)((val / 1000) * 1000);
                s_lastSetFreq = freqVal;  /* 记录目标频率 */
                cmd = 3;
            }
        }
    }
    else
    {
        /* JSON 解析失败, 简单字符串匹配兜底 */
        char msg[64];
        unsigned int len = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
        memcpy(msg, payload, len);
        msg[len] = '\0';

        if      (strstr(msg, "CMD:ON")     || strstr(msg, "\"Switch\":true"))   cmd =  1;
        else if (strstr(msg, "CMD:OFF")    || strstr(msg, "\"Switch\":false"))  cmd = -1;
        else return;
    }

    /* 转发指令给 STM32 */
    switch (cmd) {
        case  1: Serial.print("CMD:ON\n");  s_skipSwitch = 1; break;
        case -1: Serial.print("CMD:OFF\n"); s_skipSwitch = 1; break;
        case  3: {
            char buf[32];
            snprintf(buf, sizeof(buf), "CMD:SETFREQ:%lu\n", freqVal);
            Serial.print(buf);
            break;
        }
        default: break;
    }

    /* 回复 OneNET 平台 (解决"响应超时") */
    {
        const char* reqId = doc["id"];
        StaticJsonDocument<128> reply;
        if (reqId) reply["id"] = reqId;
        reply["code"] = 200;
        reply["msg"]  = "success";

        char replyBuf[128];
        serializeJson(reply, replyBuf, sizeof(replyBuf));
        mqttClient.publish(MQTT_TOPIC_PROPERTY_SET_REPLY, replyBuf);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *              非阻塞 WiFi / MQTT 重连
 * ═══════════════════════════════════════════════════════════════ */

static boolean mqttReconnect()
{
    if (mqttClient.connect(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN))
    {
        mqttClient.subscribe(MQTT_TOPIC_PROPERTY_SET);
        return true;
    }
    return false;
}

static void ensureConnected()
{
    unsigned long now = millis();
    static uint8_t wasOnline = 0;  /* 上次 loop 是否在线, 用于边沿检测 */

    /* 每 5 秒尝试一次, 避免频繁重连阻塞 loop */
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL_MS) return;
    lastReconnectAttempt = now;

    /* WiFi 断开 → 重连 (ESP8266 已存 WiFiManager 配网凭据, 无参 begin 即可) */
    if (WiFi.status() != WL_CONNECTED)
    {
        wasOnline = 0;
#ifdef DEBUG
        Serial.println("[WiFi] Disconnected or Connecting...");
#endif
        WiFi.begin();
        return;
    }

    /* MQTT 断开 → 重连 */
    if (!mqttClient.connected())
    {
        wasOnline = 0;
#ifdef DEBUG
        Serial.println("[MQTT] WiFi OK! Now Connecting to OneNET...");
#endif
        if (mqttReconnect())
        {
#ifdef DEBUG
            Serial.println("[MQTT] >>> OneNET Connected successfully! <<<");
#endif
        }
        else
        {
#ifdef DEBUG
            Serial.print("[MQTT] Connect failed, Error Code (rc) = ");
            Serial.println(mqttClient.state());
#endif
        }
    }

    /* 公共 Broker 重连: 连接成功后设置回调并订阅指令主题 */
    if (!publicMqttClient.connected())
    {
        wasOnline = 0;
        if (publicMqttClient.connect(ONENET_DEVICE_NAME))
        {
#ifdef DEBUG
            Serial.println("[Public] Connected to EMQX broker");
#endif
            publicMqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
#ifdef DEBUG
                Serial.print("[Public] <<< CMD: ");
                Serial.write(payload, length);
                Serial.println();
#endif
                Serial.write(payload, length);
                Serial.print("\n");
            });
            publicMqttClient.subscribe("wpt/20260001/cmd");
#ifdef DEBUG
            Serial.println("[Public] Subscribed to wpt/20260001/cmd");
#endif
        }
    }

    /* 双 MQTT 均在线 → 通知 STM32 (仅上升沿发送一次) */
    if (mqttClient.connected() && publicMqttClient.connected())
    {
        if (!wasOnline)
        {
            Serial.print("STATUS:ONLINE\n");
#ifdef DEBUG
            Serial.println("[Status] >>> Sent STATUS:ONLINE to STM32 <<<");
#endif
            wasOnline = 1;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *              串口 → MQTT 数据转发
 * ═══════════════════════════════════════════════════════════════ */

static void processSerialLine(const char* line)
{
    /*
     * 解析 STM32 发来的 JSON:
     *   {"V":12.50,"I":1.23,"F":100000}
     */
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) return;  /* 非 JSON 或格式错误, 静默丢弃 */

    float v = doc["V"];
    float i = doc["I"];
    unsigned long f = doc["F"];
    int    s = doc["S"] | 0;    /* 软启动状态: 0=IDLE 1=SWEEP 2=DONE 3=FAULT */
    bool   running = (s == 1 || s == 2);

    StaticJsonDocument<256> txDoc;
    txDoc["id"]      = "123";
    txDoc["version"] = "1.0";
    txDoc["params"]["V"]["value"] = v;
    txDoc["params"]["I"]["value"] = i;
    txDoc["params"]["F"]["value"] = f;
    /* Switch: 收到云端命令后跳过1次遥测, 等STM32更新状态再上报 */
    if (s_skipSwitch) {
        s_skipSwitch = 0;
    } else {
        txDoc["params"]["Switch"]["value"] = running;
    }
    txDoc["params"]["SetFreq"]["value"] = s_lastSetFreq;  /* 仅上报指令值，不覆盖 */

    char txBuf[256];
    serializeJson(txDoc, txBuf, sizeof(txBuf));

    mqttClient.publish(MQTT_TOPIC_PROPERTY_POST, txBuf);

    /* 同步发布到公共 Broker (原样 JSON, 方便 Web 端解析) */
    if (publicMqttClient.connected())
    {
        publicMqttClient.publish(PUBLIC_TOPIC_DATA, line);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *                    setup / loop
 * ═══════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(115200);
    delay(500);  /* 等待串口稳定 */

#ifdef DEBUG
    Serial.println();
    Serial.println("[System] ESP8266 Booting...");
    Serial.print("[System] Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("[System] WiFi mode: ");
    WiFi.mode(WIFI_STA);
    delay(100);
#endif

    /*
     * WiFiManager 网页配网:
     *   - 有已存凭据 → 直接连接
     *   - 无已存凭据或连接失败 → 开启 "STM32_WPT_Config" AP (无密码)
     *     手机连上此热点, 浏览器会自动弹出配网页
     */
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(true);           /* 打开 WiFiManager 调试 */
    wifiManager.setConfigPortalTimeout(180);    /* AP 模式 3 分钟后自动退出 */

#ifdef DEBUG
    Serial.println("[WiFi] Starting WiFiManager autoConnect...");
#endif

    if (!wifiManager.autoConnect(WIFI_AP_NAME))
    {
#ifdef DEBUG
        Serial.println("[WiFi] No saved WiFi — starting Config Portal...");
        Serial.print("[WiFi] Connect to AP: ");
        Serial.println(WIFI_AP_NAME);
#endif
        wifiManager.startConfigPortal(WIFI_AP_NAME);
    }

#ifdef DEBUG
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Free heap: ");
    Serial.println(ESP.getFreeHeap());
#endif

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    publicMqttClient.setServer(PUBLIC_MQTT_SERVER, PUBLIC_MQTT_PORT);
    /* 订阅和 callback 在 ensureConnected() 连接成功后执行 */
}

void loop()
{
    /* 1. 非阻塞 WiFi / MQTT 重连维护 */
    ensureConnected();

    /* 2. OneNET MQTT 心跳 + 收包 */
    mqttClient.loop();

    /* 3. 公共 Broker 心跳 (只发不收) */
    publicMqttClient.loop();

    /* 4. 串口 → MQTT: 非阻塞读取, 以 \n 为帧结束符 */
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();
        if (c == '\n')
        {
            if (serialLen > 0)
            {
                serialBuf[serialLen] = '\0';

                /* CMD:CLEAR — 清除配网凭据并重启 */
                if (strstr(serialBuf, "CMD:CLEAR"))
                {
#ifdef DEBUG
                    Serial.println("[System] CMD:CLEAR received — resetting WiFi settings...");
#endif
                    WiFiManager wifiManager;
                    wifiManager.resetSettings();
                    delay(200);
                    ESP.restart();
                }

                processSerialLine(serialBuf);
                serialLen = 0;
            }
        }
        else if (c == '\r')
        {
            /* 忽略 */
        }
        else if (serialLen < sizeof(serialBuf) - 1)
        {
            serialBuf[serialLen++] = c;
        }
    }
}
