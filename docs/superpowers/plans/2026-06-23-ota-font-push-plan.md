# ESP8266 无线 OTA 字库推送 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 通过 ESP8266 WiFiServer + Base64 编码实现对 W25Q128 字库区的无线更新（Phase A: 4KB）

**Architecture:** 4层数据路径 — PC端 Python 解析 TFT_Font_Data.h 拼装 bin → TCP WiFiServer(8266) → ESP8266 串口透传 → STM32 Base64 解码 + Page Program → W25Q128 Flash。ACK 反向路径: STM32 USART2 TX → ESP client.print → PC

**Tech Stack:** Python 3.6+ (stdlib only), Arduino C++ (ESP8266WiFi), ARMCC V5 C89 SPL (STM32)

---

## 文件结构

```
Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino  ← 修改 (+90行)
Keil_Project/User/App_Storage.c                                   ← 修改 (+85行)
Keil_Project/User/App_Storage.h                                   ← 修改 (+12行)
Keil_Project/User/App_Network.c                                   ← 修改 (+20行)
Claude_Files/tools/ota_font_push.py                               ← 新建 (~130行)
```

---

### Task 1: Base64 编解码（双平台）

**Files:**
- Modify: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino` — 在 §5 Serial_Parse 段前插入
- Modify: `Keil_Project/User/App_Storage.c` — 在文件顶部的 `#include` 块之后插入

#### ESP8266 侧 — Base64 查表 + 编解码

- [ ] **Step 1: 在 `Serial_Parse_Process_Line` 函数之前插入 Base64 表 + 解码函数**

```cpp
/* ═══════════════════════════════════════════════════════════════
 *  Base64 编解码 — OTA 字库推送用 (避免二进制 \n 截断 USART 帧)
 * ═══════════════════════════════════════════════════════════════ */
static const char BASE64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 解码: 4个Base64字符 → 3字节, 返回实际输出字节数 */
static uint8_t Base64_Decode_Quad(const char* in, uint8_t* out)
{
    uint8_t i, vals[4], out_len = 3;
    for (i = 0; i < 4; i++) {
        if (in[i] >= 'A' && in[i] <= 'Z')      vals[i] = in[i] - 'A';
        else if (in[i] >= 'a' && in[i] <= 'z') vals[i] = in[i] - 'a' + 26;
        else if (in[i] >= '0' && in[i] <= '9') vals[i] = in[i] - '0' + 52;
        else if (in[i] == '+')                 vals[i] = 62;
        else if (in[i] == '/')                 vals[i] = 63;
        else if (in[i] == '=') { vals[i] = 0; out_len--; }
        else return 0;  /* 非法字符 */
    }
    out[0] = (vals[0] << 2) | (vals[1] >> 4);
    out[1] = (vals[1] << 4) | (vals[2] >> 2);
    out[2] = (vals[2] << 6) | vals[3];
    return out_len;
}

/* 编码: 3字节 → 4个Base64字符 (用于 ACK 帧中的短数据块, 预留) */
static void Base64_Encode_3B(const uint8_t* in, uint8_t in_len, char* out)
{
    uint8_t pad = (3 - in_len) % 3;
    uint32_t v = ((uint32_t)in[0] << 16) | ((in_len > 1 ? (uint32_t)in[1] : 0U) << 8) | (in_len > 2 ? (uint32_t)in[2] : 0U);
    out[0] = BASE64_TABLE[(v >> 18) & 0x3F];
    out[1] = BASE64_TABLE[(v >> 12) & 0x3F];
    out[2] = (in_len > 1) ? BASE64_TABLE[(v >> 6) & 0x3F] : '=';
    out[3] = (in_len > 2) ? BASE64_TABLE[v & 0x3F] : '=';
    out[4] = '\0';
}
```

#### STM32 侧 — Base64 查表 + 解码

- [ ] **Step 2: 在 `App_Storage.c` 顶部 `CRC32_Compute` 之后插入 Base64 解码器**

