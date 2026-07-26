/**
 ******************************************************************************
 * @file    ESP8266_MQTT_Firmware.ino
 * @brief   ESP8266 Dual-MCU MQTT 固件 (Arduino)
 * @note    V5.1.2: 频率双档量化、严格指令解析与串口溢出保护
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

/* #define DEBUG */  /* 取消注释以启用串口调试输出 */

/* ── 串口通信 ── */
#define SERIAL_BAUDRATE       115200
#define SERIAL_LINE_MAX       256

/* ── WiFi 配网 ── */
#define WIFI_AP_NAME          "STM32_WPT_Config"
#define WIFI_AP_PASSWORD      "wpt2026conf"  /* 配网热点密码 (建议按设备定制) */
#define WIFI_AP_TIMEOUT_S     180

/* ── OneNET MQTT 设备凭证 ──
 * ⚠️ 安全警告: 以下为占位值, 部署前必须替换为实际凭证!
 *    1. 登录 OneNET Studio → 设备管理 → 设备详情 获取产品ID/设备名/Token
 *    2. Token 在 OneNET 平台生成, 过期时间建议 1 年以内
 *    3. 切勿将实际 Token 提交到 Git 仓库
 *    4. 生产环境建议用 EEPROM/SPIFFS 动态存储 Token (通过配网页面上传) */
#define MQTT_SERVER           "mqtts.heclouds.com"
#define MQTT_PORT             1883
#define ONENET_PRODUCT_ID     "YOUR_PRODUCT_ID"       /* ← 替换为实际产品 ID */
#define ONENET_DEVICE_NAME    "YOUR_DEVICE_NAME"      /* ← 替换为实际设备名称 */
#define ONENET_TOKEN          "YOUR_ONENET_TOKEN"     /* ← 替换为实际 Token */

/* ── OneNET 物模型主题 (由 PRODUCT_ID + DEVICE_NAME 自动拼接) ── */
#define MQTT_TOPIC_PROPERTY_POST      "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define MQTT_TOPIC_PROPERTY_SET       "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define MQTT_TOPIC_PROPERTY_SET_REPLY "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"

/* ── 公共 Broker (Web 控制台) ──
 * ⚠️ 安全警告: public broker 无认证, 任何人均可订阅/发布
 *    默认关闭公共通道, 需要时必须同时设置不为空的指令密钥 */
#define PUBLIC_MQTT_ENABLED   0       /* 0=禁用公共 Broker */
#define PUBLIC_MQTT_SERVER    "broker.emqx.io"
#define PUBLIC_MQTT_PORT      1883
#define PUBLIC_TOPIC_DATA     "wpt/" ONENET_DEVICE_NAME "/data"
#define PUBLIC_TOPIC_CMD      "wpt/" ONENET_DEVICE_NAME "/cmd"
#define PUBLIC_CMD_AUTH_KEY   ""      /* 若不为空, 公共指令需以此 Key 开头 (HMAC 预留) */

/* ── 重连间隔 ── */
#define RECONNECT_INTERVAL_MS 5000

/* ── 频率限制 ── */
#define FREQ_MIN_HZ           20000
#define FREQ_MAX_HZ           200000


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
    MQTT_CONN_STATE_IDLE            = 0,  /* 未启动 */
    MQTT_CONN_STATE_WIFI_CONN       = 1,  /* WiFi 连接中 */
    MQTT_CONN_STATE_MQTT_CONN       = 2,  /* MQTT 连接中 */
    MQTT_CONN_STATE_ONLINE          = 3,  /* 双 MQTT 均在线, 可收发 */
    MQTT_CONN_STATE_OFFLINE_PASSIVE = 4,  /* 被动离线 (热点断开, STM32 自动嗅探恢复) */
    MQTT_CONN_STATE_OFFLINE_ACTIVE  = 5   /* 主动离线 (用户按键OFF, 需手动ON恢复) */
} Conn_State;

static Conn_State    s_conn_state      = MQTT_CONN_STATE_IDLE;
static unsigned long s_conn_retry_ms   = 0;
static uint8_t       s_conn_retry_cnt  = 0;

/* WiFi 重连配置: ESP 侧不设上限, 持续重试; 上限判断由 STM32 App_Network 负责 */
#define WIFI_CONN_TIMEOUT_MS    15000   /* 单次 WiFi 连接超时 */
#define WIFI_RETRY_INTERVAL_MS   3000   /* 断开后重试间隔 */


/* ═══════════════════════════════════════════════════════════════
 *  4. MQTT 模块 — Mqtt_Task 命名空间
 * ═══════════════════════════════════════════════════════════════ */

