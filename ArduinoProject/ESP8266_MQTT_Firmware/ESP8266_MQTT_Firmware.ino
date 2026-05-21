/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V4.0: 双脑架构 — ESP8266 独立负责 WiFi + MQTT 连云 (OneNET)
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":xx,"I":xx,"F":xx}\n  → OneNET 物模型上报
 *            OneNET → ESP8266 → STM32:  CMD:ON\n 或 CMD:OFF\n
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
#define MQTT_TOPIC_PROPERTY_POST  "$sys/1iS397oJFL/20260001/thing/property/post"
#define MQTT_TOPIC_PROPERTY_SET   "$sys/1iS397oJFL/20260001/thing/property/set"

/* ═══════════════════════════════════════════════════════════════
 *                    全局对象
 * ═══════════════════════════════════════════════════════════════ */

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

static String          serialLine = "";           /* 串口行缓冲 */
static unsigned long   lastReconnectAttempt = 0;  /* 重连间隔控制 */

/* ═══════════════════════════════════════════════════════════════
 *                    MQTT 回调
 * ═══════════════════════════════════════════════════════════════ */

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    /*
     * 解析 OneNET 属性设置下发 JSON:
     *   {"id":"123","version":"1.0","params":{"Switch":{"value":1}}}
     */
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (const char*)payload, length);

    if (err) {
        /* JSON 解析失败, 尝试简单字符串匹配兜底 */
        char msg[64];
        unsigned int len = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
        memcpy(msg, payload, len);
        msg[len] = '\0';

        if (strstr(msg, "ON") || strstr(msg, "on") || strstr(msg, "\"value\":1")) {
            Serial.print("CMD:ON\n");
        } else if (strstr(msg, "OFF") || strstr(msg, "off") || strstr(msg, "\"value\":0")) {
            Serial.print("CMD:OFF\n");
        }
        return;
    }

    /* 解析 Switch 参数 */
    JsonObject params = doc["params"];
    if (params.containsKey("Switch")) {
        int sw = params["Switch"]["value"];
        if (sw == 1) {
            Serial.print("CMD:ON\n");
        } else {
            Serial.print("CMD:OFF\n");
        }
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

    /* 每 5 秒尝试一次, 避免频繁重连阻塞 loop */
    if (now - lastReconnectAttempt < 5000) return;
    lastReconnectAttempt = now;

    /* WiFi 断开 → 重连 (ESP8266 已存 WiFiManager 配网凭据, 无参 begin 即可) */
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Disconnected or Connecting...");
        WiFi.begin();
        return;
    }

    /* MQTT 断开 → 重连 */
    if (!mqttClient.connected())
    {
        Serial.println("[MQTT] WiFi OK! Now Connecting to OneNET...");
        if (mqttReconnect())
        {
            Serial.println("[MQTT] >>> OneNET Connected successfully! <<<");
        }
        else
        {
            Serial.print("[MQTT] Connect failed, Error Code (rc) = ");
            Serial.println(mqttClient.state());
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *              串口 → MQTT 数据转发
 * ═══════════════════════════════════════════════════════════════ */

static void processSerialLine(const String& line)
{
    /*
     * 解析 STM32 发来的 JSON:
     *   {"V":12.50,"I":1.23,"F":100000}
     */
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) return;  /* 非 JSON 或格式错误, 静默丢弃 */

    float v = doc["V"];
    float i = doc["I"];
    unsigned long f = doc["F"];

    /*
     * 重新组装为 OneNET 物模型格式:
     *   {"id":"123","version":"1.0","params":{"V":{"value":xx},"I":{"value":xx},"F":{"value":xx}}}
     */
    JsonDocument txDoc;
    txDoc["id"]      = "123";
    txDoc["version"] = "1.0";
    txDoc["params"]["V"]["value"] = v;
    txDoc["params"]["I"]["value"] = i;
    txDoc["params"]["F"]["value"] = f;

    char txBuf[256];
    serializeJson(txDoc, txBuf, sizeof(txBuf));

    mqttClient.publish(MQTT_TOPIC_PROPERTY_POST, txBuf);
}

/* ═══════════════════════════════════════════════════════════════
 *                    setup / loop
 * ═══════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[System] ESP8266 Booting...");

    /*
     * WiFiManager 网页配网:
     *   - 有已存凭据 → 直接连接
     *   - 无已存凭据或连接失败 → 开启 "STM32_WPT_Config" AP (无密码)
     *     手机连上此热点, 浏览器会自动弹出配网页
     */
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);          /* 关闭 WiFiManager 自身的调试输出 */
    wifiManager.autoConnect("STM32_WPT_Config");

    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
}

void loop()
{
    /* 1. 非阻塞 WiFi / MQTT 重连维护 */
    ensureConnected();

    /* 2. MQTT 心跳 + 收包 */
    mqttClient.loop();

    /* 3. 串口 → MQTT: 非阻塞读取, 以 \n 为帧结束符 */
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();
        if (c == '\n')
        {
            if (serialLine.length() > 0)
            {
                processSerialLine(serialLine);
                serialLine = "";
            }
        }
        else if (c == '\r')
        {
            /* 忽略 */
        }
        else
        {
            serialLine += c;
            if (serialLine.length() >= 128)
            {
                serialLine = "";  /* 行过长, 丢弃 */
            }
        }
    }
}