```c
/* ═══════════════════════════════════════════════
 *  Base64 解码 — OTA 字库推送用
 * ═══════════════════════════════════════════════ */
static const char BASE64_TABLE_OTA[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** @brief 单字符→6bit值, 非法返回 0xFF */
static uint8_t Base64_Char_Val(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 52);
    if (c == '+') return 62U;
    if (c == '/') return 63U;
    return 0xFFU;  /* '=' 或其他 — 非法 */
}

/** @brief 解码 344B Base64 → 256B 原始二进制, 返回实际解码字节数 */
static uint16_t Base64_Decode_Block(const char *in, uint16_t in_len, uint8_t *out)
{
    uint16_t out_idx = 0, i;
    for (i = 0; i + 3 < in_len; i += 4) {
        uint8_t v0 = Base64_Char_Val(in[i]);
        uint8_t v1 = Base64_Char_Val(in[i+1]);
        uint8_t v2, v3;
        if (v0 == 0xFFU || v1 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v0 << 2) | (v1 >> 4));

        if (in[i+2] == '=') break;
        v2 = Base64_Char_Val(in[i+2]);
        if (v2 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v1 << 4) | (v2 >> 2));

        if (in[i+3] == '=') break;
        v3 = Base64_Char_Val(in[i+3]);
        if (v3 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v2 << 6) | v3);
    }
    return out_idx;
}
```

---

### Task 2: ESP8266 OTA Server 状态机

**Files:**
- Modify: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`

- [ ] **Step 1: 在 `loop()` 函数之前的 `#define` 区域插入 OTA 常量**

```cpp
/* ── OTA 字库推送 ── */
#define OTA_FONT_PORT         8266
#define OTA_FONT_TIMEOUT_MS   60000   /* OTA 模式超时: 60s 无活动自动退出 */
```

- [ ] **Step 2: 在 `Mqtt_Task_Loop` 函数之后插入 OTA Server 状态机**

```cpp
/* ═══════════════════════════════════════════════════════════════
 *  OTA Font Server — TCP→Serial 双向透传
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t       s_ota_active = 0;
static WiFiServer*   s_ota_server = NULL;
static WiFiClient    s_ota_client;
static unsigned long s_ota_last_ms = 0;

static void OTA_Font_Server_Start(void)
{
    if (s_ota_active) return;
    s_ota_server = new WiFiServer(OTA_FONT_PORT);
    if (s_ota_server) {
        s_ota_server->begin();
        s_ota_active = 1;
        s_ota_client = WiFiClient();  /* 空 client */
        s_ota_last_ms = millis();
#ifdef DEBUG
        Serial.println("[OTA] Server started on port 8266");
#endif
    }
}

static void OTA_Font_Server_Stop(void)
{
    if (s_ota_client && s_ota_client.connected()) {
        s_ota_client.stop();
    }
    if (s_ota_server) {
        s_ota_server->stop();
        delete s_ota_server;
        s_ota_server = NULL;
    }
    s_ota_active = 0;
#ifdef DEBUG
    Serial.println("[OTA] Server stopped");
#endif
}

static void OTA_Font_Server_Loop(void)
{
    unsigned long now;
    if (!s_ota_active) return;
    now = millis();

    /* ── 等待客户端连接 ── */
    if (!s_ota_client || !s_ota_client.connected()) {
        s_ota_client = s_ota_server->available();
        if (s_ota_client && s_ota_client.connected()) {
            s_ota_last_ms = now;
#ifdef DEBUG
            Serial.println("[OTA] Client connected");
#endif
        } else {
            /* 60s 无客户端连接 → 超时退出 */
            if (now - s_ota_last_ms >= OTA_FONT_TIMEOUT_MS) {
                Serial.print("STATUS:OTA_TIMEOUT\n");
                OTA_Font_Server_Stop();
            }
            return;
        }
    }

    /* ── TCP → Serial 透传 ── */
    while (s_ota_client.available() > 0) {
        char c = (char)s_ota_client.read();
        Serial.print(c);
        s_ota_last_ms = now;
    }

    /* ── Serial → TCP 透传 (ACK 帧回传) ── */
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (s_ota_client && s_ota_client.connected()) {
            s_ota_client.print(c);
        }
        s_ota_last_ms = now;
    }

    /* ── OTA 完成检测: Serial 收到 OTA:DONE 或 OTA:FAIL → 透传后关闭 Server ── */
    /* 此检测在 Serial_Parse_Process_Line 中完成, 这里仅处理超时 */
    if (!s_ota_client || !s_ota_client.connected()) {
        if (now - s_ota_last_ms >= OTA_FONT_TIMEOUT_MS) {
            Serial.print("STATUS:OTA_TIMEOUT\n");
            OTA_Font_Server_Stop();
        }
    }
}
```

- [ ] **Step 3: 修改 `Serial_Parse_Process_Line` — 在开头插入 OTA 帧识别**

