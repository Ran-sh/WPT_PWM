# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **协议** | OneNET MQTT 物模型 (Dual-MCU) + ST7735 1.8寸 TFT 彩屏 |
| **版本** | V6.2 |
| **语言** | 中文交流，代码注释中英混合 |

> 其他分支（不同本地目录，**本分支推送只推 `4.0TFT`**）：
> `master` (V0.0 基版), `1.0LAN` (NetAssist 局域网 V3.4), `2.0WAN` (巴法云 TCP V3.5), `3.0ONENET` (V6.1 OLED — **已锁定，不可修改**)

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only, never modified
- **Project File**: `Keil_Project/Project.uvprojx`
- 无 CLI 编译 — 在 Keil IDE GUI 中 F7 编译 → F8 下载
- **ESP8266**: Arduino IDE, board "Generic ESP8266 Module" (Flash 1M, 80MHz), 文件 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- ARMCC V5 **不支持** `--multibyte_chars` 汇编器标志，UTF-8 中文串必须用 hex escape (`\xE6\x97\xA0...`)

## Architecture: Dual-MCU

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • PWM 发波 + PFM 调功        │    │  • WiFiManager 网页配网       │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET MQTT 物模型连云     │
│  • KEY/TFT/LED 人机交互       │    │  • 串口 JSON ↔ STM32 透传    │
│  • 纯 JSON 串口透传           │    │  • Conn_State 连接状态机      │
│  • 软启动扫频 + 过流保护       │    │  • 前缀匹配防协议误触发       │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)          │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000}  │
           │◄──────────────────────────────────┤
           │  CMD:ON\n  或  CMD:OFF\n           │
           │  CMD:SETFREQ:100000\n              │
```

**Iron rule**: STM32 never sends AT commands. ESP8266 never touches PWM/ADC. Communication is pure text JSON over USART2 at 115200bps.

## V6.2 Pin Mapping (STM32F103C8 LQFP-48)

| Pin | 网络名 | 功能 | 模块 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | TFT 复位 | Tft_Driver |
| PA1 | ESP8266RST | ESP8266 RST | Esp8266_Driver |
| PA2 | USART2_TX | ESP8266 RXD | Esp8266_Driver |
| PA3 | USART2_RX | ESP8266 TXD | Esp8266_Driver |
| PA4 | TFT_CS | TFT 片选 (软件 NSS) | Tft_Driver |
| PA5 | TFT_SCL | SPI1_SCK | Tft_Driver |
| PA6 | TFT_DC | TFT 数据/命令选择 | Tft_Driver |
| PA7 | TFT_SDA | SPI1_MOSI | Tft_Driver |
| PA8 | HINA | TIM1_CH1 (PWM 左上管) | Pwm_Driver |
| PA9 | HINB | TIM1_CH2 (PWM 右上管) | Pwm_Driver |
| PA10 | LED_COM | 通信 LED | Led_Driver |
| PA11 | LED_POWER | 电源 LED | Led_Driver |
| PA12 | LED_TEMP | 温度 LED | Led_Driver |
| PA15 | LED_SYSTEM | 系统心跳 LED | Led_Driver |
| PB0 | ADCB0 | ADC_CH8 (电流 CC6920BSO-10A) | Adc_Driver |
| PB1 | ADCB1 | ADC_CH9 (电压 20:1 分压) | Adc_Driver |
| PB3 | LED_PWM | PWM 状态 LED | Led_Driver |
| PB4 | LED_WIFI | WiFi 状态 LED | Led_Driver |
| PB5 | PAGE | 翻页按键 (IPU) | Key_Driver |
| PB6 | TFT_BL | TFT 背光 PWM (TIM4_CH1) | Tft_Driver |
| PB7 | F_DOWN | 频率减按键 (IPU) | Key_Driver |
| PB8 | F_UP | 频率加按键 (IPU) | Key_Driver |
| PB9 | ON/OFF | 启停按键 (IPU) | Key_Driver |
| PB10 | PowerContrl | 12V 动力电源闸 | main.c |
| PB11 | ESP8266EN | ESP8266 CH_PD | Esp8266_Driver |
| PB13 | LINA | TIM1_CH1N (PWM 左下管) | Pwm_Driver |
| PB14 | LINB | TIM1_CH2N (PWM 右下管) | Pwm_Driver |
| PB15 | BUZ | 有源蜂鸣器 (S8050) | Buzzer_Driver |

**重要**: 不执行任何 TIM1 重映射（默认映射），不执行 SPI1 重映射。JTAG 禁用 (`GPIO_Remap_SWJ_JTAGDisable`) 释放 PB3/PB4/PB5/PA15，SWD (PA13/PA14) 保留。

## Module Map

```
Keil_Project/Hardware/
  Tft_Driver      — ST7735 128×160 SPI1 Mode3 (CPOL=High, CPHA=2Edge), 8x16 ASCII + CN font
  Pwm_Driver      — TIM1 全桥默认映射, Up 计数, CH1=PWM1/CH2=PWM2, 1000ns 死区, 95~150kHz
  Adc_Driver      — ADC1+DMA1 CH8/CH9, 64 样本滑动窗口, 144241 周期互质采样
  Key_Driver      — 4 键 FSM (单击/双击/长按 3s)
  Led_Driver      — 6 LED (ON/OFF/SLOW/FAST), 系统心跳 500ms
  Buzzer_Driver   — PB15 GPIO, ON/BEEP(200/800ms)/OFF
  Esp8266_Driver  — USART2 行缓冲, RST(PA1)+CH_PD(PB11) 非阻塞 3s 初始化
  Inverter_Control— 软启动 SS_IDLE→SWEEP→DONE→FAULT + 频率斜坡
  Ui_Controller   — 6 态 TFT 中文界面, EMA 平滑, 5A 过流保护
