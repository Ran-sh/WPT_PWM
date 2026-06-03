# WPT_PWM — 物联网全桥谐振电源控制系统 (TFT 彩屏版)

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-red)]()
[![Display](https://img.shields.io/badge/Display-ST7735S%20128×160%20TFT-purple)]()
[![Firmware](https://img.shields.io/badge/Firmware-V6.2-brightgreen)]()
[![App](https://img.shields.io/badge/App-WeChat%20Mini%20Program-07C160)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V6.2** (2026-06-03) — 基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。V6.1 的硬件升级版：**OLED → 1.8 寸 TFT 彩屏**、2 键 → **4 键**、4 LED → **6 LED**、新增加**蜂鸣器告警**、PWM 从 PartialRemap 改为**默认映射**。采用 Dual-MCU 双脑架构：STM32 (SPL V3.5.0) 全桥 PFM 发波与保护，ESP8266 (Arduino) 独立 MQTT 固件连接 **OneNET 物模型**。支持 Cloudflare Pages 网页控制台、微信小程序远程遥控。应用于植入式医疗设备无线充电。

---

## 目录

1. [版本历史](#版本历史)
2. [系统架构](#系统架构)
3. [硬件配置](#硬件配置)
4. [启动流程](#启动流程)
5. [主循环调度](#主循环调度)
6. [PWM 全桥驱动](#pwm-全桥驱动)
7. [ADC 采集与滤波](#adc-采集与滤波)
8. [软启动与频率控制](#软启动与频率控制)
9. [TFT 界面状态机](#tft-界面状态机)
10. [远程控制](#远程控制)
11. [LED 指示](#led-指示)
12. [按键操作](#按键操作)
13. [蜂鸣器告警](#蜂鸣器告警)
14. [安全保护](#安全保护)
15. [中断服务](#中断服务)
16. [分支说明](#分支说明)
17. [项目结构](#项目结构)
18. [快速开始](#快速开始)
19. [文档](#文档)

---

## 版本历史

| 版本 | 日期 | 分支 | 主要变更 |
|:---|:---|:---|:---|
| V0.0 | 2024 | `master` | 裸机基版, 全桥 PWM + OLED + 按键 |
| V1.0 | 2024 | `1.0LAN` | NetAssist 局域网 UDP 调试 |
| V2.0 | 2024 | `2.0WAN` | 巴法云 MQTT TCP 协议 |
| V5.0 | 2025 | `3.0ONENET` | OneNET MQTT 物模型 + Dual-MCU 架构 |
| V6.0 | 2025 | `3.0ONENET` | 全模块命名规范 + 显式状态枚举 + ESP 前缀匹配 |
| V6.1 | 2026-06 | `3.0ONENET` | 8 项 Bug 修复 + 过流保护 + 代码质量提升 |
| **V6.2** | **2026-06** | **`4.0TFT`** | **OLED→TFT 彩屏 + 4 键 + 6 LED + 蜂鸣器 + PWM 默认映射 + 新引脚分配** |

### V6.2 硬件升级清单

| 模块 | V6.1 (3.0ONENET) | V6.2 (4.0TFT) | 说明 |
|:---|:---|:---|:---|
| 显示屏 | SSD1315 OLED 128×64 I2C | **ST7735S TFT 128×160 SPI** | 彩色图形界面, SPI1 硬件驱动 |
| 按键 | 2 (PB12/PB13) | **4 (PB5/PB7/PB8/PB9)** | 翻页/频率±/启停 |
| LED | 4 | **6** | 新增通信/电源/温度告警灯 |
| 蜂鸣器 | 无 | **有源 2.7kHz (PB15)** | 故障/过流声音告警 |
| TIM1 映射 | PartialRemap | **默认映射** | 释放 PA7 给 SPI1_MOSI, PB0 给 ADC_CH8 |
| ADC 电流 | PA0 (CH0) | **PB0 (CH8)** | 通道迁移 |
| ADC 电压 | PA1 (CH1) | **PB1 (CH9)** | 通道迁移 |
| 电源控制 | 无 | **PB10 12V 闸控制** | 待机切断功率级 |
| ESP8266 RST | 无独立控制 | **PA1 独立复位** | 更可靠的 WiFi 复位 |

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
│  • TIM1 全桥 PWM + PFM 调功 (默认映射) │
│  • ADC1+DMA 双通道 (PB0=CH8, PB1=CH9) │
│  • ST7735S 128×160 TFT SPI 彩屏       │
│  • 4 按键 FSM (单击/双击/长按)        │
│  • 6 LED + 有源蜂鸣器                 │
│  • 非阻塞软启动扫频 150k→100kHz       │
│  • 频率渐变斜坡 1kHz/10ms             │
│  • 过流保护 5A 硬件级关断              │
│  • PowerContrl 12V 动力电源闸          │
│  • IWDG 看门狗 + __WFI 休眠            │
└──────────────────────────────────────┘
```

### STM32 模块分层

```
Application  ┌─────────────────────────────────────────────┐
             │  App_Network    Ui_Controller                │
             │  (联网+遥测)     (TFT界面+按键+LED+蜂鸣器+过流) │
             │  Inverter_Control (软启动+频率斜坡)          │
             └─────────────────────────────────────────────┘
                              │
System       ┌────────────────┴────────────────────────────┐
             │  Sys_Timer (SysTick 1ms + DWT 周期计数器)    │
             └─────────────────────────────────────────────┘
                              │
Hardware     ┌────────────────┴────────────────────────────┐
             │  Pwm_Driver  Adc_Driver  Key_Driver         │
             │  Tft_Driver  Led_Driver  Buzzer_Driver       │
             │  Esp8266_Driver  IWDG (1.6s 独立看门狗)       │
             └─────────────────────────────────────────────┘

依赖方向: Hardware → System → Application (严格单向, 永不逆转)
```

### 通信协议

STM32 ↔ ESP8266 通过 USART2 (115200 8N1) 纯文本 JSON 通信，**零 AT 指令**。

**Iron Rule**: STM32 绝不发 AT 指令 | ESP8266 绝不碰 PWM/ADC | 前缀匹配 `Str_Starts_With` 防协议误触发

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
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM, LQFP-48, HSE 8MHz→PLL 72MHz |
| WiFi | ESP8266-01 | Arduino MQTT 固件, 独立 3.3V LDO (≥500mA), PA1 RST + PB11 CH_PD |
| 显示 | ST7735S 128×160 | 1.8 寸 TFT 彩屏, SPI1 硬件 (PA5=SCK, PA7=MOSI, PA4=CS, PA6=DC, PA0=RST), PB6 背光 PWM |
| 栅极驱动 | IR2103S | 高低侧驱动, LIN 低有效, 1000ns 死区 |
| 电流传感器 | CC6920BSO-10A | 霍尔效应, 隔离测量, 灵敏度 132mV/A, PB0 (ADC_CH8) |
| 电压采样 | 20:1 分压 | 电阻网络, PB1 (ADC_CH9), VREF=3.30V |
| 运放 | OPA2376AIDR | 双通道精密运放, 信号调理 |
| 蜂鸣器 | 有源 2.7kHz | NPN S8050 驱动 (PB15→基极 1kΩ, 集电极→蜂鸣器→5V) |
| 电源 | XL7015E1 + AMS1117-3.3 | 48V→12V→5V→3.3V 三级降压 |

### 完整引脚映射

#### 左侧排针 (20P)

| 排针 | 引脚 | 网络名 | 功能 | 配置 |
|:---|:---|:---|:---|:---|
| 1 | VBAT | VBAT | 备用电池（悬空） | — |
| 2 | PC13 | C13 | 预留悬空 | — |
| 3 | PC14 | C14 | 预留悬空 | — |
| 4 | PC15 | C15 | 预留悬空 | — |
| 5 | PA0 | 1.8TFT_RES | TFT 复位 | GPIO PP |
| 6 | PA1 | ESP8266RST | ESP8266 复位 | GPIO PP |
| 7 | PA2 | ESP8266RX | USART2_TX → ESP8266 RXD | AF_PP |
| 8 | PA3 | ESP8266TX | USART2_RX ← ESP8266 TXD | IN_FLOATING |
| 9 | PA4 | 1.8TFT_CS | TFT 片选 (软件 NSS) | GPIO PP |
| 10 | PA5 | 1.8TFT_SCL | TFT SPI 时钟 (SPI1_SCK) | AF_PP |
| 11 | PA6 | 1.8TFT_DC | TFT 命令/数据选择 | GPIO PP |
| 12 | PA7 | 1.8TFT_SDA | TFT SPI 数据 (SPI1_MOSI) | AF_PP |
| 13 | PB0 | ADCB0 | 电流传感器 CC6920 (ADC_CH8) | AIN |
| 14 | PB1 | ADCB1 | 电压分压 20:1 (ADC_CH9) | AIN |
| 15 | PB10 | PowerContrl | 12V 动力电源闸控制 | GPIO PP |
| 16 | PB11 | ESP8266EN | ESP8266 CH_PD/EN | GPIO PP |
| 17 | RESET | RESET | 系统复位（悬空） | — |
| 18 | +3.3V | 3.3V | 外部 3.3V 供电 | — |
| 19 | GND | GND | 接地 | — |
| 20 | GND | GND | 接地 | — |

#### 右侧排针 (20P)

| 排针 | 引脚 | 网络名 | 功能 | 配置 |
|:---|:---|:---|:---|:---|
| 1 | PB12 | B12 | 预留悬空 | — |
| 2 | PB13 | LINA | 逆变器左下管 PWM (TIM1_CH1N) | AF_PP |
| 3 | PB14 | LINB | 逆变器右下管 PWM (TIM1_CH2N) | AF_PP |
| 4 | PB15 | BUZ | 有源蜂鸣器 (2.7kHz NPN S8050) | GPIO PP |
| 5 | PA8 | HINA | 逆变器左上管 PWM (TIM1_CH1) | AF_PP |
| 6 | PA9 | HINB | 逆变器右上管 PWM (TIM1_CH2) | AF_PP |
| 7 | PA10 | LED_COM | 通信报警灯 | GPIO PP |
| 8 | PA11 | LED_POWER | 电源报警灯 | GPIO PP |
| 9 | PA12 | LED_TEMP | 温度报警灯 | GPIO PP |
| 10 | PA15 | LED_SYSETM | 系统状态心跳灯 | GPIO PP |
| 11 | PB3 | LED_PWM | PWM 运行状态灯 | GPIO PP |
| 12 | PB4 | LED_WIFI | WiFi 状态灯 | GPIO PP |
| 13 | PB5 | PAGE | 翻页按键 | GPIO IPU |
| 14 | PB6 | 1.8TFT_BL | TFT 背光 PWM 调光 (TIM4_CH1) | AF_PP |
| 15 | PB7 | F_DOWN | 频率减按键 | GPIO IPU |
| 16 | PB8 | F_UP | 频率加按键 | GPIO IPU |
| 17 | PB9 | ON/OFF | 启停按键 | GPIO IPU |
| 18 | 5V | 5V | 5V 供电 | — |
| 19 | GND | GND | 接地 | — |
| 20 | 3.3V | 3.3V | 3.3V 供电 | — |

> ⚠️ **关键硬件注意事项**:
> - ESP8266 需独立 3.3V LDO (AMS1117-3.3, ≥500mA), 100μF+0.1μF 去耦电容
> - ESP8266 RST (PA1): 10kΩ 上拉到 3.3V
> - ESP8266 GPIO0: 烧录固件时拉低, 正常运行时上拉 10kΩ
> - ESP8266 GPIO2: 上拉 10kΩ 到 3.3V
> - **不执行任何 TIM1 重映射** (默认映射: PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N)
> - SPI1 使用默认引脚 (PA5=SCK, PA7=MOSI), 不重映射
> - SPI 模式: Mode 0 (CPOL=0, CPHA=0), 只写不读 (MISO 不使用), 软件 NSS
> - PB3/PB4/PB5/PA15 默认被 JTAG 占用, 必须在固件中禁用 JTAG: `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`
> - SWD (PA13/PA14) 保留, ST-Link 可正常调试和烧录

---

## 启动流程

系统上电后经过 4 个阶段进入非阻塞主循环, 整个过程中全桥 MOE 保持关断 (零输出):

```
上电
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段1: 硬件层初始化 (MOE=OFF, PowerContrl=OFF)        │
│   Pwm_Driver_Init → Tft_Driver_Init → Led_Driver_Init      │
│   → Buzzer_Driver_Init → Adc_Driver_Init → Key_Driver_Init │
│   6 个模块顺序初始化, TFT 显示启动画面                        │
└─────────────────────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段2: 系统时基初始化                                 │
│   Sys_Timer_Init: SysTick 1ms + DWT 72MHz 周期计数器  │
│   全局时基, 所有任务依赖                                │
└─────────────────────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段2.5: IWDG 独立看门狗初始化                         │
│   LSI 40kHz / 64 = 625Hz, reload=1000 → 1.6s 超时     │
│   DBGMCU->CR |= DBG_IWDG_STOP (调试/下载时冻结)        │
└─────────────────────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段3: 自动联网启动 (非阻塞 CH_PD 时序)               │
│   RST_LOW (PA1=0, 1000ms) → CH_PD=1 (PB11=1)          │
│   → BOOT_WAIT (2000ms) → READY                        │
│   → ESP8266 自启 WiFiManager + MQTT                   │
│   → 发 STATUS:ONLINE 给 STM32                         │
│   总计约 3 秒, 全程非阻塞状态机驱动                     │
└─────────────────────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段4: while(1) 主循环                                │
│   9 个任务全非阻塞调度 → __WFI 休眠 → SysTick 唤醒    │
│   空闲电流 ~30mA → ~5mA                                │
└─────────────────────────────────────────────────────┘
```

> [安全] 以上 4 阶段 PWM MOE 始终关断, PowerContrl 保持 OFF

---

## 主循环调度

9 个任务在 `while(1)` 中按独立周期调度, 全非阻塞 (timestamp-diff 模式)。

### 调度总览

| 任务 | 周期 | 所属层 | 职责 |
|:---|:---|:---|:---|
| `Key_Driver_Task` | 10ms | Hardware | 4 键 FSM: IDLE→DEBOUNCE(10ms)→PRESS→WAIT_DOUBLE(200ms)→CLICK/DOUBLE_CLICK/LONG_PRESS(3s) |
| `Adc_Driver_Filter_Task` | ~2ms | Hardware | DWT 144241 周期节拍 (与 100kHz PWM 互质), 64 样本滑动窗口 → DC 分量提取 |
| `Inverter_Control_Soft_Start_Task` | 10ms | Application | FSM: SS_IDLE→SWEEP→DONE/FAULT, 150k→100kHz 扫频 |
| `Inverter_Control_Freq_Ramp_Task` | 10ms | Application | 远程 CMD:SETFREQ 触发, 1kHz/10ms 渐变到目标 |
| `Ui_Controller_Task` | 200ms | Application | TFT 界面状态机 + 按键分发 + LED 联动 + 蜂鸣器 + 过流检测 |
| `App_Network_Task` | 实时 | Application | ESP8266 RST/CH_PD 时序 + 重试管理 + CMD 解析 + 遥测 (500ms) |
| `Led_Driver_Task` | 实时 | Hardware | 6 LED 驱动 |
| `Buzzer_Driver_Task` | 实时 | Hardware | 蜂鸣器状态管理 (故障/过流时鸣响) |
| `IWDG_ReloadCounter + __WFI` | 1.6s | System | 喂狗 + 休眠等 SysTick 唤醒 |

### 调度模式

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
        last = Sys_Timer_Get_Tick();
        // 业务逻辑
    }
}
```

`uint32_t` 无符号减法天然防溢出 (49.7 天回绕窗口)。`Sys_Timer_Delay_Ms()` 已废弃。

### 主循环伪代码

```c
while(1) {
    Key_Driver_Task();                          // 10ms
    Adc_Driver_Filter_Task();                   // ~2ms
    Ui_Controller_Task();                       // 200ms
    App_Network_Task();                         // 实时
    Inverter_Control_Soft_Start_Task();         // 10ms
    Inverter_Control_Freq_Ramp_Task();          // 10ms
    Led_Driver_Task();                          // 实时
    Buzzer_Driver_Task();                       // 实时
    IWDG_ReloadCounter();                       // 喂狗
    __WFI();                                     // 休眠
}
```

---

## PWM 全桥驱动

### 核心参数 (不可改, 源自 V0.0 已验证硬件)

| 参数 | 值 | 说明 |
|:---|:---|:---|
| 架构 | TIM1 CH1+CH1N + CH2+CH2N | 全桥四通道互补输出 |
| 引脚映射 | **默认映射 (无 Remap)** | PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N |
| 计数模式 | **TIM_CounterMode_Up** | 不可改为 CenterAligned (频率公式不同) |
| 通道模式 | **CH1=PWM1, CH2=PWM2** | 两路不同模式实现对角线交替导通 |
| 输出极性 | **TIM_OCNPolarity_Low** | IR2103S LIN 为低有效 |
| 空闲电平 | **TIM_OCNIdleState_Set** | MOE 关断时下管必须关断 (LIN=HIGH) |
| 死区 | **1000ns** | `PWM_DRIVER_DEADTIME_NS` 宏统一定义 |
| 频率范围 | **95kHz ~ 150kHz** | `PWM_DRIVER_FREQ_MIN_HZ` / `MAX_HZ` |
| OSSR/OSSI | **Disable** | 防止空闲态意外输出 |
| 占空比 | 50% 锁定 | 周期 ticks 强制偶数 (防偏磁) + 影子寄存器原子更新 |

### ⚠️ V6.2 关键变更: PWM 默认映射

V6.1 使用 `GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE)` 将 CH1N 映射到 PA7, CH2N 映射到 PB0。V6.2 **不执行任何 TIM1 重映射**, 使用默认映射释放 PA7 给 SPI1_MOSI (TFT), PB0 给 ADC_CH8 (电流采样)。

| 信号 | V6.1 引脚 (PartialRemap) | V6.2 引脚 (默认映射) |
|:---|:---|:---|
| CH1 | PA8 | PA8 (不变) |
| CH1N | PA7 | **PB13** |
| CH2 | PA9 | PA9 (不变) |
| CH2N | PB0 | **PB14** |

---

## ADC 采集与滤波

### 硬件配置

- **ADC1 + DMA1_Channel1** 双通道扫描 (PB0=电流 CH8, PB1=电压 CH9)
- **连续转换模式** + **循环 DMA** 写入 `s_adc_raw[2]`
- **采样时间**: 239.5 周期 (最大, 适应高阻抗信号源)
- **PCLK2 分频**: 6 分频 (ADC_CLK = 72/6 = 12MHz)

### ⚠️ V6.2 ADC 通道变更

| 信号 | V6.1 | V6.2 |
|:---|:---|:---|
| 电流 | PA0 (ADC_CH0) | **PB0 (ADC_CH8)** |
| 电压 | PA1 (ADC_CH1) | **PB1 (ADC_CH9)** |

### 互质相位采样 (Anti-Aliasing)

```
DWT 采样周期 = 144241 CPU 周期
PWM 周期 = 720 CPU 周期 (72MHz / 100kHz)

互质性: gcd(144241, 720) = 1 ✓

采样在 720 个不同 PWM 相位均匀分布
→ 64 样本滑动窗口 (128ms) 收敛至 DC 分量
→ 有效抑制 100kHz 开关纹波
```

---

## 软启动与频率控制

### 软启动状态机

```
                      Trigger()
  SS_IDLE ─────────────────────→ SS_SWEEP
    ↑                               │
    │                               │ 每 10ms 降 200Hz
    │ Stop()                        │ 150kHz → 100kHz (~2.5s)
    │                               ↓
    ├────────────────────────── SS_DONE ← 可远程 CMD:SETFREQ 调频
    │
    └── SS_FAULT ← Fault() ← 过流检测 (5A) + 蜂鸣器鸣响
         (不可自动恢复, 按键复位)
```

### 频率渐变斜坡

仅 `SS_DONE` 状态可触发, 1kHz/10ms (100kHz/s) 平滑渐变:

```
CMD:SETFREQ:108000 → FreqRamp_Trigger(108000)
  → RAMP_ACTIVE
  → 100000 → 101000 → 102000 → ... → 108000 (每10ms)
  → |current - target| ≤ 1000 → Set_Freq(target) → RAMP_IDLE
```

---

## TFT 界面状态机

ST7735S 128×160 彩色 TFT 替代 V6.1 的 SSD1315 OLED。SPI1 硬件驱动 (PA5=SCK, PA7=MOSI), 软件 NSS (PA4=CS)。

### TFT 引脚连接

| TFT 引脚 | STM32 引脚 | 功能 | 配置 |
|:---|:---|:---|:---|
| SCK | PA5 | SPI1 时钟 | AF_PP |
| SDA/MOSI | PA7 | SPI1 数据 | AF_PP |
| CS | PA4 | 片选 (软件控制) | GPIO PP |
| DC | PA6 | 数据/命令选择 | GPIO PP |
| RST | PA0 | 硬件复位 | GPIO PP |
| BL | PB6 | 背光 PWM 调光 (TIM4_CH1) | AF_PP |
| VCC | 3.3V | 供电 | — |
| GND | GND | 地 | — |

SPI 模式: Mode 0 (CPOL=0, CPHA=0), 只写不读 (MISO 未使用)。

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
- 部署: Cloudflare Pages
- 默认账号: `admin / 123456789`

---

## LED 指示

6 LED 指示灯, 比 V6.1 多 2 路:

| LED | 引脚 | 网络名 | 颜色 | 指示内容 | 模式 |
|:---|:---|:---|:---|:---|:---|
| LED1 | PB4 | LED_WIFI | 蓝 | WiFi 状态 | 灭=未初始化, 慢闪=等待, 快闪=连接中, 常亮=在线 |
| LED2 | PB3 | LED_PWM | 绿 | PWM 运行 | 灭=停止, 闪=扫频, 常亮=运行 |
| LED3 | PA10 | LED_COM | 蓝 | 通信 | 收发时翻转 |
| LED4 | PA11 | LED_POWER | 绿 | 供电状态 | 常亮=正常, 快闪=欠压/过压 |
| LED5 | PA12 | LED_TEMP | 红 | 温度告警 | 灭=正常, 常亮=过温 (>85°C) |
| LED6 | PA15 | LED_SYSETM | 黄 | 系统就绪 | 灭=初始化, 常亮=就绪, 快闪=故障 |

> ⚠️ LED 采用 0805 贴片 LED (KT-0805G), 串 1kΩ 限流电阻到 GND

---

## 按键操作

4 键输入, 全部 GPIO IPU (内部上拉), 10ms 去抖:

| 按键 | 引脚 | 网络名 | 功能 | 事件 |
|:---|:---|:---|:---|:---|
| KEY0 | PB9 | ON/OFF | 启停 | 单击=软启动/停止, 长按=清WiFi |
| KEY1 | PB8 | F_UP | 频率加 | 单击=+1kHz, 长按=连发 |
| KEY2 | PB7 | F_DOWN | 频率减 | 单击=-1kHz, 长按=连发 |
| KEY3 | PB5 | PAGE | 切页 | 双击=控制/监测模式切换 |

---

## 蜂鸣器告警

有源 2.7kHz 电磁式蜂鸣器, NPN S8050 驱动:

| 元件 | 连接 | 说明 |
|:---|:---|:---|
| 蜂鸣器 | 有源 2.7kHz 电磁式 | — |
| 驱动管 | S8050 NPN | PB15→基极(串1kΩ), 集电极→蜂鸣器→5V, 射极→GND |

触发条件:
- 过流 (>5A): 连续鸣响, 直到按键复位
- 故障 (FAULT): 连续鸣响
- 欠压/过压告警: 间歇鸣响 (可选)

---

## 安全保护

### 5 层安全防护体系

```
① 上电安全
   Pwm_Driver_Init MOE=OFF, PowerContrl=OFF
   全桥零输出 + 功率级断电

② 故障保护
   4 个 Fault Handler (HardFault/MemManage/BusFault/UsageFault)
   全部关断 MOE → 桥臂无直通风险

③ 看门狗
   IWDG 1.6s 超时
   主循环喂狗, 任何任务卡死 → 硬件自动复位

④ 状态机容错
   FAULT 状态不可自动恢复
   必须用户按键确认后手动复位

⑤ ADC 零点校准
   上电 50 样本取平均 + EMA 慢速追踪温漂
```

### 保护场景汇总

| 场景 | 检测机制 | 动作 | 恢复方式 |
|:---|:---|:---|:---|
| 上电 | 硬件初始化 | PWM MOE=OFF, PowerContrl=OFF | 软件触发 Enable |
| 过流 (>5A) | `Ui_Controller_Task` 每 200ms | MOE 关断 + FAULT 锁存 + 蜂鸣器 | KEY0 按键 |
| CPU 故障 | Fault ISR ×4 | `TIM_CtrlPWMOutputs(DISABLE)` → 死循环 | IWDG 复位 |
| 主循环卡死 | IWDG (1.6s) | 硬件自动复位 | 自动 |
| 频率越界 | `Pwm_Driver_Set_Frequency` 钳位 | 拒绝执行 | 自动校正 |

---

## 中断服务

### NVIC 配置

- **优先级分组**: `NVIC_PriorityGroup_2`
- **SysTick**: 系统默认优先级 (最高)
- **USART2**: 抢占 1, 子 0

### 故障处理器 ×4

```c
void HardFault_Handler(void) {
    TIM_CtrlPWMOutputs(TIM1, DISABLE);  // ← 关键: 先关断桥臂!
    while(1);  // 死等 IWDG 复位
}
// MemManage_Handler, BusFault_Handler, UsageFault_Handler 相同
```

---

## 分支说明

| 分支 | 版本 | 协议 | 显示 | 说明 |
|:---|:---:|:---|:---|:---|
| `master` | V0.0 | — | OLED | 裸机基版, 全桥 PWM + OLED + 按键 |
| `1.0LAN` | V3.4 | NetAssist TCP | OLED | 局域网 PC 调试 |
| `2.0WAN` | V3.5 | 巴法云 MQTT | OLED | 巴法云创客云版本 |
| `3.0ONENET` | V6.1 | OneNET MQTT | OLED | Dual-MCU + 命名规范 + 状态机 + 过流保护 |
| **`4.0TFT`** | **V6.2** | **OneNET MQTT** | **TFT 彩屏** | **OLED→TFT + 4键 + 6 LED + 蜂鸣器 + PWM默认映射** |

---

## 项目结构

```
WPT_PWM/
├── Arduino_Project/                  # ESP8266 Arduino MQTT 固件
│   └── ESP8266_MQTT_Firmware/
│       └── ESP8266_MQTT_Firmware.ino  # 单文件, 注释分段 (配置/连接/MQTT/串口)
├── Keil_Project/                     # Keil MDK STM32 固件
│   ├── Hardware/                     # 硬件驱动层
│   │   ├── Pwm_Driver.c/h           # TIM1 全桥 PWM 默认映射 (95-150kHz, 1000ns死区)
│   │   ├── Inverter_Control.c/h     # 软启动状态机 + 频率渐变
│   │   ├── Adc_Driver.c/h           # ADC1+DMA 双通道 (PB0=CH8, PB1=CH9) + 64样本互质滤波
│   │   ├── Key_Driver.c/h           # 4 按键 FSM (PB5/PB7/PB8/PB9)
│   │   ├── Tft_Driver.c/h           # ST7735S 128×160 SPI 彩屏驱动
│   │   ├── Esp8266_Driver.c/h       # USART2 ISR + 行缓冲 + PA1 RST + PB11 CH_PD
│   │   ├── Ui_Controller.c/h        # TFT 界面状态机 + 按键分发 + LED联动 + 蜂鸣器 + 过流保护
│   │   ├── Led_Driver.c/h           # 6 LED 状态管理
│   │   └── Buzzer_Driver.c/h        # 有源蜂鸣器驱动 (PB15, S8050)
│   ├── System/                       # 系统服务层
│   │   └── Sys_Timer.c/h            # SysTick 1ms + DWT 周期计数器
│   ├── User/                         # 应用层
│   │   ├── Main.c                   # main() 入口, 4 阶段启动 + 非阻塞主循环
│   │   ├── App_Network.c/h          # 联网管理 + 重试超时 + 遥测门控 + CMD 解析
│   │   ├── stm32f10x_conf.h         # SPL 头文件配置
│   │   └── stm32f10x_it.c           # ISR (SysTick + USART2 + Fault ×4)
│   ├── Library/                      # SPL V3.5.0 (只读, 从不修改)
│   ├── Start/                        # 启动文件 + system_stm32f10x
│   └── Project.uvprojx               # Keil MDK 工程文件
├── ONENETapp/                        # 网页控制台 (→ Ran-sh/WPT_Onenet_IoT)
├── 安卓app/                           # 微信小程序
├── Railway_Deploy/                   # Railway 桥接 (历史)
├── Claude_Files/                     # AI 文档与工具
│   ├── docs/                         # 开发指南 (MD+DOCX 配对)
│   ├── diagrams/                     # Visio 系统工作流
│   └── tools/                        # DOCX 生成
├── 硬件原理图/                        # 硬件设计文件
├── CLAUDE.md                         # AI 辅助开发规范 (V6.2)
└── README.md                         # 本文件
```

### 编码规范 (V6.0+)

- **命名**: `Module_Name_Action_Object()` — 帕斯卡 + 下划线, 全模块统一
- **状态机**: 必须用显式 `typedef enum`, 禁止隐式 bool/int 标志组合
- **调度**: 时间戳差值 `tick - last >= PERIOD`, 禁止 `Delay_Ms`
- **临界区**: PRIMASK 保存/恢复, 禁止裸 `__enable_irq()`
- **OOP in C**: 相关变量封装 struct, 状态机打包 (状态+定时器+上下文)
- **模块隔离**: `.h` 只放公开接口, `static` 保护内部函数, 禁止 `extern` 私有变量
- **图层依赖**: Hardware → System → Application, 严格单向

---

## 快速开始

### 1. 烧录 STM32

1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. Rebuild (F7) → ST-Link Download (F8)
3. TFT 显示启动画面 → 自动进入连接中界面

### 2. 烧录 ESP8266

1. ESP8266-01 接 USB-TTL (**GPIO0 接 GND**, VCC 接 3.3V)
2. Arduino IDE 安装库: `ArduinoJson v7` + `PubSubClient` + `WiFiManager`
3. 打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
4. 修改凭证: `ONENET_PRODUCT_ID`, `ONENET_DEVICE_NAME`, `ONENET_TOKEN`
5. 开发板选 "Generic ESP8266 Module", Flash 1M, CPU 80MHz → 上传
6. **烧录后断开 GPIO0-GND** → 重新上电

### 3. 配网 (首次)

1. ESP8266 上电 → 开热点 `STM32_WPT_Config` (无密码)
2. 手机连上 → 自动弹出配网页 → 选 WiFi 输密码
3. 配网成功 → ESP8266 重启 → 自动连 OneNET
4. TFT: 连接中 → `STATUS:ONLINE` → READY

### 4. 网页控制台

浏览器打开 `https://wptonenet.483763727.workers.dev`, 登录 `admin / 123456789`。

---

## 文档

| 文档 | 格式 | 说明 |
|:---|:---|:---|
| [WPT无线充电系统-从零搭建全指南](Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md) | MD+DOCX | 完整开发指南 |
| [WPT_PWM_系统工作流](Claude_Files/diagrams/WPT_PWM_系统工作流.vsdx) | Visio VSDX | 4 页系统流程图 |
| [CLAUDE.md](CLAUDE.md) | MD | AI 辅助开发规范 (V6.2) |

---

## 作者

**Rssss**

## 许可

MIT