static WiFiClient    s_mqtt_esp_client;
static PubSubClient  s_mqtt_client(s_mqtt_esp_client);
static WiFiClient    s_mqtt_public_client;
static PubSubClient  s_mqtt_public(s_mqtt_public_client);

static uint32_t s_mqtt_last_set_freq = 100000;  /* 最后一次 SetFreq 目标值 (Hz) */
static uint8_t  s_mqtt_skip_switch   = 0;  /* 收到 Switch 命令后跳变一次遥测, 防止 OneNET 属性覆盖 */

/* ── 防误触: 前缀匹配替代 strstr 子串搜索 ── */
static int Str_Starts_With(const char* str, const char* prefix)
{
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

/* 完整指令必须精确匹配, 防止附加字符被当成合法操作 */
static int Str_Equals(const char* left, const char* right)
{
    return strcmp(left, right) == 0;
}

/* 低频档步进 0.1kHz, 高频档步进 1kHz */
static uint32_t Mqtt_Task_Quantize_Frequency(uint32_t frequency_hz)
{
    uint32_t step_hz;
    uint32_t quantized;

    if (frequency_hz < FREQ_MIN_HZ || frequency_hz > FREQ_MAX_HZ) return 0;
    step_hz = (frequency_hz < 100000UL) ? 100UL : 1000UL;
    quantized = ((frequency_hz + step_hz / 2UL) / step_hz) * step_hz;
    if (quantized < FREQ_MIN_HZ) quantized = FREQ_MIN_HZ;
    if (quantized > FREQ_MAX_HZ) quantized = FREQ_MAX_HZ;
    return quantized;
}

/* ── OneNET 指令 → STM32 ── */
static void Mqtt_Task_Parse_Command(const char* payload, unsigned int length)
{
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, (const char*)payload, length);

    int8_t   switch_cmd = 0;
    uint8_t  freq_cmd   = 0;
    uint32_t freq_hz    = 0;

    /* 防重入锁: 同一帧 payload 可能被 OneNET Broker 重发, 重复处理无意义 */
    {
        static uint32_t s_last_cmd_ms  = 0;
        static char     s_last_cmd_buf[64] = "";
        static unsigned int s_last_cmd_len = 0;
        uint32_t now = millis();
        if (length < sizeof(s_last_cmd_buf)
            && length == s_last_cmd_len
            && memcmp(payload, s_last_cmd_buf, length) == 0
            && length > 0
            && now - s_last_cmd_ms < 2000) {
            return;  /* 2s 内完全相同的指令直接丢弃 */
        }
        if (length < sizeof(s_last_cmd_buf)) {
            memcpy(s_last_cmd_buf, payload, length);
            s_last_cmd_buf[length] = '\0';
            s_last_cmd_len = length;
        } else {
            s_last_cmd_len = 0;
        }
        s_last_cmd_ms = now;
    }

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
            switch_cmd = val ? 1 : -1;
        }

        if (params.containsKey("SetFreq")) {
            JsonVariant sf = params["SetFreq"];
            int val = 0;
            if (sf.is<int>())
                val = sf.as<int>();
            else if (sf.containsKey("value"))
                val = sf["value"].as<int>();
            freq_hz = Mqtt_Task_Quantize_Frequency((uint32_t)val);
            if (freq_hz != 0) {
                s_mqtt_last_set_freq = freq_hz;
                freq_cmd = 1;
            }
        }
    } else {
        /* 非 JSON → 前缀字符串匹配兜底, 防子串误触 */
        char msg[64];
        unsigned int len = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
        memcpy(msg, payload, len);
        msg[len] = '\0';

        if (Str_Equals(msg, "CMD:ON")) {
            switch_cmd = 1;
        } else if (Str_Equals(msg, "CMD:OFF")) {
            switch_cmd = -1;
        } else if (Str_Starts_With(msg, "CMD:SETFREQ:")) {
            char* end_ptr;
            unsigned long requested = strtoul(msg + strlen("CMD:SETFREQ:"), &end_ptr, 10);
            if (*end_ptr != '\0') return;
            freq_hz = Mqtt_Task_Quantize_Frequency((uint32_t)requested);
            if (freq_hz == 0) return;
            s_mqtt_last_set_freq = freq_hz;
            freq_cmd = 1;
        } else {
            return;
        }
    }

    /* 透传到 STM32 串口 */
    if (switch_cmd > 0) {
        Serial.print("CMD:ON\n");
        s_mqtt_skip_switch = 1;
    } else if (switch_cmd < 0) {
        Serial.print("CMD:OFF\n");
        s_mqtt_skip_switch = 1;
    }
    if (freq_cmd != 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "CMD:SETFREQ:%lu\n", freq_hz);
        Serial.print(buf);
    }

    /* set_reply 应答 */
    {
        const char* req_id = doc["id"];
        StaticJsonDocument<128> reply;
        if (req_id) reply["id"] = req_id;
        uint8_t accepted = (switch_cmd != 0 || freq_cmd != 0) ? 1 : 0;
        reply["code"] = accepted ? 200 : 400;
        reply["msg"]  = accepted ? "success" : "invalid command";
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
#if PUBLIC_MQTT_ENABLED
    size_t key_length = strlen(PUBLIC_CMD_AUTH_KEY);
    /* 公共通道禁止空密钥, 密钥后的内容仍需通过严格指令解析 */
    if (key_length == 0 || length <= key_length ||
        memcmp(payload, PUBLIC_CMD_AUTH_KEY, key_length) != 0) {
        return;
    }
    payload += key_length;
    length  -= key_length;
#ifdef DEBUG
    Serial.print("[Public] <<< CMD: ");
    Serial.write(payload, length);
    Serial.println();
#endif
    Mqtt_Task_Parse_Command((const char*)payload, length);
#endif /* PUBLIC_MQTT_ENABLED */
}

