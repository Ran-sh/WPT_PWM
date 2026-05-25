# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-red)]()
[![Firmware](https://img.shields.io/badge/Firmware-V5.1-brightgreen)]()
[![App](https://img.shields.io/badge/App-WeChat%20Mini%20Program-07C160)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。采用 **Dual-MCU 双脑架构**，STM32 负责物理层发波与保护，ESP8266 独立运行 Arduino MQTT 固件连接 **OneNET 物模型**。支持 OLED 7 界面状态机本地控制、Cloudflare Pages 网页控制台、微信小程序远程遥控。应用于植入式医疗设备无线充电。

---

## 系统架构

```
┌──────────────────────────────────────────────────────────┐
│                      ☁️ OneNET Studio                     │
│              MQTT 物模型 (V/I/F/Switch/SetFreq)           │
│              HTTP API (查询 + 属性设置)                   │
└────┬──────────────┬──────────────┬───────────────────────┘
     │ MQTT         │ HTTPS        │ HTTPS
     ▼              ▼              ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│ ESP8266  │  │ Cloudflare│  │ 微信小程序 │
│ Arduino  │  │ Pages    │  │ WeChat    │
│ MQTT 固件 │  │ 网页控制台 │  │ Mini App  │
└────┬─────┘  └──────────┘  └──────────┘
     │ USART2 115200 JSON
     ▼
┌──────────────────────────────────────┐
│         STM32F103C8T6 (物理脑)        │
│  • TIM1 全桥 PWM (95k~150kHz, PFM)   │
│  • ADC 双通道 + 滑动滤波              │
│  • OLED 7界面状态机 + 双按键 UI        │
│  • 非阻塞软启动扫频 + 频率渐变斜坡     │
│  • 过流锁存保护                       │
└──────────────────────────────────────┘
```

## 硬件

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM |
| WiFi | ESP8266-01 | Arduino MQTT 固件, OneNET 物模型 |
| 显示 | SSD1315 128x64 | 0.96寸 4针 OLED, I2C |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量 |

## 关键特性

- **Dual-MCU 双脑架构**: STM32 只管物理层, ESP8266 只管联网, 纯 JSON 透传, 互不干扰
- **PFM 调功**: 95-150kHz, 50% 固定占空比, 1000ns 可调死区, 影子寄存器原子更新
- **7 界面 OLED 状态机**: 上电自动连 WiFi → 3 次重试 → 连接成功/失败提示, 扫频实时进度条
- **非阻塞软启动**: 150kHz→100kHz, 200Hz/10ms, ~2.5s, 防浪涌
- **频率渐变斜坡**: SETFREQ 后 500Hz/10ms 平滑过渡, 50kHz/s
- **OneNET MQTT 物模型**: 属性上报 (V/I/F/Switch/SetFreq) + 属性设置下发
- **WiFiManager 网页配网**: 首次上电开热点 `STM32_WPT_Config`, 手机配网, 凭据存闪存
- **多端远程控制**: 网页控制台 (Cloudflare Pages) + 微信小程序 (OneNET 直连)
- **遥测门控**: 仅 UI >= 界面3 时发送遥测, 保证"设备在线=可操作"
- **LED 状态灯**: PB3=WiFi 状态, PB4=Start 可操作, PB5=KEY1 可调频

## 快速开始

### STM32 (Keil MDK)
1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. Rebuild → ST-Link Download
3. 上电 → OLED "Wireless Charge" → 自动连 WiFi → "Press KEY0 Start"

### ESP8266 (Arduino IDE)
1. 安装库: ArduinoJson v7 + PubSubClient + WiFiManager
2. 打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
3. 修改 OneNET 凭证: `ONENET_PRODUCT_ID`, `ONENET_DEVICE_NAME`, `ONENET_TOKEN`
4. 开发板选 "Generic ESP8266 Module", Flash 1M, 115200 上传

### 网页控制台
- 线上: `https://wptonenet.483763727.workers.dev`
- 源码: `ONENETapp/`, 部署于 Cloudflare Pages

### 微信小程序
- 源码: `安卓app/`, WeChat DevTools 打开后上传

## 远程指令

| 指令 | 方向 | 说明 |
|:---|:---|:---|
| `Switch=true` | 云端→设备 | 触发扫频 (仅 IDLE 状态) |
| `Switch=false` | 云端→设备 | PWM 关断 (任意状态) |
| `SetFreq=108000` | 云端→设备 | 频率渐变到 108kHz (仅 DONE 状态) |
| `{"V":x,"I":x,"F":x,"S":x}` | 设备→云端 | 遥测数据, 每 500ms |

## 按键操作

| 按键 | 单击 | 双击 | 长按(>3s) |
|:---|:---|:---|:---|
| KEY0 (PB12) | 连WiFi / 触发扫频 / 关断 | 切页 (控制/监测) | **清除WiFi配网** |
| KEY1 (PB13) | 关断扫频 / +1kHz / 复位故障 | — | — |

## OLED 界面流转

```
上电 → 界面2(连接中) → STATUS:ONLINE → 界面3(READY) → KEY0 Start
        ├─ 15s×3超时 → 界面1(初始) + 错误提示
        └─ 界面1 按 KEY0 → 重新连接

界面3 → KEY0 → 界面4(扫频中, 实时频率+进度条)
界面4 → 扫频完成 → 界面5(运行中, K0:Stop K1:+1k)
界面5 → 双击 KEY0 → 界面6/7(控制面板/监测模式)
```

## 项目结构

```
├── Arduino_Project/     # ESP8266 Arduino 固件
├── Keil_Project/        # Keil MDK STM32 固件
│   ├── Hardware/        # PWM, ADC, OLED, KEY, ESP8266, LED, UI
│   ├── System/          # SysTimer
│   ├── User/            # main, App_Net
│   ├── Library/         # SPL V3.5.0 (只读)
│   └── Start/           # 启动文件
├── ONENETapp/           # 网页控制台 (Cloudflare Pages)
├── 安卓app/              # 微信小程序
├── Claude_Files/        # AI 辅助文档与工具
└── CLAUDE.md            # AI 开发规范
```

## 分支

| 分支 | 版本 | 协议 | 说明 |
|:---|:---:|:---|:---|
| `master` | V1.0 | — | 裸机基版 |
| `LAN` | V3.3 | NetAssist TCP | 局域网调试 |
| `WAN` | V4.0 | 巴法云 MQTT | 历史版本 |
| **`ONENET`** | **V5.1** | OneNET MQTT | 双脑架构 + 7界面状态机 |

## 文档

| 文档 | 说明 |
|:---|:---|
| [WPT无线充电系统-从零搭建全指南](Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md) | 完整开发指南 (MD+DOCX) |
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 |

## 作者

**Rssss**

## 许可

MIT