在现有 `if (Str_Starts_With(line, "CMD:WIFI_DISC"))` 之前插入：

```cpp
    /* OTA 帧识别 — 由 STM32 侧的 OTA:START/OTA:DONE/OTA:FAIL 触发 Server 启停 */
    if (Str_Starts_With(line, "OTA:START")) {
        OTA_Font_Server_Start();
        /* OTA:START 同时透传给 STM32 (下一行 Serial.print 不执行, 因为 OTA:START
         * 本身来自 STM32 的上游 — 实际流程: PC→TCP→Serial→STM32, START 由 PC 发,
         * ESP 收到后启动 Server + 原样透传到 Serial)
         * 纠正: OTA:START 由 PC 通过 TCP 发来, ESP OTA_Font_Server_Loop 已经
         * 用 Serial.print 原样透传了, 这里不需要额外处理。 */
        return;
    }
    if (Str_Starts_With(line, "OTA:DONE") || Str_Starts_With(line, "OTA:FAIL")) {
        OTA_Font_Server_Stop();
        return;
    }
```

**重审**: PC 端发 OTA:START → ESP OTA_Font_Server_Loop 中 client.available 读到后 Serial.print 原样发出 → STM32 USART2 收到。但此时 Server 还没启动，问题的根因是时序：

修正方案 — ESP 的 OTA Server 应该始终处于可启动状态（或者 OTA:START 不应从 PC 走 TCP，而是从 PC 走现有 Public MQTT → ESP → Serial → STM32 再回传）。

**最终修正**: OTA:START 走 **Public MQTT cmd topic** 触发 ESP 启动 Server，后续数据走 TCP。这更合理 — 省去 TCP 端口始终开放的开销。

- [ ] **Step 3 (修正版): 修改 `Mqtt_Task_On_Public_Message` 触发 OTA**

在现有的 `Serial.write(payload, length); Serial.print("\n");` 之后插入：

```cpp
    /* OTA 触发: PC 通过 Public MQTT 发 "OTA:START" → 启动 TCP Server */
    if (length == 9 && memcmp(payload, "OTA:START", 9) == 0) {
        /* 先透传给 STM32 (触发 STM32 进入 OTA 模式) */
        /* 然后启动 WiFiServer 等待 TCP 数据连接 */
        OTA_Font_Server_Start();
    }
```

**修正后的完整流程**:
1. PC → Public MQTT `wpt/20260001/cmd` 发 `OTA:START`
2. ESP Public MQTT callback → Mqtt_Task_On_Public_Message → Serial.print `OTA:START\n` + `OTA_Font_Server_Start()`
3. STM32 收到 `OTA:START` → App_Storage_OTA_Begin → ACK
4. PC 收到 ACK 后 → TCP connect ESP8266:8266 → 发数据页
5. 数据传输完成后 PC 发 `OTA:END` 通过 TCP
6. ESP 透传到 Serial → STM32 校验 → Serial 回 `OTA:DONE`
7. ESP `OTA_Font_Server_Loop` Serial 读到 `OTA:DONE` 透传回 TCP → PC 显示成功

- [ ] **Step 4: 修改 `loop()` 末尾 — 在 `Mqtt_Task_Loop()` 之后插入 OTA tick**

```cpp
    OTA_Font_Server_Loop();  /* OTA 模式: TCP↔Serial 透传 (非阻塞) */
```

在现有 `loop()` 中 `Mqtt_Task_Loop();` 之后、`Serial_Parse_Read_Loop();` 之前插入。

- [ ] **Step 5: 修正 `OTA_Font_Server_Loop` 中的 Serial→TCP 透传逻辑**

OTA_Font_Server_Loop 中的 `Serial.avaiable()` 不能和 `Serial_Parse_Read_Loop()` 同时读取同一串口数据 — 会产生乒乓竞态。

**修正**: OTA_Font_Server_Loop 的回传方向改为在 `Serial_Parse_Process_Line` 中完成 — 当解析到以 `OTA:ACK`/`OTA:ERR`/`OTA:DONE`/`OTA:FAIL` 开头的行时，直接调用一个函数将整行 + `\n` 写入 TCP client。

在 `Serial_Parse_Process_Line` 中 `Mqtt_Task_Publish_Telemetry(line);` 之前插入：

