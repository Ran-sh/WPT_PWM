# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Version](https://img.shields.io/badge/Firmware-V3.4-brightgreen)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统，支持 OLED 本地控制与 WiFi 远程遥测。应用于植入式医疗设备无线充电。

---

## 硬件架构

```
  STM32F103C8T6 (Cortex-M3, 72MHz)
       │
       ├── TIM1 全桥 PWM (CH1/CH1N/CH2/CH2N)
       │     └── IR2103S 栅极驱动 → MOSFET 全桥 → LCC-S 谐振网络
       │
       ├── ADC1+DMA 双通道扫描
       │     ├── PA0 ← CC6920-10A 霍尔电流传感器
       │     └── PA1 ← 20:1 分压网络
       │
       ├── USART2 (115200bps)
       │     └── ESP8266-01 WiFi 模块 (AT 透传)
       │
       ├── SSD1315 128x64 OLED (软件 I2C, PA11/PA12)
       │
       ├── 双按键 (PB12/PB13, 单击/双击识别)
       │
       └── 4 LED 状态指示 (PC13/PB3/PB4/PB5)
```

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM |
| WiFi | ESP8266-01 | AT 指令透传, TCP Client |
| 显示 | SSD1315 128x64 | 1315驱动 0.96寸 4针 OLED, I2C, 8x16 字体, 双页面 |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量 |
| 电压采样 | 20:1 分压 | 电阻网络 |

## 关键特性

- **PFM 调功**: 95-150kHz 频率范围, 50% 固定占空比, 1000ns 可调死区 (DEADTIME_NS 宏)
- **非阻塞软启动**: 150kHz → 100kHz 自动扫频, 200Hz/10ms 步进, ~2.5s, 防浪涌冲击
- **V3.2 异步联网**: 9 态 AT 指令状态机, 支持 KEY1 取消, 3 次自动重试, 全程非阻塞
- **V3.4 静默看门狗**: ESP8266 掉电/卡死 30s 后自动关断 PWM, 无需依赖 CLOSED 帧 (联网成功新 30s 窗口)
- **双页 OLED UI**: 控制面板 (可操作) + 锁屏监控 (只读), KEY0 双击切换
- **四灯状态**: PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready, 快闪/慢闪/常亮三级指示
- **远程协议**: `CMD:ON` / `CMD:OFF` 指令, JSON 遥测 `{"V":x,"I":x,"F":x}`
- **安全红线**: 95kHz 硬下限, 死区编译期断言 ≤127, 上电 MOE=OFF, 过流 SS_FAULT 锁存

## 快速开始

1. **Keil MDK-ARM V5** 打开 `Keil_Project/Project.uvprojx`
2. 修改 `User/App_Net.c` 中的 WiFi 配置:
   ```c
   #define WIFI_SSID       "YourWiFi"
   #define WIFI_PASSWORD   "YourPassword"
   #define SERVER_IP       "192.168.x.x"    // PC 端 IPv4
   #define SERVER_PORT     8080
   ```
3. 编译 → ST-Link 烧录
4. 上电 → OLED 显示 "Wireless Charge" → 按 **KEY0** 联网
5. 联网成功后再按 **KEY0** 触发软启动扫频
6. PC 端 NetAssist 配置 TCP Server 监听, 发送 `CMD:ON` / `CMD:OFF` 遥控

## 按键操作

| 按键 | 单击 | 双击 |
|:---|:---|:---|
| KEY0 (PB12) | 联网 / 触发扫频 / 关断 / 复位故障 | 切页 (控制面板 ↔ 锁屏) |
| KEY1 (PB13) | 关断 / 频率+1kHz / 取消联网 / 复位故障 | — |

## 安全保护矩阵

| 场景 | 检测机制 | 响应时间 | PWM 动作 |
|:---|:---|:---|:---|
| TCP 正常断开 | ESP8266 发 `CLOSED` 帧 | < 1ms | 立即关断 |
| ESP8266 掉电 | 30s RX 静默看门狗 | ≤ 30s | 自动关断 |
| ESP8266 卡死 | 30s RX 静默看门狗 | ≤ 30s | 自动关断 |
| STM32 掉电后上电 | PWM_Init MOE=OFF | 硬件级 | 上电即关 |
| 过流 | Inverter_SoftStart_Fault | < 1ms | 紧急关断 + 锁存 |
| 频率越界 | PWM_SetFrequency 硬钳位 | 即时 | 拒绝执行 |

## 分支说明

> **仓库**: [github.com/Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM)  
> **本地根目录**: `D:\Claude Code Project\`

| 分支 | 本地目录 | 版本 | 网络协议 | 服务器 | LED | 看门狗 | 适用场景 |
|:---|:---|:---:|:---|:---|:---:|:---:|:---|
| `master` | `WPT_PWM_V0.0` | V1.0 | 无 (纯本地) | 无 | 4 灯 | 无 | 裸机固件基版 |
| **`1.0LAN`** ⬅ | `WPT_PWM_NetAssistant_LAN_V1.0` | **V3.4** | NetAssist TCP | PC 局域网 :8080 | 4 灯 | 30s | 内网调试 |
| `2.0WAN` | `WPT_PWM_Bemfa_WAN_V2.0` | V3.5 | 巴法云 TCP | tcp.bemfa.com :8344 | 4 灯 | 无 | 远程控制 |
| `3.0ONENET` | `WPT_PWM_ONENET_V3.0` | V6.1 | OneNET MQTT | OneNET Studio | 4 LED | IWDG 1.6s | 物联网双脑架构 |
| `4.0TFT` | `WPT_PWM_V4.0_ONENET_TFT` | V6.2 | OneNET MQTT | OneNET Studio | 6 LED | IWDG 1.6s | TFT彩屏升级版 |

**分支间关系**: `master` 是基版 → `1.0LAN` 增加 ESP8266 + 局域网联网 → `2.0WAN` 在 LAN 基础上改为巴法云协议 → `3.0ONENET` 升级为 OneNET MQTT 双脑架构 → `4.0TFT` OLED→TFT 彩屏硬件升级

## 项目结构

```
├── User/          应用层 (main.c, App_Net.c)
├── Hardware/      硬件驱动 (PWM, ESP8266, ADC, LED, KEY, OLED, UI)
├── System/        系统服务 (SysTimer)
├── Library/       SPL V3.5.0 (只读)
├── Start/         启动文件
├── claude_code/   AI 辅助文件 (docs, tools, superpowers)
└── CLAUDE.md      项目开发指南
```

## 文档

| 文档 | 说明 |
|:---|:---|
| [软件架构与开发者指南](claude_code/docs/软件架构与开发者指南.md) | 完整技术架构, 模块详解, 数据流图 |
| [NetAssist局域网联调操作指南](claude_code/docs/NetAssist局域网联调操作指南.md) | NetAssist 配置, 闭环测试, 故障排查 |

| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 |

## 作者

**Ranssss**

## 许可

MIT
