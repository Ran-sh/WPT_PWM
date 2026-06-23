# WPT_PWM — 全桥谐振电源控制系统 (基版)

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Version](https://img.shields.io/badge/Firmware-V0.0.0-brightgreen)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 的 100kHz LCC-S 谐振全桥无线供电系统，支持 OLED 本地控制和按键操作。**纯本地固件，无网络功能。**

---

## 硬件架构

```
  STM32F103C8T6 (Cortex-M3, 72MHz)
       │
       ├── TIM1 全桥 PWM (默认映射: PA8/PA9/PB13/PB14)
       │     └── IR2103S 栅极驱动 → MOSFET 全桥 → LCC-S 谐振网络
       │
       ├── ADC1+DMA 双通道扫描
       │     ├── PB0 ← CC6920-10A 霍尔电流传感器 (CH8)
       │     └── PB1 ← 20:1 分压网络 (CH9)
       │
       ├── SSD1306 128x64 OLED (软件 I2C, PA11/PA12)
       │
       ├── 双按键 (PB9/PB8, 单击/双击识别)
       │
       └── 3 LED 状态指示 (PA15 心跳 / PB3 PWM / PB4 Ready)
```

## 引脚分配

| 引脚 | 功能 | 连接目标 |
|:---|:---|:---|
| PA8 | TIM1_CH1 | 上半桥左臂上管 |
| PA9 | TIM1_CH2 | 上半桥右臂上管 |
| PB13 | TIM1_CH1N | 下半桥左臂下管 |
| PB14 | TIM1_CH2N | 下半桥右臂下管 |
| PB0 | ADC_CH8 | CC6920-10A 霍尔电流传感器 |
| PB1 | ADC_CH9 | 20:1 分压网络 |
| PA11 | GPIO (OD) | OLED SCL |
| PA12 | GPIO (OD) | OLED SDA |
| PA15 | GPIO (PP) | 系统心跳 LED (高有效, 1Hz) |
| PB3 | GPIO (PP) | PWM 状态 LED (扫频快闪/稳态慢闪) |
| PB4 | GPIO (PP) | Ready LED (就绪常亮) |
| PB9 | GPIO (IPU) | KEY0 (启停/翻页/故障复位) |
| PB8 | GPIO (IPU) | KEY1 (停止/调频/故障复位) |
| PA13 | SWDIO | ST-Link 调试 |
| PA14 | SWCLK | ST-Link 调试 |

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM |
| 显示 | SSD1306 128x64 | 0.96寸 4针 OLED, I2C, 8x16 字体, 双页面 |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量 |
| 电压采样 | 20:1 分压 | 电阻网络 |

## 关键特性

- **PFM 调功**: 95-150kHz 频率范围, 50% 固定占空比, 1000ns 可调死区 (DEADTIME_NS 宏)
- **非阻塞软启动**: 150kHz → 100kHz 自动扫频, 200Hz/10ms 步进, ~2.5s, 防浪涌冲击
- **双页 OLED UI**: 控制面板 (可操作) + 锁屏监控 (只读), KEY0 双击切换
- **三灯状态**: PA15 心跳 (1Hz) + PB3 PWM (快闪=扫频/慢闪=稳态) + PB4 Ready (就绪常亮)
- **安全红线**: 95kHz 硬下限, 死区编译期断言 ≤127, 上电 MOE=OFF, 过流 SS_FAULT 锁存

## 快速开始

1. **Keil MDK-ARM V5** 打开 `Project.uvprojx`
2. 编译 → ST-Link 烧录
3. 上电 → OLED 显示控制面板 → 按 **KEY0** 触发软启动扫频
4. 按 **KEY1** 关断或调节频率

## 按键操作

| 按键 | 单击 | 双击 |
|:---|:---|:---|
| KEY0 (PB9) | 触发扫频 / 关断 / 复位故障 | 切页 (控制面板 ↔ 锁屏) |
| KEY1 (PB8) | 关断 / 频率+1kHz / 复位故障 | — |

## 安全保护矩阵

| 场景 | 检测机制 | 响应时间 | PWM 动作 |
|:---|:---|:---|:---|
| STM32 掉电后上电 | PWM_Init MOE=OFF | 硬件级 | 上电即关 |
| 过流 | Inverter_SoftStart_Fault | < 1ms | 紧急关断 + 锁存 |
| 频率越界 | PWM_SetFrequency 硬钳位 | 即时 | 拒绝执行 |

## 分支说明

> **仓库**: [github.com/Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM)  
> **本地根目录**: `D:\Claude Code Project\`

| 分支 | 本地目录 | 版本 | 网络协议 | 服务器 | LED | 看门狗 | 适用场景 |
|:---|:---|:---:|:---|:---|:---:|:---:|:---|
| **`master`** ⬅ | `WPT_PWM_V0.0` | **V0.0.0** | 无 (纯本地) | 无 | 4 灯 | 无 | 裸机固件基版 |
| `1.0LAN` | `WPT_PWM_NetAssistant_LAN_V1.0` | V3.4 | NetAssist TCP | PC 局域网 :8080 | 4 灯 | 30s | 内网调试 |
| `2.0WAN` | `WPT_PWM_Bemfa_WAN_V2.0` | V3.5 | 巴法云 TCP | tcp.bemfa.com :8344 | 4 灯 | 无 | 远程控制 |
| `3.0ONENET` | `WPT_PWM_ONENET_V3.0` | V6.2 | OneNET MQTT | OneNET Studio | 4 LED | IWDG 1.6s | 物联网双脑架构 |
| `4.0TFT` | `WPT_PWM_V4.0_ONENET_TFT` | V6.2 | OneNET MQTT | OneNET Studio | 6 LED | IWDG 1.6s | TFT彩屏升级版 |

**分支间关系**: `master` (本分支) 是基版 → `1.0LAN` 增加 ESP8266 + 局域网联网 → `2.0WAN` 改为巴法云协议 → `3.0ONENET` 升级为 OneNET MQTT 双脑架构 → `4.0TFT` OLED→TFT 彩屏硬件升级

## 项目结构

```
├── User/          应用层 (main.c, stm32f10x_it.c)
├── Hardware/      硬件驱动 (PWM, ADC, LED, KEY, OLED, UI)
├── System/        系统服务 (SysTimer)
├── Library/       SPL V3.5.0 (只读)
├── Start/         启动文件
├── Claude_Files/  AI 辅助文件
└── CLAUDE.md      项目开发指南
```

> 需要网络功能？请切换到 `LAN` 或 `WAN` 分支。

## 作者

**Ranssss**

## 许可

MIT
