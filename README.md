# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-red)]()
[![Firmware](https://img.shields.io/badge/Firmware-V6.0-brightgreen)]()
[![App](https://img.shields.io/badge/App-WeChat%20Mini%20Program-07C160)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。采用 **Dual-MCU 双脑架构**：STM32 (SPL V3.5.0) 全桥 PFM 发波与保护，ESP8266 (Arduino) 独立 MQTT 固件连接 **OneNET 物模型**。支持 OLED 7 界面状态机本地控制、Cloudflare Pages 网页控制台、微信小程序远程遥控。应用于植入式医疗设备无线充电。

---

## 目录

1. [系统架构](#系统架构)
2. [硬件配置](#硬件配置)
3. [关键特性](#关键特性)
4. [固件开发](#固件开发)
5. [OLED 界面状态机](#oled-界面状态机)
6. [远程控制](#远程控制)
7. [LED 指示](#led-指示)
8. [按键操作](#按键操作)
9. [安全保护](#安全保护)
10. [分支说明](#分支说明)
11. [项目结构](#项目结构)
12. [快速开始](#快速开始)
13. [文档](#文档)

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
     │ USART2 115200
     │ 纯文本 JSON (零 AT 指令)
     ▼
┌──────────────────────────────────────┐
│         STM32F103C8T6 (物理脑)        │
│  ────────────────────────────────────│
│  • TIM1 全桥 PWM + PFM 调功           │
│  • ADC1+DMA 双通道采集 + 滑动滤波     │
│  • OLED 128x64 I2C + 7界面状态机      │
│  • 双按键 FSM (单击/双击/长按)        │
│  • 非阻塞软启动扫频 150k→100kHz       │
│  • 频率渐变斜坡 500Hz/10ms            │
│  • 过流锁存保护                       │
│  • 4 LED 状态灯                       │
└──────────────────────────────────────┘
```

### 通信协议

STM32 ↔ ESP8266 通过 USART2 (115200 8N1) 纯文本 JSON 通信，**零 AT 指令**。

**STM32 → ESP8266 (遥测, 每 500ms)**:
```json
{"V":12.50,"I":1.23,"F":100000,"S":2}
```

| 字段 | 含义 | 范围 |
|:---|:---|:---|
| `V` | 电压 (V) | 0 ~ 50 |
| `I` | 电流 (A) | 0 ~ 10 |
| `F` | 当前 PWM 频率 (Hz) | 95000 ~ 150000 |
| `S` | 软启动状态 | 0=IDLE, 1=SWEEP, 2=DONE, 3=FAULT |

**ESP8266 → STM32 (指令)**:
```
CMD:ON                  → 触发软启动 (仅 IDLE)
CMD:OFF                 → 关断逆变器 (任意状态)
CMD:SETFREQ:<Hz>        → 频率渐变到目标值 (仅 DONE)
STATUS:ONLINE           → ESP8266 已联网 (上升沿, 只发一次)
CMD:CLEAR               → STM32→ESP8266: 清除WiFi配网 (KEY0 长按)
```

---

## 硬件配置

### 主控板

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM, LQFP-48 |
| WiFi | ESP8266-01 | Arduino MQTT 固件, 独立 3.3V LDO (≥500mA) |
| 显示 | SSD1315 128x64 | 0.96 寸 4 针 OLED, 软件 I2C (PA11=SCL, PA12=SDA) |
| 栅极驱动 | IR2103S | 高低侧驱动, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量, 接入 PA0 |
| 电压采样 | 20:1 分压 | 电阻网络, 接入 PA1 |

### 引脚映射

| 引脚 | 功能 | 连接 |
|:---|:---|:---|
| PA0 | ADC_CH0 | 电流传感器 (CC6920-10A) |
| PA1 | ADC_CH1 | 电压分压 (20:1) |
| PA2 | USART2_TX | ESP8266 RXD |
| PA3 | USART2_RX | ESP8266 TXD |
| PA7 | TIM1_CH1N | 半桥左低侧 |
| PA8 | TIM1_CH1 | 半桥左高侧 |
| PA9 | TIM1_CH2 | 半桥右高侧 |
| PA11 | I2C_SCL | OLED SCL |
| PA12 | I2C_SDA | OLED SDA |
| PB0 | TIM1_CH2N | 半桥右低侧 |
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN |
| PB3 | GPIO (PP) | WiFi LED |
| PB4 | GPIO (PP) | PWM LED |
| PB5 | GPIO (PP) | Ready LED |
| PB12 | GPIO (IPU) | KEY0 |
| PB13 | GPIO (IPU) | KEY1 |
| PC13 | GPIO (PP) | 心跳 LED (低有效) |

> ⚠️ ESP8266 需独立 3.3V LDO (AMS1117-3.3, ≥500mA), 100μF+0.1μF 去耦。STM32 板载 LDO 无法满足 WiFi 突发电流。

---

## 关键特性

### PWM 与 PFM 调功

- **频率范围**: 95kHz ~ 150kHz, 1kHz 步进
- **占空比**: 50% 锁定 (TIM1 CCRx = ARR/2)
- **死区**: 1000ns (编译期宏 `DEADTIME_NS`, BDTR DTG 线性段)
- **防偏磁**: 周期 ticks 强制偶数 + 影子寄存器原子更新 (`UDIS`→写 ARR+CCR→`UG`→清 `UDIS`)
- **四通道互补输出**: CH1+CH1N+CH2+CH2N, 全桥驱动

### 频率量化

TIM1_CLK = 72MHz。`ticks = 72M / freq_Hz`, 奇数强制 +1 (防偏磁)。并非所有 kHz 值可达:

| 目标 kHz | ticks | 实际 Hz | 实际 kHz |
|:---|:---|:---|:---|
| 95 | 758 | 94987 | 94 |
| 100 | 720 | 100000 | 100 |
| 105 | 684 | 105263 | 105 |
| 110 | 654 | 110092 | 110 |
| 150 | 480 | 150000 | 150 |

微信小程序和网页端滑动选频时只显示可达值。

### 非阻塞软启动扫频

```
Inverter_SoftStart_Trigger() → SS_SWEEP
  Inverter_SoftStart_Task() 每 10ms 步进:
    150kHz → 100kHz (向下扫频)
    200Hz/10ms, ~2.5s 完成
    OLED 实时频率 + 进度条 [########  ]
  SS_DONE → 正常运行, 可随时停止/调频
```

### 频率渐变斜坡

仅 `SS_DONE` 时可触发, 500Hz/10ms (50kHz/s) 平滑过渡:

```
CMD:SETFREQ:108000 → Inverter_FreqRamp_Trigger(108000)
  → Inverter_FreqRamp_Task() 每 10ms:
    100000 → 100500 → 101000 → ... → 108000
  新目标覆盖旧目标, 方向自适应
```

### 串口安全规则

- `USART2_IRQHandler` 先检查 `USART_FLAG_ORE` (过载), 否则 ISR 死锁
- `ESP8266_CopyRxFrame()` 原子复制 + 清帧, 临界区内保护尾字节
- `ESP8266_SendString` 仅验证 TXE (TX 缓冲空), 不等待 TC (TX 完成)
- 帧分隔符兼容 `\r`/`\n`/`\r\n`

### 调度: SysTimer 时间戳差值法

整个系统使用单一时基 (SysTick 1ms), **不在 ISR 里设任何 Flag**:

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (SysTimer_GetTick() - last >= PERIOD_MS) {
        last = SysTimer_GetTick();
        // 业务逻辑
    }
}
```

uint32_t 无符号减法天然防溢出 (49.7 天窗口)。

### OLED 安全规则

- `OLED_Clear()` 仅在状态迁移时调用 (100ms 耗时, 过多会阻塞保护任务)
- 日常刷新用 16 字符全宽行覆盖, 不调用 Clear
- Cortex-M3 无硬件 FPU, 浮点运算用 `float` 非 `double`, 查表替代 `pow10`

---

## 固件开发

### STM32 (Keil MDK-ARM V5)

**环境**: ARMCC V5.06 update 5, Device Pack Keil.STM32F1xx_DFP.2.2.0

**依赖**: STM32 Standard Peripheral Library (SPL) V3.5.0 (位于 `Keil_Project/Library/`, 只读不修改)

```
Keil_Project/
├── Hardware/          ← 硬件驱动层 (只增删 .c/.h, 不改架构)
│   ├── Pwm_Driver     ← TIM1 全桥PWM 硬件抽象 (95-150kHz, 1000ns死区)
│   ├── Inverter_Control ← 软启动状态机 + 频率渐变 (应用层)
│   ├── Adc_Driver     ← ADC1+DMA 双通道 + 64样本互质相位滤波 + 自动零点校准
│   ├── Key_Driver     ← 双按键FSM (单击/双击/长按, 10ms去抖)
│   ├── Oled_Driver    ← SSD1315 I2C 驱动 + 浮点显示
│   ├── Esp8266_Driver ← USART2 ISR + 环形缓冲 + 纯JSON透传
│   ├── Ui_Controller  ← 7界面状态机 + 按键分发 + LED联动
│   └── Led_Driver     ← 4 LED 状态管理
├── System/            ← 系统服务层
│   └── Sys_Timer      ← SysTick 1ms + DWT周期计数器 (亚毫秒定时)
├── User/              ← 应用层
│   ├── Main.c         ← 主入口, 4阶段启动 + 非阻塞主循环
│   ├── App_Network    ← 联网管理 + 遥测门控 + 指令接收
│   ├── stm32f10x_conf.h ← SPL 头文件配置
│   └── stm32f10x_it.c   ← ISR (SysTick+USART2, 故障关断PWM)
├── Library/           ← SPL V3.5.0 (只读)
└── Start/             ← 启动文件 + system_stm32f10x
```

**编译配置**:
- 工程文件: `Project.uvprojx`
- 输出: `Objects/Project.hex` (HEX-80), `Objects/Project.axf` (调试)
- 编译后处理: `Target 1.BAT`

### ESP8266 (Arduino IDE)

**环境**: Arduino IDE 1.8.x / 2.x, 开发板 Generic ESP8266 Module, Flash 1M, CPU 80MHz

**库依赖**:
- ESP8266WiFi (内置)
- PubSubClient (Nick O'Leary)
- ArduinoJson v7 (Benoit Blanchon)
- WiFiManager v2.x (tzapu)

**关键配置宏** (在 `.ino` 文件顶部):
| 宏 | 值 | 说明 |
|:---|:---|:---|
| `MQTT_SERVER` | `mqtts.heclouds.com` | OneNET MQTT 服务器 |
| `MQTT_PORT` | `1883` | 非 TLS 端口 (s=Studio) |
| `ONENET_PRODUCT_ID` | `1iS397oJFL` | 产品 ID |
| `ONENET_DEVICE_NAME` | `20260001` | 设备名称 |
| `ONENET_TOKEN` | `version=2018-10-31&res=...` | 设备密钥 Token |

**核心流程**:
```
setup()
  ├─ Serial.begin(115200)
  ├─ WiFiManager.autoConnect("STM32_WPT_Config")
  │   ├─ 有凭据 → 连路由器
  │   └─ 无凭据 → 开 AP 热点 (手机配网)
  ├─ mqttClient.setServer + setCallback
  └─ publicMqttClient.setServer

loop()
  ├─ Mqtt_Task_Loop()        连接状态机 + 双 Broker 心跳
  └─ Serial_Parse_Read_Loop() 逐字节读行 + 前缀匹配 + MQTT 发布
```

**V6.0 关键改进**:
- **全模块统一命名**: `Module_Name_Action_Object()` 帕斯卡+下划线, 头文件保护 `MODULE_NAME_H`
- **PWM 拆分**: `Pwm_Driver` (硬件抽象) + `Inverter_Control` (软启动+斜坡)
- **ADC 防混叠**: DWT 周期计数器 + 互质采样周期 (144241 cycles), 64 样本窗口, 自动零点 EMA 追踪
- **故障保护**: HardFault/MemManage/BusFault/UsageFault 死循环前强制关断 PWM
- **ESP8266 协议安全**: `Str_Starts_With()` 前缀匹配替代 `strstr()`, 防子串误触发
- **连接状态机**: `Conn_State` 显式状态枚举 (IDLE→WIFI→MQTT→ONLINE→FAILED)

---

## OLED 界面状态机

```
上电 → main.c App_Net_StartConnect() → 界面2(连接中)
         ├─ STATUS:ONLINE → 界面3(READY) → KEY0 Start
         └─ 15s×3次超时 → 界面1(初始) + "WiFi Failed x3"
                          └─ 按 KEY0 → 重新连接
```

| 界面 | 状态 | OLED 显示 | 按键 |
|:---|:---|:---|:---|
| **1** | INIT | "Press KEY0 WiFi" + 错误信息 | KEY0=连WiFi, 长按=清除配网 |
| **2** | CONNECTING | "Retry: X/3" | 无 (等待中) |
| **3** | READY | "Press KEY0 Start" | KEY0=触发扫频 |
| **4** | SWEEPING | 实时频率 + 进度条 `[####      ]` | KEY0/KEY1=停止 |
| **5** | RUNNING | V/I/F + "K0:Stop K1:+1k" | KEY0=停止, KEY1=+1kHz |
| — | FAULT | "!!! Over Current !!!" | KEY0/KEY1=复位 |
| **6/7** | 双击切页 | 控制面板 / 监测模式 | 所有已连接界面可双击 |

**OneNET 遥测门控**: 仅在界面 >= 3 (READY) 时发送遥测, 界面 1/2 时设备在 OneNET 上**离线**。这样网页/小程序看到的"在线" = 用户已可操作。

---

## 远程控制

### OneNET 物模型属性

| 标识符 | 类型 | 方向 | 说明 |
|:---|:---|:---|:---|
| `V` | double | 上报 | 电压 (V) |
| `I` | double | 上报 | 电流 (A) |
| `F` | int64 | 上报 | 当前 PWM 频率 (Hz) |
| `Switch` | bool | 上报+下发 | PWM 开关 (true=运行) |
| `SetFreq` | int64 | 上报+下发 | 目标频率 (Hz), 95k~150k, 1kHz 步进 |

### 指令下发路径

```
小程序/网页 → OneNET HTTP API (POST set-device-property)
           → OneNET MQTT → ESP8266 → CMD:XXX\n → STM32
```

### 网页控制台

- 地址: **https://wptonenet.483763727.workers.dev**
- 仓库: [Ran-sh/WPT_Onenet_IoT](https://github.com/Ran-sh/WPT_Onenet_IoT)
- 部署: Cloudflare Pages (监听 `gh-pages` + `master`)
- 功能: 仪表盘 / 实时监控 / 远程控制 / 历史图表 / 数据模型管理 / PWA 离线

### 微信小程序

- 仓库: [Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM) 子目录 `安卓app/`
- 架构: OneNET HTTP API 直连, 零中间桥接
- 轮询: 2 秒间隔单次 API 调用 (fetchAll)
- 在线检测: 数据时间戳超过 10s → 离线
- Switch 复位: 3s 验证 + 自动重发
- Swiper: 首次连接同步一次频率, 之后仅受手指控制

---

## LED 指示

| 状态 | PB3 WiFi | PB4 (Start) | PB5 (KEY1) |
|:---|:---|:---|:---|
| 硬件未就绪 / 初始化 | 慢闪 | 灭 | 灭 |
| 连接 WiFi 中 | **快闪** | 灭 | 灭 |
| 已连接, 待机 (READY) | 常亮 | **亮** | 灭 |
| 扫频中 (SWEEPING) | 常亮 | 灭 | 灭 |
| 运行中 (RUNNING) | 常亮 | 灭 | **亮** |
| 故障 (FAULT) | 常亮 | 灭 | **亮** |

- **PC13**: 心跳, 500ms 翻转 (系统运行指示)
- **PB3**: WiFi 连接状态 — 慢闪(等待) → 快闪(连接中) → 常亮(已连接)
- **PB4**: Start 按钮可用 (仅在 READY 界面)
- **PB5**: KEY1 可 +1kHz 或复位 (RUNNING/FAULT)

---

## 按键操作

KEY0 (PB12) 和 KEY1 (PB13) 均内部上拉 (IPU), 10ms 扫描, 八态 FSM:

| 事件 | KEY0 | KEY1 |
|:---|:---|:---|
| **单击** | 连WiFi / 触发扫频 / 关断 / 复位故障 | 关断扫频 / +1kHz / 复位故障 |
| **双击** | 切页 (控制面板 ↔ 监测模式) | — |
| **长按 (>3s)** | **清除 ESP8266 WiFi 配网** | — |

按键事件消费机制: `KEY_Get_Event()` 阅后即焚 (1=单击, 2=双击, 3=长按)。

---

## 安全保护

| 场景 | 机制 | 动作 |
|:---|:---|:---|
| 上电 | `PWM_Init(MOE=OFF)` | 硬件级安全, 全桥无输出 |
| 过流 | `Inverter_SoftStart_Fault()` | 紧急关断 + SS_FAULT 锁存 (仅 KEY0/KEY1 可复位) |
| 频率越界 | `PWM_SetFrequency` 硬钳位 95k~150k | 拒绝执行 |
| 死区不足 | 编译期 `__deadtime_linear_check` typedef 断言 | 编译失败 |
| 占空比偏移 | 影子寄存器 + `UDIS` 原子更新 | 防止周期裁剪磁饱和 |
| ESP8266 断线 | ESP8266 自管理重连 | STM32 无感, 继续发 JSON |
| STM32 掉电后上电 | `PWM_Init(MOE=OFF)` | 安全态启动 |
| 连接超时 | 15s×3 次硬件复位重试 | 失败回初始界面 + 错误提示 |

---

## 分支说明

| 分支 | 版本 | 协议 | 说明 |
|:---|:---:|:---|:---|
| `master` | V1.0 | — | 裸机基版 |
| `LAN` | V3.3 | NetAssist TCP | 局域网 PC 调试 |
| `WAN` | V4.0 | 巴法云 MQTT | 历史版本 |
| **`ONENET`** | **V6.0** | OneNET MQTT | 双脑架构 + 模块化命名 + 连接状态机 |

---

## 项目结构

```
WPT_PWM/
├── Arduino_Project/           # ESP8266 Arduino MQTT 固件
│   └── ESP8266_MQTT_Firmware/
│       └── ESP8266_MQTT_Firmware.ino
├── Keil_Project/              # Keil MDK STM32 固件
│   ├── Hardware/              # 硬件驱动层
│   ├── System/                # 系统服务层
│   ├── User/                  # 应用层
│   ├── Library/               # SPL V3.5.0 (只读)
│   ├── Start/                 # 启动文件
│   └── Project.uvprojx        # Keil 工程
├── ONENETapp/                 # 网页控制台 (→ Ran-sh/WPT_Onenet_IoT)
├── 安卓app/                    # 微信小程序 (pages/index/)
├── Railway_Deploy/            # Railway 桥接 (→ Ran-sh/WPT_Railway, 历史)
├── Claude_Files/              # AI 文档与工具
│   ├── docs/                  # 开发指南 (MD+DOCX)
│   └── tools/                 # ngrok 脚本 / DOCX 生成
├── CLAUDE.md                  # AI 开发规范
└── README.md                  # 本文件
```

---

## 快速开始

### 1. 烧录 STM32

1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. Rebuild → ST-Link Download
3. OLED 显示 "Wireless Charge" → "Booting ESP..." → 自动进入连接中界面

### 2. 烧录 ESP8266

1. ESP8266-01 接 USB-TTL (**GPIO0 接 GND**, VCC 接 3.3V)
2. Arduino IDE 安装库: ArduinoJson v7 + PubSubClient + WiFiManager
3. 打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
4. 修改凭证: `ONENET_PRODUCT_ID`, `ONENET_DEVICE_NAME`, `ONENET_TOKEN`
5. 开发板选 "Generic ESP8266 Module", Flash 1M, CPU 80MHz → 上传
6. **烧录后断开 GPIO0-GND** → 重新上电

### 3. 配网 (首次)

1. ESP8266 上电 → 开热点 `STM32_WPT_Config` (无密码)
2. 手机连上 → 自动弹出配网页 → 选 WiFi 输密码
3. 配网成功 → ESP8266 重启 → 自动连 OneNET
4. OLED: 连接中 → STATUS:ONLINE → READY

### 4. 网页控制台

浏览器打开 `https://wptonenet.483763727.workers.dev`, 登录 `admin / 123456789`。

### 5. 微信小程序

WeChat DevTools 打开 `安卓app/` 目录, 上传体验版。

---

## 文档

| 文档 | 格式 | 说明 |
|:---|:---|:---|
| [WPT无线充电系统-从零搭建全指南](Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md) | MD+DOCX | 完整开发指南: 概述→硬件→OneNET→STM32→ESP8266→网页→小程序→联调→故障速查 |
| [CLAUDE.md](CLAUDE.md) | MD | AI 辅助开发规范 |

---

## 作者

**Rssss**

## 许可

MIT
