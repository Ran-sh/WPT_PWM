# 巴法云 TCP 创客云接入 (V3.4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `App_Net.c`/`App_Net.h` from LAN raw-TCP protocol to Bemfa Cloud TCP IoT protocol, with two-path subscription injection and cmd=2 telemetry envelope.

**Architecture:** Config macros live in `App_Net.h` for single-file branch differentiation. A new `Bemfa_Subscribe()` static helper is called from both blocking (`App_Net_Init`) and non-blocking (`App_Net_Connect_Task:on_success`) connection paths. Telemetry wraps bare JSON in `cmd=2&uid=...&topic=...&msg=` envelope at 2000ms interval. Silent watchdog removed (Bemfa is silent by default). Command parsing unchanged.

**Tech Stack:** STM32F103C8 SPL V3.5.0, ESP8266 AT command set, ARMCC V5.06, SysTimer timestamp-diff scheduling.

---

### Task 1: Fix App_Net.h — Remove stale BEMFA_SILENT_TIMEOUT

**Files:**
- Modify: `User/App_Net.h:23`

Design chose B (delete watchdog entirely), so remove the leftover macro.

- [ ] **Step 1: Delete BEMFA_SILENT_TIMEOUT line**

```c
// Delete line 23:
#define BEMFA_SILENT_TIMEOUT  120000            /* 云端静默超时 120s ... */
```

Expected result: `App_Net.h` has 6 config macros (WIFI_SSID, WIFI_PASSWORD, SERVER_IP, SERVER_PORT, BEMFA_UID, BEMFA_TOPIC) and no timeout macro.

- [ ] **Step 2: Commit**

```bash
git add User/App_Net.h
git commit -m "fix: remove stale BEMFA_SILENT_TIMEOUT — watchdog will be deleted per V3.4 design"
```

---

### Task 2: Add Bemfa_Subscribe() static helper

**Files:**
- Modify: `User/App_Net.c` — insert after `Net_Remote_Off()` (line 94), before public interface section (line 96)

- [ ] **Step 1: Insert Bemfa_Subscribe function**

Insert these 8 lines after line 94 (`}` closing `Net_Remote_Off`), before the public interface comment block:

```c

/**
 * @brief  巴法云订阅 — 透传通道就绪后发送 cmd=1 订阅主题
 * @note   必须在 s_WiFiConnected=1 之后调用 (ESP8266_SendString 依赖已初始化的 USART2)
 */
static void Bemfa_Subscribe(void)
{
    char subBuf[64];
    snprintf(subBuf, sizeof(subBuf),
             "cmd=1&uid=%s&topic=%s\r\n", BEMFA_UID, BEMFA_TOPIC);
    ESP8266_SendString(subBuf);
}
```

- [ ] **Step 2: Commit**

```bash
git add User/App_Net.c
git commit -m "feat: add Bemfa_Subscribe() helper for cmd=1 topic subscription"
```

---

### Task 3: Replace inline subscription in App_Net_Init with Bemfa_Subscribe()

**Files:**
- Modify: `User/App_Net.c:162-168`

The existing inline subscription block (lines 162-168) was a draft edit. Replace with a clean call.

- [ ] **Step 1: Replace inline block**

Replace lines 162-168:
```c
    /* V3.4: 巴法云订阅 — 透传通道就绪后立即发送 cmd=1 订阅主题 */
    {
        char subBuf[64];
        snprintf(subBuf, sizeof(subBuf),
                 "cmd=1&uid=%s&topic=%s\r\n", BEMFA_UID, BEMFA_TOPIC);
        ESP8266_SendString(subBuf);
    }

    return 0;
```

With:
```c
    /* V3.4: 透传通道就绪 → 巴法云订阅主题 */
    Bemfa_Subscribe();

    return 0;
```

- [ ] **Step 2: Commit**

```bash
git add User/App_Net.c
git commit -m "refactor: use Bemfa_Subscribe() helper in App_Net_Init blocking path"
```

---

### Task 4: Inject Bemfa_Subscribe() in App_Net_Connect_Task on_success

**Files:**
- Modify: `User/App_Net.c:399-405` (`on_success` label block)

- [ ] **Step 1: Add Bemfa_Subscribe call after s_WiFiConnected**

Replace:
```c
on_success:
        ESP8266_SetWaitCallback(NULL);
        s_WiFiConnected      = 1;
        s_WiFiConnected = 1;
        LED_Update_WiFi(LED_OFF);
        return;
```

With:
```c
on_success:
        ESP8266_SetWaitCallback(NULL);
        s_WiFiConnected      = 1;
        s_WiFiConnected = 1;
        Bemfa_Subscribe();   /* V3.4: 透传通道就绪 → 巴法云订阅主题 */
        LED_Update_WiFi(LED_OFF);
        return;
```

- [ ] **Step 2: Commit**

```bash
git add User/App_Net.c
git commit -m "feat: inject Bemfa_Subscribe() in non-blocking connect path on_success"
```

---

### Task 5: Delete silent watchdog

**Files:**
- Modify: `User/App_Net.c:183-188`

- [ ] **Step 1: Remove silent watchdog block**

Delete lines 183-188:
```c
    /* ── ESP8266 静默看门狗: 120s 无数据 → 判定离线 → 关 PWM (巴法云长静默适配) ── */
    if (SysTimer_GetTick() - ESP8266_GetLastRxTime() > BEMFA_SILENT_TIMEOUT) {
        Inverter_SoftStart_Stop();
        s_WiFiConnected = 0;
        return;
    }
```

Also delete the blank line after it so there's exactly one blank line before the telemetry sub-function.

