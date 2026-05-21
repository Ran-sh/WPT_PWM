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
 *          烧录: Arduino IDE → 选择 "Generic ESP8266 Module" → 115200 上传
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
