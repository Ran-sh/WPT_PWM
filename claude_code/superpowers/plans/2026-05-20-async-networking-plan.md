# 非阻塞异步联网状态机 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development

**Goal:** 阻塞 App_Net_Init → 非阻塞 App_Net_Connect_Task 状态机，联网期间主循环全功能响应，支持 KEY1 取消、失败自动重试 3 次。

**Architecture:** App_Net.c 新增 9 态 FSM + Trigger/Cancel/Task/GetState/GetErrorCode。每步分"发送指令"和"轮询响应"两阶段，时间戳差值法判超时。UI.c 联网路径改为 Trigger + Task 驱动。

**Tech Stack:** STM32F103 SPL V3.5.0

---

## 文件结构

| 文件 | 操作 |
|:---|:---|
| `User/App_Net.h` | 修改 — NetState_t + 5 函数声明 |
| `User/App_Net.c` | 修改 — 状态机变量 + Trigger/Cancel/Task + CIPSEND 特殊处理 |
| `Hardware/UI.c` | 修改 — 联网触发改为非阻塞 |
| `User/main.c` | 修改 — 主循环加 App_Net_Connect_Task |

---

### Task 1: App_Net.h — API 声明

**Files:** Modify `User/App_Net.h`

- [ ] **Step 1: 替换文件**

```c
#ifndef __APP_NET_H
#define __APP_NET_H

#include "stm32f10x.h"

typedef enum {
    NET_IDLE = 0,
    NET_STEP_AT,  NET_STEP_CWMODE, NET_STEP_CWJAP,
    NET_STEP_CIPSTART, NET_STEP_CIPMODE, NET_STEP_CIPSEND,
    NET_SUCCESS, NET_FAIL
} NetState_t;

uint8_t    App_Net_Init(void);
void       App_Net_Task(void);
uint8_t    App_Net_IsConnected(void);

void        App_Net_Connect_Trigger(void);
void        App_Net_Connect_Cancel(void);
void        App_Net_Connect_Task(void);
NetState_t  App_Net_GetConnectState(void);
uint8_t     App_Net_GetErrorCode(void);

#endif
```

---

### Task 2: App_Net.c — 状态机实现

**Files:** Modify `User/App_Net.c`

- [ ] **Step 1: 在 `s_WiFiConnected` 后追加状态机私有变量**

```c
/* V3.2 非阻塞联网 */
static NetState_t s_net_state  = NET_IDLE;
static uint8_t    s_net_retry  = 0;
static uint32_t   s_net_tstart = 0;
static uint8_t    s_net_error  = 0;
static uint8_t    s_net_cancel = 0;
static uint8_t    s_net_sending = 0;   /* 1=需要发送指令, 0=等待响应 */
static char       s_net_cmdbuf[128];
```

- [ ] **Step 2: 追加 5 个公开函数到文件末尾**