- [ ] **Step 2: Commit**

```bash
git add User/App_Net.c
git commit -m "feat: remove silent watchdog — Bemfa cloud is silent by default, rely on CLOSED frame only"
```

---

### Task 6: Update telemetry — interval 2000ms + Bemfa cmd=2 envelope

**Files:**
- Modify: `User/App_Net.c:190-211`

- [ ] **Step 1: Change interval from 1000 to 2000**

Line 194:
```c
// Old: if (SysTimer_GetTick() - last_telemetry >= 1000)
// New:
        if (SysTimer_GetTick() - last_telemetry >= 2000)
```

- [ ] **Step 2: Update snprintf to Bemfa cmd=2 format**

Replace lines 204-209:
```c
                char jsonBuf[128];
                snprintf(jsonBuf, sizeof(jsonBuf),
                        "{\"V\":%.2f,\"I\":%.2f,\"F\":%lu}\r\n",
                        Get_Real_Voltage(),
                        Get_Real_Current(),
                        (unsigned long)PWM_GetFrequency());
                ESP8266_SendString(jsonBuf);
```

With:
```c
                char jsonBuf[160];
                snprintf(jsonBuf, sizeof(jsonBuf),
                        "cmd=2&uid=%s&topic=%s&msg={\"V\":%.2f,\"I\":%.2f,\"F\":%lu}\r\n",
                        BEMFA_UID, BEMFA_TOPIC,
                        Get_Real_Voltage(),
                        Get_Real_Current(),
                        (unsigned long)PWM_GetFrequency());
                ESP8266_SendString(jsonBuf);
```

Note: buffer size 128 → 160 to accommodate the ~80 extra chars from the Bemfa envelope.

- [ ] **Step 3: Update @note in function header comment**

Lines 176-177:
```c
 *          - JSON 遥测: 每 1000ms 执行一次, 格式 {"V":电压,"I":电流,"F":频率}
```
Replace with:
```c
 *          - JSON 遥测: 每 2000ms 执行一次, 巴法云 cmd=2 信封格式
```

- [ ] **Step 4: Commit**

```bash
git add User/App_Net.c
git commit -m "feat: Bemfa cmd=2 telemetry envelope, 2000ms interval (1Hz rate limit)"
```

---

### Task 7: Update file header — V3.4 version tag

**Files:**
- Modify: `User/App_Net.c:1-23` (file header comment block)
- Modify: `User/App_Net.h:1-6` (file header comment block)

- [ ] **Step 1: Update App_Net.c header**

Change line 7 from:
```c
 *          模块职责:
 *            1. 管理 WiFi / TCP 服务端连接参数 (通过宏配置)
 *            2. App_Net_Init() — 阻塞式联网初始化
 *               先调用 ESP8266_Init() 配置 USART2 硬件,
 *               再调用 ESP8266_ConnectToServer() 执行 AT 联网状态机。
 *               若联网失败则冻结在 OLED 错误码页面。
 *            3. App_Net_Task() — 非阻塞周期任务 (由 main.c 主循环高频调用)
 *               - 每 1000ms: 采集电压/频率 → 封装 JSON → ESP8266_SendString 发送
 *               - 实时轮询: 检查 ESP8266 接收标志 → 解析 ON/OFF 指令 → 执行控制
```

To:
```c
 *          模块职责:
 *            1. 管理 WiFi / 巴法云 TCP 连接参数 (宏配置在 App_Net.h)
 *            2. App_Net_Init() — 阻塞式联网初始化 + 巴法云订阅
 *            3. App_Net_Task() — 非阻塞周期任务 (V3.4 巴法云协议)
 *               - 每 2000ms: 采集电压/频率 → cmd=2 信封 → ESP8266_SendString
 *               - 实时轮询: 解析 CMD:ON/CMD:OFF 指令 (兼容巴法云 cmd=2 下发)
```

Add V3.4 note to line 3-5 area. Insert after `@note` line 2:
```c
 * @note    V3.4: 巴法云 TCP 创客云接入 — cmd=1 订阅 + cmd=2 遥测信封 + 删静默看门狗
```

- [ ] **Step 2: Update App_Net.h header**

Change line 5 from:
```c
 * @note    V3.2: App_Net_Connect_Task 非阻塞联网状态机替代阻塞 App_Net_Init
```
To:
```c
 * @note    V3.4: 巴法云 TCP 创客云接入, 配置宏从 .c 移至 .h 实现分支差异化
```

- [ ] **Step 3: Commit**

```bash
git add User/App_Net.c User/App_Net.h
git commit -m "docs: bump version tags to V3.4 Bemfa Cloud"
```

---

### Task 8: Final verification

- [ ] **Step 1: Review diff against design spec**

```bash
git diff HEAD~7..HEAD -- User/App_Net.h User/App_Net.c
```

Verify:
- No function names modified ✓
- No variable names modified ✓
- `Bemfa_Subscribe()` is the only new identifier ✓
- Silent watchdog deleted ✓
- Telemetry: 2000ms + cmd=2 format ✓
- Both paths call `Bemfa_Subscribe()` after `s_WiFiConnected=1` ✓
- 6 config macros in App_Net.h ✓

- [ ] **Step 2: Check for compilation issues (dry run)**

Verify `#include` chain: `App_Net.c` includes `App_Net.h` → `BEMFA_UID`/`BEMFA_TOPIC` visible. `ESP8266.h` includes `stm32f10x.h` → `uint8_t`/`uint32_t` visible.

- [ ] **Step 3: Push to WAN branch when ready**

```bash
git push origin WAN
```