```cpp
    /* OTA ACK 帧: STM32 → Serial → TCP 回传 PC */
    if (Str_Starts_With(line, "OTA:ACK") || Str_Starts_With(line, "OTA:ERR")
        || Str_Starts_With(line, "OTA:DONE") || Str_Starts_With(line, "OTA:FAIL")) {
        if (s_ota_active && s_ota_client && s_ota_client.connected()) {
            s_ota_client.print(line);
            s_ota_client.print("\n");
        }
        /* OTA:DONE 和 OTA:FAIL 触发关闭 Server, 回到正常模式 */
        if (Str_Starts_With(line, "OTA:DONE") || Str_Starts_With(line, "OTA:FAIL")) {
            OTA_Font_Server_Stop();
        }
        return;  /* ACK 帧不进入 MQTT 遥测发布 */
    }
```

同时，`OTA_Font_Server_Loop()` 中移除 Serial→TCP 的 `while(Serial.available())` 块（避免和 Serial_Parse_Read_Loop 竞态）。仅保留 TCP→Serial 方向 + 超时管理。

- [ ] **Step 6: 编译验证 (Arduino IDE)**

在 Arduino IDE 中编译 ESP8266 固件，确认 0 错误。无需上传（STM32 侧同步开发完成后统一上传测试）。

---

### Task 3: STM32 App_Storage OTA Handler

**Files:**
- Modify: `Keil_Project/User/App_Storage.c` — 在现有 `Blackbox_Read_Entry` 之后插入
- Modify: `Keil_Project/User/App_Storage.h` — 在 `Blackbox_Read_Entry` 声明之后插入

- [ ] **Step 1: 修改 App_Storage.h — 新增 OTA 函数声明**

在 `Blackbox_Read_Entry` 声明之后、`#endif` 之前插入：

```c
/* ── OTA 字库推送 (P4.5) ── */
/** @brief 进入 OTA 模式: 擦除字库区前4KB, 显示进度 */
void App_Storage_OTA_Begin(void);
/** @brief 处理单帧 OTA 数据: 解析 Base64 → 解码 → 写页 → 回 ACK */
void App_Storage_OTA_Handler(const char *frame);
/** @brief 完成 OTA: CRC32 全量校验 → FONT_OK 或 FONT_CORRUPT → 发 DONE/FAIL */
void App_Storage_OTA_End(void);
```

- [ ] **Step 2: 修改 App_Storage.c — 在 `App_Storage_Init` 之前插入 OTA 静态状态**

```c
/* ═══════════════════════════════════════════════
 *  OTA 字库推送 (Phase A: 4KB 最小可行版)
 * ═══════════════════════════════════════════════ */
static uint8_t  s_ota_active = 0;
static uint16_t s_ota_page_total = 0;
static uint16_t s_ota_page_done  = 0;
static uint8_t  s_ota_buf[256];       /* 单页解码缓冲 */

/* ── 进度上报 ── */
static void OTA_Send_ACK(uint16_t seq)
{
    char buf[20];
    uint16_t written;
    written = (uint16_t)snprintf(buf, sizeof(buf), "OTA:ACK:%u\n", (unsigned int)seq);
    if (written > 0 && written < sizeof(buf))
        Esp8266_Driver_Send_String(buf);
}

static void OTA_Send_ERR(uint16_t seq, const char *reason)
{
    char buf[40];
    uint16_t written;
    written = (uint16_t)snprintf(buf, sizeof(buf), "OTA:ERR:%u,%s\n", (unsigned int)seq, reason);
    if (written > 0 && written < sizeof(buf))
        Esp8266_Driver_Send_String(buf);
}
```

- [ ] **Step 3: 实现 `App_Storage_OTA_Begin()`**

```c
/** @brief 进入 OTA 模式: 擦除字库区前4KB + 初始化状态
 *  @note  仅 IDLE 态可调用 (由 App_Network OTA:START 帧门控保证)
 *          L4 防线天然放行 — IDLE 态不发波 */
void App_Storage_OTA_Begin(void)
{
    s_ota_active      = 1;
    s_ota_page_total  = 0;
    s_ota_page_done   = 0;

    /* 擦除字库区前 4KB (头部 + ASCII + 76字 CJK) */
    W25Q_Driver_Erase_Sector(W25Q_ADDR_FONT);

    /* 通知 PC 就绪 */
    Esp8266_Driver_Send_String("OTA:READY\n");
}
```

- [ ] **Step 4: 实现 `App_Storage_OTA_Handler()`**

