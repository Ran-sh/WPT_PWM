# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **版本** | V8 |
| **语言** | 中文交流，代码注释中英混合 |

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06
- **MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only
- **Project File**: `Keil_Project/Project.uvprojx`
- 无 CLI 编译 — Keil IDE 中 F7 编译 → F8 下载
- ARMCC V5 不支持 `--multibyte_chars`，UTF-8 中文必须用 hex escape (`\xE6\x97\xA0...`)
- 字符串拼接如 `"\xe5\x8f\x8c\xe5\x87\xbb" "Back"` 可避免 ARMCC #27-D 警告

## Architecture

### Dual-MCU

STM32 ↔ ESP8266 通过 USART2 115200 纯文本 JSON 通信，零 AT 指令。STM32 绝不发 AT 指令，ESP8266 绝不碰 PWM/ADC。

```
STM32 发:  {"V":12.50,"I":1.23,"F":100000}
ESP8266 收: CMD:ON\n / CMD:OFF\n / CMD:SETFREQ:100000\n / STATUS:ONLINE
```

### Pin Mapping

| Pin | 功能 | Pin | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | ADC_CH8 (电流) |
| PA1 | ESP8266 RST | PB1 | ADC_CH9 (电压) |
| PA2 | USART2_TX | PB3 | LED_PWM |
| PA3 | USART2_RX | PB4 | LED_WIFI |
| PA4 | TFT_CS | PB5 | PAGE 按键 |
| PA5 | SPI1_SCK | PB6 | TFT 背光 TIM4_CH1 |
| PA6 | TFT_DC | PB7 | F_DOWN 按键 |
| PA7 | SPI1_MOSI | PB8 | F_UP 按键 |
| PA8 | TIM1_CH1 | PB9 | ON/OFF 按键 |
| PA9 | TIM1_CH2 | PB10 | PowerContrl |
| PA10 | LED_COM | PB11 | ESP8266 CH_PD |
| PA11 | LED_POWER | PB13 | TIM1_CH1N |
| PA12 | LED_TEMP | PB14 | TIM1_CH2N |
| PA15 | LED_SYSTEM | PB15 | 蜂鸣器 |

TIM1 默认映射（不执行重映射），SPI1 默认映射。JTAG 禁用释放 PB3/PB4/PB5/PA15。

### 模块分层

```
Application: App_Network, Ui_Controller, Inverter_Control
System:      Sys_Timer (SysTick 1ms + DWT)
Hardware:    Tft_Driver, Pwm_Driver, Adc_Driver, Key_Driver,
             Led_Driver, Buzzer_Driver, Esp8266_Driver
```

依赖方向: Hardware → System → Application，严格单向。

## TFT 驱动 (ST7735 Green Tab, 已验证)

| 参数 | 值 |
|:---|:---|
| SPI | Mode 3 (CPOL=High, CPHA=2Edge), 18MHz |
| 分辨率 | 160×128 横屏 |
| MADCTL | **0xA0** (MY=1,MX=0,MV=1) |
| SetWin 偏移 | X+1, Y+2 (横屏) |
| 背光 | PB6, TIM4_CH1 PWM 1kHz (**APB1**) |
| 字库 | ASCII LSB-first (`0x01<<b`) + CN 73字 LSB-first (`0x01<<bit`) |
| 初始化 | 硬件复位 → SLPOUT → FRMCTR → PWCTR → GAMMA → COLMOD → DISPON (无 SWRESET) |

## PWM Baseline (不可改)

- TIM_CounterMode_Up, CH1=PWM1/CH2=PWM2, OCNPolarity_Low, OCNIdleState_Set
- 死区 1000ns, 频率 95~150kHz, 软启动 150k→100kHz, 200Hz/10ms

## 编码规范

| 规则 | 说明 |
|:---|:---|
| 命名 | `Module_Name_Verb_Noun()` PascalCase+下划线，枚举 `MODULE_VALUE` |
| 状态机 | `typedef enum` + 单一状态变量 |
| 调度 | `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值 |
| 临界区 | PRIMASK 保存/恢复 |
| 分层 | `.h` 只放公开接口，`static` 保护私有函数 |

## UI 状态机 (V8)

7 态: INIT → CONNECTING → FAILED → READY → SWEEPING → RUNNING → FAULT

子页: s_page=0 综合/扫频, 1 频率表, 2 电压表, 3 电流表

### 按键功能

| 按键 | 事件 | 功能 |
|:---|:---|:---|
| ON/OFF | 单击 | READY→启动扫频, SWEEPING/RUNNING→停止, FAULT→复位 |
| ON/OFF | 双击 | 智能WIFI切换 (未连→连, 已连→断) |
| ON/OFF | 长按 | 清除WiFi配网+进入无WIFI |
| F+ | 单击 | RUNNING状态 +1kHz |
| F- | 单击 | RUNNING状态 -1kHz |
| PAGE | 单击 | SWEEPING/RUNNING→切子页, FAILED/CONNECTING/READY→进无WIFI调试 |
| PAGE | 双击 | SWEEPING/RUNNING→回综合监测, 其余→切换无WIFI |

### WiFi 显示

所有界面WiFi状态通过 `Get_WiFi_Str()` 和 `Get_WiFi_Color()` 实时检测，三层检查: `s_no_wifi_mode` → `Esp8266_Driver_Is_Ready()` → `App_Network_Get_Connect_Status()`

## LED 规则

| LED | 逻辑 |
|:---|:---|
| SYSTEM | 500ms 心跳 |
| WiFi | 离线慢闪 / 连接中快闪 / 在线常亮 |
| PWM | 扫频快闪 / 其余灭 |
| COM | MQTT在线常亮 |
| POWER | >12V 常亮 |
| TEMP | 暂未启用灭 |

## Safety

- Fault Handler 先关断 PWM MOE
- IWDG: LSI 40kHz/64, reload=1000 → 1.6s
- PB10 PowerContrl: 上电拉低, 软启动时拉高
- 过流 5A: 每 200ms 检查 → Fault + Buzzer BEEP

## Git

```bash
git add -A && git commit -m "..." && git push origin 4.0TFT
```