```c
void App_Net_Connect_Trigger(void)
{
    if (s_net_state != NET_IDLE) return;

    Inverter_SoftStart_Stop();
    ESP8266_Init();

    s_net_state   = NET_STEP_AT;
    s_net_retry   = 0;
    s_net_cancel  = 0;
    s_net_error   = 0;
    s_net_sending = 1;              /* 首次发送指令 */
    s_net_tstart  = SysTimer_GetTick();

    ESP8266_SetWaitCallback(AT_DotAnim);
}

void App_Net_Connect_Cancel(void)
{
    if (s_net_state > NET_IDLE && s_net_state < NET_SUCCESS)
        s_net_cancel = 1;
}

NetState_t App_Net_GetConnectState(void) { return s_net_state; }
uint8_t    App_Net_GetErrorCode(void)    { return s_net_error; }

void App_Net_Connect_Task(void)
{
    /*
     * 每步两阶段:
     *   阶段 A (sending=1): 发送 AT 指令 → sending=0
     *   阶段 B (sending=0): 轮询响应 / 超时判断
     */

    if (s_net_state <= NET_IDLE || s_net_state >= NET_SUCCESS)
        return;

    /* ── KEY1 取消 ── */
    if (s_net_cancel) {
        s_net_cancel = 0;
        s_net_state  = NET_IDLE;
        ESP8266_SetWaitCallback(NULL);
        return;
    }

    /* ═══════ 阶段 A: 发送指令 ═══════ */
    if (s_net_sending) {
        s_net_sending = 0;
        s_net_tstart  = SysTimer_GetTick();
        ESP8266_ClearRxBuffer();

        switch (s_net_state) {
            case NET_STEP_AT:
                ESP8266_SendString("AT\r\n");           break;
            case NET_STEP_CWMODE:
                ESP8266_SendString("AT+CWMODE=1\r\n");  break;
            case NET_STEP_CWJAP:
                snprintf(s_net_cmdbuf, sizeof(s_net_cmdbuf),
                    "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
                ESP8266_SendString(s_net_cmdbuf);       break;
            case NET_STEP_CIPSTART:
                snprintf(s_net_cmdbuf, sizeof(s_net_cmdbuf),
                    "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", SERVER_IP, SERVER_PORT);
                ESP8266_SendString(s_net_cmdbuf);       break;
            case NET_STEP_CIPMODE:
                ESP8266_SendString("AT+CIPMODE=1\r\n"); break;
            case NET_STEP_CIPSEND:
                ESP8266_SendString("AT+CIPSEND\r\n");   break;
            default: break;
        }
        return;
    }

    /* ═══════ 阶段 B: 轮询响应 ═══════ */

    /* CIPSEND 特殊处理: 应答是 ">" 不带 \r\n, 不触发帧标志 */
    if (s_net_state == NET_STEP_CIPSEND) {
        char c;
        USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
        c = (strstr(s_RxBuf, ">") != NULL);
        USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
        if (c) { s_net_state = NET_SUCCESS; goto on_success; }
        /* 超时? */
        if (SysTimer_GetTick() - s_net_tstart < ESP8266_CMD_TIMEOUT) return;
        goto on_timeout;
    }

    /* 其余步骤: 等帧标志 */
    if (!ESP8266_GetRxFlag()) {
        /* 超时检查 */
        uint16_t tmo = (s_net_state == NET_STEP_CWJAP)  ? ESP8266_WIFI_TIMEOUT
                     : (s_net_state == NET_STEP_CIPSTART) ? ESP8266_TCP_TIMEOUT
                     :                                      ESP8266_CMD_TIMEOUT;
        if (SysTimer_GetTick() - s_net_tstart < tmo) return;

on_timeout:
        s_net_retry++;
        if (s_net_retry >= 3) {
            s_net_error = (uint8_t)(s_net_state - NET_STEP_AT + 1);
            s_net_state = NET_FAIL;
            ESP8266_SetWaitCallback(NULL);
            OLED_Clear();
            OLED_ShowString(1, 1, "!!! WiFi Error !!!");
            OLED_ShowString(2, 1, "Err Code:");
            OLED_ShowNum(2, 11, s_net_error, 1);
            LED_Update_WiFi(LED_OFF);
            return;
        }
        s_net_sending = 1;  /* 重发当前步指令 */
        return;
    }

    /* 有响应: 检查 ERROR/OK */
    {
        char localBuf[64];
        uint8_t has_err, has_ok;

        ESP8266_CopyRxFrame(localBuf, sizeof(localBuf));
        has_err = (strstr(localBuf, "ERROR") || strstr(localBuf, "FAIL"));
        has_ok  = (strstr(localBuf, "OK") != NULL);

        if (has_err) {
            s_net_retry++;
            if (s_net_retry >= 3) {
                s_net_error = (uint8_t)(s_net_state - NET_STEP_AT + 1);
                s_net_state = NET_FAIL;
                ESP8266_SetWaitCallback(NULL);
                OLED_Clear();
                OLED_ShowString(1, 1, "!!! WiFi Error !!!");
                OLED_ShowString(2, 1, "Err Code:");
                OLED_ShowNum(2, 11, s_net_error, 1);
                LED_Update_WiFi(LED_OFF);
                return;
            }
            s_net_sending = 1;  /* 重试 */
            return;
        }

        if (!has_ok) return;  /* 等完整帧 */

        /* OK → 下一步 */
        s_net_retry = 0;
        s_net_state = (NetState_t)((uint8_t)s_net_state + 1);

        if (s_net_state == NET_SUCCESS) {
on_success:
            ESP8266_SetWaitCallback(NULL);
            s_NetReady      = 1;
            s_WiFiConnected = 1;
            OLED_Clear();
            OLED_ShowString(1, 1, "WiFi Connected!");
            OLED_ShowString(2, 1, "TCP: OK");
            OLED_ShowString(3, 1, "Port:");
            OLED_ShowNum(3, 6, SERVER_PORT, 5);
            SysTimer_DelayMs(2000);
            OLED_Clear();
            LED_Update_WiFi(LED_OFF);
            return;
        }
        s_net_sending = 1;  /* 发下一步指令 */
    }
}
```

