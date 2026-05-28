/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V5.1: 面向对象架构 — 命名空间前缀 + 显式连接状态机 + 防误触协议解析
 *
 *          Dual-MCU 通信协议 (115200 8N1):
 *            STM32 → ESP8266:  {"V":xx,"I":xx,"F":xx,"S":x}\n
 *            ESP8266 → STM32:  CMD:ON\n  /  CMD:OFF\n  /  CMD:SETFREQ:<Hz>\n
 *                              STATUS:ONLINE\n  (WiFi+MQTT 就绪)
 *
 *          依赖: ESP8266WiFi + PubSubClient + ArduinoJson v7 + WiFiManager
 *          烧录: Arduino IDE → Generic ESP8266 Module → 115200
 *
 *          ⚠️ Arduino IDE 库管理器安装:
 *             - ArduinoJson (by Benoit Blanchon) — v7
 *             - PubSubClient (by Nick O'Leary)
 *             - WiFiManager (by tzapu) — 网页配网
 ******************************************************************************
 */

/* ═══════════════════════════════════════════════════════════════
 *  1. 用户配置 — 改参数只改这里
 * ═══════════════════════════════════════════════════════════════ */

// #define DEBUG  /* 取消注释以启用串口调试输出 */

/* ── 串口通信 ── */
#define SERIAL_BAUDRATE       115200
#define SERIAL_LINE_MAX       128

/* ── WiFi 配网 ── */
#define WIFI_AP_NAME          "STM32_WPT_Config"
#define WIFI_AP_TIMEOUT_S     180

/* ── OneNET MQTT 设备凭证 ── */
#define MQTT_SERVER           "mqtts.heclouds.com"
#define MQTT_PORT             1883
#define ONENET_PRODUCT_ID     "1iS397oJFL"
#define ONENET_DEVICE_NAME    "20260001"
#define ONENET_TOKEN          "version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D"

/* ── OneNET 物模型主题 ── */
#define MQTT_TOPIC_PROPERTY_POST      "$sys/1iS397oJFL/20260001/thing/property/post"
#define MQTT_TOPIC_PROPERTY_SET       "$sys/1iS397oJFL/20260001/thing/property/set"
#define MQTT_TOPIC_PROPERTY_SET_REPLY "$sys/1iS397oJFL/20260001/thing/property/set_reply"

/* ── 公共 Broker (Web 控制台) ── */
#define PUBLIC_MQTT_SERVER    "broker.emqx.io"
#define PUBLIC_MQTT_PORT      1883
#define PUBLIC_TOPIC_DATA     "wpt/20260001/data"
#define PUBLIC_TOPIC_CMD      "wpt/20260001/cmd"

/* ── 重连间隔 ── */
#define RECONNECT_INTERVAL_MS 5000

/* ── 频率限制 ── */
#define FREQ_MIN_HZ           95000
#define FREQ_MAX_HZ           150000


/* ═══════════════════════════════════════════════════════════════
 *  2. 库引用
 * ═══════════════════════════════════════════════════════════════ */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>


/* ═══════════════════════════════════════════════════════════════
 *  3. 连接状态机 — 显式状态枚举, 替代隐式 bool 标志组合
 * ═══════════════════════════════════════════════════════════════ */

typedef enum {
    CONN_STATE_IDLE        = 0,  /* 未启动 */
    CONN_STATE_WIFI_CONN   = 1,  /* WiFi 连接中 */
    CONN_STATE_MQTT_CONN   = 2,  /* MQTT 连接中 */
    CONN_STATE_ONLINE      = 3,  /* 双 MQTT 均在线, 可收发 */
    CONN_STATE_FAILED      = 4   /* 重试耗尽 */
} Conn_State;

static Conn_State    s_conn_state      = CONN_STATE_IDLE;
static unsigned long s_conn_retry_ms   = 0;
static uint8_t       s_conn_retry_cnt  = 0;
static uint8_t       s_conn_max_retry  = 3;
static unsigned long s_conn_timeout_ms = 15000;


/* ═══════════════════════════════════════════════════════════════
 *  4. MQTT 模块 — Mqtt_Task 命名空间
 * ═══════════════════════════════════════════════════════════════ */

static WiFiClient    s_mqtt_esp_client;
static PubSubClient  s_mqtt_client(s_mqtt_esp_client);
static WiFiClient    s_mqtt_public_client;
static PubSubClient  s_mqtt_public(s_mqtt_public_client);

static uint32_t s_mqtt_last_set_freq = 100000;
static uint8_t  s_mqtt_skip_switch   = 0;

/* ── 防误触: 前缀匹配替代 strstr 子串搜索 ── */
static int Str_Starts_With(const char* str, const char* prefix)
{
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

/* ── OneNET 指令 → STM32 ── */
static void Mqtt_Task_Parse_Command(const char* payload, unsigned int length)
{
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, (const char*)payload, length);

    int8_t   cmd     = 0;
    uint32_t freq_hz = 0;

    if (!err) {
        JsonObject params = doc["params"];

        if (params.containsKey("Switch")) {
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

        if (params.containsKey("SetFreq")) {
            JsonVariant sf = params["SetFreq"];
            int val = 0;
            if (sf.is<int>())
                val = sf.as<int>();
            else if (sf.containsKey("value"))
                val = sf["value"].as<int>();
            if (val >= FREQ_MIN_HZ && val <= FREQ_MAX_HZ) {
                freq_hz            = (uint32_t)((val / 1000) * 1000);
                s_mqtt_last_set_freq = freq_hz;
                cmd = 3;
            }
        }
    } else {
        /* 非 JSON → 前缀字符串匹配兜底, 防子串误触 */
        char msg[64];
        unsigned int len = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
        memcpy(msg, payload, len);
        msg[len] = '\0';

        if      (Str_Starts_With(msg, "CMD:ON")  || strstr(msg, "\"Switch\":true"))  cmd =  1;
        else if (Str_Starts_With(msg, "CMD:OFF") || strstr(msg, "\"Switch\":false")) cmd = -1;
        else return;
    }

    /* 透传到 STM32 串口 */
    switch (cmd) {
        case  1: Serial.print("CMD:ON\n");  s_mqtt_skip_switch = 1; break;
        case -1: Serial.print("CMD:OFF\n"); s_mqtt_skip_switch = 1; break;
        case  3: {
            char buf[32];
            snprintf(buf, sizeof(buf), "CMD:SETFREQ:%lu\n", freq_hz);
            Serial.print(buf);
            break;
        }
        default: break;
    }

    /* set_reply 应答 */
    {
        const char* req_id = doc["id"];
        StaticJsonDocument<128> reply;
        if (req_id) reply["id"] = req_id;
        reply["code"] = 200;
        reply["msg"]  = "success";
        char buf[128];
        serializeJson(reply, buf, sizeof(buf));
        s_mqtt_client.publish(MQTT_TOPIC_PROPERTY_SET_REPLY, buf);
    }
}

static void Mqtt_Task_On_OneNET_Message(char* topic, byte* payload, unsigned int length)
{
    Mqtt_Task_Parse_Command((const char*)payload, length);
}

static void Mqtt_Task_On_Public_Message(char* topic, byte* payload, unsigned int length)
{
#ifdef DEBUG
    Serial.print("[Public] <<< CMD: ");
    Serial.write(payload, length);
    Serial.println();
#endif
    Serial.write(payload, length);
    Serial.print("\n");
}

/* ── 连接维护: 驱动 Conn_State 状态机 ── */
static void Mqtt_Task_Maintain_Connection(void)
{
    unsigned long now = millis();

    switch (s_conn_state) {
        case CONN_STATE_IDLE:
            break;  /* 等待外部触发 */

        case CONN_STATE_WIFI_CONN:
            if (WiFi.status() == WL_CONNECTED) {
                s_conn_state = CONN_STATE_MQTT_CONN;
            } else if (now - s_conn_retry_ms >= s_conn_timeout_ms) {
                s_conn_retry_cnt++;
                if (s_conn_retry_cnt >= s_conn_max_retry) {
                    s_conn_state = CONN_STATE_FAILED;
                } else {
                    s_conn_retry_ms = now;
                    WiFi.begin();
                }
            }
            break;

        case CONN_STATE_MQTT_CONN: {
            boolean one_ok  = s_mqtt_client.connected() ||
                s_mqtt_client.connect(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN);
            if (one_ok) s_mqtt_client.subscribe(MQTT_TOPIC_PROPERTY_SET);

            boolean pub_ok = s_mqtt_public.connected() ||
                s_mqtt_public.connect(ONENET_DEVICE_NAME);
            if (pub_ok && !s_mqtt_public.connected()) {
                /* 刚连上: 首次设置回调 */
            }
            if (pub_ok) {
                s_mqtt_public.setCallback(Mqtt_Task_On_Public_Message);
                s_mqtt_public.subscribe(PUBLIC_TOPIC_CMD);
            }

            if (one_ok && pub_ok) {
                s_conn_state = CONN_STATE_ONLINE;
                Serial.print("STATUS:ONLINE\n");
#ifdef DEBUG
                Serial.println("[Status] >>> Sent STATUS:ONLINE to STM32 <<<");
#endif
            }
            break;
        }

        case CONN_STATE_ONLINE:
            /* 周期性检查: 任一掉线则回退 */
            if (WiFi.status() != WL_CONNECTED) {
                s_conn_state = CONN_STATE_WIFI_CONN;
                s_conn_retry_ms = now;
                WiFi.begin();
            } else if (!s_mqtt_client.connected() || !s_mqtt_public.connected()) {
                s_conn_state = CONN_STATE_MQTT_CONN;
            }
            break;

        case CONN_STATE_FAILED:
            break;  /* 需外部复位 */
    }
}

/* ── 公开接口 ── */
static void Mqtt_Task_Start_Connect(void)
{
    s_conn_state     = CONN_STATE_WIFI_CONN;
    s_conn_retry_ms  = millis();
    s_conn_retry_cnt = 0;
    WiFi.begin();
}

static void Mqtt_Task_Soft_Reset(void)
{
    s_conn_state     = CONN_STATE_WIFI_CONN;
    s_conn_retry_ms  = millis();
    s_conn_retry_cnt = 0;
}

static uint8_t Mqtt_Task_Get_Connect_Status(void)
{
    return (uint8_t)s_conn_state;  /* 0=IDLE 1=WIFI 2=MQTT 3=ONLINE 4=FAILED */
}

static uint8_t Mqtt_Task_Get_Retry_Count(void) { return s_conn_retry_cnt; }
static uint8_t Mqtt_Task_Is_Connected(void)     { return s_conn_state == CONN_STATE_ONLINE; }

static void Mqtt_Task_Loop(void)
{
    Mqtt_Task_Maintain_Connection();
    s_mqtt_client.loop();
    s_mqtt_public.loop();
}

static void Mqtt_Task_Publish_Telemetry(const char* stm32_json)
{
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, stm32_json)) return;

    float        v = doc["V"];
    float        i = doc["I"];
    unsigned long f = doc["F"];
    int           s = doc["S"] | 0;
    bool    running = (s == 1 || s == 2);

    StaticJsonDocument<256> tx;
    tx["id"]      = "123";
    tx["version"] = "1.0";
    tx["params"]["V"]["value"]        = v;
    tx["params"]["I"]["value"]        = i;
    tx["params"]["F"]["value"]        = f;
    tx["params"]["SetFreq"]["value"]  = s_mqtt_last_set_freq;

    if (s_mqtt_skip_switch) {
        s_mqtt_skip_switch = 0;
    } else {
        tx["params"]["Switch"]["value"] = running;
    }

    char buf[256];
    serializeJson(tx, buf, sizeof(buf));
    s_mqtt_client.publish(MQTT_TOPIC_PROPERTY_POST, buf);

    if (s_mqtt_public.connected()) {
        s_mqtt_public.publish(PUBLIC_TOPIC_DATA, stm32_json);
    }
}


