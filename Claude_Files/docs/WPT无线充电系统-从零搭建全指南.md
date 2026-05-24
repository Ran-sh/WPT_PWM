> #### Word 级排版视觉规范声明
>
> | 元素 | 字体 | 字号 | 颜色 | 行距/样式 |
> |:---|:---|:---|:---|:---|
> | 文档主标题 | 微软雅黑 | 22pt | #1A1A2E | 段后 18pt, 居中加粗 |
> | 一级章节 (##) | 微软雅黑 | 16pt | #2B579A | 段前 12pt, 段后 6pt, 加粗 |
> | 二级章节 (###) | 微软雅黑 | 14pt | #2B579A | 段前 8pt, 段后 4pt, 加粗 |
> | 三级章节 (####) | 微软雅黑 | 12pt | #3A6EA5 | 段前 6pt, 段后 3pt, 加粗 |
> | 正文 | 宋体 | 11pt | #333333 | 1.5 倍行距 |
> | 代码块 | Consolas | 9.5pt | #2D2D2D | 浅灰底纹 (#F5F5F5), 单倍行距 |
> | 表格内容 | 宋体 | 10pt | #333333 | 1.2 倍行距, 表头加粗 + 浅蓝底纹 (#D5E8F0) |

---

## 项目仓库对照表

本项目各模块分别存放在不同的 GitHub 仓库中：

| 仓库 | 内容 | 用途 |
|:---|:---|:---|
| [Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM) (分支 `ONENET`) | 全部源代码 | 主仓库：STM32 固件 + ESP8266 固件 + 微信小程序 + 文档 |
| [Ran-sh/WPT_Onenet_IoT](https://github.com/Ran-sh/WPT_Onenet_IoT) (分支 `master`) | `ONENETapp/` 网页控制台 | Cloudflare Pages 部署源 |
| [Ran-sh/WPT_Railway](https://github.com/Ran-sh/WPT_Railway) (分支 `main`) | `bridge.mjs` + `package.json` | Railway 桥接服务器 (小程序用) |

**文件对应关系**:

| 文档章节 | GitHub 源文件路径 |
|:---|:---|
| 第 4 章 STM32 固件 | `Keil_Project/Hardware/*.c`, `Keil_Project/User/*.c`, `Keil_Project/System/*.c` |
| 第 5 章 ESP8266 固件 | `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino` |
| 第 6 章 网页控制台 | `ONENETapp/` (仓库 Ran-sh/WPT_Onenet_IoT) |
| 第 7 章 微信小程序 | `安卓app/pages/index/*`, `安卓app/server/bridge.cjs` |
| 第 10 章 踩坑记录 | 基于全部代码迭代历史总结 |

---

## 文档控制信息

| 字段 | 内容 |
|:---|:---|
| **文档版本** | V5.0 |
| **最后更新** | 2026-05-24 |
| **对应固件版本** | V5.0 |
| **作者** | Rssss |

### 修改日志

| 版本 | 日期 | 变更说明 |
|:---|:---|:---|
| V5.0 | 2026-05-24 | 合并架构指南+部署指南+小程序总结为一份零基础全指南; 新增踩坑记录与方案演进章节; 更新为Cloudflare Pages部署方案 |
| V4.2 | 2026-05-22 | SETFREQ 替代 F_UP/DOWN + 频率渐变斜坡; 网页端迁移至 Cloudflare |

---

# WPT 无线充电全桥谐振控制系统 — 从零搭建全指南

> **适用硬件**: STM32F103C8T6 + ESP8266-01 + SSD1315 128x64 OLED
> **固件库**: STM32 SPL V3.5.0 + Arduino ESP8266 Core 3.x
> **编译工具**: Keil MDK-ARM V5 + Arduino IDE
> **协议**: OneNET MQTT 物模型 (Dual-MCU V5.0)
> **面向读者**: 零基础小白，有基础单片机概念即可

---

## 1. 项目概述

### 1.1 这是什么系统

这是一套**无线充电全桥谐振电源的远程监控系统**。通俗地说：你可以用手机或电脑，在任何有网络的地方，实时看到设备的电压、电流、频率，还能远程开关机和调节频率。

**核心功能**：
- 手机上实时看电压/电流/频率数据
- 远程开关机和调整频率（95kHz~150kHz）
- 历史数据曲线图
- 故障自动保护

### 1.2 为什么需要这个系统

无线充电设备工作时会产生强磁场（100kHz 级别），人不能靠近。有了远程监控，你可以在安全的距离外通过手机或电脑查看设备状态、控制开关、调整频率。

### 1.3 整体架构图

```
┌─────────────────────────────────────────────────────────┐
│                     你的手机/电脑                          │
│              浏览器打开网页 或 微信小程序                   │
└────────────┬────────────────────────────────────────────┘
             │ HTTPS
             ▼
┌────────────────────────┐     ┌──────────────────────────┐
│   Cloudflare Pages      │     │   Railway (可选)          │
│   (网页控制台)           │     │   (小程序桥接服务器)       │
└────────────┬───────────┘     └────────────┬─────────────┘
             │ HTTP API                     │ MQTT
             ▼                              ▼
┌──────────────────────────────────────────────────────────┐
│               OneNET 云平台 (中国移动)                      │
│            物模型: V/I/F/Switch/SetFreq                   │
└────────────┬─────────────────────────────────────────────┘
             │ MQTT
             ▼
┌──────────────────────────────────────────────────────────┐
│                   ESP8266-01 (联网脑)                      │
│  • WiFi 联网 (WiFiManager 配网)                           │
│  • MQTT 数据上传/指令接收                                  │
│  • JSON 串口透传给 STM32                                  │
└────────────┬─────────────────────────────────────────────┘
             │ USART2 115200bps
             ▼
┌──────────────────────────────────────────────────────────┐
│               STM32F103C8T6 (物理脑)                      │
│  • TIM1 全桥 PWM 发波 (95k~150kHz, 50% 占空比)            │
│  • ADC 采集电压/电流                                       │
│  • 非阻塞软启动扫频                                        │
│  • 过流保护                                                │
│  • OLED 显示 + 按键交互                                   │
└──────────────────────────────────────────────────────────┘
```

**一句话总结**：STM32 管硬件，ESP8266 管联网，OneNET 做中转，网页做界面。

### 1.4 Dual-MCU 双脑架构

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • PWM 发波 + PFM 调功        │    │  • WiFiManager 网页配网       │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET MQTT 物模型连云     │
│  • KEY/OLED/LED 人机交互      │    │  • 双 MQTT (OneNET + EMQX)   │
│  • 纯 JSON 串口透传           │    │  • WiFi/MQTT 自动重连         │
│  • 软启动扫频 + 过流保护       │    │  • 状态上报 (STATUS:ONLINE)   │
│  • 频率渐变斜坡               │    │  • 清除配网凭据 (CMD:CLEAR)   │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)          │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000,  │
           │   "S":2}                           │
           │◄──────────────────────────────────┤
           │  CMD:ON / CMD:OFF /               │
           │  CMD:SETFREQ:100000 /             │
           │  STATUS:ONLINE                    │
```

**铁律**: STM32 绝不发送 AT 指令，ESP8266 绝不操作 PWM/ADC。通信协议为纯文本 JSON over USART2 115200bps。

### 1.5 三层软件架构 (STM32 侧)

```
         ┌──────────────────────────┐
         │   应用层 (Application)    │  User/main.c, User/App_Net.c
         ├──────────────────────────┤
         │   系统服务层 (System)      │  System/SysTimer.c
         ├──────────────────────────┤
         │   硬件驱动层 (Hardware)    │  Hardware/ESP8266, PWM, ADC, KEY, OLED, LED, UI
         ├──────────────────────────┤
         │   SPL 外设库 (Library)     │  只读，绝不修改
         └──────────────────────────┘
```

**层间依赖规则**: Hardware 层仅依赖 SPL 头文件; System 层依赖 Hardware; Application 层依赖两者。绝不反向依赖。

### 1.6 引脚分配总表

| Pin | Function | Connected To | 备注 |
|:---|:---|:---|:---|
| PA0 | ADC_CH0 | 电流传感器 CC6920-10A | I_SENSITIVITY=0.132V/A |
| PA1 | ADC_CH1 | 电压分压网络 20:1 | - |
| PA2 | USART2_TX | ESP8266 RXD | 交叉连接 |
| PA3 | USART2_RX | ESP8266 TXD | 交叉连接 |
| PA7 | TIM1_CH1N | 下半桥左臂下管 | IR2103S 栅极驱动 |
| PA8 | TIM1_CH1 | 上半桥左臂上管 | IR2103S 栅极驱动 |
| PA9 | TIM1_CH2 | 上半桥右臂上管 | IR2103S 栅极驱动 |
| PA11 | GPIO (OD) | OLED SCL | 软件模拟 I2C |
| PA12 | GPIO (OD) | OLED SDA | 软件模拟 I2C |
| PB0 | TIM1_CH2N | 下半桥右臂下管 | IR2103S 栅极驱动 |
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN | 1000ms 低→高复位时序 |
| PB3 | GPIO (PP) | WiFi LED | JTAG 禁用后释放 |
| PB4 | GPIO (PP) | PWM LED | JTAG 禁用后释放 |
| PB5 | GPIO (PP) | Ready LED | - |
| PB12 | GPIO (IPU) | KEY0 | 单击/双击/长按 |
| PB13 | GPIO (IPU) | KEY1 | 单击 |
| PC13 | GPIO (PP) | Heartbeat LED | 低电平有效 |

**关键硬件注意**: ESP8266 需独立 3.3V LDO (≥500mA, 如 AMS1117-3.3)，STM32 板载稳压器无法承受 WiFi 突发电流 (~300mA)。ESP8266 CH_PD/EN 由 STM32 PB1 控制复位时序。

> 笔者踩坑：一开始直接用 STM32 核心板上的 3.3V 给 ESP8266 供电，结果 WiFi 一发包就反复重启。换了独立 AMS1117 后问题消失。

---

## 2. 硬件准备

### 2.1 物料清单

| 物料 | 规格/型号 | 数量 | 用途 | 参考价格 |
|:---|:---|:---|:---|:---|
| 主控板 | STM32F103C8T6 最小系统板 | 1 | 物理脑 | ¥10~20 |
| WiFi 模块 | ESP8266-01 | 1 | 联网脑 | ¥5~10 |
| OLED 屏 | SSD1315 128x64 0.96寸 I2C | 1 | 本地显示 | ¥8~15 |
| 稳压模块 | AMS1117-3.3V | 1 | ESP8266 独立供电 | ¥1~2 |
| USB-TTL 模块 | CH340G | 1 | 烧录 ESP8266 | ¥5~10 |
| ST-Link 下载器 | ST-Link V2 | 1 | 烧录 STM32 | ¥10~20 |
| 全桥驱动板 | IR2103S + MOSFET | 1 | 功率输出 | 自制 |
| 电流传感器 | CC6920-10A | 1 | 电流采集 | ¥5~10 |
| 分压电阻 | 200kΩ + 10kΩ | 1组 | 电压采集 20:1 | ¥0.5 |
| 按键 | 轻触开关 | 2 | KEY0/KEY1 | ¥0.5 |
| LED | 3mm 红绿蓝 | 4 | 状态指示 | ¥1 |
| 杜邦线 | 母对母 | 若干 | 接线 | ¥2 |
| 面包板 | 830孔 | 1 | 焊接前测试 | ¥5 |

### 2.2 接线图

**ESP8266 与 STM32 之间只需要 4 根线**：

```
STM32 PA2 (TX)  ──── ESP8266 RXD      (数据: STM32 → ESP8266)
STM32 PA3 (RX)  ──── ESP8266 TXD      (数据: ESP8266 → STM32)
STM32 PB1       ──── ESP8266 CH_PD/EN (硬件复位控制)
STM32 GND       ──── ESP8266 GND      (共地)
```

**ESP8266 独立供电**：
```
AMS1117 Vin  ──── 5V 电源
AMS1117 Vout ──── ESP8266 VCC + ESP8266 CH_PD (上拉 10kΩ)
AMS1117 GND  ──── 电源 GND + ESP8266 GND
```

> ESP8266 RST 引脚接 10kΩ 上拉到 3.3V 即可，不需要 MCU 控制。

**烧录 ESP8266 时的接线**：
```
USB-TTL TXD ──── ESP8266 RXD
USB-TTL RXD ──── ESP8266 TXD
USB-TTL 3.3V ─── ESP8266 VCC + CH_PD
USB-TTL GND ──── ESP8266 GND + GPIO0    ← GPIO0 必须接 GND 进入烧录模式!
```

### 2.3 供电注意事项

**ESP8266 WiFi 发包时瞬间电流可达 300mA**。STM32F103C8 最小系统板上的 AMS1117-3.3 通常只能提供 200~300mA，除了 STM32 自用外所剩无几。给 ESP8266 供电时必须**额外加一片独立的 AMS1117-3.3V**，并在输入输出端各并 100μF + 0.1μF 电容。

验证供电是否充足：ESP8266 上电后如果串口反复输出 `[System] ESP8266 Booting...` 就是供电不足。

---

## 3. OneNET 平台配置

### 3.1 注册与创建产品

OneNET 是中国移动提供的物联网云平台，免费使用。

1. 打开 [open.iot.10086.cn](https://open.iot.10086.cn) → 注册/登录
2. 进入**开发者中心** → 点击**创建产品**
3. 填写产品信息：

| 字段 | 值 |
|:---|:---|
| 产品名称 | WPT 无线充电 (可自定义) |
| 产品类别 | 其他 |
| 产品协议 | MQTT (新版 Studio) |
| 节点类型 | 直连设备 |
| 联网方式 | Wi-Fi |

4. 创建产品后，进入**设备管理** → **添加设备**，设备名称填 `20260001`（或自定义）

5. 记录以下信息，后续步骤要用：

| 字段 | 值 | 用途 |
|:---|:---|:---|
| 产品 ID (Product ID) | 如 `1iS397oJFL` | 唯一标识你的产品 |
| 设备名称 (Device Name) | 如 `20260001` | 设备唯一 ID |
| Token | (自动生成) | MQTT 连接鉴权 |

### 3.2 物模型定义

物模型是 OneNET 描述设备"有什么属性"的方式。**标识符必须与 ESP8266 固件中一致**，否则数据对不上。

在 OneNET Studio 控制台 → **物模型** → **添加属性**：

| 标识符 | 名称 | 数据类型 | 取值范围 | 步长 | 单位 | 读写 |
|:---|:---|:---|:---|:---|:---|:---|
| V | 电压 | double (浮点) | 0~50 | 0.01 | V | 读写 |
| I | 电流 | double (浮点) | 0~10 | 0.01 | A | 读写 |

> **注意**: V 和 I 是**外部输入**到逆变器的直流电压和电流，即电源适配器的输出电压和电流。它们是通过 STM32 的 ADC 引脚（PA1 分压采样、PA0 霍尔采样）直接测量得到的实际值，不是计算出来的。与高频谐振腔内部的高压/高频无关。
| F | 显示频率 | int32 (整数) | 95000~150000 | 1000 | Hz | 读写 |
| Switch | PWM功能开关 | bool (布尔) | true/false | 1 | - | 读写 |
| SetFreq | 设置频率 | int32 (整数) | 95000~150000 | 1000 | Hz | 读写 |

> **关键**: 标识符大小写必须一致。`V` 不能写成 `v`，`Switch` 不能写成 `switch`。

### 3.3 Token 获取

Token 是设备连接 OneNET 的"密码"。在设备详情页可以复制 Token。

Token 格式示例：
```
version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D
```

其中 `et=2063362960` 是过期时间戳，过期后需重新生成。

### 3.4 常见配置错误与解决

| 错误 | 原因 | 解决 |
|:---|:---|:---|
| MQTT rc=-2 | 服务器地址填错 | 确认是 `mqtts.heclouds.com:1883` (s=Studio, 非 TLS) |
| 响应超时 | 缺少 set_reply 应答 | 更新 ESP8266 固件 (最新版已含) |
| authentication failed: invalid res | Token 中 `device` 写成了单数 | Token 里必须是 `devices` (复数) |
| 网页端"正在连接中"一直不消 | OneNET 未配置或 Token 填错 | 进设置页重新填写 OneNET 信息 |
| 网页端显示旧数据 | 浏览器缓存 | 清除 localStorage 或开无痕窗口 |
| SetFreq 无数据 | 物模型未添加 SetFreq 属性 | 在 OneNET 控制台添加 SetFreq 属性 |

> 笔者踩坑：好几次 Token 复制错了，`devices` 写成了 `device`（少一个 s），查了半天日志才发现。

---

## 4. STM32 固件

### 4.1 开发环境：Keil MDK 安装

1. 下载 Keil MDK-ARM V5（官网 [keil.com](https://www.keil.com)）
2. 安装 Keil.STM32F1xx_DFP.2.2.0 器件包
3. 打开工程文件 `Keil_Project/Project.uvprojx`

### 4.2 工程结构

```
Keil_Project/
├── Hardware/          ← 硬件驱动层
│   ├── PWM.c/.h       (全桥PWM+软启动+频率渐变)
│   ├── ADC.c/.h       (ADC采集+滑动滤波)
│   ├── KEY.c/.h       (按键FSM)
│   ├── OLED.c/.h      (OLED显示)
│   ├── LED.c/.h       (LED指示)
│   ├── UI.c/.h        (双页UI调度)
│   └── ESP8266.c/.h   (ESP8266硬件驱动)
├── System/
│   └── SysTimer.c/.h  (时基总管)
├── User/
│   ├── main.c         (初始化+主循环)
│   ├── App_Net.c/.h   (网络应用层)
│   └── stm32f10x_it.c (中断处理)
├── Library/           (SPL V3.5.0, 只读)
└── Start/             (启动文件)
```

### 4.3 编译与烧录

1. 打开 `Project.uvprojx` → 点击 **Build (F7)**
2. 确认 Output 窗口显示 `0 Error(s)`
3. ST-Link 连接 STM32 (SWDIO/SWCLK/GND/3.3V)
4. 点击 **Download (F8)** 烧录

### 4.4 核心代码说明

#### 4.4.1 SysTimer — 全项目唯一时基

所有定时任务共用这个毫秒计数器，通过时间戳差值法调度：

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (SysTimer_GetTick() - last >= PERIOD_MS) {
        last = SysTimer_GetTick();
        // 你的业务代码
    }
}
```

uint32_t 无符号减法自动处理 49.7 天溢出回绕，不需要特殊处理。

```c
void SysTick_Handler(void) {
    SysTimer_IncTick();   // 只能有这一行!
}
```

**禁止在 SysTick_Handler 中放业务代码**（如 KEY 扫描、OLED 刷新等），这是所有裸机开发者必须改掉的坏习惯。

#### 4.4.2 PWM 驱动 — 频率计算与死区

频率公式：`ticks = 72,000,000 / freq_Hz`，奇数强制 +1（防偏磁）。

**频率量化**：由于定时器只能整数分频，不是每个 kHz 都能精确输出。例如设 103kHz，`ticks=72000000/103000=699`(奇数→700)，实际频率=`72000000/700=102857Hz`≈**102kHz**。网页端和 OLED 显示的值会被截断到整数 kHz，所以会有一致的结果（都显示 102）。

死区配置 `DEADTIME_NS=1000ns`，编译期自动换算寄存器值，断言确保 ≤127（BDTR 线性段）。

**原子更新 ARR+CCR 防止偏磁**：
```c
TIM1->CR1 |= TIM_CR1_UDIS;     // 暂停影子寄存器
TIM_SetAutoreload(TIM1, arr);
TIM_SetCompare1(TIM1, ccr);
TIM_SetCompare2(TIM1, ccr);
TIM1->EGR |= TIM_EGR_UG;      // 软件触发, 原子加载
TIM1->CR1 &= ~TIM_CR1_UDIS;   // 恢复硬件更新
```

#### 4.4.3 非阻塞软启动扫频

```
SS_IDLE → Trigger(150kHz) → SS_SWEEP → 250步(200Hz/10ms) → SS_DONE(100kHz)
                                           ↓
                                    Stop/过流 → SS_IDLE/SS_FAULT
```

参数: 起始 150kHz, 目标 100kHz, 步长 200Hz, 节拍 10ms, 共 250 步 ≈ 2.5 秒。

**原子状态切换**: `s_ss_state` 写入必须通过 `Inverter_SetState()`（关全局中断→写→恢复），防止按键和 WiFi 指令并发抢占。

**频率硬下限 95kHz** 是容性区的绝对红线——低于此频率谐振腔进入容性区，MOSFET 承受极大开关应力，会有炸管风险。

#### 4.4.4 ADC 采集与滑动滤波

100kHz 强磁场下 DMA 瞬时值噪声严重，使用 16 样本 O(1) 滑动平均。`ADC_Filter_Task()` 以 2ms 独立节拍运行，响应延迟 32ms。

```c
// O(1) 滑动平均 — 不随窗口大小增加计算量
uint16_t old = buf[idx];           // 保存最旧值
buf[idx] = new_sample;             // 覆盖新值
accum += buf[idx];                 // 累加新值
if (filled >= WINDOW)
    accum -= old;                  // 减去最旧值
```

`Get_Real_Voltage()` / `Get_Real_Current()` 直接返回预计算值 (O(1))，不需要每次调用都重新算一遍。

**浮点精度坑**: 整数除法 `(float)(accum / filled)` 会导致截断误差。正确写法是 `(float)accum / filled`——先把分子转为 float，再做浮点除法。

#### 4.4.5 OLED 双页 UI

SSD1315 128×64, 软件 I2C (PA11=SCL, PA12=SDA)。8×16 ASCII 字体, 4 行 × 16 字符。

- **Page 0 — 控制面板**: 可查看状态、可按键操作
- **Page 1 — 锁屏监控**: 只读，仅 KEY0 双击可切回

`OLED_Clear()` 耗时 ~100ms，**只在状态迁移或切页时调用**。日常 200ms 刷新用 16 字符全宽行覆盖，不调用清屏（避免阻塞保护任务）。

**关键优化**: Cortex-M3 无硬件 FPU，浮点运算极慢。OLED_ShowFloatNum 改用 float 而非 double 参数，pow10 用查找表替代运行时计算。

#### 4.4.6 KEY 按键 — 七态 FSM

八态(原七态+长按)状态机: IDLE → DEBOUNCE → PRESSED → WAIT_RELEASE → WAIT_DOUBLE → WAIT_DOUBLE_REL → LONG_PRESS。

`KEY_Task()`: 10ms 时间戳差值调度。`KEY_Get_Event()`: 阅后即焚。

| 键 | 事件 | 功能 |
|:---|:---|:---|
| KEY0 单击 (1) | HW 初始化 / Trigger 扫频 / Stop |
| KEY0 双击 (2) | 切页 (控制面板 ↔ 锁屏) |
| KEY0 长按 (3, >3s) | 清除 ESP8266 配网凭据 |
| KEY1 单击 (1) | Stop / +1kHz 调频 |

#### 4.4.7 App_Net — 网络应用层

`App_Net_Init()`: ESP8266 硬件复位 (~3s 阻塞)，设置 `s_network_online=0`，等待 `STATUS:ONLINE`。

`App_Net_Task()`: 每 500ms JSON 遥测 + 实时串口指令轮询。

`App_Net_IsConnected()`: `ESP8266_IsReady() && s_network_online`——硬件就绪 **且** ESP8266 确认联网。

**指令协议**:
- `CMD:ON\n` → 开启软启动 (仅 IDLE 状态)
- `CMD:OFF\n` → 关断逆变器
- `CMD:SETFREQ:100000\n` → 频率渐变到 100kHz (仅 DONE 状态)
- `STATUS:ONLINE\n` → ESP8266 通知 STM32 "已联网" (STM32 收到后置 `s_network_online=1`)

---

## 5. ESP8266 固件

### 5.1 Arduino IDE 环境搭建

1. 下载 [Arduino IDE](https://www.arduino.cc/en/software) (1.8.x 或 2.x)

2. 添加 ESP8266 开发板：
   - 文件 → 首选项 → 附加开发板管理器网址 → 填入：
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - 工具 → 开发板 → 开发板管理器 → 搜 `ESP8266` → 安装

3. 安装库（工具 → 管理库）：
   - `ArduinoJson` (by Benoit Blanchon) — **V7 版本**
   - `PubSubClient` (by Nick O'Leary)
   - `WiFiManager` (by tzapu)

4. Arduino IDE 设置：
   - 开发板: **Generic ESP8266 Module**
   - Flash Size: `1M (no SPIFFS)`
   - Upload Speed: `115200`
   - CPU Frequency: `80 MHz`

5. 打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`

### 5.2 WiFiManager 配网流程

首次上电时，ESP8266 没有已保存的 WiFi 凭据：

1. ESP8266 自动开启热点 **`STM32_WPT_Config`**（无密码）
2. 用手机连这个热点 → 浏览器自动弹出配网页
3. 选你的路由器 → 输密码 → 保存
4. ESP8266 自动重启 → 连上 WiFi → 凭据存入闪存
5. 以后每次上电自动连，不需要重新配网

**换 WiFi 时**: 长按 STM32 的 KEY0 > 3 秒 → STM32 发 `CMD:CLEAR\n` → ESP8266 清除凭据并重启 → 重新进入配网模式。

> 笔者踩坑：WiFiManager v2.x 的 `autoConnect()` 在没有已存凭据时不自动启动 AP，必须加 `startConfigPortal()` 兜底。最初没加这行，反复重启让人怀疑人生。

### 5.3 代码关键逻辑

#### 5.3.1 双 MQTT 连接

ESP8266 同时连接两个 MQTT 服务器：

| 连接 | 服务器 | 用途 |
|:---|:---|:---|
| OneNET MQTT | `mqtts.heclouds.com:1883` | 物模型数据上报 + 接收云平台指令 |
| EMQX 公共 | `broker.emqx.io:1883` | Web 端数据直读 (可选) |

**非阻塞重连**: 每 5 秒检查 WiFi + MQTT 状态，断开后自动重连，不阻塞 loop 循环。

#### 5.3.2 JSON 转换

STM32 发来的格式：
```json
{"V":12.50, "I":1.23, "F":100000, "S":2}
```

ESP8266 转换为 OneNET 物模型格式后发布：
```json
{
  "id": "123",
  "version": "1.0",
  "params": {
    "V": {"value": 12.50},
    "I": {"value": 1.23},
    "F": {"value": 100000},
    "Switch": {"value": true},
    "SetFreq": {"value": 100000}
  }
}
```

其中 `S` 字段（软启动状态）映射为 Switch:
- S=1(SWEEP) 或 S=2(DONE) → Switch=true (运行中)
- S=0(IDLE) 或 S=3(FAULT) → Switch=false (停止)

#### 5.3.3 指令透传

OneNET 下发 `Switch=true` → ESP8266 解析 → 串口发 `CMD:ON\n` → STM32 执行。

OneNET 下发 `SetFreq=105000` → ESP8266 解析 → 串口发 `CMD:SETFREQ:105000\n` → STM32 频率渐变。

OneNET 的 Switch 支持三种格式（防御性兼容）:
- `Switch: true` (纯布尔)
- `Switch: {"value": 1}` (对象值)
- `Switch: 1` (纯数字)

#### 5.3.4 状态上报 (STATUS:ONLINE)

ESP8266 在 WiFi+MQTT 双连接成功后，向 STM32 发送 `STATUS:ONLINE\n`。STM32 收到后更新 `s_network_online=1`，WiFi LED 从慢闪变常亮，OLED 从 "No WiFi" 切回正常。

**如果没有 STATUS:ONLINE 机制**: STM32 只知道"ESP8266 硬件初始化过"，不知道它是否真的连上了 WiFi。用户可能在手机热点没开的情况下以为一切正常——这是早期版本的坑。

#### 5.3.5 set_reply 应答

收到属性设置后必须回复 OneNET：
```json
{"id": "123", "code": 200, "msg": "success"}
```
否则 OneNET 控制台显示"响应超时"。

### 5.4 烧录方法

1. USB-TTL 连接 ESP8266-01（GPIO0 必须接 GND）
2. Arduino IDE 点击**上传**(→)
3. 等待编译+烧录 (~30 秒)
4. 烧录完成后断开 GPIO0-GND → 重新上电

### 5.5 常见问题

| 问题 | 原因 | 解决 |
|:---|:---|:---|
| 烧录失败 (espcomm_sync failed) | GPIO0 未拉低 / 供电不足 | 确保 GPIO0 可靠接 GND，独立 3.3V 供电 |
| 烧录后不运行 | 仍在烧录模式，GPIO0 仍接地 | 断开 GPIO0-GND 重新上电 |
| 反复重启 (串口重复 Booting) | 供电不足或无 WiFi 凭据 | 检查独立 LDO；升级固件（含 startConfigPortal 兜底） |
| WiFi 连不上 (无已存凭据) | 首次使用或凭据被清除 | 手机搜 `STM32_WPT_Config` 配网 |
| WiFi 已连但 OneNET 离线 | MQTT 地址错 / Token 错 | 检查 `mqtts.heclouds.com:1883`；重新生成 Token |
| 串口收到乱码 | 波特率不匹配 | 确认 115200 |
| 下发命令无反应 | TXD/RXD 接反 | STM32 和 ESP8266 之间是交叉连接 (TX→RX) |

---

## 6. 网页控制台

### 6.1 本地运行

如果你只想在电脑上本地使用（不需要公网访问）：

```bash
cd ONENETapp
node -e "const h=require('http');const f=require('fs');const p=require('path');
h.createServer((q,r)=>{let fp=q.url.split('?')[0];if(fp==='/')fp='/login.html';
if(!p.extname(fp))fp+='.html';
f.readFile(p.join(__dirname,fp),(e,d)=>{if(e){f.readFile(p.join(__dirname,
'login.html'),(e2,d2)=>{r.writeHead(200);r.end(d2)});return}
r.writeHead(200);r.end(d)})}).listen(4567,()=>console.log('http://localhost:4567'))"
```

浏览器打开 `http://localhost:4567` → 登录 → 设置页配 OneNet → 完成。

### 6.2 Cloudflare Pages 部署（推荐，免费公网）

1. 确保代码已推送到 GitHub: `https://github.com/Ran-sh/WPT_Onenet_IoT`
2. 打开 [pages.cloudflare.com](https://pages.cloudflare.com) → GitHub 登录
3. Create a project → 选 `Ran-sh/WPT_Onenet_IoT` 仓库
4. 部署设置留空（静态站点） → Save and Deploy
5. 几秒后得到公网地址如 `https://wptonenet.483763727.workers.dev`

任何联网设备都能访问，不需要开电脑。免费额度足够个人使用。

### 6.3 配置 OneNet 参数

部署后打开网页 → 登录（账号 `admin`，密码 `123456789`）→ 进入**设置** → 点 "OneNet 平台配置" → 填入：

| 字段 | 值 |
|:---|:---|
| Product ID | `1iS397oJFL` |
| Device Name | `20260001` |
| Token | (你的 OneNET Token) |

点"保存并重启"，网页自动重新加载，首页开始显示实时数据。

### 6.4 各页面功能

| 页面 | 功能 |
|:---|:---|
| **首页** | 电压/电流/频率实时数据横排显示 + 启停控制条 |
| **控制** | 启停开关 + 频率设置(kHz) + 操作记录表 |
| **监测** | 实时折线图 (动态Y轴) |
| **历史** | 历史数据表 + 长期变化曲线 |
| **设置** | OneNet 配置 / 数据模型管理 / 系统标题 |

### 6.5 方案演进 — 我们试过哪些，为什么换了

| 方案 | 结果 | 原因 |
|:---|:---|:---|
| **Netlify** | 先成功，后弃用 | 免费带宽耗尽，网站被暂停 |
| **GitHub Pages** | 部署成功但路由有问题 | 子目录项目，`/monitoring` 无法映射到 `.html` |
| **Cloudflare Pages** ✅ | **当前方案** | 免费不限量，支持 SPA 路由 |

### 6.6 网页端常见问题

| 问题 | 解决 |
|:---|:---|
| 首页一直"连接中" | 进设置页配置 OneNet 参数 |
| 数据显示但控制无反应 | 检查 Token 是否正确（devices 复数） |
| 频率显示不正确 | 清除浏览器 localStorage 后重新配置 |
| 手机端布局错乱 | 下拉刷新页面（Cloudflare 已部署最新版） |
| 登录密码忘了 | admin / 123456789 |

---

## 7. 微信小程序 (可选)

### 7.1 架构原理

微信小程序真机强制 HTTPS/WSS，不能直连 MQTT Broker。需要一个桥接服务器把 HTTP 请求转成 MQTT：

```
小程序 ──HTTPS── 桥接服务器 ──MQTT── EMQX ── ESP8266 ── STM32
```

### 7.2 桥接服务器

`安卓app/server/bridge.cjs` 是一个 Node.js Express 服务器：
- `GET /data` → 返回最新遥测数据
- `POST /cmd` → 下发控制指令
- 连接 EMQX，订阅 `wpt/20260001/data`，发布到 `wpt/20260001/cmd`

### 7.3 方案演进

| 阶段 | 方案 | 遇到的问题 |
|:---|:---|:---|
| 1 | **WebSocket 直连 EMQX** | 微信真机不认 EMQX TLS 证书，`wx.connectSocket` 失败 |
| 2 | **HTTP 轮询 + ngrok 隧道** | DNS 污染导致 ngrok 无法连接; 免费版限流 |
| 3 | **HTTP 轮询 + Railway** ✅ | 稳定 HTTPS 域名，免费额度够用 |

**ngrok DNS 污染问题**: 教育网 (CERNET) DNS 将 `connect.ngrok-agent.com` 投毒解析到 `127.0.0.1`。修复方法是在 hosts 文件中添加 ngrok 服务器的真实 IP (`3.1.215.86`)，但这只能临时解决。

### 7.4 Railway 部署

1. 将 `安卓app/server/bridge.mjs` + `package.json` + `Procfile` 推送到 GitHub
2. Railway 新建项目 → Deploy from GitHub → 自动部署
3. 获取 Railway 分配的 HTTPS 域名
4. 在小程序 `index.js` 中更新 `BRIDGE` 地址

### 7.5 小程序与网页端对比

| | 网页端 (推荐) | 微信小程序 |
|:---|:---|:---|
| 部署难度 | 低 (Cloudflare 直接部署) | 中 (需 Railway + 微信开发者工具) |
| 使用平台 | 手机/电脑浏览器 | 微信内 |
| 实时性 | 2~5 秒 (HTTP 轮询) | 2 秒 (HTTP 轮询) |
| 成本 | 免费 | 免费额度 |
| 外网访问 | 自动支持 | 自动支持 |

**建议**: 日常使用网页端即可，手机浏览器打开一样好用。小程序适合需要微信集成的高级场景。

---

## 8. 联调指南

### 8.1 上电顺序

1. **先给 ESP8266 上电**（独立 3.3V），等待 ~5 秒 WiFi+MQTT 连接
2. **再给 STM32 上电**
3. STM32 按 KEY0 → 硬件初始化 ESP8266 (~3 秒) → OLED 显示 `HW: READY`
4. 等待 ESP8266 发送 `STATUS:ONLINE` → OLED 从 "No WiFi" 切正常状态
5. 打开网页确认数据显示

### 8.2 逐环节验证

按以下顺序排查，每步通过才能下一步：

| 步骤 | 检查点 | 通过标准 |
|:---|:---|:---|
| 1 | STM32 串口输出 | 按 KEY0 后用串口助手(115200 8N1)收到 `{"V":...}` JSON |
| 2 | ESP8266 WiFi | 串口监视器显示 `Connected! IP: xxx` |
| 3 | ESP8266 MQTT | 串口监视器显示 `OneNET Connected successfully!` |
| 4 | OneNET 在线 | OneNET 控制台设备状态显示"在线" |
| 5 | OneNET 数据 | OneNET 控制台数据流能看到 V/I/F |
| 6 | STM32-ESP8266 联调 | STM32 串口输出被 ESP8266 转发到 OneNET |
| 7 | 网页数据 | 网页首页显示实时 V/I/F |
| 8 | 网页控制 | 网页点 Switch 开关，OLED 响应 |

### 8.3 调试工具

| 工具 | 用途 |
|:---|:---|
| **串口助手** (115200 8N1) | 连 STM32 PA2/PA3 看 JSON 输出；模拟发送 CMD 指令 |
| **Arduino 串口监视器** | 看 ESP8266 WiFi/MQTT 连接日志 |
| **OneNET 控制台** | 看设备在线状态、数据流、下发指令 |
| **浏览器 F12 → Console** | 看网页 API 请求日志、数据同步状态 |
| **浏览器 F12 → Application → Local Storage** | 清除 `iot_onenet_config` 和 `iot_data_model` 重置配置 |
| **curl 命令** | 直接测试 OneNET HTTP API |

**curl 测试 OneNET API**（不依赖任何网页，直接确认云端状态）：
```bash
curl "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=1iS397oJFL&device_name=20260001" \
  -H "Authorization: version=2018-10-31&res=products%2F1iS397oJFL%2Fdevices%2F20260001&et=2063362960&method=md5&sign=phYCE26jNI80tiXEeMxxRA%3D%3D"
```

### 8.4 验证 STM32 串口（无 ESP8266）

1. USB-TTL 模块接线：TXD→STM32 PA3, RXD→STM32 PA2, GND→GND
2. 串口助手 115200 8N1，给 STM32 上电
3. 按 KEY0，OLED 显示 `HW Init...` → `HW: READY`
4. 串口助手收到 `{"V":0.00,"I":0.00,"F":150000,"S":0}`
5. 发送 `CMD:ON\n`，STM32 进入扫频模式
6. 发送 `CMD:OFF\n`，STM32 停止
7. 发送 `CMD:SETFREQ:100000\n`，频率渐变到 100kHz

---

## 9. 故障速查表

### 9.1 按症状分类

| 症状 | 可能原因 | 排查方法 |
|:---|:---|:---|
| ESP8266 反复重启 | 供电不足 | 检查独立 LDO，测量 3.3V 是否稳定 |
| WiFi 已连但 OneNET 离线 | 服务器地址/T 错误 | 检查 `mqtts.heclouds.com:1883` |
| OneNET 命令"响应超时" | set_reply 缺失 | 确认 ESP8266 固件最新 |
| 网页"未连接" | ESP8266 没联网 | 查 OneNET 设备是否在线 |
| 网页数据不刷新 | Token 填错 | F12 Console 看错误日志 |
| STM32 收不到 CMD | TXD/RXD 交叉接反 | 交换 PA2/PA3 的接线 |
| OLED "No WiFi" | ESP8266 未发 STATUS:ONLINE | 确认 ESP8266 已烧录最新固件 |
| 频率显示和设置不一致 | PWM 频率量化 | 正常现象，网页端已自动修正 |
| 长按 KEY0 没反应 | 固件没更新 | 重新烧录 STM32 (KEY FSM 含长按检测) |
| 网页控制端 404 | Cloudflare 部署分支不对 | 确认 `master` 和 `gh-pages` 同步 |
| 首页一直"连接中" | OneNet 配置未填或 Token 错 | 进设置页重新配置 |

### 9.2 调试技巧

- **串口是最可靠的调试手段**：先确认 STM32 串口能发 JSON，再确认 ESP8266 能把 JSON 转到 MQTT
- **分层排查**：不要一上来就怀疑全部环节，一段一段确认
- **浏览器 F12 是网页调试神器**：Console 看日志，Network 看请求，Application 清缓存
- **OneNET 控制台** 看数据流可以确认 ESP8266 是否正常上报

---

## 10. 踩坑记录与方案演进

### 10.1 频率控制演进

| 版本 | 指令 | 方式 | 问题 | 状态 |
|:---|:---|:---|:---|:---|
| V1 | CMD:F_UP / CMD:F_DOWN | 每次 ±1kHz | 小程序点一下触发两次(双重点击) | 废弃 |
| V2 | OneNET FreqAdd/FreqSub | 虚拟按键点动 | OneNET 物模型复杂(两个布尔键) | 废弃 |
| V3 | CMD:SETFREQ:\<Hz\> | 直接设置目标频率 | 瞬间跳变，不好控制 | 废弃 |
| V4 | CMD:SETFREQ:\<Hz\> + 渐变斜坡 | 设置目标→500Hz/10ms 渐变到达 | 平滑，可中断 | **当前** |

**渐变斜坡参数**: 步长 500Hz, 间隔 10ms, 速率 50kHz/s。50kHz 跨度约 1 秒完成。

### 10.2 网页部署演进

| 版本 | 平台 | 遇到的问题 | 状态 |
|:---|:---|:---|:---|
| V1 | **本地 localhost** | 只能自己电脑用 | 保留 (调试方便) |
| V2 | **Netlify** | 免费带宽耗尽，网站被暂停 | 废弃 |
| V3 | **GitHub Pages** | 子目录路由 `/monitoring` → 404 | 废弃 |
| V4 | **Cloudflare Pages** | 免费不限量，支持 SPA 路由 | **当前** |

### 10.3 小程序通信演进

| 版本 | 方案 | 遇到的问题 | 状态 |
|:---|:---|:---|:---|
| V1 | **WebSocket 直连 EMQX** | 微信真机不认 EMQX TLS 证书 | 废弃 |
| V2 | **ngrok 隧道** | DNS 污染 + 免费版限流 | 废弃 |
| V3 | **Railway 桥接** | 需要 GitHub + Railway 部署 | 可选方案 |

### 10.4 频率显示精度问题

**根本原因**：STM32 TIM1 定时器只能整数分频 (72MHz/ticks)，奇数 tick 会被强制 +1（防偏磁）。这导致部分 kHz 值无法精确输出。

例如设置 103kHz：`72000000/103000=699`(奇→700) → 实际 `72000000/700=102857Hz` → 截断显示 `102kHz`。

**解决方案**：网页端预计算所有可达频率，只显示实际能输出的 kHz 值。用户选择的每个值都对应准确的 OLED 显示值。

### 10.5 WiFi 连接状态追踪

**问题**: ESP8266 硬件初始化 (`ESP8266_Init()`) 只做 CH_PD 复位+串口初始化，`ESP8266_IsReady()` 只表示"硬件就绪"，不代表"WiFi 已连"。

**解决**: ESP8266 在 WiFi+MQTT 双连成功后向 STM32 发送 `STATUS:ONLINE\n`。STM32 收到后才标记 `s_network_online=1`。`App_Net_IsConnected()` = 硬件就绪 + 网络在线。

**如果没连上**: OLED 显示 "No WiFi"，KEY0 可以重新触发硬件初始化（再给 ESP8266 一次机会）。ESP8266 自己每 5 秒尝试重连 WiFi。

### 10.6 DNS 污染与 hosts 修复

**问题**: 教育网 (CERNET) 的 DNS 服务器将 `connect.ngrok-agent.com` 投毒解析到 `127.0.0.1`，导致 ngrok 无法连接服务器。

**诊断方法**:
```bash
nslookup connect.ngrok-agent.com
# 如果返回 127.0.0.1 或 ::1，说明 DNS 被污染
```

**修复方法**: 在 hosts 文件添加 ngrok 服务器的真实 IP。先用 Google DNS 查询真实 IP：
```bash
nslookup connect.ngrok-agent.com 8.8.8.8
```

然后将其中一个公网 IP 加到 `C:\Windows\System32\drivers\etc\hosts`:
```
3.1.215.86 connect.ngrok-agent.com
```

> 这是临时方案，后来我们换成了 Railway 和 Cloudflare Pages，彻底绕过了 ngrok。

### 10.7 CORS 跨域踩坑

**问题**: 浏览器从网页端 (netlify.app / localhost) 发请求到 OneNET API 时，可能被浏览器的同源策略拦截。

**诊断**: 浏览器 F12 → Console 看到 `Access-Control-Allow-Origin` 相关错误。

**结果**: OneNET HTTP API 实际上返回 `Access-Control-Allow-Origin: *`，支持跨域。真正的问题是 Token 填错或 Device Name 写错。这个坑排了很久，最后才发现根本就不是 CORS 的问题。

### 10.8 ESP8266 WiFiManager v2.x 兼容性

**问题**: WiFiManager v2.x 的 `autoConnect()` 在没有已保存凭据时返回 `false` 而不自动启动配置门户 (Config Portal)。

**现象**: ESP8266 反复重启，串口循环输出 `Booting...`，手机搜不到 `STM32_WPT_Config` 热点。

**解决**: 在 `autoConnect()` 返回 `false` 后显式调用 `startConfigPortal()`:
```cpp
if (!wifiManager.autoConnect(WIFI_AP_NAME)) {
    wifiManager.startConfigPortal(WIFI_AP_NAME);
}
```

### 10.9 `device` vs `devices` — 一个字母的惨案

OneNET Token 中包含资源路径 `res=products%2F{pid}%2Fdevices%2F{dname}`。注意是 `devices`（复数），不是 `device`（单数）。

`%2F` 是 `/` 的 URL 编码，整句话解码后是 `products/{pid}/devices/{dname}`。

**写错的结果**: authentication failed: invalid res。排查了半小时才发现少了一个 `s`。

### 10.10 网页 UI 组件选型

| 组件 | 尝试过的方案 | 最终选择 | 原因 |
|:---|:---|:---|:---|
| 频率选取器 | Slider → 齿轮滚轮 → 自定义触摸尺 → Swiper | Swiper (display-multiple-items=1) | 滑动检测最可靠 |
| 页面布局 | 卡片网格 → 2×2 → 七行横排 | 全横排 | 手机端最清晰 |
| 状态标识 | 文字"正常/异常" → ✓/✗ → 圆底徽章 | 圆底 ✓/✗ | 视觉最直观 |
| 图表 Y 轴 | 固定范围 (0~60V) | 动态缩放 (data±25%) | 10V 数据不再压成平线 |

### 10.11 关键经验总结

1. **分层调试, 不要跳步**：先确认串口能发数据 → 再确认 ESP8266 能联网 → 再确认云平台能收到 → 最后看网页。跳步骤会浪费大量时间。

2. **供电是硬件调试第一关**：ESP8266 WiFi 发包瞬间电流大，独立 LDO 是必需品不是可选项。

3. **Token/ID 拼写错误是最常见的"玄学"问题**：每次复制粘贴后都检查一下。

4. **浏览器缓存是网页调试第一大坑**：改了代码没生效？先开无痕窗口试试。

5. **免费服务都有隐性限制**：Netlify 限带宽、ngrok 限请求数。Cloudflare Pages 目前最良心。

---

> **文档结束**
> *本文档涵盖从芯片引脚到云平台全链路技术细节，包含大量笔者亲历的踩坑记录。建议配合项目源码阅读。*
