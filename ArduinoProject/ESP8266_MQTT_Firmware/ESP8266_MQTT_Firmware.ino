/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V4.0: 双脑架构 — ESP8266 独立负责 WiFi + MQTT 连云 (OneNET)
 *          通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":xx,"I":xx,"F":xx}\n  → OneNET 物模型上报
 *            OneNET → ESP8266 → STM32:  CMD:ON\n 或 CMD:OFF\n
 *
 *          依赖: ESP8266WiFi.h + PubSubClient.h + ArduinoJson.h (v6)
 *          烧录: Arduino IDE → 选择 "Generic ESP8266 Module" → 115200 上传
 ******************************************************************************
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/* ═══════════════════════════════════════════════════════════════
 *          用户配置宏 (须根据实际环境修改)
 * ═══════════════════════════════════════════════════════════════ */

#define WIFI_SSID       "YourWiFiSSID"
#define WIFI_PASSWORD   "YourWiFiPassword"

/* OneNET MQTT 服务器 */
#define MQTT_SERVER     "mqtts.heclouds.com"
#define MQTT_PORT       1883

/* OneNET 设备凭证 */
#define ONENET_PRODUCT_ID   "1iS397oJFL"
#define ONENET_DEVICE_NAME  "20260001"
#define ONENET_TOKEN        ""   /* 用户填入 OneNET 设备鉴权 Token */

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
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);

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
    JsonObject params = doc["params"].as<JsonObject>();
    if (params.containsKey("Switch")) {
        int sw = params["Switch"]["value"].as<int>();
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

    /* WiFi 断开 → 重连 */
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        return;   /* WiFi.begin 后需等待 DHCP, 下一轮再连 MQTT */
    }

    /* MQTT 断开 → 重连 */
    if (!mqttClient.connected())
    {
        mqttReconnect();
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
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) return;  /* 非 JSON 或格式错误, 静默丢弃 */

    float v = doc["V"].as<float>();
    float i = doc["I"].as<float>();
    unsigned long f = doc["F"].as<unsigned long>();

    /*
     * 重新组装为 OneNET 物模型格式:
     *   {"id":"123","version":"1.0","params":{"V":{"value":xx},"I":{"value":xx},"F":{"value":xx}}}
     */
    StaticJsonDocument<256> txDoc;
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

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