```c
/** @brief 处理 OTA:<seq>,<base64> 帧
 *  @param frame  完整的文本帧 (不含 \n) */
void App_Storage_OTA_Handler(const char *frame)
{
    uint16_t seq, data_len;
    const char *comma, *b64_start;
    uint32_t page_addr;

    if (!s_ota_active) return;

    /* 解析 seq */
    if (strstr(frame, "OTA:") != frame) return;  /* 必须以 OTA: 开头 */
    seq = (uint16_t)strtol(frame + 4, NULL, 10);
    comma = strstr(frame, ",");
    if (comma == 0) return;
    b64_start = comma + 1;

    /* Base64 解码 (344B→256B) */
    data_len = Base64_Decode_Block(b64_start,
        (uint16_t)(strlen(b64_start)), s_ota_buf);
    if (data_len != W25Q_PAGE_SIZE) {
        OTA_Send_ERR(seq, "B64LEN");
        return;
    }

    /* 写页 (L1+L2 在 W25Q_Driver 层自动处理) */
    page_addr = W25Q_ADDR_FONT + ((uint32_t)seq * W25Q_PAGE_SIZE);
    W25Q_Driver_Write_Page(page_addr, s_ota_buf, W25Q_PAGE_SIZE);

    s_ota_page_done++;
    if (seq + 1 > s_ota_page_total) s_ota_page_total = seq + 1;

    OTA_Send_ACK(seq);
}
```

- [ ] **Step 5: 实现 `App_Storage_OTA_End()`**

```c
/** @brief OTA 传输完成 — 写入头部 + CRC32 校验 + 标记 FONT_OK */
void App_Storage_OTA_End(void)
{
    uint8_t header[32]; uint32_t i, crc_val;

    if (!s_ota_active) return;
    s_ota_active = 0;

    /* ── 写入字库头部 (32B) ── */
    *(uint16_t*)(header + 0)  = FONT_MAGIC;           /* 0x574B */
    *(uint16_t*)(header + 2)  = 1;                    /* Version */
    *(uint32_t*)(header + 4)  = 0;                    /* CRC32 占位 */
    *(uint32_t*)(header + 8)  = 1520U;               /* ASCII_Size */
    *(uint32_t*)(header + 12) = FONT_CJK_BASE_UNICODE;/* 0x4E00 */
    *(uint32_t*)(header + 16) = FONT_CJK_COUNT;       /* 20902 */
    for (i = 20; i < 32; i++) header[i] = 0x00;

    /* 计算头部 + 数据区 CRC32 */
    crc_val = CRC32_Compute(header + 4, 28);           /* 跳过 magic 和 crc32 占位 */
    /* 注: 全量 4KB CRC32 对OTA收尾来说可接受 (~2ms), 此处计算头部+数据区 */
    {
        uint8_t buf[256]; uint32_t addr;
        for (addr = 32; addr < (uint32_t)s_ota_page_total * W25Q_PAGE_SIZE; addr += 256) {
            W25Q_Driver_Read(W25Q_ADDR_FONT + addr, buf, 256);
            /* 增量 CRC32 */
            { uint32_t j, k;
              for (j = 0; j < 256; j++) {
                crc_val ^= (uint32_t)buf[j] << 24;
                for (k = 0; k < 8; k++)
                    crc_val = (crc_val & 0x80000000U) ? (crc_val << 1) ^ 0x04C11DB7U : (crc_val << 1);
              }
            }
        }
        crc_val ^= 0xFFFFFFFFU;  /* final XOR */
    }

    *(uint32_t*)(header + 4) = crc_val;
    W25Q_Driver_Write_Page(W25Q_ADDR_FONT, header, 32);

    g_font_status = FONT_OK;
    Esp8266_Driver_Send_String("OTA:DONE\n");
}
```

- [ ] **Step 6: 编译验证 (Keil F7)**

按 F7 编译，确认 0 错误 0 警告。

---

### Task 4: STM32 App_Network 帧分发

**Files:**
- Modify: `Keil_Project/User/App_Network.c` — 在 `skip_frame:` 之前插入 OTA 帧分发

- [ ] **Step 1: 在 CMD:SETFREQ 的 else if 分支之后、`}` 闭合和 `skip_frame:` 之前插入 OTA 帧分发**