/* ── 连接维护: 驱动 Conn_State 状态机 ── */
static void Mqtt_Task_Maintain_Connection(void)
{
    unsigned long now = millis();

    switch (s_conn_state) {
        case MQTT_CONN_STATE_IDLE:
            break;  /* 等待外部触发 */

        case MQTT_CONN_STATE_WIFI_CONN:
            if (WiFi.status() == WL_CONNECTED) {
                s_conn_state = MQTT_CONN_STATE_MQTT_CONN;
            } else if (now - s_conn_retry_ms >= WIFI_RETRY_INTERVAL_MS) {
                /* 持续重试, 不设上限 — 上限判断由 STM32 App_Network 负责 */
                s_conn_retry_cnt++;
                s_conn_retry_ms = now;
                WiFi.begin();
                /* 上报重试次数给 STM32, 方便 TFT 显示 */
                Serial.print("STATUS:RETRY=");
                Serial.print(s_conn_retry_cnt);
                Serial.print("\n");
            }
            break;

        case MQTT_CONN_STATE_MQTT_CONN: {
            boolean one_ok  = s_mqtt_client.connected() ||
                s_mqtt_client.connect(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN);
            if (one_ok) s_mqtt_client.subscribe(MQTT_TOPIC_PROPERTY_SET);

#if PUBLIC_MQTT_ENABLED
            if (PUBLIC_CMD_AUTH_KEY[0] != '\0' &&
                (s_mqtt_public.connected() || s_mqtt_public.connect(ONENET_DEVICE_NAME))) {
                s_mqtt_public.setCallback(Mqtt_Task_On_Public_Message);
                s_mqtt_public.subscribe(PUBLIC_TOPIC_CMD);
            }
#endif

            if (one_ok) {
                s_conn_state = MQTT_CONN_STATE_ONLINE;
                Serial.print("STATUS:ONLINE:RSSI=");
                Serial.print(WiFi.RSSI());
                Serial.print("\n");
#ifdef DEBUG
                Serial.println("[Status] >>> Sent STATUS:ONLINE to STM32 <<<");
#endif
            }
            break;
        }

        case MQTT_CONN_STATE_ONLINE: {
            /* 周期性检查: 任一掉线则回退 */
            if (WiFi.status() != WL_CONNECTED) {
                s_conn_state = MQTT_CONN_STATE_WIFI_CONN;
                s_conn_retry_ms = now;
                Serial.print("STATUS:DISCONNECTED\n");
                WiFi.begin();
            } else if (!s_mqtt_client.connected()) {
                s_conn_state = MQTT_CONN_STATE_MQTT_CONN;
            }
#if PUBLIC_MQTT_ENABLED
            /* 公共通道是可选功能, 断开时独立重连, 不影响 OneNET 主通道 */
            if (PUBLIC_CMD_AUTH_KEY[0] != '\0' && !s_mqtt_public.connected() &&
                s_mqtt_public.connect(ONENET_DEVICE_NAME)) {
                s_mqtt_public.setCallback(Mqtt_Task_On_Public_Message);
                s_mqtt_public.subscribe(PUBLIC_TOPIC_CMD);
            }
#endif
            /* 每 2s 上报 RSSI */ {
                static unsigned long last_rssi = 0;
                if (now - last_rssi >= 2000) {
                    last_rssi = now;
                    Serial.print("STATUS:RSSI=");
                    Serial.print(WiFi.RSSI());
                    Serial.print("\n");
                }
            }
            break;
        }

        case MQTT_CONN_STATE_OFFLINE_PASSIVE:
        case MQTT_CONN_STATE_OFFLINE_ACTIVE:
            /* 离线状态: 持续监测 WiFi 恢复, 恢复后自动上报 STATUS:ONLINE 触发 STM32 嗅探 */
            if (WiFi.status() == WL_CONNECTED) {
                s_conn_state     = MQTT_CONN_STATE_MQTT_CONN;
                s_conn_retry_cnt = 0;
                Serial.print("STATUS:MQTT\n");
            } else {
                /* 每 WIFI_RETRY_INTERVAL_MS 重试一次连接 */
                if (now - s_conn_retry_ms >= WIFI_RETRY_INTERVAL_MS) {
                    s_conn_retry_ms = now;
                    WiFi.begin();
                }
            }
            break;
    }
}