/* ═══════════════════════════════════════════════════════════════
 *  5. 串口解析模块 — Serial_Parse 命名空间
 * ═══════════════════════════════════════════════════════════════ */

static char    s_serial_buf[SERIAL_LINE_MAX];
static uint8_t s_serial_len = 0;

static void Serial_Parse_Process_Line(const char* line)
{
    /* CMD:CLEAR — 前缀匹配防误触 */
    if (Str_Starts_With(line, "CMD:CLEAR")) {
#ifdef DEBUG
        Serial.println("[System] CMD:CLEAR — resetting WiFi...");
#endif
        WiFiManager wm;
        wm.resetSettings();
        Serial.flush();          /* 等 UART FIFO 排空再重启 */
        delay(200);
        ESP.restart();
    }

    Mqtt_Task_Publish_Telemetry(line);
}

static void Serial_Parse_Read_Loop(void)
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n') {
            if (s_serial_len > 0) {
                s_serial_buf[s_serial_len] = '\0';
                Serial_Parse_Process_Line(s_serial_buf);
                s_serial_len = 0;
            }
        } else if (c != '\r' && s_serial_len < sizeof(s_serial_buf) - 1) {
            s_serial_buf[s_serial_len++] = c;
        }
    }
}


/* ═══════════════════════════════════════════════════════════════
 *  6. setup() & loop()
 * ═══════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(SERIAL_BAUDRATE);

#ifdef DEBUG
    Serial.println();
    Serial.println("[System] ESP8266 Booting...");
    Serial.print("[System] Free heap: ");
    Serial.println(ESP.getFreeHeap());
#endif

    WiFi.mode(WIFI_STA);

    WiFiManager wifi_manager;
    wifi_manager.setDebugOutput(true);
    wifi_manager.setConfigPortalTimeout(WIFI_AP_TIMEOUT_S);

#ifdef DEBUG
    Serial.println("[WiFi] Starting WiFiManager autoConnect...");
#endif

    if (!wifi_manager.autoConnect(WIFI_AP_NAME)) {
#ifdef DEBUG
        Serial.println("[WiFi] No saved WiFi — starting Config Portal...");
        Serial.print("[WiFi] Connect to AP: ");
        Serial.println(WIFI_AP_NAME);
#endif
        wifi_manager.startConfigPortal(WIFI_AP_NAME);
    }

#ifdef DEBUG
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Free heap: ");
    Serial.println(ESP.getFreeHeap());
#endif

    /* 初始化 MQTT 客户端并启动联网状态机 */
    s_mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
    s_mqtt_client.setCallback(Mqtt_Task_On_OneNET_Message);
    s_mqtt_public.setServer(PUBLIC_MQTT_SERVER, PUBLIC_MQTT_PORT);

    Mqtt_Task_Start_Connect();
}

void loop()
{
    Mqtt_Task_Loop();
    Serial_Parse_Read_Loop();
}