```c
        /* ── OTA 字库推送帧分发 ── */
        else if (strstr(local_buf, "OTA:START")) {
            /* IDLE 态门控: 仅停机状态可进入 OTA 模式,
             * 防运行时误触发擦除 Flash (L4 也拦一层) */
            if (g_sys_state == SYS_STATE_IDLE) {
                App_Storage_OTA_Begin();
            }
        }
        else if (strstr(local_buf, "OTA:END")) {
            App_Storage_OTA_End();
        }
        else if (strstr(local_buf, "OTA:") == local_buf) {
            /* OTA:<seq>,<base64> — 委托 App_Storage 处理 */
            App_Storage_OTA_Handler(local_buf);
        }
```

**放置位置**: 在现有的 `else if ((p = strstr(local_buf, "CMD:SETFREQ:")) != 0)` 块闭合花括号之后、外层 `}` 闭合之前。需确保 OTA 帧在 `CMD:OFF`/`CMD:ON`/`CMD:SETFREQ` 检查和 `skip_frame` 之间的正确位置。

完整的插入位置（参照 App_Network.c 第 277-288 行区域）：

```c
/* 第 288 行后 (CMD:SETFREQ 的 else if 块闭合后) 插入: */
        else if (strstr(local_buf, "OTA:START")) {
            if (g_sys_state == SYS_STATE_IDLE) {
                App_Storage_OTA_Begin();
            }
        }
        else if (strstr(local_buf, "OTA:END")) {
            App_Storage_OTA_End();
        }
        else if (strstr(local_buf, "OTA:") == local_buf) {
            App_Storage_OTA_Handler(local_buf);
        }
```

- [ ] **Step 2: 编译验证 (Keil F7)**

按 F7 编译，确认 0 错误 0 警告。

---

### Task 5: PC 端推送工具

**Files:**
- Create: `Claude_Files/tools/ota_font_push.py`

- [ ] **Step 1: 创建 ota_font_push.py**