/* ── 公开接口 ── */
static void Mqtt_Task_Start_Connect(void)
{
    s_conn_state     = MQTT_CONN_STATE_WIFI_CONN;
    s_conn_retry_ms  = millis();
    s_conn_retry_cnt = 0;
    WiFi.begin();
}

static void Mqtt_Task_Soft_Reset(void)
{
    s_conn_state     = MQTT_CONN_STATE_IDLE;
    s_conn_retry_ms  = millis();
    s_conn_retry_cnt = 0;
}

static uint8_t Mqtt_Task_Get_Connect_Status(void)
{
    return (uint8_t)s_conn_state;  /* 0=IDLE 1=WIFI 2=MQTT 3=ONLINE 4=被动离线 5=主动离线 */
}

static uint8_t Mqtt_Task_Get_Retry_Count(void) { return s_conn_retry_cnt; }
static uint8_t Mqtt_Task_Is_Connected(void)     { return s_conn_state == MQTT_CONN_STATE_ONLINE; }

static void Mqtt_Task_Loop(void)
{
    Mqtt_Task_Maintain_Connection();
    s_mqtt_client.loop();
#if PUBLIC_MQTT_ENABLED
    s_mqtt_public.loop();
#endif
}

static void Mqtt_Task_Publish_Telemetry(const char* stm32_json)
{
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, stm32_json)) return;

    float        v = doc["V"];
    float        i = doc["I"];
    unsigned long f = doc["F"];
    int           s = doc["S"] | 0;
    bool    running = (s == 2);  /* 仅S=2上报Switch=true；S=1的实际扫频F仍正常透传 */

    if (running && f >= FREQ_MIN_HZ && f <= FREQ_MAX_HZ) {
        s_mqtt_last_set_freq = Mqtt_Task_Quantize_Frequency((uint32_t)f);
    }

    StaticJsonDocument<256> tx;
    tx["id"]      = "123";
    tx["version"] = "1.0";
    tx["params"]["V"]["value"]        = v;
    tx["params"]["I"]["value"]        = i;
    tx["params"]["F"]["value"]        = f;  /* STM32 侧已完成 F=0 停机处理, 直接透传 */
    tx["params"]["SetFreq"]["value"]  = s_mqtt_last_set_freq;

    if (s_mqtt_skip_switch) {
        s_mqtt_skip_switch = 0;
    } else {
        tx["params"]["Switch"]["value"] = running;
    }

    char buf[256];
    serializeJson(tx, buf, sizeof(buf));
    s_mqtt_client.publish(MQTT_TOPIC_PROPERTY_POST, buf);

#if PUBLIC_MQTT_ENABLED
    if (s_mqtt_public.connected()) {
        s_mqtt_public.publish(PUBLIC_TOPIC_DATA, stm32_json);
    }
#endif
}


/* ═══════════════════════════════════════════════════════════════
 *  5. 串口解析模块 — Serial_Parse 命名空间
 * ═══════════════════════════════════════════════════════════════ */

static char    s_serial_buf[SERIAL_LINE_MAX];
static uint8_t s_serial_len = 0;
static uint8_t s_serial_overflowed = 0;

