# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Version](https://img.shields.io/badge/Firmware-V3.5-brightgreen)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统，支持 OLED 本地控制与**巴法云 TCP 创客云 WAN 远程控制**。应用于植入式医疗设备无线充电。

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
       │     └── ESP8266-01 WiFi 模块 (AT 透传 → 巴法云 TCP)
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
| WiFi | ESP8266-01 | AT 指令透传, TCP Client → 巴法云 |
| 显示 | SSD1315 128x64 | 1315驱动 0.96寸 4针 OLED, I2C, 8x16 字体, 双页面 |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量 |
| 电压采样 | 20:1 分压 | 电阻网络 |

## 关键特性

- **PFM 调功**: 95-150kHz 频率范围, 50% 固定占空比, 1000ns 可调死区 (DEADTIME_NS 宏)
- **非阻塞软启动**: 150kHz → 100kHz 自动扫频, 200Hz/10ms 步进, ~2.5s, 防浪涌冲击
- **V3.5 巴法云远程控制**: WAN 广域网接入, cmd=1 订阅 + cmd=2 遥测信封, 手机/PC 远程遥控; WiFi LED 连接后常亮
- **V3.2 异步联网**: 9 态 AT 指令状态机, 支持 KEY1 取消, 3 次自动重试, 双重复位机制
- **双页 OLED UI**: 控制面板 (可操作) + 锁屏监控 (只读), KEY0 双击切换
- **四灯状态**: PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready, 快闪/慢闪/常亮三级指示
- **远程协议**: `CMD:ON` / `CMD:OFF` 指令, 巴法云 cmd=2 遥测 `{"V":x,"I":x,"F":x}` 每 2s 上报
- **安全红线**: 95kHz 硬下限, 死区编译期断言 ≤127, 上电 MOE=OFF, 过流 SS_FAULT 锁存

## 快速开始

1. **Keil MDK-ARM V5** 打开 `Keil_Project/Project.uvprojx`
2. 修改 `User/App_Net.h` 中的配置:
   ```c
   #define WIFI_SSID       "YourWiFi"           // 你的 WiFi 名
   #define WIFI_PASSWORD   "YourPassword"       // WiFi 密码
   #define BEMFA_UID       "your-uid-here"      // 巴法云用户私钥
   #define BEMFA_TOPIC     "WPT001"             // 主题名 (可自定义)
   ```
3. 编译 → ST-Link 烧录
4. 上电 → OLED 显示 "Wireless Charge" → 按 **KEY0** 联网
5. 联网成功后自动订阅巴法云主题，设备上线
6. 按 **KEY0** 触发软启动扫频，或通过**巴法云 APP/网页**发送 `CMD:ON` 远程遥控

## 远程控制

| 指令 | 格式 | 说明 |
|:---|:---|:---|
| 开机 | `CMD:ON` | 触发软启动扫频 150k→100kHz, ~2.5s |
| 关机 | `CMD:OFF` | PWM 立即关断, 全桥安全停止 |

支持**巴法云 APP** (应用商店搜索 "Bemfa") 或 **巴法云网页控制台** (bemfa.com) 下发指令。详细操作见 [PC端联调操作指南](Claude_Files/docs/PC端联调操作指南.md)。

## 按键操作

| 按键 | 单击 | 双击 |
|:---|:---|:---|
| KEY0 (PB12) | 联网 / 触发扫频 / 关断 / 复位故障 | 切页 (控制面板 ↔ 锁屏) |
| KEY1 (PB13) | 关断 / 频率+1kHz / 取消联网 / 复位故障 | — |

## 安全保护矩阵

| 场景 | 检测机制 | 响应时间 | PWM 动作 |
|:---|:---|:---|:---|
| TCP 正常断开 | ESP8266 发 `CLOSED` 帧 | < 1ms | 立即关断 |
| ESP8266 掉电 | TCP RST → CLOSED 帧 | < 1ms | 自动关断 |
| STM32 掉电后上电 | PWM_Init MOE=OFF | 硬件级 | 上电即关 |
| 过流 | Inverter_SoftStart_Fault | < 1ms | 紧急关断 + 锁存 |
| 频率越界 | PWM_SetFrequency 硬钳位 | 即时 | 拒绝执行 |

## 分支说明

> **仓库**: [github.com/Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM)  
> **本地根目录**: `D:\Claude Code Project\`

| 分支 | 本地目录 | 版本 | 网络协议 | 服务器 | LED | 看门狗 | 适用场景 |
|:---|:---|:---:|:---|:---|:---:|:---:|:---|
| `master` | `WPT_PWM_V0.0` | V1.0 | 无 (纯本地) | 无 | 4 灯 | 无 | 裸机固件基版 |
| `1.0LAN` | `WPT_PWM_NetAssistant_LAN_V1.0` | V3.4 | NetAssist TCP | PC 局域网 :8080 | 4 灯 | 15s | 内网调试 |
| **`2.0WAN`** ⬅ | `WPT_PWM_Bemfa_WAN_V2.0` | **V3.5** | 巴法云 TCP | tcp.bemfa.com :8344 | 4 灯 | 无 | 远程控制 |
| `3.0ONENET` | `WPT_PWM_ONENET_V3.0` | V6.1 | OneNET MQTT | OneNET Studio | 4 LED | IWDG 1.6s | 物联网双脑架构 |
| `4.0TFT` | `WPT_PWM_V4.0_ONENET_TFT` | V6.2 | OneNET MQTT | OneNET Studio | 6 LED | IWDG 1.6s | TFT彩屏升级版 |

**分支间关系**: `master` 是基版 → `1.0LAN` 增加 ESP8266 + 局域网联网 → `2.0WAN` 在 LAN 基础上改为巴法云协议 → `3.0ONENET` 升级为 OneNET MQTT 双脑架构 → `4.0TFT` OLED→TFT 彩屏硬件升级

## 项目结构

```
├── Keil_Project/User/          应用层 (main.c, App_Net.c)
├── Keil_Project/Hardware/      硬件驱动 (PWM, ESP8266, ADC, LED, KEY, OLED, UI)
├── Keil_Project/System/        系统服务 (SysTimer)
├── Keil_Project/Library/       SPL V3.5.0 (只读)
├── Keil_Project/Start/         启动文件
├── Claude_Files/   AI 辅助文件 (docs, tools, superpowers)
└── CLAUDE.md      项目开发指南
```

## 文档

| 文档 | 说明 |
|:---|:---|
| [软件架构与开发者指南](Claude_Files/docs/软件架构与开发者指南.md) | 完整技术架构, 模块详解, 数据流图 |
| [巴法云WAN远程联调操作指南](Claude_Files/docs/巴法云WAN远程联调操作指南.md) | 巴法云配置, 远程控制测试, 故障排查 |
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 |

## 作者

**Ranssss**

## 许可

MIT
