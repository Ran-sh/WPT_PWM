# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Display](https://img.shields.io/badge/Display-ST7735%20160×128%20TFT-red)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-purple)]()
[![Firmware](https://img.shields.io/badge/Firmware-V5.1.0-brightgreen)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![Web](https://img.shields.io/badge/Web-Cloudflare%20Pages-F38020)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V5.1.0** (2026-07-22) — 五项设置、双档启动频率、全局菜单光标与递增式独立表盘

---

## 目录

1. [项目简介](#项目简介)
2. [系统架构](#系统架构)
3. [硬件清单 (BOM)](#硬件清单-bom)
4. [硬件接线](#硬件接线)
5. [软件环境搭建](#软件环境搭建)
6. [STM32 固件编译与烧录](#stm32-固件编译与烧录)
7. [ESP8266 固件编译与烧录](#esp8266-固件编译与烧录)
8. [W25Q128 Flash 字库烧录](#w25q128-flash-字库烧录)
9. [OneNET 云平台配置](#onenet-云平台配置)
10. [网页控制台部署](#网页控制台部署)
11. [微信小程序配置](#微信小程序配置)
12. [首次上电与使用](#首次上电与使用)
13. [LED / 按键 / 蜂鸣器说明](#led--按键--蜂鸣器说明)
14. [TFT 屏幕界面](#tft-屏幕界面)
15. [故障排查](#故障排查)
16. [版本历史](#版本历史)

---

## 项目简介

WPT_PWM 是一套**无线充电 (Wireless Power Transfer) 全桥谐振电源控制系统**，采用 STM32 + ESP8266 双 MCU 架构，支持本地 TFT 彩屏与五键操作，以及云端远程多端控制。

### 它能做什么？

- **无线充电控制**: TIM1 全桥 PWM 输出 20~200kHz；低频档与高频档分别按保存目标执行软启动扫频
- **实时监测**: TIM3 以 500Hz 硬件触发双通道 ADC，64 点显示窗口 + 8 点安全窗口 + 连续 3 样本过流确认
- **本地操作**: 1.8 寸 TFT 彩屏 + 5 个物理按键 + 4 个 LED 指示灯
- **远程控制**: 手机网页 / 微信小程序 / OneNET 平台，三端同步
- **数据记录**: W25Q128 16MB Flash 存储可恢复循环日志、故障前后各 5 秒快照和校验参数双副本

### 核心参数

| 参数 | 值 |
|:---|:---|
| 主控 MCU | STM32F103C8T6 (Cortex-M3, 64KB Flash, 20KB SRAM) |
| 联网模块 | ESP8266 (WiFi + MQTT) |
| PWM 频率范围 | 20 ~ 200 kHz |
| PWM 死区时间 | 1000 ns |
| ADC 采样 | TIM3 TRGO 500Hz；双通道 DMA；64 点显示窗口 + 8 点安全窗口 |
| 显示屏 | ST7735 1.8" TFT, 160×128 横屏, RGB565 |
| 外部存储 | W25Q128 16MB SPI Flash (全字库 + 黑匣子) |
| 固件库 | STM32 SPL V3.5.0 (标准外设库) |
| 编译器 | ARMCC V5.06 (Keil MDK-ARM V5) |
| 云平台 | OneNET Studio (MQTT 物模型) |

---

## 系统架构

```
                        ☁️ OneNET Studio
                     MQTT 物模型 (V/I/F/Switch/SetFreq)
                    ┌─────────┼─────────┐
                    │ MQTT    │ HTTPS   │ HTTPS
                    ▼         ▼         ▼
              ┌──────────┐ ┌──────────┐ ┌──────────┐
              │ ESP8266  │ │Cloudflare│ │ 微信小程序 │
              │ 联网脑    │ │ Pages    │ │ WeChat    │
              │ Arduino  │ │ 网页控制台│ │ Mini App  │
              └────┬─────┘ └──────────┘ └──────────┘
                   │ USART2 115200
                   │ 纯文本 JSON (零 AT 指令)
                   ▼
┌──────────────────────────────────────────────────┐
│              STM32F103C8T6 (物理脑)                │
│                                                   │
│  ┌─────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │ TIM1    │ │ ADC1     │ │ SPI1 (分时复用)   │   │
│  │ 全桥PWM │ │ V/I 采集 │ │ TFT + W25Q128    │   │
│  └─────────┘ └──────────┘ └──────────────────┘   │
│                                                   │
│  5 状态机: INIT → IDLE → SWEEP → RUNNING → FAULT  │
│  5 按键 + 4 LED + 蜂鸣器 + IWDG 看门狗            │
└──────────────────────────────────────────────────┘
```

### 双 MCU 分工 (铁律)

| | STM32 (物理脑) | ESP8266 (联网脑) |
|:---|:---|:---|
| **负责** | PWM 发波、ADC 采集、TFT 显示、安全保护 | WiFi 联网、MQTT 通信、远程指令 |
| **不负责** | 不处理 WiFi/AT 指令 | 不碰 PWM/ADC |
| **通信** | USART2 发送遥测 JSON → | USART2 接收遥测 → 上报云端 |
| | USART2 ← 接收控制指令 | USART2 ← MQTT 指令下发 |

---

## 硬件清单 (BOM)

### 核心模组

| 序号 | 物料 | 型号/规格 | 数量 | 参考价 (¥) | 备注 |
|:---|:---|:---|:---:|:---|:---|
| 1 | 主控板 | STM32F103C8T6 最小系统板 (Blue Pill) | 1 | ~12 | LQFP-48, 带 8MHz 晶振 |
| 2 | WiFi 模块 | ESP8266-01S | 1 | ~8 | 需烧录 Arduino 固件 |
| 3 | 显示屏 | ST7735 1.8" TFT (128×160) | 1 | ~15 | Green Tab 版本, SPI 接口 |
| 4 | Flash 存储 | W25Q128JVSIQ (16MB SPI NOR) | 1 | ~5 | SOIC-8, 焊在目标板 |
| 5 | USB-TTL | CH340G / CP2102 | 1 | ~5 | STM32 串口烧录用 |

### 功率电路

| 序号 | 物料 | 型号/规格 | 数量 | 参考价 (¥) | 备注 |
|:---|:---|:---|:---:|:---|:---|
| 6 | MOS 驱动 | IR2104S (半桥驱动) | 2 | ~3 | SOP-8, 驱动全桥 4 个 MOS |
| 7 | MOS 管 | IRF540N / IRLZ44N | 4 | ~2 | TO-220, 全桥拓扑 |
| 8 | 电流传感器 | CC6920BSO | 1 | ~8 | 5A 量程, 模拟输出 0-3.3V |
| 9 | 电压分压 | 20:1 电阻分压 (100K + 5.1K) | 1 组 | ~1 | 适配 0-50V → 0-2.5V |
| 10 | 12V 电源 | 12V DC 适配器 | 1 | ~15 | 3A 以上 |
| 11 | 电源开关 | PB10 控制 N-MOS 开关 12V | 1 | ~2 | MCU GPIO 控制通断 |

### 人机交互

| 序号 | 物料 | 型号/规格 | 数量 | 参考价 (¥) | 备注 |
|:---|:---|:---|:---:|:---|:---|
| 12 | 按键 | 6×6mm 轻触开关 | 5 | ~0.5 | KEY0-KEY4, 接 PB5-PB9 |
| 13 | LED | 3mm 发光二极管 | 4 | ~0.3 | 蓝×2 (WIFI, HEARTBEAT) + 绿 (POWER) + 黄 (STATUS) |
| 14 | 电阻 | 220Ω 限流电阻 | 5 | ~0.1 | LED + 按键各串一个 |
| 15 | 蜂鸣器 | 有源蜂鸣器 3.3V | 1 | ~2 | PB15 驱动 |

### 烧录工具

| 序号 | 物料 | 型号/规格 | 数量 | 参考价 (¥) | 备注 |
|:---|:---|:---|:---:|:---|:---|
| 16 | ST-Link V2 | STM32 调试/下载器 | 1 | ~10 | SWD 接口, 4 线 |
| 17 | CH341A | USB-SPI 编程器 | 1 | ~15 | 仅烧录 W25Q128 字库时用 |
| 18 | USB 数据线 | Micro USB + Type-C | 各 1 | ~5 | 分别给 STM32/ESP8266 供电 |

> **总预算**: ~120 元 (不含 PCB 和焊接工具)

---

## 硬件接线

### STM32F103C8T6 完整引脚分配 (V5.0)

```
                     STM32F103C8T6  LQFP-48
                  ┌──────────────────────────┐
      TFT_RST PA0 │● 1                    48 ○│ PB9  KEY0 (电源)
    ESP_RST PA1  │  2                    47 ○│ PB8  KEY1 (返回)
   USART2_TX PA2  │  3                    46 ○│ PB7  KEY2 (UP)
   USART2_RX PA3  │  4                    45 ○│ PB6  KEY3 (DOWN)
      TFT_CS PA4  │  5                    44 ○│ PB5  KEY4 (确定)
     SPI_SCK PA5  │  6                    43 ○│ PB4  LED_WIFI (蓝)
  TFT_DC/MISO PA6 │  7                    42 ○│ PB3  LED_POWER (绿)
    SPI_MOSI PA7  │  8                    41 ○│ PB2  BOOT1 (GND)
     HINA PA8     │  9                    40 ○│ PB1  ADC_V (电压)
     HINB PA9     │ 10                    39 ○│ PB0  ADC_I (电流)
        PA10 ──── │ 11                    38 ○│ PB15 Buzzer
        PA11 ──── │ 12                    37 ○│ PB14 LINB
TFT_BL PA12      │ 13                    36 ○│ PB13 LINA
  SWDIO PA13     │ 14                    35 ○│ PB12 W25Q128_CS
  SWCLK PA14     │ 15                    34 ○│ PB11 ESP8266_EN
  STATUS PA15    │ 16                    33 ○│ PB10 PowerCtrl (12V)
        PC13 ────│ 17                    32 ○│ VBAT → 3.3V
  8MHz  OSC_IN   │ 18                    31 ○│ VDD_2 → 3.3V
  8MHz  OSC_OUT  │ 19                    30 ○│ VDD_1 → 3.3V
       NRST      │ 20                    29 ○│ VDDA → 3.3V
                 │   21  22  23  24  25  26  │
                 │   GND GND VDD_3 GND VDD_4 │
                 └──────────────────────────┘

  PA10/PA11: V5.0 移除 (旧版 LED 引脚, 新版不接)
  PC13: V5.0 新增 HEARTBEAT 板载 LED (active LOW)
  PA12: V5.0 改为 TFT_BL (GPIO 直接开关, 不用 TIM4 PWM)
  PB12: V5.0 改为 W25Q128_CS (旧版为 PA12)
```

### 外设接线速查表

| STM32 引脚 | 连接对象 | 信号说明 |
|:---|:---|:---|
| **显示 (SPI1)** | | |
| PA4 | ST7735 CS | TFT 片选 (低有效) |
| PA5 | ST7735 SCK + W25Q128 CLK | SPI 时钟 (共用) |
| PA6 | ST7735 DC + W25Q128 MISO | 命令/数据 + Flash 读数据 (动态切换) |
| PA7 | ST7735 SDA + W25Q128 MOSI | SPI 数据输出 (共用) |
| PA0 | ST7735 RST | TFT 硬件复位 |
| PA12 | ST7735 BL | 背光 (GPIO, HIGH=亮) |
| **全桥 PWM** | | |
| PA8 | IR2104 HIN #1 | 半桥 1 上管 |
| PA9 | IR2104 HIN #2 | 半桥 2 上管 |
| PB13 | IR2104 LIN #1 | 半桥 1 下管 |
| PB14 | IR2104 LIN #2 | 半桥 2 下管 |
| **模拟采集** | | |
| PB0 | CC6920BSO VOUT | 电流传感器 (0-3.3V, 零点=1.65V) |
| PB1 | 电压分压中点 | 20:1 分压 (0-50V → 0-2.5V) |
| **ESP8266** | | |
| PA2 | ESP8266 RX | USART2 TX → ESP RX |
| PA3 | ESP8266 TX | USART2 RX ← ESP TX |
| PA1 | ESP8266 RST | 硬件复位 (GPIO) |
| PB11 | ESP8266 EN (CH_PD) | 使能脚 (GPIO) |
| **按键 (全部 IPU, 按下为低)** | | |
| PB9 | KEY0 一端, 另一端 GND | 电源开关 |
| PB8 | KEY1 一端, 另一端 GND | 返回 (双击回主菜单) |
| PB7 | KEY2 一端, 另一端 GND | UP / 加 |
| PB6 | KEY3 一端, 另一端 GND | DOWN / 减 |
| PB5 | KEY4 一端, 另一端 GND | 确定 / PWM 启停 |
| **LED (串 220Ω → LED → GND)** | | |
| PB4 | LED_WIFI (蓝) | WiFi 状态 |
| PB3 | LED_POWER (绿) | 12V 指示 |
| PA15 | LED_STATUS (黄) | PWM 状态 |
| PC13 | LED_HEARTBEAT (蓝, 板载) | MCU 运行指示 (active LOW) |
| **其他** | | |
| PB10 | N-MOS 栅极 (控制 12V 通断) | 电源开关 |
| PB15 | 蜂鸣器 | 有源蜂鸣器 (HIGH=响) |
| PB12 | W25Q128 /CS | Flash 片选 |

### 供电说明

```
12V DC 适配器
    │
    ├──→ IR2104 × 2 (MOS 驱动供电)
    ├──→ 全桥功率级
    └──→ PB10 控制 N-MOS → 3.3V 稳压模块 → STM32 + ESP8266 + TFT + W25Q128

上电顺序:
1. STM32 上电 → PB10 默认 LOW (12V 后端断开)
2. KEY0 单击 → PB10 HIGH → 12V 后级导通 → POWER LED 亮
3. KEY4 单击 → 软启动 → SWEEP → RUNNING
4. KEY0 再单击 → 停 PWM → PB10 LOW → 12V 后级断开
```

---

## 软件环境搭建

### 1. 安装 Keil MDK-ARM V5

| 步骤 | 操作 |
|:---|:---|
| 1 | 下载 [Keil MDK-ARM V5](https://www.keil.com/download/product/) |
| 2 | 安装到默认路径 `C:\Keil_v5\` |
| 3 | 安装 STM32F1xx 设备包: 打开 Keil → Pack Installer → 搜索 `STM32F1` → Install |
| 4 | 注册 License (Cortex-M3 用免费版即可，代码量 64KB 以内无限制) |

### 2. 安装 Arduino IDE (ESP8266 用)

| 步骤 | 操作 |
|:---|:---|
| 1 | 下载 [Arduino IDE](https://www.arduino.cc/en/software) |
| 2 | 打开 Arduino IDE → File → Preferences → Additional Boards Manager URLs |
| 3 | 填入: `https://arduino.esp8266.com/stable/package_esp8266com_index.json` |
| 4 | Tools → Board → Boards Manager → 搜索 `ESP8266` → Install |
| 5 | Tools → Board → 选择 `Generic ESP8266 Module` |
| 6 | 安装以下库 (Sketch → Include Library → Manage Libraries): |
| | • ArduinoJson (by Benoit Blanchon) — **v7** |
| | • PubSubClient (by Nick O'Leary) |
| | • WiFiManager (by tzapu) |

### 3. 安装 ST-Link 驱动

| 步骤 | 操作 |
|:---|:---|
| 1 | 下载 [ST-Link USB Driver](https://www.st.com/en/development-tools/stsw-link009.html) |
| 2 | 安装后插上 ST-Link V2，设备管理器应显示 `STMicroelectronics STLink Virtual COM Port` |

### 4. 安装 Python (W25Q128 字库烧录用)

| 步骤 | 操作 |
|:---|:---|
| 1 | 下载 [Python 3.10+](https://www.python.org/downloads/)，安装时勾选 "Add to PATH" |
| 2 | 打开终端，安装依赖: `pip install pillow` |

---

## STM32 固件编译与烧录

### 项目结构

```
Keil_Project/
├── Project.uvprojx          ← Keil 工程文件 (双击打开)
├── keilkill.bat             ← 清理编译产物 (推送前必须执行)
├── Hardware/                ← 硬件驱动层 (12 个模块)
│   ├── Adc_Driver.c/h       ← TIM3 500Hz触发 + 双通道DMA + 显示/安全双窗口
│   ├── Buzzer_Driver.c/h    ← 蜂鸣器驱动
│   ├── Esp8266_Driver.c/h   ← USART2 收发环形缓冲 + 中断发送
│   ├── Inverter_Control.c/h ← 软启动扫频 + 频率斜坡
│   ├── Key_Driver.c/h       ← 5键 FSM (去抖/单击/双击/长按)
│   ├── Led_Driver.c/h       ← 4 LED (WIFI/POWER/STATUS/HEARTBEAT)
│   ├── Pwm_Driver.c/h       ← TIM1 全桥 PWM 20-200kHz
│   ├── Spi1_Shared.c/h      ← TFT/W25Q128 总线所有权、切换与故障恢复
│   ├── TFT_Font_Data.h      ← ROM 回退字库 (ASCII 95 + 中文 4 + 图标)
│   ├── Tft_Driver.c/h       ← ST7735 增量显示 + Flash/ROM 双路径字库
│   ├── Ui_Controller.c/h    ← 15页面 UI 状态机 + 递增式独立表盘
│   └── W25Q_Driver.c/h      ← 16MB Flash 边界检查 + 超时 + 二分检索
├── User/                    ← 应用层
│   ├── main.c               ← 程序入口 + 主循环状态机
│   ├── Sys_Core.c/h         ← 初始化/安全/电源控制/调度
│   ├── App_Network.c/h      ← WiFi/MQTT 状态机 + 心跳 + 遥测
│   └── App_Storage.c/h      ← 后台校验保存 + Blackbox V2日志/故障快照
├── System/
│   ├── Checksum.c/h         ← CRC32/CRC8 统一校验服务
│   └── Sys_Timer.c/h        ← SysTick 1ms 时基 + 初始化延时
├── Start/                   ← CMSIS + 启动文件
└── Library/                 ← SPL V3.5.0 (只读, 不可修改)
```

### 编译步骤

```
1. 双击 Keil_Project/Project.uvprojx 打开工程
2. 点击工具栏 Rebuild (F7) 编译全部
3. 等待 Build Output 显示 "0 Error(s), 0 Warning(s)"
4. 连接 ST-Link V2:
   - SWCLK → PA14
   - SWDIO → PA13
   - GND   → GND
   - 3.3V  → 3.3V (可不接, 板子独立供电)
5. 点击 Download (F8) 烧录
6. 按一下 NRST 复位，观察 SPLASH 开机动画
```

### 常见编译问题

| 错误 | 原因 | 解决 |
|:---|:---|:---|
| `#7 unrecognized token` | 注释含 Unicode box-drawing 字符 (`┌└├│`) | 改用纯 ASCII 画框 (`+` `-` `|`) |
| `#77-D no storage class` | 同上, ARMCC V5 C89 限制 | 同上 |
| `#27-D` 字符串警告 | 中英文字符串拼接 | 用 hex escape 或分两行 |
| Flash Download failed | ST-Link 连接问题 / NRST 脚故障 | 手动 NRST→GND 拉低后点击 Download |

---

## ESP8266 固件编译与烧录

### 烧录前配置

在 Arduino IDE 中打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`，修改以下三个宏：

```cpp
#define ONENET_PRODUCT_ID     "你的产品ID"       // ← 从 OneNET 平台获取
#define ONENET_DEVICE_NAME    "你的设备名"        // ← 从 OneNET 平台获取
#define ONENET_TOKEN          "你的设备Token"     // ← 从 OneNET 平台生成
```

### 烧录步骤

```
1. ESP8266-01S 连接 USB-TTL (CH340G):
   ┌──────────────┐
   │ ESP8266-01S  │
   │ TX  ──── RX  │ CH340G
   │ RX  ──── TX  │
   │ GND ──── GND │
   │ VCC ──── 3.3V│
   │ CH_PD ─ 3.3V │ ← EN 脚拉高
   │ GPIO0 ─ GND  │ ← 烧录模式 (上电前接 GND)
   └──────────────┘

2. Arduino IDE 设置:
   - Board: Generic ESP8266 Module
   - Flash Size: 1M (no SPIFFS)
   - CPU Frequency: 80 MHz
   - Upload Speed: 115200

3. GPIO0 接 GND → 上电 → 点击 Upload → 等待编译+上传完成

4. 断开 GPIO0-GND → 重新上电 → 打开 Serial Monitor (115200 bps)
   观察是否有配网热点广播
```

### 首次配网

```
1. ESP8266 上电 → 手机搜 WiFi → 连 "STM32_WPT_Config" (密码: wpt2026conf)
2. 手机会自动弹出配网页 (WiFiManager)，选择你的家庭 WiFi 并输入密码
3. 点击 Save → ESP8266 自动连接家庭 WiFi → 连接 OneNET MQTT
4. 配网成功后，STM32 串口会收到 "STATUS:ONLINE" 帧
```

---

## W25Q128 Flash 字库烧录

> ⚠️ **仅首次使用或字库损坏时需要**。如果购买的是预烧录好的 W25Q128，跳过此步。

W25Q128 存储 GB2312 全字库 (20897 汉字 + 95 ASCII + 31 图标)，通过 CH341A USB-SPI 编程器烧录。

### 物理接线 (V5.0)

```
CH341A 编程器              目标板 (W25Q128 已焊在板上)
┌──────────────┐           ┌─────────────────────┐
│ CS   ─── 白 ──────────── PB12 (FLASH_CS)      │
│ CLK  ─── 黄 ──────────── PA5  (SPI1_SCK)      │
│ MOSI ─── 蓝 ──────────── PA7  (SPI1_MOSI)     │
│ MISO ─── 绿 ──────────── PA6  (SPI1_MISO)     │
│ GND  ─── 黑 ──────────── GND                  │
│ 3.3V ─── 红 ──────────── 3.3V                 │
└──────────────┘           └─────────────────────┘

⚠️ 关键警告:
  - CH341A 跳线帽必须插在 3.3V (5V 会烧毁 W25Q128!)
  - STM32 必须完全断电 (拔掉 USB / 断开 12V)
  - 杜邦线母对母, 直接夹板端排针
```

### 烧录操作

```bash
# 1. 进入工具目录
cd ch341

# 2. 安装依赖 (仅首次)
pip install -r requirements.txt

# 3. 生成字库并烧录 (一条命令完成)
python burn_flash.py

# burn_flash.py 自动执行:
#   Step 1: 生成 GB2312 全字库 2MB 镜像 (generate_font.py)
#   Step 2: 备份当前 Flash 内容 → backup.bin
#   Step 3: 融合开机画面 + 字库 → 写入 16MB
#   Step 4: 逐字节校验 (Verify OK = 成功)
```

> 详细操作指南: [ch341/README.md](ch341/README.md)

---

## OneNET 云平台配置

### 1. 注册与创建产品

```
1. 打开 https://open.iot.10086.cn/ → 注册/登录
2. 进入 OneNET Studio (新版)
3. 控制台 → 新建产品:
   - 产品名称: WPT_PWM (可自定义)
   - 节点类型: 直连设备
   - 接入协议: MQTT
   - 数据格式: JSON
4. 创建成功 → 记录 产品 ID (如 1iS397oJFL)
```

### 2. 添加设备

```
1. 产品详情 → 设备管理 → 添加设备
2. 设备名称: 如 20260001
3. 生成设备 Token (过期时间建议 1 年)
4. 记录: 产品ID + 设备名称 + Token
```

### 3. 定义物模型

在 OneNET Studio 产品详情 → 物模型 → 添加以下属性:

**传感器 (只读)**:
| 标识符 | 名称 | 数据类型 | 单位 | 说明 |
|:---|:---|:---|:---|:---|
| `V` | 电压 | float | V | 0-50V |
| `I` | 电流 | float | A | 0-5A |
| `F` | 频率 | int32 | Hz | 95000-150000 |

**控制器 (可读写)**:
| 标识符 | 名称 | 数据类型 | 说明 |
|:---|:---|:---|:---|
| `Switch` | 启停开关 | bool | true=开, false=关 |
| `SetFreq` | 设定频率 | int32 | 95000-150000 Hz |

STM32到ESP8266的串口遥测固定为 `{"V":xx,"I":xx,"F":xx,"S":x}\n`。其中 `S=0/1/2/3` 分别表示 IDLE、SWEEP、RUNNING、FAULT；SWEEP和RUNNING发送实际PWM频率，其余状态强制 `F=0`。OneNET的 `Switch` 只有在 `S=2` 时为 `true`，扫频阶段仍视为过渡态。

### 4. 填入凭证

将产品 ID、设备名、Token 填入 ESP8266 固件的对应宏 → 重新编译烧录 ESP8266。

---

## 网页控制台部署

网页控制台是纯静态 HTML/JS/CSS，部署在 Cloudflare Pages，通过 OneNET HTTP API 直连云平台。

### 配置

编辑 `ONENETapp/js/config.js`，填入 OneNET API 凭证:

```js
const ONENET_CONFIG = {
    productId: '你的产品ID',
    deviceName: '你的设备名',
    token: '你的Token'
};
```

### 部署到 Cloudflare Pages

```
方式 1 — Git 自动部署 (推荐):
  1. Fork ONENETapp/ 目录到独立 GitHub 仓库
  2. 登录 Cloudflare Dashboard → Workers & Pages → Create → Pages
  3. Connect Git → 选择仓库 → 设置:
     - Build command: (留空)
     - Output directory: /
  4. Deploy → 获得 https://xxx.pages.dev 域名

方式 2 — 手动上传:
  1. Cloudflare Pages → Direct Upload
  2. 拖入 ONENETapp/ 整个文件夹
  3. 部署完成
```

### 访问

```
登录页: https://你的域名/login.html
默认账号: admin
默认密码: 123456789

登录后自动跳转仪表盘，可切换监控/控制/历史/设置页面。
```

> 详细文档: [ONENETapp/README.md](ONENETapp/README.md)

---

## 微信小程序配置

微信小程序代码位于 `安卓app/` 目录。

### 前置条件

- 微信小程序 AppID (在 [mp.weixin.qq.com](https://mp.weixin.qq.com) 注册)
- Node.js 桥接服务器 (用于中转 OneNET API，因为小程序不能直连第三方 API)

### 部署桥接服务器

```
1. 打开 https://railway.app → Sign in with GitHub
2. New Project → Deploy from GitHub → 选择 WPT_PWM 仓库
3. Root Directory 设为: 安卓app/server
4. Railway 自动检测 Node.js 并部署
5. 获取生成的域名 (如 xxx.up.railway.app)
```

### 配置小程序

```js
// 编辑 utils/config.js，修改以下内容:
const ONENET_CONFIG = {
    productId: '你的产品ID',
    deviceName: '你的设备名',
    token: '你的Token'
};
const BRIDGE_URL = 'https://xxx.up.railway.app';  // Railway 桥接地址
```

### 导入微信开发者工具

```
1. 下载 微信开发者工具 (https://developers.weixin.qq.com/miniprogram/dev/devtools/download.html)
2. 打开工具 → 导入项目 → 选择 安卓app/ 目录
3. 填入 AppID → 编译预览
4. 测试通过后 → 上传 → 提交审核 → 发布
```

> 详细文档: `安卓app/部署文档.md`

---

## 首次上电与使用

### 开机流程

```
1. 接通 12V DC → STM32 上电
   ├── PC13 HEARTBEAT 闪烁 (MCU 存活指示)
   ├── 4 LED 自检全亮 ~500ms → 全灭
   └── TFT SPLASH 开机动画 (~4.8s, 背光渐亮 + 逐字)

2. ESP8266 自动联网 (约 3-5s)
   ├── WIFI LED: 快闪 (配网中) → 慢闪 (连接中) → 常亮 (已在线)
   └── TFT 右上角 WiFi 图标 + MQTT 图标

3. 进入主菜单 (SYS_IDLE)
```

### 基本操作流程

```
按键功能速查:
  KEY0 (PB9) — 电源开关: 单击 = 开/关 12V
  KEY1 (PB8) — 返回: 单击=上一页, 双击=主菜单
  KEY2 (PB7) — UP/加: 菜单上移/数值增加
  KEY3 (PB6) — DOWN/减: 菜单下移/数值减少
  KEY4 (PB5) — 确定: 进入子菜单/确认选项
            — PWM启停: 在仪表盘页 单击=启动/停止

操作流程:
  ┌─ 开机 ──→ 仪表盘 (IDLE)
  │
  ├─→ KEY0 单击 ──→ 12V 通电, POWER LED 亮
  │   再单击 KEY0 ──→ 12V 断电 (如 PWM 正在运行会自动停止)
  │
  ├─→ KEY4 单击 ──→ 软启动开始
  │   STATUS LED 慢闪 (SWEEP 按当前双档配置降至保存目标)
  │   → STATUS LED 常亮 (RUNNING, 频率可调节)
  │   → KEY4 再单击 → 停止 PWM, 回 IDLE
  │
  ├─→ KEY2/KEY3 ──→ 浏览菜单
  ├─→ KEY4 ──→ 进入子页面
  └─→ KEY1 ──→ 返回 / 双击→主菜单

远程控制 (网页/小程序):
  开关: Toggle Switch → 等价 KEY4 单击
  频率: 输入目标值 (kHz) → 自动 ×1000 → 云端 → ESP8266 → STM32
```

---

## LED / 按键 / 蜂鸣器说明

### LED 指示灯

| LED | 引脚 | 颜色 | 行为 | 含义 |
|:---|:---|:---|:---|:---|
| HEARTBEAT | PC13 | 蓝 (板载) | 500ms 闪烁 | MCU 程序正常运行, 类似心跳 |
| WIFI | PB4 | 蓝 | 常亮 | WiFi + MQTT 均已连接 |
| | | | 慢闪 (500ms) | 正在重连 |
| | | | 灭 | 离线 |
| POWER | PB3 | 绿 | 常亮 | 12V 后级已开启 |
| | | | 灭 | 12V 后级关闭 |
| STATUS | PA15 | 黄 | 灭 | IDLE / FAULT 态 (PWM 未运行) |
| | | | 慢闪 (500ms) | SWEEP 扫频中 |
| | | | 常亮 | RUNNING (PWM 工作中) |

### 按键行为表

| 按键 | 引脚 | 模式 | 单击 | 双击 | 长按 (3s) |
|:---|:---|:---|:---|:---|:---|
| KEY0 | PB9 | 单击仅 | 切换 12V 电源 | — | — |
| KEY1 | PB8 | 含双击 | 返回上一页 | 跳转主菜单 | — |
| KEY2 | PB7 | 单击仅 | 光标上移 / 数值+ | — | — |
| KEY3 | PB6 | 单击仅 | 光标下移 / 数值- | — | — |
| KEY4 | PB5 | 单击+长按 | 确定 / PWM启停 | — | 仅 WiFi 配网页请求清除凭证 |

### 蜂鸣器

| 场景 | 蜂鸣器行为 |
|:---|:---|
| 过流故障 (I > 5A) | BEEP 持续响, 直到 KEY4 复位 |
| FAULT 页 KEY4 复位 | 停止蜂鸣 |

---

## TFT 屏幕界面

### 页面结构

```
MAIN_MENU (主菜单)
  ├── SWEEP                    ← 按锁定档位显示实际软启动进度
  ├── MONITOR_SUB_MENU
  │   ├── MONITOR_SUMMARY      ← F/V/I 综合监测
  │   ├── MONITOR_FREQ         ← 频率仪表盘
  │   ├── MONITOR_VOLT         ← 电压仪表盘
  │   └── MONITOR_CURR         ← 电流仪表盘
  ├── WIFI_SETUP               ← 联网状态；KEY4 长按请求清除凭证
  ├── SETTING
  │   ├── SETTING_LANG         ← 中文/English
  │   ├── SETTING_FREQUENCY    ← 低频/高频启动档位与目标
  │   ├── SETTING_SPACING      ← 字间距
  │   ├── SETTING_ICONS        ← 全局菜单光标
  │   └── SETTING_COLOR        ← 颜色方案
  └── FAULT                    ← 故障锁存页；KEY4 单击复位

共 15 页。设置菜单固定为语言、启动频率、字符间距、光标图标和配色方案五项；PA12 背光保持 GPIO 开关能力，不提供设置页面。
```

### 仪表盘说明

```
     ┌────────────────────────────┐
     │  [WiFi 图标]  [MQTT 图标]   │ ← Phase 0: 顶部右侧图标
     │                            │
     │        当前项目圆弧表盘       │ ← 频率/电压/电流独立页面
     │     外圈刻度 + 填充能量条     │
     │                            │
     │       综合页显示 F/V/I       │
     │                            │
     │    状态: OK / WRN / HI      │ ← Row 4: 状态
     │        12.5                 │ ← Row 5: 数值 (黄)
     │        电压 V               │ ← Row 6: 标签 (青)
     └────────────────────────────┘

状态码:
  OK  = 正常运行      WRN = 接近阈值
  HI  = 超阈值警告    SWP = 扫频进行中
  IDL = 空闲停机
```

---

## 故障排查

### STM32 端

| 现象 | 可能原因 | 排查步骤 |
|:---|:---|:---|
| 上电白屏 | W25Q128 CS 浮空 / SPI 所有权异常 | V5.0.2 已加入 PB12 启动钳位与共享总线恢复；仍异常时检查 PB12 焊接和 PA5/PA6/PA7 |
| TFT 无显示 | SPI 接线错误 | 检查 PA4(CS)/PA5(SCK)/PA6(DC)/PA7(SDA) 焊接 |
| TFT 花屏 | DMA 超时 / SPI 冲突 | 按 NRST 复位, 观察 SPLASH 是否正常 |
| 按键无反应 | 焊接虚焊 | 用万用表蜂鸣档测按键两端到 STM32 引脚 |
| LED 不亮 | 正负极焊反 / 虚焊 | LED 长脚(+)串 220Ω→GPIO, 短脚(-)→GND |
| PC13 LED 不闪 | 程序卡死 | 检查 IWDG 是否复位, 排查 main 循环是否进入 |
| 无法下载程序 | NRST 脚故障 | 手动杜邦线 NRST→GND, 点击 Download, 松开 |
| PWM 无输出 | 12V 未开启 | 确认 POWER LED 亮 (KEY0 开了吗?) |
| 过流误触发 | ADC 零点偏移 / 采样线噪声 | 先断开 PB10 负载并冷启动校准；需强制重校准时清除配置扇区 0x300000/0x301000 后重启 |

### ESP8266 端

| 现象 | 可能原因 | 排查步骤 |
|:---|:---|:---|
| 搜不到配网热点 | ESP8266 未启动 / 已配过网 | 进入 WiFi 配网页，长按 KEY4 3s 发起清除凭证，再按提示确认 |
| 连不上 WiFi | 密码错误 / 信号差 | 用手机连同一 WiFi 测试 |
| MQTT 连不上 | Token 过期 / 产品 ID 错误 | 登录 OneNET 检查设备状态 |
| STATUS:ONLINE 未收到 | USART2 接线 | 检查 PA2→ESP RX, PA3→ESP TX (交叉!) |
| 网页控制无效 | Token 未替换 | ESP8266 固件中 ONENET_TOKEN 是占位值吗? |
| 指令去抖误拦 | 2s 内重复发同一指令 | 等 2s 再发, 或修改去抖窗口 |

### 网页端

| 现象 | 可能原因 | 排查步骤 |
|:---|:---|:---|
| 显示 `--` | OneNET 凭证未配置 | 进设置页 → OneNET 配置 → 填入产品ID/设备名/Token |
| `authentication failed` | Token 格式错误 | res 字段必须用复数 `devices` (不是 `device`) |
| 开关无效 | ESP 离线 | 检查 TFT 右上角 WiFi/MQTT 图标 |
| 页面空白 | JS 报错 | F12 控制台查看错误 |

---

## 项目文件结构

```
WPT_PWM_V5.0/
├── README.md                     ← 本文件
├── CLAUDE.md                     ← AI 开发规范 (引脚表/命名/注释/架构)
├── Keil_Project/                 ← STM32 固件 (~5926 行逻辑代码)
│   ├── Project.uvprojx           ← Keil 工程入口
│   ├── keilkill.bat              ← 清理编译产物
│   ├── Hardware/                 ← 硬件驱动层（含 SPI1 共享仲裁）
│   ├── User/                     ← 应用层 (main/状态机/网络/存储)
│   ├── System/                   ← SysTick 1ms + CRC 校验服务
│   ├── Start/                    ← CMSIS + 启动文件
│   └── Library/                  ← SPL V3.5.0 (只读)
├── Arduino_Project/              ← ESP8266 固件 (~522 行)
│   └── ESP8266_MQTT_Firmware/...ino
├── ONENETapp/                    ← 网页控制台 (Cloudflare Pages, ~3444 行)
│   ├── index.html                ← 仪表盘
│   ├── control.html              ← 远程控制
│   ├── monitoring.html           ← 实时监测
│   ├── history.html              ← 历史趋势
│   ├── alerts.html               ← 告警
│   ├── settings.html             ← 设置
│   ├── login.html                ← 登录
│   └── js/
│       ├── config.js             ← 数据模型 + XSS 防护
│       └── onenet.js             ← OneNET API 核心
├── 安卓app/                      ← 微信小程序 (~3303 行)
│   ├── utils/config.js           ← OneNET 配置
│   ├── pages/                    ← 6 页面
│   ├── custom-tab-bar/           ← 底部导航组件
│   ├── 操作手册.md
│   └── 部署文档.md
├── ch341/                        ← W25Q128 字库烧录工具链
│   ├── README.md                 ← 完整操作指南
│   ├── generate_font.py          ← GB2312 全字库生成器
│   ├── burn_flash.py             ← 烧录编排
│   └── flashrom-1.4/             ← flashrom 烧录工具
└── Claude_Files/                 ← AI 辅助文档
    ├── docs/                     ← 开发指南 + 架构师技能文件
    ├── diagrams/                 ← Visio 流程图
    └── tools/                    ← 文档生成工具
```

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|:---|:---|:---|
| **V5.1.0** | **2026-07-22** | **设置菜单固定为语言、启动频率、字符间距、光标图标、配色五项；配置升级为可迁移双档启动频率与全局光标；PWM 统一为20–200kHz，低频99.9kHz/100Hz步进与高频200kHz/1kHz步进分别扫频；独立电压、电流、频率表盘改为分段递增刻度与2倍主数值差分刷新。** |
| **V5.0.2** | **2026-07-19** | **STM32 全面优化：TIM1 原子更新与 PB10/PWM/FAULT 硬互锁；TIM3 500Hz ADC 双窗口及校准门控；SPI1 共享仲裁与超时恢复；W25Q128 越界保护；后台校验保存；Blackbox V2 双元数据、可恢复循环日志、故障前后各 5 秒快照；5键能力拆分；14页 UI 与 GPIO 背光清理；USART2 中断发送；S=0/1/2/3 协议对齐；统一调度、看门狗和 C89 边界清理** |
| **V5.0.1** | **2026-07-11** | **GPIO 全量重映射 + 5键系统 + 四灯系统: PA12→TFT_BL, PB12→W25Q128_CS, KEY0-KEY4 五键, WIFI/POWER/STATUS/HEARTBEAT 四灯, PB12 Flash CS 钳位防 SPI 总线冲突(白屏), MENU UP 键 wrapping 修复** |
| V5.0.0 | 2026-07-11 | 初始 GPIO 重映射: PA12→TFT_BL(GPIO), PB12→W25Q128_CS, TIM4 停用, PA10/PA11 移除, PC13 HEARTBEAT 新增 |
| V4.5.2 | 2026-07-11 | SPI时序回归 + DMA修复: 花屏根治, 18MHz恢复, Flash批量读, ROM优先中文/图标, EMA全状态更新 |
| V4.5.1 | 2026-07-02 | 全平台安全审查修复 (16项): Token占位符 + DMA超时 + 环形缓冲 + 黑匣子持久化 |
| V4.5.0 | 2026-07-02 | 设置系统重构: 8页PIC预览 + 字间距/亮度/颜色全功能 + 纯像素间隙渲染 |
| V4.3.2 | 2026-06-29 | W25Q128 全字库: 初始化铁序 + CRC32修正 + SPLASH纯代码 + CS翻转二分搜索 |
| V4.3.0 | 2026-06-22 | W25Q128 16MB SPI Flash: 全字库 + 黑匣子 + 双副本参数 + 四大硬件防线 |
| V4.2.1 | 2026-06-17 | 全项目 README 重写 + 4分支统一分支表 |
| V4.2.0 | 2026-06-17 | 全平台版本号统一 + TFT字库修复 + 底部栏简化 + 16轮全链路审查 |
| V4.1.0 | 2026-06-11 | TFT 彩屏 9 页面 + 圆弧能量条仪表盘 + Sys_Safety 独立安全 + EMA双级滤波 |
| V4.0.0 | 2026-06-01 | 系统全局状态机 + Sys_Core 模块化 + 全链路数据一致性 |

---

## 分支

| 分支 | 本地目录 | 版本 | 显示 | 协议 | 说明 |
|:---|:---|:---:|:---:|:---|:---|
| `master` | `WPT_PWM_V0.0` | V0.0.0 | 无 | 无 | 裸机固件基版 |
| `1.0LAN` | `WPT_PWM_NetAssistant_LAN_V1.0` | V1.0.0 | OLED | NetAssist TCP | 局域网调试 |
| `2.0WAN` | `WPT_PWM_Bemfa_WAN_V2.0` | V2.0.0 | OLED | 巴法云 TCP | 远程控制 |
| `3.0ONENET` | `WPT_PWM_ONENET_V3.0` | V3.0.0 | OLED | OneNET MQTT | 物联网双脑架构 |
| `4.0TFT` | `WPT_PWM_V4.0_ONENET_TFT` | V4.5.2 | TFT 彩屏 | OneNET MQTT | 4键+6灯旧版 PCB |
| **`5.0`** | **`WPT_PWM_V5.0`** | **V5.1.0** | **TFT 彩屏** | **OneNET MQTT** | **5键+4灯新版 PCB (当前)** |

## 文档

| 文档 | 说明 |
|:---|:---|
| [CLAUDE.md](CLAUDE.md) | AI 开发规范 (命名/注释/安全/架构/引脚表) |
| [开发指南](Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md) | 完整开发者指南 (V5.1.0) |
| [ONENETapp/README.md](ONENETapp/README.md) | 网页控制台部署文档 |
| [ch341/README.md](ch341/README.md) | CH341A Flash 字库烧录操作指南 |
| [安卓app/部署文档.md](安卓app/部署文档.md) | 微信小程序 + Railway 桥接部署 |

## 许可

MIT