static void Serial_Parse_Process_Line(const char* line)
{
    /* CMD:WIFI_DISC — 断开 WiFi 但不清除凭证, 进入被动离线 (可自动恢复)
     * 注意: ESP 侧用 PASSIVE, STM32 侧用 ACTIVE — 设计意图是 ESP 保持 WiFi 自动重连,
     * 但 STM32 的门控阻止应用层重连, 直到用户手动 ON 才放行 */
    if (Str_Equals(line, "CMD:WIFI_DISC")) {
        WiFi.disconnect();
        s_conn_state     = MQTT_CONN_STATE_OFFLINE_PASSIVE;
        s_conn_retry_cnt = 0;
        Serial.print("STATUS:DISCONNECTED\n");
        return;
    }

    /* CMD:CLEAR — 清除配网凭证并重启, 进入配网模式 (需二次确认防误触)
     * 第一次收到 → 回复 CLEAR_CONFIRM? 等待确认
     * 5s 内再次收到 → 执行清除+重启 */
    if (Str_Equals(line, "CMD:CLEAR")) {
        static unsigned long s_clear_first_ms = 0;
        unsigned long now = millis();
        if (s_clear_first_ms == 0 || now - s_clear_first_ms > 5000) {
            s_clear_first_ms = now;
            Serial.print("STATUS:CLEAR_CONFIRM?\n");
            return;
        }
        /* 第二次确认 → 执行清除 */
        s_clear_first_ms = 0;
#ifdef DEBUG
        Serial.println("[System] CMD:CLEAR confirmed — resetting WiFi...");
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
    /* 1s 无换行 → 丢弃半帧缓冲, 防止噪声/断帧导致永久卡死 */
    static unsigned long s_last_char_ms = 0;
    unsigned long now = millis();
    if ((s_serial_len > 0 || s_serial_overflowed) && now - s_last_char_ms >= 1000) {
        s_serial_len = 0;
        s_serial_overflowed = 0;  /* 丢弃卡死或过长的半帧 */
    }

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        s_last_char_ms = millis();
        if (c == '\n') {
            if (s_serial_len > 0 && !s_serial_overflowed) {
                s_serial_buf[s_serial_len] = '\0';
                Serial_Parse_Process_Line(s_serial_buf);
            }
            s_serial_len = 0;
            s_serial_overflowed = 0;
        } else if (c != '\r' && s_serial_len < sizeof(s_serial_buf) - 1) {
            s_serial_buf[s_serial_len++] = c;
        } else if (c != '\r') {
            s_serial_overflowed = 1;
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
    wifi_manager.setDebugOutput(false);       /* 生产环境关闭调试输出 (防 WiFi SSID 泄露) */
    wifi_manager.setConfigPortalTimeout(WIFI_AP_TIMEOUT_S);

#ifdef DEBUG
    wifi_manager.setDebugOutput(true);
    Serial.println("[WiFi] Starting WiFiManager autoConnect...");
#endif

    if (!wifi_manager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD)) {
#ifdef DEBUG
        Serial.println("[WiFi] No saved WiFi — starting Config Portal...");
        Serial.print("[WiFi] Connect to AP: ");
        Serial.println(WIFI_AP_NAME);
#endif
        wifi_manager.startConfigPortal(WIFI_AP_NAME, WIFI_AP_PASSWORD);
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
#if PUBLIC_MQTT_ENABLED
    s_mqtt_public.setServer(PUBLIC_MQTT_SERVER, PUBLIC_MQTT_PORT);
#endif

    Mqtt_Task_Start_Connect();
}

void loop()
{
    static unsigned long last_check = 0;
    static unsigned long last_wifi_retry = 0;
    unsigned long now = millis();

    Mqtt_Task_Loop();
    Serial_Parse_Read_Loop();

    /* 全局 WiFi 断连检测: 每 500ms 检查一次, 排除 IDLE 和 离线状态 */
    if (now - last_check >= 500) {
        last_check = now;
        if (s_conn_state != MQTT_CONN_STATE_IDLE &&
            s_conn_state != MQTT_CONN_STATE_OFFLINE_PASSIVE &&
            s_conn_state != MQTT_CONN_STATE_OFFLINE_ACTIVE) {
            if (WiFi.status() != WL_CONNECTED) {
                /* WiFi 断连 → 回退到 WIFI_CONN, 播报 DISCONNECTED */
                if (s_conn_state != MQTT_CONN_STATE_WIFI_CONN) {
                    s_conn_state     = MQTT_CONN_STATE_WIFI_CONN;
                    s_conn_retry_cnt = 0;
                    Serial.print("STATUS:DISCONNECTED\n");
                }
                /* 每 3s 重试 WiFi.begin(), 利用保存的凭证重连 */
                if (now - last_wifi_retry >= WIFI_RETRY_INTERVAL_MS) {
                    last_wifi_retry = now;
                    s_conn_retry_ms  = now;
                    WiFi.mode(WIFI_STA);
                    WiFi.begin();
                }
            } else {
                /* WiFi 已连接, 重置重试计时器 */
                last_wifi_retry = now;
            }
        }
    }
}