```python
#!/usr/bin/env python3
"""ESP8266 OTA Font Push Tool — Phase A: 4KB (95 ASCII + 76 CJK)

用法:
    python ota_font_push.py --font-data <path_to_TFT_Font_Data.h> [--ip <ESP_IP>] [--port 8266]

依赖: Python 3.6+ (标准库 only, 无外部 pip)
"""

import argparse
import base64
import os
import re
import socket
import struct
import sys
import time

# ═══════════════════════════════════════════════════════════════
#  字体数据提取 — 解析 TFT_Font_Data.h
# ═══════════════════════════════════════════════════════════════

def parse_hex_byte(s):
    """解析 0xHH 格式的十六进制字节"""
    s = s.strip()
    if s.startswith("0x") or s.startswith("0X"):
        return int(s[2:], 16)
    return None

def extract_c_array(text, array_name):
    """从 C 源码中提取 static const uint8_t ARRAY_NAME[][16] = {...} 或 [][32] = {...}
    返回 (element_size, list_of_byte_arrays)"""
    # 匹配: static const uint8_t NAME[][N] = { ... };
    pattern = re.escape(array_name) + r'\[\]\[(\d+)\]\s*=\s*\{([^;]+)\};'
    m = re.search(pattern, text, re.DOTALL)
    if not m:
        pattern2 = re.escape(array_name) + r'\[\d+\]\[(\d+)\]\s*=\s*\{([^;]+)\};'
        m = re.search(pattern2, text, re.DOTALL)
    if not m:
        raise ValueError(f"Array '{array_name}' not found in source")
    elem_size = int(m.group(1))
    body = m.group(2)
    # 提取每个 {} 块
    blocks = re.findall(r'\{([^}]+)\}', body)
    result = []
    for blk in blocks:
        bytes_list = []
        for token in blk.split(','):
            token = token.strip()
            if not token:
                continue
            b = parse_hex_byte(token)
            if b is not None:
                bytes_list.append(b)
        result.append(bytes(bytes_list))
    return elem_size, result

def build_font_bin(header_path):
    """从 TFT_Font_Data.h 构建 4KB bin (与 App_Storage_Burn_Font_From_SRAM 一致)"""
    with open(header_path, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()

    # 提取 ASCII 字模 (95字 × 16B)
    _, ascii_data = extract_c_array(text, 'TFT_FONT_8X16')
    if len(ascii_data) < 95:
        raise ValueError(f"Expected 95 ASCII glyphs, got {len(ascii_data)}")

    # 提取 CJK 字模 (76字 × 32B)
    _, cjk_data = extract_c_array(text, 'CN_FONT_16X16')
    cjk_count = len(cjk_data)

    # 构建 bin: Header(32B) + ASCII(95×16=1520B) + CJK(N×32B)
    PAGE_SIZE = 256
    FONT_MAGIC = 0x574B
    FONT_CJK_BASE_UNICODE = 0x4E00
    FONT_CJK_COUNT = 20902

    header = bytearray(32)
    struct.pack_into('<H', header, 0, FONT_MAGIC)     # magic
    struct.pack_into('<H', header, 2, 1)              # version
    struct.pack_into('<I', header, 4, 0)              # CRC32 占位
    struct.pack_into('<I', header, 8, 1520)           # ASCII_Size
    struct.pack_into('<I', header, 12, FONT_CJK_BASE_UNICODE)  # CJK_Base
    struct.pack_into('<I', header, 16, FONT_CJK_COUNT)         # CJK_Count
    # header[20:32] = zeros

    bin_data = bytearray()
    bin_data.extend(header)  # 32B

    # ASCII: 从 offset 0x000020 开始
    ascii_offset = 0x20
    padding = ascii_offset - len(bin_data)
    if padding > 0:
        bin_data.extend(b'\x00' * padding)
    for g in ascii_data[:95]:
        bin_data.extend(g)  # 每字 16B

    # CJK: 从 offset 0x000700 开始
    cjk_base = 0x700
    padding = cjk_base - len(bin_data)
    if padding > 0:
        bin_data.extend(b'\x00' * padding)
    for g in cjk_data:
        bin_data.extend(g)  # 每字 32B

    # 补齐到 PAGE_SIZE 的整数倍
    remainder = len(bin_data) % PAGE_SIZE
    if remainder:
        bin_data.extend(b'\x00' * (PAGE_SIZE - remainder))

    total_pages = len(bin_data) // PAGE_SIZE
    print(f"[Build] ASCII x95 + CJK x{cjk_count} → {len(bin_data)}B ({total_pages} pages)")
    return bytes(bin_data), total_pages


# ═══════════════════════════════════════════════════════════════
#  OTA 推送协议
# ═══════════════════════════════════════════════════════════════

class OTAFontPusher:
    def __init__(self, ip, port=8266, timeout=5.0):
        self.ip = ip
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self):
        """TCP 连接 ESP8266 WiFiServer"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        try:
            self.sock.connect((self.ip, self.port))
            print(f"[Connect] TCP {self.ip}:{self.port} OK")
            return True
        except (socket.timeout, ConnectionRefusedError, OSError) as e:
            print(f"[Error] Cannot connect to ESP8266: {e}")
            print("[Hint] 确认 PC 和 ESP8266 在同一 WiFi 下")
            return False

    def send_frame(self, frame_str):
        """发送一行帧 (末尾自动加 \\n)"""
        self.sock.sendall((frame_str + '\n').encode('ascii'))

    def recv_line(self):
        """接收一行 ACK (阻塞, 带超时)"""
        data = b''
        while len(data) < 512:
            try:
                ch = self.sock.recv(1)
            except socket.timeout:
                if not data:
                    return None
                break
            if not ch:
                break
            if ch == b'\n':
                break
            if ch != b'\r':
                data += ch
        return data.decode('ascii', errors='replace').strip()

    def push(self, bin_data, total_pages):
        """主推送流程"""
        PAGE_SIZE = 256

        # Step 1: 等待 STM32 就绪
        reply = self.recv_line()
        if not reply or 'OTA:READY' not in reply:
            print(f"[Error] STM32 not ready (reply: {reply})")
            return False

        # Step 2: 逐页推送
        ok_pages = 0
        err_pages = 0
        for seq in range(total_pages):
            offset = seq * PAGE_SIZE
            page_data = bin_data[offset:offset + PAGE_SIZE]
            b64 = base64.b64encode(page_data).decode('ascii')
            frame = f"OTA:{seq},{b64}"
            self.send_frame(frame)

            # 等待 ACK
            for retry in range(3):
                reply = self.recv_line()
                if reply and f'OTA:ACK:{seq}' in reply:
                    ok_pages += 1
                    break
                else:
                    if retry < 2:
                        print(f"  [Retry] seq={seq} (attempt {retry+2}/3)")
                        self.send_frame(frame)  # 重发
                    else:
                        err_pages += 1
                        print(f"  [Fail] seq={seq}: {reply}")

            if (seq + 1) % 4 == 0 or seq == total_pages - 1:
                print(f"  [{seq+1}/{total_pages}] ok={ok_pages} err={err_pages}")

        if err_pages > 0:
            print(f"[Error] {err_pages} pages failed — aborting")
            return False

        # Step 3: 触发 CRC32 校验
        self.send_frame("OTA:END")
        reply = self.recv_line()
        if reply and 'OTA:DONE' in reply:
            print(f"[Done] Font updated successfully ({total_pages} pages, {ok_pages} ok)")
            return True
        else:
            print(f"[Fail] Final verify failed: {reply}")
            return False

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


# ═══════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description='ESP8266 OTA Font Push Tool — Phase A (4KB)')
    parser.add_argument('--font-data',
                        default='../../Keil_Project/Hardware/TFT_Font_Data.h',
                        help='Path to TFT_Font_Data.h')
    parser.add_argument('--ip', required=True,
                        help='ESP8266 IP address (required)')
    parser.add_argument('--port', type=int, default=8266,
                        help='ESP8266 WiFiServer port (default: 8266)')
    args = parser.parse_args()

    # 解析字体
    font_path = os.path.join(os.path.dirname(__file__), args.font_data)
    font_path = os.path.normpath(font_path)
    if not os.path.exists(font_path):
        print(f"[Error] Font data file not found: {font_path}")
        sys.exit(1)

    print(f"[Font] Parsing: {font_path}")
    try:
        bin_data, total_pages = build_font_bin(font_path)
    except Exception as e:
        print(f"[Error] Failed to parse font data: {e}")
        sys.exit(1)

    # 推送
    pusher = OTAFontPusher(args.ip, args.port)
    if not pusher.connect():
        sys.exit(1)

    try:
        ok = pusher.push(bin_data, total_pages)
        sys.exit(0 if ok else 1)
    finally:
        pusher.close()


if __name__ == '__main__':
    main()
```

