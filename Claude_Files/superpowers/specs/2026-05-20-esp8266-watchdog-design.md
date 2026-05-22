# ESP8266 静默看门狗 — 掉电自动关断 PWM

> **状态:** 已批准  
> **目标:** ESP8266 模块异常离线时（掉电/卡死/不发 CLOSED），15 秒内自动关断逆变器 PWM 输出

## 问题

当前仅靠 ESP8266 主动发送 `CLOSED` 字符串来检测 TCP 断开。以下场景不会触发：

- ESP8266 模块突然掉电（VCC 断开）→ USART2 静默，MCU 不知道
- ESP8266 固件卡死 → 不发任何数据

此时 `s_WiFiConnected=1`，PWM 继续输出，系统失去远程关断能力。

## 设计

ESP8266 每收到 1 字节就记录 SysTick 时间戳。`App_Net_Task` 在联网状态下检查：超过 15 秒没收任何数据 → 判定离线 → 关 PWM + 复位 WiFi 状态。

### 改动文件

| 文件 | 改动 | 行数 |
|:---|:---|:---|
| `Hardware/ESP8266.h` | 新增 `ESP8266_SILENT_TIMEOUT` 宏 + `ESP8266_GetLastRxTime()` 声明 | +3 |
| `Hardware/ESP8266.c` | `s_LastRxTick` 变量, 在 `RxChar`/`Init` 中更新时间戳, 新增 getter | +8 |
| `User/App_Net.c` | `App_Net_Task` 中新增静默超时分支 | +5 |

### 超时阈值: 15 秒

- 与现有 `ESP8266_WIFI_TIMEOUT`（15s）一致
- PC 端 NetAssist 正常使用时会发 TCP ACK/keepalive，不会误触发
- 15 秒内无人干预的 SS_DONE 状态本就该停

### 场景覆盖

| 场景 | USART2 行为 | 触发机制 | PWM 结果 | 已有/新增 |
|:---|:---|:---|:---|:---|
| TCP 正常断开 | 收到 `CLOSED` | 立即 `Inverter_SoftStart_Stop()` | 关 | 已有 |
| ESP8266 掉电 | 静默 | 15s 超时 | 关 | **新增** |
| ESP8266 卡死 | 静默/乱码 | 15s 超时 | 关 | **新增** |
| STM32 掉电后上电 | — | `PWM_Init(MOE=OFF)` | 关 | 已有 |
| PC 长期不发指令（空闲） | TCP keepalive 字节 | 不超时 | 保持 | 无影响 |

### 接口

```c
// ESP8266.h
#define ESP8266_SILENT_TIMEOUT  15000   // ms, 静默判定离线阈值
uint32_t ESP8266_GetLastRxTime(void);   // 返回最后收到字节的 SysTick

// App_Net_Task 内新增
if (s_WiFiConnected && (SysTimer_GetTick() - ESP8266_GetLastRxTime() > ESP8266_SILENT_TIMEOUT)) {
    Inverter_SoftStart_Stop();
    s_WiFiConnected = 0;
}
```

### 约束

- 不新增依赖，不修改现有接口签名
- 时间戳记录在 `ESP8266_RxChar` 内（ISR 上下文），仅写一个 `uint32_t`，无临界区问题
- `App_Net_Task` 检查是主循环轮询，遵循现有 `SysTimer_GetTick() - last` 差值模式
