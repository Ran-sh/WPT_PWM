# 非阻塞异步联网状态机

- **日期**: 2026-05-20
- **状态**: Approved
- **涉及文件**: `User/App_Net.c`, `User/App_Net.h`, `Hardware/UI.c`, `User/main.c`

## 背景

当前 `App_Net_Init()` 是阻塞调用（20~30s），从 `UI_Task` 中直接调用，期间主循环冻结：LED 停、显示停、按键无响应、PWM 软启动被卡。需改为非阻塞状态机，联网期间系统保持全功能响应。

## 设计

### 1. App_Net 新增非阻塞联网状态机

新增类型和函数（`App_Net.h`）：

```c
typedef enum {
    NET_IDLE = 0,
    NET_STEP_AT,        // Step 1: AT 测试
    NET_STEP_CWMODE,    // Step 2: STA 模式
    NET_STEP_CWJAP,     // Step 3: 连 WiFi
    NET_STEP_CIPSTART,  // Step 4: TCP 连接
    NET_STEP_CIPMODE,   // Step 5: 透传模式
    NET_STEP_CIPSEND,   // Step 6: 进入透传
    NET_SUCCESS,
    NET_FAIL
} NetState_t;

void     App_Net_Connect_Trigger(void);    // UI 触发联网
void     App_Net_Connect_Cancel(void);     // KEY1 取消联网
void     App_Net_Connect_Task(void);       // 主循环每轮调用, 非阻塞步进
NetState_t App_Net_GetConnectState(void);  // 供 UI 查询当前步骤
uint8_t    App_Net_GetErrorCode(void);      // 失败时返回错误码 1~6
```

### 2. 状态机流程

```
NET_IDLE → Trigger → NET_STEP_AT → (OK) → NET_STEP_CWMODE → ... → NET_SUCCESS
                         ↓超时                      ↓超时
                      retry<3?                 retry<3?
                      重试当前步                 重试当前步
                         ↓retry≥3                 ↓retry≥3
                      NET_FAIL                 NET_FAIL
                         ↓                        ↓
                   记录 err=1                 记录 err=2
                   UI 显示错误码             UI 显示错误码
                   3s 后回 IDLE              3s 后回 IDLE

任意步: KEY1 取消 → NET_IDLE (无错误码)
```

### 3. 主循环集成

```c
// main.c while(1):
KEY_Task();
ADC_Filter_Task();
UI_Task();
App_Net_Task();
App_Net_Connect_Task();      // 新增: 非阻塞联网步进
Inverter_SoftStart_Task();
LED_Task();
```

### 4. UI 集成

- **联网未启动**: KEY0 → `App_Net_Connect_Trigger()`（替代原 `App_Net_Init` 调用）
- **联网进行中**: KEY1 → `App_Net_Connect_Cancel()`, 立即回待机; KEY0 无效; 双击可切页
- **联网成功**: OLED 显示 "WiFi Connected!" 2s, LED PB3 常亮 2s 后灭, 进入 IDLE
- **联网失败**: OLED 显示错误码 3s, 自动回待联网界面; KEY0 可重试
- **LED**: 联网期间 PB3 常亮, PB5 灭（系统忙）, PB4 正常（PWM 状态）
- **点动画**: `AT_DotAnim` 回调继续在 WaitResponse 轮询时跳动

### 5. 实现要点

- 每步用 `static uint32_t step_start` 记录进入时间, `SysTimer_GetTick() - step_start >= timeout` 判超时
- `retry_count` 每步独立: 同一步超时重试最多 3 次
- ESP8266_CopyRxFrame 检查响应 (ERROR/FAIL/OK/expected_str)
- `static NetState_t s_net_state` + 相关变量全部 static 私有

### 不变项

- `s_NetReady` 门禁不变
- JSON 遥测格式不变
- CMD:ON/OFF 协议不变
- CLOSED 处理不变
- 死区、PWM、ADC 均不动

## 风险

| 风险 | 缓解 |
|:---|:---|
| 状态机死锁在某步 | 每步独立超时 + 全局限次重试 |
| 联网中掉电/断线 | CLOSED 检测已有, 复位 s_WiFiConnected |
| 按键/WiFi 指令并发 | Trigger 仅 NET_IDLE 时有效 |
