# 五灯状态指示系统

- **日期**: 2026-05-19
- **状态**: Approved
- **涉及文件**: `Hardware/LED.c`, `Hardware/LED.h`, `Hardware/UI.c`

## 引脚映射

| 引脚 | 功能 | 电平/驱动 |
|:---|:---|:---|
| PC13 | 系统心跳 | 低电平亮, 500ms翻转 |
| PB3 | WiFi状态 | 高电平亮, 推挽输出 |
| PB4 | PWM工作状态 | 高电平亮, 推挽输出 |
| PB5 | 按键可操作指示 | 高电平亮, 推挽输出 |

PB3=JTDO，需重映射为 GPIO (SWD 不受影响)。
PB4=JNTRST，需重映射为 GPIO。

## 状态表

| 状态 | PC13心跳 | PB3 WiFi | PB4 PWM | PB5 可操作 |
|:---|:---:|:---:|:---:|:---:|
| 启动中 | 慢闪1Hz | ○ | ○ | ○ |
| 待联网 | 慢闪1Hz | 慢闪1Hz | ○ | ● 常亮 |
| 联网中 | 慢闪1Hz | 快闪5Hz | ○ | ○ |
| WiFi成功瞬间 | 慢闪1Hz | 常亮2s→灭 | ○ | ● 常亮 |
| 联网失败 | 慢闪1Hz | 回慢闪1Hz | ○ | ● 常亮 |
| IDLE待机 | 慢闪1Hz | ○ | ○ | ● 常亮 |
| 扫频中 SWEEP | 慢闪1Hz | ○ | 慢闪1Hz | ○ |
| 谐振运行 DONE | 慢闪1Hz | ○ | 慢闪1Hz | ● 常亮 |

**闪烁参数:**
- 慢闪: 500ms亮 / 500ms灭 (1Hz), 与 PC13 同频
- 快闪: 100ms亮 / 100ms灭 (5Hz)

**PB5 逻辑:** 仅 IDLE / DONE / 待联网 / 失败重试 时常亮（用户可操作）。
联网中 / 扫频中熄灭（系统忙，按键操作被屏蔽或受限）。

## 实现方案

### LED.c 新增

```c
// 引脚定义
#define LED_WIFI_PORT   GPIOB
#define LED_WIFI_PIN    GPIO_Pin_3
#define LED_PWM_PORT    GPIOB
#define LED_PWM_PIN     GPIO_Pin_4
#define LED_READY_PORT  GPIOB
#define LED_READY_PIN   GPIO_Pin_5

// 状态更新函数 (由 UI_Task 调用)
void LED_Update_WiFi(led_wifi_state_t state);
void LED_Update_PWM (led_pwm_state_t  state);
void LED_Update_Ready(uint8_t on_off);

// 由 UI_Task 内部调用, 在 200ms 刷新周期内更新状态
```

式中 `led_wifi_state_t` 枚举: `LED_WIFI_OFF / LED_WIFI_SLOW / LED_WIFI_FAST / LED_WIFI_SOLID`
`led_pwm_state_t` 枚举: `LED_PWM_OFF / LED_PWM_SLOW`

### PB3/PB4 GPIO 重映射

在 LED_Init 中:
```c
GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); // JTAG→GPIO, SWD保留
```

### UI.c 调用

UI_Task 内 200ms 刷新时, 根据当前系统状态调用:
```c
// WiFi 状态
if (!wifi_connected)  LED_Update_WiFi(LED_WIFI_SLOW);
if (net_connecting)   LED_Update_WiFi(LED_WIFI_FAST);
// ...

// PWM 状态
if (ss == SS_SWEEP || ss == SS_DONE) LED_Update_PWM(LED_PWM_SLOW);
else LED_Update_PWM(LED_PWM_OFF);

// Ready
LED_Update_Ready(wifi_connected && (ss==SS_IDLE||ss==SS_DONE));
```

### 不变项

- PC13 心跳灯逻辑不变 (LED_Task 500ms)
- ESP8266 EN/PB1 不动
- KEY/PWM/USART 均不动