System/
  Sys_Timer       — SysTick 1ms + DWT 周期计数器
User/
  Main.c          — 4 阶段启动 → while(1) 9 任务 + IWDG + __WFI
  App_Network     — 15s×3 重试, 500ms JSON 遥测, CMD:ON/OFF/SETFREQ
  stm32f10x_it.c  — ISR: SysTick→Inc_Tick, USART2→Rx_Char, Fault×4→MOE关断
```

## 编码规范速查

| 规则 | 说明 |
|:---|:---|
| **状态机** | 用 `typedef enum` + 单一状态变量，不准用 `s_flag1`+`s_flag2` 拼凑 |
| **调度** | `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值，整型溢出天然安全 |
| **阻塞延时** | `Sys_Timer_Delay_Ms()` 仅限初始化阶段，运行时绝对禁止 |
| **模块架构** | `.h` 只放公开接口, `.c` 放实现 + static，不准 `#include ".c"` |
| **临界区** | PRIMASK 保存/恢复模式，不准裸 `__disable_irq()` / `__enable_irq()` |
| **OOP in C** | 变量封装 struct，状态机打包 (状态+定时器+上下文) |
| **分层依赖** | Hardware → System → Application，严格单向不可逆 |
| **命名** | `Module_Name_Verb_Noun()` 帕斯卡+下划线，枚举值 `MODULE_NAME_VALUE` 全大写 |
| **注释** | 只写 WHY (为什么这样做, 踩过什么坑)，不写 HOW (代码本身说明) |

## TFT 驱动关键参数 (中景园 ZJY180S0800TG01 ST7735)

| 参数 | 值 | 说明 |
|:---|:---|:---|
| **SPI 模式** | Mode 3 (CPOL=High, CPHA=2Edge) | **不是 Mode 0!** |
| **SPI 速率** | 18MHz (PCLK/4) | |
| **MADCTL** | 0x00 (竖屏), 0xC0 (横屏) | 中景园标准值 |
| **SetWin X 偏移** | +2 | 竖屏模式 |
| **SetWin Y 偏移** | +1 | 竖屏模式 |
| **字库格式** | LSB 优先 (`0x01 << b`) | 中景园 `ascii_1608` |
| **中文字库** | `TFT_CN_Font.h` 56 字符 | UTF-8 三字节 hex escape |
| **背光** | PB6, TIM4_CH1 PWM 1kHz | TIM4 在 **APB1** 总线！ |
| **Gamma** | 中景园标准值 (0x04,0x22,0x07...) | |
| **初始化顺序** | 硬件复位 → SLPOUT → FRMCTR → PWCTR → GAMMA → COLMOD → DISPON | 无 SWRESET |
| **芯片型号** | ST7735（非 S） | Green Tab |

## PWM Baseline (不可改动, 源于 V0.0 已验证硬件)

- **计数模式**: `TIM_CounterMode_Up` (不可 CenterAligned)
- **通道模式**: CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` (对角线交替导通)
- **输出极性**: `TIM_OCNPolarity_Low` (IR2103S LIN 低有效)
- **空闲电平**: `TIM_OCNIdleState_Set` (MOE 关断时下管关断)
- **死区**: 1000ns (`PWM_DRIVER_DEADTIME_NS`)，频率 95kHz~150kHz
- **软启动**: 150k→100kHz, 200Hz/10ms，频率斜坡容差 ±1kHz

## Safety

- 故障处理器进入死循环前必须 `TIM_CtrlPWMOutputs(TIM1, DISABLE)`
- IWDG: LSI 40kHz/64, reload=1000 → 1.6s，主循环喂狗
- PB10 PowerContrl: 初始化立即拉低（关断 12V），仅在 `Soft_Start_Trigger()` 时拉高
- 过流 5A: `Ui_Controller_Task` 每 200ms 检查 → `Soft_Start_Fault()` + Buzzer BEEP

## 多仓库推送规则

| 本地文件夹 | 远程仓库 | 分支 | 说明 |
|:---|:---|:---|:---|
| `Keil_Project/`, `Arduino_Project/`, `安卓app/`, `Claude_Files/`, 根目录 | `Ran-sh/WPT_PWM` | `4.0TFT` | 主仓库 |
| `ONENETapp/` | `Ran-sh/WPT_Onenet_IoT` | `master` | 网页控制台 |
| `Railway_Deploy/` | `Ran-sh/WPT_Railway` | `main` | Railway 桥接服务器 |

```bash
git add -A && git commit -m "..." && git push origin 4.0TFT
```

## 参考资料

- 中景园 ST7735 源程序: `D:\BaiduNetdiskDownload\83991\中景园ZJY180S0800TG01技术资料\02-1.8LCD程序源码\11-1.8LCD显示屏STM32F103硬件SPI+DMA例程\`