> **注意**: CIPSEND 步访问了 `s_RxBuf`（ESP8266 内部变量）。需要在 ESP8266.c 中暴露 `extern` 或将此步改为调用 `ESP8266_GetRxBuffer()` + `strstr` 的模式。采用后者——用 `ESP8266_GetRxBuffer()` 替代直接访问 `s_RxBuf`。修正：
> ```c
> const char *rx = ESP8266_GetRxBuffer();
> USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
> c = (strstr(rx, ">") != NULL);
> USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
> ```

---

### Task 3: UI.c — 联网触发改为非阻塞

**Files:** Modify `Hardware/UI.c`

- [ ] **Step 1: `UI_TryConnectWiFi` 函数替换为简单触发**

将 `UI_TryConnectWiFi` 函数体替换为非阻塞触发:

```c
static void UI_TryConnectWiFi(void)
{
    /* 非阻塞触发: 联网由 App_Net_Connect_Task 推进 */
    App_Net_Connect_Trigger();
    LED_Update_WiFi(LED_SOLID);
    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi Connecting ");
}
```

- [ ] **Step 2: UI_Task 中处理联网中/成功/失败状态**

在 `UI_Task` 的 `UI_Page == 0` 分支中，`!App_Net_IsConnected()` 块改为:

```c
if (!App_Net_IsConnected()) {
    NetState_t ns = App_Net_GetConnectState();

    if (ns > NET_IDLE && ns < NET_SUCCESS) {
        /* 联网进行中: KEY1 取消 */
        if (key1 == 1) App_Net_Connect_Cancel();
        if (need_refresh) {
            OLED_ShowString(1, 1, "[Control Mode] ");
            OLED_ShowString(2, 1, "WiFi Connecting ");
            OLED_ShowString(3, 1, "KEY1: Cancel    ");
        }
    } else if (ns == NET_FAIL) {
        /* 失败显示 3s 后自动清除 (由 App_Net_Connect_Task 设置 NET_IDLE) */
        if (need_refresh) { /* 保持错误信息, 等 3s */ }
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
}
```

**NET_FAIL 自动恢复**: 在 `App_Net_Connect_Task` 中进入 NET_FAIL 后，用时间戳等待 3s 后自动转为 NET_IDLE:

在 `App_Net_Connect_Task` 开头增加:
```c
if (s_net_state == NET_FAIL) {
    if (SysTimer_GetTick() - s_net_tstart >= 3000) {
        s_net_state = NET_IDLE;
        OLED_Clear();
    }
    return;
}
```

---

### Task 4: main.c — 主循环加 Connect_Task

**Files:** Modify `User/main.c`

- [ ] **Step 1: 主循环加入 `App_Net_Connect_Task`**

```c
while (1)
{
    KEY_Task();
    ADC_Filter_Task();
    UI_Task();
    App_Net_Task();
    App_Net_Connect_Task();      /* 非阻塞联网步进 */
    Inverter_SoftStart_Task();
    LED_Task();
}
```

---

### Task 5: 编译验证

- [ ] **Step 1: Keil 编译, 预期 0 errors 0 warnings**
- [ ] **Step 2: 烧录测试**
  - 上电 → 按 KEY0 → "WiFi Connecting" + KEY1:Cancel 提示
  - 联网期间 PB3 常亮, 点动画持续, 按 KEY1 取消回到待机
  - 成功 → "WiFi Connected!" 2s → IDLE 待机
  - 失败 → 错误码 3s → 自动回待机, 再按 KEY0 可重试