---

### Task 6: 集成验证

**Files:** 无新建, 验证编译+协议通路

- [ ] **Step 1: Keil 全编译**

```
Keil F7 Rebuild all
Expected: 0 Errors, 0 Warnings
```

- [ ] **Step 2: Arduino IDE 编译**

```
Arduino IDE → Verify
Expected: 0 Errors
Board: Generic ESP8266 Module, Flash 1M, 80MHz
```

- [ ] **Step 3: Python 脚本语法检查**

```bash
cd Claude_Files/tools
python ota_font_push.py --help
Expected: Usage text displayed
```

- [ ] **Step 4: 完整烧录 + OTA 推送测试**

```bash
# 1. Keil F8 烧录 STM32
# 2. Arduino IDE 上传 ESP8266
# 3. 等待设备 ONLINE
# 4. PC 运行推送工具
python Claude_Files/tools/ota_font_push.py --ip <ESP_IP>
# Expected: TFT 显示 OTA 完成, 字库正常渲染
```

- [ ] **Step 5: 验证字库完整性**

推送完成后观察 TFT 屏幕：
- 主菜单中文标题正常显示
- 各页面中文字符正确
- 无花屏/乱码/黑块

---

### Task 7: 文档更新

**Files:**
- Modify: `CLAUDE.md` — 审查历史追加 + 文件结构更新
- Modify: `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` — 新增 OTA 操作步骤
- Update: `memory/ota-font-push-phase-b.md` — 追加 Phase A 完成标记

- [ ] **Step 1: 更新 CLAUDE.md 审查历史**

在审查历史表追加:
```
| V4.3.1 | ESP8266 WiFiServer OTA 字库推送: Base64编码+TCP透传+IDLE门控, PC端Python推送工具, Phase A 4KB字库无线更新 |
```

- [ ] **Step 2: 追加执行教训到技能文件**

在 `Claude_Files/docs/embedded-architect-system-prompt.md` 第4节追加本次教训。

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "feat: V4.3.1 ESP8266 WiFiServer OTA font push (Phase A)"
git push origin 4.0TFT
```

---

## 实施顺序建议

```
Task 1 (Base64 双平台) ──→ Task 3 (STM32) ──→ Task 6 (集成) ──→ Task 7 (文档)
                         ↘ Task 4 (STM32帧分发) ↗
Task 1 (Base64 双平台) ──→ Task 2 (ESP8266)  ↗
Task 5 (PC工具) ──────────────────────────────────→ Task 6 (集成)
```

- Task 1 + Task 5 可并行（无依赖）
- Task 3 + Task 4 依赖 Task 1（Base64 解码器已在 App_Storage.c）
- Task 2 依赖 Task 1（Base64 已在 ESP8266）
- Task 6 依赖 Task 2/3/4/5 全部完成
