# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-red)]()
[![Version](https://img.shields.io/badge/Firmware-V4.1-brightgreen)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。采用 **Dual-MCU 双脑架构**，STM32 负责物理层发波与保护，ESP8266 独立运行 Arduino MQTT 固件连接 **OneNET 物模型**，支持 OLED 本地控制与云平台远程遥控。应用于植入式医疗设备无线充电。

---

## 架构: Dual-MCU 双脑 (V4.0)

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│      STM32F103C8T6 (物理脑)   │    │     ESP8266-01 (联网脑)       │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • TIM1 全桥 PWM + PFM 调功   │    │  • WiFiManager 网页配网       │
│  • ADC1+DMA 双通道采集        │    │  • OneNET MQTT 物模型        │
│  • OLED + 双按键 UI           │    │  • JSON ↔ 串口 双向透传      │
│  • 软启动扫频 150k→100k       │    │  • WiFi/MQTT 自动重连         │
│  • 过流锁存保护                │    │  • ArduinoJson v7 JSON 解析  │
│      USART2 (115200)         │    │                              │
│      "{"V":x,"I":x,"F":x}"   │    │  MQTT publish → OneNET       │
│      "CMD:ON" / "CMD:OFF"    │    │  MQTT subscribe ← OneNET     │
└──────────────────────────────┘    └──────────────────────────────┘
```

## 硬件架构

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM |
| WiFi | ESP8266-01 | Arduino MQTT 固件, OneNET 物模型 |
| 显示 | SSD1315 128x64 | 0.96寸 4针 OLED, I2C, 8x16 字体 |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量 |
| 电压采样 | 20:1 分压 | 电阻网络 |

## 关键特性

- **Dual-MCU 双脑架构**: STM32 只管物理层, ESP8266 独立连云, 职责隔离, 互不干扰
- **PFM 调功**: 95-150kHz 频率范围, 50% 固定占空比, 1000ns 可调死区
- **非阻塞软启动**: 150kHz → 100kHz 自动扫频, 200Hz/10ms 步进, ~2.5s, 防浪涌冲击
- **OneNET MQTT 物模型**: 属性上报 + 属性设置下发, 云端远程开关控制
- **WiFiManager 网页配网**: 首次上电开热点 `STM32_WPT_Config`, 手机配网, 凭据存闪存
- **双页 OLED UI**: 控制面板 + 锁屏监控, KEY0 双击切换
- **四灯状态**: PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready
- **安全红线**: 95kHz 硬下限, 死区编译期断言, 上电 MOE=OFF, 过流 SS_FAULT 锁存

## 快速开始

### STM32 (Keil MDK)
1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. 编译 → ST-Link 烧录
3. 上电 → OLED 显示 "Wireless Charge" → 按 KEY0 初始化硬件 → "WiFi: READY"
4. 按 KEY0 触发软启动扫频; 或等待 ESP8266 连云后云端下发指令

### ESP8266 (Arduino IDE)
1. 安装库: ArduinoJson v7 + PubSubClient + WiFiManager (tzapu)
2. 打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
3. 修改 OneNET 设备凭证宏: `ONENET_PRODUCT_ID`, `ONENET_DEVICE_NAME`, `ONENET_TOKEN`
4. 选择 "Generic ESP8266 Module" → Flash 1M → 115200 上传
5. 首次上电 → 手机连热点 `STM32_WPT_Config` → 配网 → 自动连云

## 远程控制

| 指令 | 方向 | 说明 |
|:---|:---|:---|
| `{"V":x,"I":x,"F":x}` | STM32 → ESP8266 → OneNET | 遥测数据, 每 2s 上报 |
| OneNET 属性设置 `Switch=true` | OneNET → ESP8266 → STM32 | `CMD:ON\n` → 触发扫频 |
| OneNET 属性设置 `Switch=false` | OneNET → ESP8266 → STM32 | `CMD:OFF\n` → PWM 关断 |
| OneNET 属性设置 `FreqAdd=true` | OneNET → ESP8266 → STM32 | `CMD:F_UP\n` → +1kHz (点动复位) |
| OneNET 属性设置 `FreqSub=true` | OneNET → ESP8266 → STM32 | `CMD:F_DOWN\n` → -1kHz (点动复位) |

## 按键操作

| 按键 | 单击 | 双击 |
|:---|:---|:---|
| KEY0 (PB12) | 硬件初始化 / 触发扫频 / 关断 / 复位故障 | 切页 |
| KEY1 (PB13) | 关断 / 频率+1kHz / 复位故障 | — |

## 安全保护

| 场景 | 机制 | PWM |
|:---|:---|:---|
| 上电 | `PWM_Init(MOE=OFF)` | 硬件级安全 |
| 过流 | `Inverter_SoftStart_Fault()` | 紧急关断 + 锁存 |
| 频率越界 | `PWM_SetFrequency` 硬钳位 | 拒绝执行 |
| ESP8266 掉线 | ESP8266 自管理重连 | STM32 无感, 继续发 JSON |

## 分支说明

| 分支 | 版本 | 协议 | 说明 |
|:---|:---:|:---|:---|
| `master` | V1.0 | 无 | 裸机固件基版 |
| `LAN` | V3.3 | NetAssist TCP | 局域网调试 |
| `WAN` | V4.0 | OneNET MQTT | 巴法云 TCP (历史版本) |
| **`ONENET`** ⬅ | **V4.1** | OneNET MQTT | 双脑架构 + 虚拟按键调频 |

## 项目结构

```
WPT_PWM_V3.0/
├── Arduino_Project/     Arduino 固件 (ESP8266 MQTT)
├── Keil_Project/                Keil MDK STM32 固件
│   ├── Hardware/       硬件驱动层
│   ├── System/         系统服务层
│   ├── User/           应用层
│   ├── Library/        SPL V3.5.0 (只读)
│   └── Start/          启动文件
├── Claude_Files/        AI 辅助文档与工具
└── CLAUDE.md           AI 开发规范
```

## 文档

| 文档 | 说明 |
|:---|:---|
| [软件架构与开发者指南](Claude_Files/docs/软件架构与开发者指南.md) | 完整技术架构, 模块详解 |
| [双脑架构V4.0验证与烧录指南](Claude_Files/docs/双脑架构V4.0验证与烧录指南.md) | STM32 验证, ESP8266 烧录, 联调 |
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 |

## 作者

**Ranssss**

## 许可

MIT
