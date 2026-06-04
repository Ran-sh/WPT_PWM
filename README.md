# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-red)]()
[![Firmware](https://img.shields.io/badge/Firmware-V6.1-brightgreen)]()
[![App](https://img.shields.io/badge/App-WeChat%20Mini%20Program-07C160)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V6.1** (2026-06-01) — 基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。采用 **Dual-MCU 双脑架构**：STM32 (SPL V3.5.0) 全桥 PFM 发波与保护，ESP8266 (Arduino) 独立 MQTT 固件连接 **OneNET 物模型**。支持 OLED 7 界面状态机本地控制、Cloudflare Pages 网页控制台、微信小程序远程遥控。应用于植入式医疗设备无线充电。

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
9. [OLED 界面状态机](#oled-界面状态机)
10. [远程控制](#远程控制)
11. [LED 指示](#led-指示)
12. [按键操作](#按键操作)
13. [安全保护](#安全保护)
14. [中断服务](#中断服务)
15. [分支说明](#分支说明)
16. [项目结构](#项目结构)
17. [快速开始](#快速开始)
18. [文档](#文档)

---

## 版本历史

| 版本 | 日期 | 分支 | 主要变更 |
|:---|:---|:---|:---|
| V0.0 | 2024 | `master` | 裸机基版, 全桥 PWM + OLED + 按键 |
| V3.4 | 2024 | `1.0LAN` | NetAssist 局域网 UDP 调试 |
| V3.5 | 2024 | `2.0WAN` | 巴法云 MQTT TCP 协议 |
| V5.0 | 2025 | `3.0ONENET` | OneNET MQTT 物模型 + Dual-MCU 架构 |
| V6.0 | 2025 | `3.0ONENET` | 全模块命名规范 + 显式状态枚举 + ESP 前缀匹配 |
| **V6.1** | **2026-06** | **`3.0ONENET`** | **8 项 Bug 修复 + 过流保护 + 代码质量提升** |
| **V6.2** | **2026-06** | **`4.0TFT`** | **OLED→TFT 彩屏 + 4键 + 6 LED + 蜂鸣器 + PWM默认映射** |

### V6.1 Bug 修复清单

| 级别 | 问题 | 文件 | 修复 |
|:---|:---|:---|:---|
| **CRITICAL** | PWM 频率斜坡永不收敛 | `Inverter_Control.c` | 精确相等比较 → 容差范围 `\|diff\| ≤ 1000Hz` |
| **CRITICAL** | PWM 基线偏离 V0.0 | `Pwm_Driver.c` | 恢复 Up 计数 + PartialRemap + PWM1/PWM2 + OCNPolarity_Low + OCNIdleState_Set |
| **CRITICAL** | 过流保护未接入 | `Ui_Controller.c` | 新增 5A 阈值检测, 触发 `Inverter_Control_Soft_Start_Fault()` |
| **HIGH** | RX 缓冲区残留帧 | `Esp8266_Driver.c` | `Start_Init()` 时清空缓冲, 防 ESP 复位后误消费 |
| **HIGH** | 裸 `__enable_irq()` | `Esp8266_Driver.c` `Key_Driver.c` | 3 处改为 PRIMASK 保存/恢复模式 |
| **MEDIUM** | ADC 时钟假设无保护 | `Adc_Driver.c` | 编译期静态断言 `SystemCoreClock == 72MHz` |
| **MEDIUM** | `double` 无硬件 FPU 开销 | `Oled_Driver.c/h` | 全部 `double` → `float` |
| **MEDIUM** | EMA 显示不复位 | `Ui_Controller.c` | 模块级 EMA + 状态转移时 `Reset_Display_EMA()` |
| **LOW** | 嵌套 `return` 风格 | `App_Network.c` | 嵌套 `return` 门控 → 单 `if(allow_telemetry)` 模式 |

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
│  • ADC1+DMA 双通道采集 + 64样本互质滤波 │
│  • OLED 128x64 I2C + 7界面状态机      │
│  • 双按键 FSM (单击/双击/长按)        │
│  • 非阻塞软启动扫频 150k→100kHz       │
│  • 频率渐变斜坡 1kHz/10ms             │
│  • 过流保护 5A 硬件级关断              │
│  • 4 LED 状态灯 + IWDG 看门狗          │
│  • __WFI 休眠 (<5mA 空闲电流)          │
└──────────────────────────────────────┘
```

### STM32 模块分层

```
Application  ┌─────────────────────────────────────────────┐
             │  App_Network    Ui_Controller                │
             │  (联网+遥测)     (7界面+按键+LED+过流保护)    │
             │  Inverter_Control (软启动+频率斜坡)          │
             └─────────────────────────────────────────────┘
                              │
System       ┌────────────────┴────────────────────────────┐
             │  Sys_Timer (SysTick 1ms + DWT 周期计数器)    │
             └─────────────────────────────────────────────┘
                              │
Hardware     ┌────────────────┴────────────────────────────┐
             │  Pwm_Driver  Adc_Driver  Key_Driver         │
             │  Oled_Driver  Led_Driver  Esp8266_Driver     │
             │  IWDG (1.6s 独立看门狗)                      │
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
| WiFi | ESP8266-01 | Arduino MQTT 固件, 独立 3.3V LDO (≥500mA) |
| 显示 | SSD1315 128x64 | 0.96 寸 4 针 OLED, 软件 I2C (PA11=SCL, PA12=SDA), 8x16 英文字体 |
| 栅极驱动 | IR2103S | 高低侧驱动, LIN 低有效, 1000ns 死区 |
| 电流传感器 | CC6920-10A | 霍尔效应, 隔离测量, 灵敏度 132mV/A, 零点 1.65V, 接入 PA0 |
| 电压采样 | 20:1 分压 | 电阻网络, 接入 PA1, VREF=3.30V |

### 完整引脚映射

| 引脚 | 功能 | 连接对象 | 配置 |
|:---|:---|:---|:---|
| PA0 | ADC_CH0 | CC6920-10A 电流传感器 | AIN |
| PA1 | ADC_CH1 | 电压分压器 20:1 | AIN |
| PA2 | USART2_TX | ESP8266 RXD | AF_PP |
| PA3 | USART2_RX | ESP8266 TXD | IN_FLOATING |
| PA7 | TIM1_CH1N | 半桥左低侧 (IR2103S LIN) | AF_PP |
| PA8 | TIM1_CH1 | 半桥左高侧 (IR2103S HIN) | AF_PP |
| PA9 | TIM1_CH2 | 半桥右高侧 (IR2103S HIN) | AF_PP |
| PA11 | GPIO (OD) | OLED SCL (模拟 I2C) | Out_OD |
| PA12 | GPIO (OD) | OLED SDA (模拟 I2C) | Out_OD |
| PB0 | TIM1_CH2N | 半桥右低侧 (IR2103S LIN) | AF_PP |
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN 使能引脚 | Out_PP |
| PB3 | GPIO (PP) | WiFi 状态 LED (JTAG 禁用后释放) | Out_PP |
| PB4 | GPIO (PP) | PWM 状态 LED (JTAG 禁用后释放) | Out_PP |
| PB5 | GPIO (PP) | Ready 状态 LED | Out_PP |
| PB12 | GPIO (IPU) | KEY0 按键 | IPU |
| PB13 | GPIO (IPU) | KEY1 按键 | IPU |
| PC13 | GPIO (PP) | 心跳 LED (低电平有效) | Out_PP |

> ⚠️ **关键硬件注意事项**:
> - ESP8266 需独立 3.3V LDO (AMS1117-3.3, ≥500mA), 100μF+0.1μF 去耦电容。STM32 板载 LDO 无法满足 WiFi 突发电流
> - ESP8266 RST 引脚: 10kΩ 上拉到 3.3V
> - ESP8266 GPIO0: 烧录固件时拉低, 正常运行时悬空或上拉
> - `GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE)` — 将 TIM1_CH1N 映射到 PA7, CH2N 映射到 PB0, **缺失则全桥无输出**
> - PB3/PB4 默认被 JTAG 占用, 必须在 `Led_Driver_Init()` 中禁用 JTAG (`GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`)

---

## 启动流程

系统上电后经过 4 个阶段进入非阻塞主循环, 整个过程中全桥 MOE 保持关断 (零输出):

```
上电
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段1: 硬件层初始化 (MOE=OFF, 全桥零输出)            │
│   Pwm_Driver_Init → Oled_Driver_Init → Led_Driver_Init    │
│   → Adc_Driver_Init → Key_Driver_Init                      │
│   5 个模块顺序初始化, OLED 显示 "Wireless Charge"          │
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
│   RESET_LOW (1000ms) → CH_PD=1 → BOOT_WAIT (2000ms)   │
│   → READY → ESP8266 自启 WiFiManager + MQTT           │
│   → 发 STATUS:ONLINE 给 STM32                         │
│   总计约 3 秒, 全程非阻塞状态机驱动                     │
└─────────────────────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────┐
│ 阶段4: while(1) 主循环                                │
│   8 个任务全非阻塞调度 → __WFI 休眠 → SysTick 唤醒    │
│   空闲电流 ~30mA → ~5mA                                │
└─────────────────────────────────────────────────────┘
```

> [安全] 以上 4 阶段 PWM MOE 始终关断, 全桥零输出 | OLED 显示 "Wireless Charge / Booting ESP..."

---

## 主循环调度

8 个任务在 `while(1)` 中按独立周期调度, 全非阻塞 (timestamp-diff 模式), 无一毫秒 busy-wait。

### 调度总览

| 任务 | 周期 | 所属层 | 职责 |
|:---|:---|:---|:---|
| `Key_Driver_Task` | 10ms | Hardware | 双键 FSM: IDLE→DEBOUNCE(10ms)→PRESS→WAIT_DOUBLE(200ms)→CLICK/DOUBLE_CLICK/LONG_PRESS(3s) |
| `Adc_Driver_Filter_Task` | ~2ms | Hardware | DWT 144241 周期节拍 (与 100kHz PWM 互质), 64 样本滑动窗口 → DC 分量提取 |
| `Inverter_Control_Soft_Start_Task` | 10ms | Application | FSM: SS_IDLE→SWEEP→DONE/FAULT, 150k→100kHz 扫频, 200Hz/10ms |
| `Inverter_Control_Freq_Ramp_Task` | 10ms | Application | 远程 CMD:SETFREQ 触发, 1kHz/10ms 渐变到目标, 容差收敛 |
| `Ui_Controller_Task` | 200ms | Application | 6 态 FSM + OLED 绘制 + 按键分发 + LED 联动 + 过流检测 |
| `App_Network_Task` | 实时 | Application | ESP8266 CH_PD 时序驱动 + 重试管理 (15s×3) + CMD 解析 + 遥测 (500ms) |
| `Led_Driver_Task` | 实时 | Hardware | 4 LED 驱动 (PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready) |
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

`uint32_t` 无符号减法天然防溢出 (49.7 天回绕窗口)。`Sys_Timer_Delay_Ms()` 已废弃, 运行时绝对禁止阻塞延时。

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
    IWDG_ReloadCounter();                       // 喂狗 (1.6s 窗口)
    __WFI();                                     // 休眠
}
```

---

## PWM 全桥驱动

### 核心参数 (不可改, 源自 V0.0 已验证硬件)

| 参数 | 值 | 说明 |
|:---|:---|:---|
| 架构 | TIM1 CH1+CH1N + CH2+CH2N | 全桥四通道互补输出 |
| 计数模式 | **TIM_CounterMode_Up** | 不可改为 CenterAligned (频率公式不同) |
| 通道模式 | **CH1=PWM1, CH2=PWM2** | 两路不同模式实现对角线交替导通, 同模式则桥间电压为零 |
| 输出极性 | **TIM_OCNPolarity_Low** | IR2103S LIN 为低有效, 不可改为 High |
| 空闲电平 | **TIM_OCNIdleState_Set** | MOE 关断时下管必须关断 (LIN=HIGH), 不可改为 Reset |
| 重映射 | **GPIO_PartialRemap_TIM1** | 缺失则 PA7=CH1N + PB0=CH2N 无输出 |
| 死区 | **1000ns** | `PWM_DRIVER_DEADTIME_NS` 宏统一定义 |
| 频率范围 | **95kHz ~ 150kHz** | `PWM_DRIVER_FREQ_MIN_HZ` / `MAX_HZ` |
| OSSR/OSSI | **Disable** | 防止空闲态意外输出 |
| 占空比 | 50% 锁定 | 周期 ticks 强制偶数 (防偏磁) + 影子寄存器原子更新 |

### 频率量化

TIM1_CLK = 72MHz。Up 计数模式: `f_actual = 72MHz / ticks`, ticks 强制偶数 (防 DC 偏磁)。

| 目标 kHz | ticks | 实际 Hz | 实际 kHz | 误差 |
|:---|:---|:---|:---|---:|
| 95 | 758 | 94,987 | 94.99 | -0.01% |
| 100 | 720 | 100,000 | 100.00 | 0.00% |
| 105 | 684 | 105,263 | 105.26 | +0.25% |
| 110 | 654 | 110,092 | 110.09 | +0.08% |
| 115 | 626 | 115,016 | 115.02 | +0.01% |
| 120 | 600 | 120,000 | 120.00 | 0.00% |
| 125 | 576 | 125,000 | 125.00 | 0.00% |
| 130 | 554 | 129,963 | 129.96 | -0.03% |
| 135 | 534 | 134,831 | 134.83 | -0.13% |
| 140 | 514 | 140,078 | 140.08 | +0.06% |
| 145 | 496 | 145,161 | 145.16 | +0.11% |
| 150 | 480 | 150,000 | 150.00 | 0.00% |

**要点**: 硬件整数分频 → 并非所有 kHz 值可达 → 频率斜坡使用容差收敛 `|diff| ≤ 1000Hz` 而非精确相等比较。

### 原子更新机制

```c
TIM1->CR1 |= TIM_CR1_UDIS;      // 禁止更新事件
TIM1->ARR  = (uint16_t)(ticks - 1);
TIM1->CCR1 = (uint16_t)(ticks / 2);  // 50% 占空
TIM1->CCR2 = (uint16_t)(ticks / 2);
TIM1->EGR  = TIM_EGR_UG;        // 软件触发一次更新 (同时加载 ARR+CCR)
TIM1->CR1 &= ~TIM_CR1_UDIS;     // 恢复更新事件
```

`UDIS` 批量加载确保 ARR 和 CCR 同步更新, 防止周期裁剪导致变压器偏磁。

---

## ADC 采集与滤波

### 硬件配置

- **ADC1 + DMA1_Channel1** 双通道扫描 (PA0=电流, PA1=电压)
- **连续转换模式** + **循环 DMA** 写入 `s_adc_raw[2]`
- **采样时间**: 239.5 周期 (最大, 适应高阻抗信号源)
- **PCLK2 分频**: 6 分频 (ADC_CLK = 72/6 = 12MHz)

### 互质相位采样 (Anti-Aliasing)

```
DWT 采样周期 = 144241 CPU 周期
PWM 周期 = 720 CPU 周期 (72MHz / 100kHz)

互质性: gcd(144241, 720)
  144241 ÷ 720 = 200 余 241
  720 ÷ 241 = 2 余 238
  241 ÷ 238 = 1 余 3
  238 ÷ 3 = 79 余 1
  → gcd = 1 ✓

采样在 720 个不同 PWM 相位均匀分布
→ 64 样本滑动窗口 (128ms) 收敛至 DC 分量
→ 有效抑制 100kHz 开关纹波
```

**编译期保护**: `typedef char Adc_Driver_Assert_HSE_72MHz[(SystemCoreClock == 72000000) ? 1 : -1]` — 确保 HSE 始终 72MHz。

### 滤波链

```
DMA → s_adc_raw[2] ──→ Filter_Window (64样本滑动) ──→ DC 分量
                         accum += new - old
                         V = (accum/64/4095) * 3.30V * 20 (分压比)
                         I = (Vpin - i_offset) / 0.132Ω/V (灵敏度)
```

### 自动零点校准

1. **初始化阶段**: 逆变器未工作时采集 50 样本 (0.5s) 取平均 → `i_offset`
2. **在线追踪**: `i_offset = 0.95 × i_offset + 0.05 × Vpin` (EMA, α=0.05, τ≈100s)
3. **调用时机**: `Ui_Controller_Task` 在 READY 状态 + SS_IDLE 时持续调用 `Adc_Driver_Calibrate_Offset()`

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
    └── SS_FAULT ← Fault() ← 过流检测 (5A)
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

> [V6.1 修复] 收敛判定从 `current == s_ramp_target` 改为 `|diff| ≤ FREQ_RAMP_STEP_HZ`, 消除整数分频永不收敛的 bug。

---

## OLED 界面状态机

```
上电 → main.c App_Network_Start_Connect() → 界面2(连接中)
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
| — | FAULT | "!!! FAULT !!! / Over Current" | KEY0/KEY1=复位 |
| **6/7** | 双击切页 | 控制面板 / 监测模式 | 所有已连接界面可双击 |

### EMA 显示平滑

V/I/F 显示使用指数移动平均 (α=0.25, τ≈800ms) 消除 OLED 数值跳变:

```c
s_disp_v = s_disp_v * 0.75f + Adc_Driver_Get_Voltage() * 0.25f;
```

[V6.1 修复] EMA 状态提升到模块级, 状态转移时 `Reset_Display_EMA()` 重置, 消除重启后的 ~800ms 收敛滞后。

**OneNET 遥测门控**: 仅在界面 >= 3 (READY) 时发送遥测, 界面 1/2 时设备在 OneNET 上**离线**。网页/小程序看到的"在线" = 用户已可操作。

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
- 默认账号: `admin / 123456789`

### 微信小程序

- 仓库: [Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM) 子目录 `安卓app/`
- 架构: OneNET HTTP API 直连, 零中间桥接
- 轮询: 2 秒间隔单次 API 调用 (fetchAll)
- 在线检测: 数据时间戳超过 10s → 离线
- Switch 复位: 3s 验证 + 自动重发
- Swiper: 首次连接同步一次频率, 之后仅受手指控制

---

## LED 指示

| 系统状态 | PC13 心跳 | PB3 WiFi | PB4 PWM | PB5 Ready | 说明 |
|:---|:---|:---|:---|---:|:---|
| 初始化 / 硬件未就绪 | 500ms 翻转 | 慢闪 (500ms) | 灭 | 灭 | ESP 未初始化, OLED 显示 "Booting..." |
| 连接 WiFi 中 | 500ms 翻转 | **快闪 (200ms)** | 灭 | 灭 | ESP CH_PD 时序进行中或 WiFi 连接中 |
| 已连接, 待机 (READY) | 500ms 翻转 | 常亮 | 灭 | 灭 | 等待 KEY0 触发软启动 |
| 扫频中 (SWEEPING) | 500ms 翻转 | 常亮 | **亮** | 灭 | 150k→100kHz 向下扫频, ~2.5s |
| 运行中 (RUNNING) | 500ms 翻转 | 常亮 | 灭 | **亮** | 谐振工作, 可 +1kHz 微调 |
| 故障 (FAULT) | 500ms 翻转 | 常亮 | 灭 | **亮** | 过流锁存, 需 KEY0/KEY1 复位 |
| 联网失败 (FAILED) | 500ms 翻转 | 慢闪 | 灭 | 灭 | 3 次重试耗尽, 提示 "WiFi Failed x3" |

---

## 按键操作

KEY0 (PB12) 和 KEY1 (PB13) 均内部上拉 (IPU), 10ms 扫描, 八态 FSM:

| 事件 | KEY0 | KEY1 |
|:---|:---|:---|
| **单击** | 连WiFi / 触发扫频 / 关断 / 复位故障 | 关断扫频 / +1kHz / 复位故障 |
| **双击** | 切页 (控制面板 ↔ 监测模式) | — |
| **长按 (>3s)** | **清除 ESP8266 WiFi 配网** | — |

按键事件消费机制: `Key_Driver_Get_Event()` 阅后即焚 — 每次调用后事件清零。

---

## 安全保护

### 5 层安全防护体系

```
① 上电安全
   Pwm_Driver_Init MOE=OFF, 全桥零输出
   软件可控开通 — 硬件级安全基线

② 故障保护
   4 个 Fault Handler (HardFault/MemManage/BusFault/UsageFault)
   全部关断 MOE → 桥臂无直通风险
   即使 CPU 跑飞也能保护硬件

③ 看门狗
   IWDG 1.6s 超时 (LSI 40kHz/64=625Hz, reload=1000)
   主循环喂狗, 任何任务卡死 → 硬件自动复位
   系统自愈无需人工干预

④ 状态机容错
   FAULT 状态不可自动恢复
   必须用户按键确认后手动复位
   防止反复重启损坏设备

⑤ ADC 零点校准
   上电 50 样本取平均 (0.5s)
   + EMA 慢速追踪温漂 (α=0.05, τ≈100s)
   保证电流测量精度
```

### 保护场景汇总

| 场景 | 检测机制 | 动作 | 恢复方式 |
|:---|:---|:---|:---|
| 上电 | 硬件初始化 | PWM MOE=OFF, 全桥零输出 | 软件触发 Enable |
| 过流 (>5A) | `Ui_Controller_Task` 每 200ms 检测 | `Soft_Start_Fault()` → MOE 关断 + FAULT 锁存 | KEY0/KEY1 按键 |
| CPU 故障 | HardFault/MemManage/BusFault/UsageFault ISR | 强制 `TIM_CtrlPWMOutputs(DISABLE)` → 死循环 | IWDG 复位 |
| 主循环卡死 | IWDG 独立看门狗 (1.6s) | 硬件自动复位 | 自动 |
| 频率越界 | `Pwm_Driver_Set_Frequency` 硬钳位 95k~150k | 拒绝执行, 返回钳位值 | 自动校正 |
| ESP8266 掉线 | ESP8266 自管理重连 + `Conn_State` FSM | STM32 无感, 继续发 JSON | ESP 自动重连 |
| 连接超时 | 15s × 3 次硬件复位重试 | 失败回初始界面 + "WiFi Failed x3" | KEY0 重试 |
| 死区不足 | 编译期 `__deadtime_linear_check` typedef 断言 | 编译失败 | 修改宏 |

### 临界区规范

**绝对禁止** 裸 `__enable_irq()`, 必须使用 PRIMASK 保存/恢复模式:

```c
uint32_t primask = __get_PRIMASK();
__disable_irq();
/* 临界区操作 */
__set_PRIMASK(primask);  // 恢复到调用前的状态, 而非无条件开中断
```

这确保在中断已禁用的上下文中调用时不会意外开中断。

### 低功耗

主循环末尾 `__WFI()` 休眠等 SysTick 中断唤醒, 空闲电流 ~30mA → ~5mA。

---

## 中断服务

### NVIC 配置

- **优先级分组**: `NVIC_PriorityGroup_2` (2 位抢占 + 2 位子优先级)
- **SysTick**: 系统默认优先级 (最高)
- **USART2**: 抢占 1, 子 0

### SysTick_Handler (每 1ms)

```c
void SysTick_Handler(void) {
    Sys_Timer_Inc_Tick();  // 仅递增 s_sys_tick, 无任何业务逻辑!
}
```

极简设计 — ISR 只做计数, 业务逻辑全部在任务中轮询。

### USART2_IRQHandler (每个字节)

```c
void USART2_IRQHandler(void) {
    // 1. 先处理 ORE 溢出 (读 DR 清标志, 防 ISR 死锁)
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE)) {
        (void)USART_ReceiveData(USART2);
    }
    // 2. RXNE → 读数据
    if (USART_GetITStatus(USART2, USART_IT_RXNE)) {
        Esp8266_Driver_Rx_Char((char)USART_ReceiveData(USART2));
        // 拼接行缓冲, \r / \n 触发帧标志
    }
}
```

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

| 分支 | 版本 | 协议 | 说明 |
|:---|:---:|:---|:---|
| `master` | V0.0 | — | OLED | 裸机基版, 全桥 PWM + OLED + 按键 |
| `1.0LAN` | V3.4 | NetAssist TCP | OLED | 局域网 PC 调试 |
| `2.0WAN` | V3.5 | 巴法云 MQTT | OLED | 巴法云创客云版本 |
| **`3.0ONENET`** | **V6.1** | **OneNET MQTT** | **OLED** | **Dual-MCU + 命名规范 + 状态机 + 过流保护** |
| **`4.0TFT`** | **V6.2** | **OneNET MQTT** | **TFT 彩屏** | **OLED→TFT + 4键 + 6 LED + 蜂鸣器 + PWM默认映射** |

---

## 项目结构

```
WPT_PWM/
├── Arduino_Project/                  # ESP8266 Arduino MQTT 固件
│   └── ESP8266_MQTT_Firmware/
│       └── ESP8266_MQTT_Firmware.ino  # 单文件, 注释分段 (配置/连接/MQTT/串口)
├── Keil_Project/                     # Keil MDK STM32 固件
│   ├── Hardware/                     # 硬件驱动层 (Hardware → 不依赖 Application)
│   │   ├── Pwm_Driver.c/h           # TIM1 全桥 PWM 硬件抽象 (95-150kHz, 1000ns死区)
│   │   ├── Inverter_Control.c/h     # 软启动状态机 + 频率渐变 (应用层)
│   │   ├── Adc_Driver.c/h           # ADC1+DMA 双通道 + 64样本互质滤波 + 自动零点校准
│   │   ├── Key_Driver.c/h           # 双按键 FSM (单击/双击/长按, 10ms去抖)
│   │   ├── Oled_Driver.c/h          # SSD1315 I2C 驱动 + 浮点显示 (8x16 字体)
│   │   ├── Esp8266_Driver.c/h       # USART2 ISR + 行缓冲 + 纯JSON透传 + CH_PD时序
│   │   ├── Ui_Controller.c/h        # 7界面状态机 + 按键分发 + LED联动 + 过流保护
│   │   └── Led_Driver.c/h           # 4 LED 状态管理 (PC13+PB3/4/5)
│   ├── System/                       # 系统服务层
│   │   └── Sys_Timer.c/h            # SysTick 1ms + DWT 周期计数器 (亚毫秒)
│   ├── User/                         # 应用层
│   │   ├── Main.c                   # main() 入口, 4 阶段启动 + 非阻塞主循环
│   │   ├── App_Network.c/h          # 联网管理 + 重试超时 + 遥测门控 + CMD 解析
│   │   ├── stm32f10x_conf.h         # SPL 头文件配置
│   │   └── stm32f10x_it.c           # ISR (SysTick + USART2 + Fault ×4)
│   ├── Library/                      # SPL V3.5.0 (只读, 从不修改)
│   ├── Start/                        # 启动文件 + system_stm32f10x
│   ├── Project.uvprojx               # Keil MDK 工程文件
│   └── Target 1.BAT                  # 编译后处理 (fromelf hex)
├── ONENETapp/                        # 网页控制台 (→ Ran-sh/WPT_Onenet_IoT)
├── 安卓app/                           # 微信小程序 (pages/index/)
├── Railway_Deploy/                   # Railway 桥接 (→ Ran-sh/WPT_Railway, 历史)
├── Claude_Files/                     # AI 文档与工具
│   ├── docs/                         # 开发指南 (MD+DOCX 配对)
│   │   ├── WPT无线充电系统-从零搭建全指南.md/.docx
│   │   └── embedded-architect-system-prompt.md
│   ├── diagrams/                     # Visio 系统工作流
│   │   ├── WPT_PWM_系统工作流.vsdx   # 4 页 A4 横版 (启动/调度/中断/数据流)
│   │   ├── draw_visio_v4.py          # 自动化生成脚本 (pywin32 + Visio COM)
│   │   └── README.md                 # 图示目录说明
│   └── tools/                        # ngrok 脚本 / DOCX 生成
├── 硬件原理图/                        # 硬件设计文件
├── CLAUDE.md                         # AI 辅助开发规范 (V6.1)
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
3. OLED 显示 "Wireless Charge" → "Booting ESP..." → 自动进入连接中界面

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
4. OLED: 连接中 → `STATUS:ONLINE` → READY

### 4. 网页控制台

浏览器打开 `https://wptonenet.483763727.workers.dev`, 登录 `admin / 123456789`。

### 5. 微信小程序

WeChat DevTools 打开 `安卓app/` 目录, 上传体验版。

---

## 文档

| 文档 | 格式 | 说明 |
|:---|:---|:---|
| [WPT无线充电系统-从零搭建全指南](Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md) | MD+DOCX | 完整开发指南: 概述→硬件→OneNET→STM32→ESP8266→网页→小程序→联调→故障速查 |
| [WPT_PWM_系统工作流](Claude_Files/diagrams/WPT_PWM_系统工作流.vsdx) | Visio VSDX | 4 页系统流程图 (启动/主循环调度/中断与安全/Dual-MCU数据流) |
| [CLAUDE.md](CLAUDE.md) | MD | AI 辅助开发规范 (模块架构/命名/调度/安全/编码规则) |

---

## 作者

**Rssss**

## 许可

MIT
