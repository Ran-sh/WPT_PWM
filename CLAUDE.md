# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **协议** | OneNET MQTT 物模型 (Dual-MCU) + ST7735S TFT 彩屏 |
| **版本** | V6.2 |

> 其他分支（不同本地目录，**本分支推送只推 `4.0TFT`**）：
> `master` (V0.0 基版), `1.0LAN` (NetAssist 局域网 V3.4), `2.0WAN` (巴法云 TCP V3.5), `3.0ONENET` (V6.1 OLED — **已锁定，不可修改**)

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支 (`4.0TFT`，**绝不推其他分支**)

**状态机**
- 禁止用隐式 bool/int 标志组合表达状态 — 必须用显式 `typedef enum`
- 每个状态机有命名的枚举类型 + 单一状态变量, 不准用 `s_flag1`+`s_flag2` 拼凑
- 状态转换集中在一个 Task 函数内, 用 `switch` 分发
- 示例: `App_Network_Conn_State` 替代 `s_network_online`+`s_connecting` 双 bool

**调度**
- 所有周期任务用 `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值模式
- `Sys_Timer_Delay_Ms()` 仅限初始化阶段, 运行时绝对禁止阻塞延时
- 初始化序列也优先用非阻塞状态机 (参考 `Esp8266_Driver_Init_Task` 的 CH_PD 时序)
- 主循环末尾 `__WFI()` 休眠, SysTick 唤醒, 不空转

**模块架构**
- 每个模块 `.h` 只放公开接口, `.c` 放全部实现 + 静态变量
- `.c` 内部函数一律 `static`, 不准跨模块 `extern` 访问私有变量
- 不允许 `#include ".c"` 文件
- 模块内部辅助函数建议加模块前缀避免与 SPL 库函数冲突 (如 `Oled_I2C_Init` 而非 `I2C_Init`)
- 头文件保护: `MODULE_NAME_H`, 禁止双下划线前缀 (`__NAME_H` 是 C 保留字)

**OOP 在 C 中的实践**
- 相关变量封装到 struct 中, 避免多个分散的 static 变量
- 状态机用 struct 打包 (状态 + 定时器 + 上下文)
- 一个 `.c` 只管理自己定义的结构体, 外部通过函数接口访问

**分层依赖**
- Hardware → System → Application, 严格单向
- 应用层不直接操作寄存器, 不绕过硬件抽象层
- `Ui_Controller` 可通过 `Inverter_Control` 间接访问 PWM, 但不直接调 `Pwm_Driver`

**临界区**
- 统一 PRIMASK 保存/恢复模式, 禁止裸 `__disable_irq()`:
  ```c
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  /* critical section */
  __set_PRIMASK(primask);
  ```

**注释**
- 公开函数: `@brief` 一行说明功能, `@param`/`@retval` 标注参数和返回值
- 模块 `.h` 顶部: `@brief` 一句话 + `@note` 关键设计约束
- 不写 HOW (代码本身说明), 只写 WHY (为什么这样做, 踩过什么坑)

**安全**
- 故障处理器进入死循环前必须关断 PWM (`TIM_CtrlPWMOutputs(TIM1, DISABLE)`)
- IWDG 看门狗在 main() 初始化, 主循环喂狗
- 编译期可验证的约束用 `typedef char assertion[(condition)?1:-1]` 检查

**全桥 PWM 基线 (重构不改, 关系到全桥是否输出波形)**
- `GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE)` — PA7=CH1N, PB0=CH2N, 缺失则无输出
- `TIM_CounterMode_Up` — 不可改为 CenterAligned (频率公式不同, 两路 CH1=PWM1+CH2=PWM2 配合 Up 计数实现对角线交替导通)
- CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` — 两路不同模式, 桥间产生差分电压; 同模式则桥间电压为零
- `TIM_OCNPolarity_Low` — IR2103S LIN 为低有效, 不可改为 High
- `TIM_OCNIdleState_Set` — MOE 关断时下管必须关断 (LIN=HIGH), 不可改为 Reset
- 死区 1000ns, 由 `PWM_DRIVER_DEADTIME_NS` 宏统一定义
- 频率范围 95kHz~150kHz (`PWM_DRIVER_FREQ_MIN_HZ`/`MAX_HZ`), 软启动从 150k 扫到 100k
- 以上参数源自 V0.0 已验证硬件, 重构时逐行对照, 不准擅自改动

**可维护性**
- 魔法数字命名常量, 不准裸值散落代码中
- 显示字符串集中为 `#define STR_*` 宏, 方便多语言替换
- 频率/电压/电流限制单一定义, 全项目引用同一处
- 不保留废弃代码和旧文件, 删干净避免维护陷阱
- 生成的文件放到指定目录, 不准散落在桌面或其他无关位置; 不确定存放路径时先询问

## Architecture: Dual-MCU

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • PWM 发波 + PFM 调功        │    │  • WiFiManager 网页配网       │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET MQTT 物模型连云     │
│  • KEY/OLED/LED 人机交互      │    │  • 串口 JSON ↔ STM32 透传    │
│  • 纯 JSON 串口透传           │    │  • Conn_State 连接状态机      │
│  • 软启动扫频 + 过流保护       │    │  • 前缀匹配防协议误触发       │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)          │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000}  │
           │◄──────────────────────────────────┤
           │  CMD:ON\n  或  CMD:OFF\n           │
           │  CMD:SETFREQ:100000\n              │
```

**Iron rule**: STM32 never sends AT commands. ESP8266 never touches PWM/ADC. Communication is pure text JSON over USART2 at 115200bps.

## 多仓库推送规则 (Multi-Repo Push Doctrine)

| 本地文件夹 | 远程仓库 | 分支 | 说明 |
|:---|:---|:---|:---|
| `Keil_Project/`、`Arduino_Project/`、`安卓app/`、`Claude_Files/`、根目录文件 | `Ran-sh/WPT_PWM` | `4.0TFT` | 主仓库 |
| `ONENETapp/` | `Ran-sh/WPT_Onenet_IoT` | `master` | 网页控制台 (Cloudflare Pages) |
| `Railway_Deploy/` | `Ran-sh/WPT_Railway` | `main` | Railway 桥接服务器 |

```bash
# 主仓库
git add -A && git commit -m "..." && git push origin 4.0TFT

# ONENETapp (需同时推到 gh-pages)
**执行期间**: 全部权限自动通过，不中断等待用户确认。

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only, never modified
- **Project File**: `Keil_Project/Project.uvprojx`
- No CLI build — compile in Keil IDE GUI (F7 build → F8 download)
- **ESP8266**: Arduino IDE, board "Generic ESP8266 Module" (Flash 1M, 80MHz), file `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`

## Architecture

Dual-MCU: STM32 (全桥 PWM + ADC + TFT + 按键 + 保护) ↔ ESP8266 (WiFi + OneNET MQTT) 通过 USART2 115200 纯 JSON 通信，零 AT 指令。

三层依赖 Hardware → System → Application，单向不可逆。应用层不直接操作寄存器。

## V6.2 Pin Mapping (STM32F103C8 LQFP-48)

| Pin | 网络名 | 功能 | 模块 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | TFT 复位 | Tft_Driver |
| PA1 | ESP8266RST | ESP8266 RST | Esp8266_Driver |
| PA2 | USART2_TX | ESP8266 RXD | Esp8266_Driver |
| PA3 | USART2_RX | ESP8266 TXD | Esp8266_Driver |
| PA4 | TFT_CS | TFT 片选 (软件 NSS) | Tft_Driver |
| PA5 | TFT_SCL | SPI1_SCK | Tft_Driver |
| PA6 | TFT_DC | TFT 数据/命令选择 | Tft_Driver |
| PA7 | TFT_SDA | SPI1_MOSI | Tft_Driver |
| PA8 | HINA | TIM1_CH1 (PWM 左上管) | Pwm_Driver |
| PA9 | HINB | TIM1_CH2 (PWM 右上管) | Pwm_Driver |
| PA10 | LED_COM | 通信 LED | Led_Driver |
| PA11 | LED_POWER | 电源 LED | Led_Driver |
| PA12 | LED_TEMP | 温度 LED | Led_Driver |
| PA15 | LED_SYSTEM | 系统心跳 LED | Led_Driver |
| PB0 | ADCB0 | ADC_CH8 (电流 CC6920BSO-10A) | Adc_Driver |
| PB1 | ADCB1 | ADC_CH9 (电压 20:1 分压) | Adc_Driver |
| PB3 | LED_PWM | PWM 状态 LED | Led_Driver |
| PB4 | LED_WIFI | WiFi 状态 LED | Led_Driver |
| PB5 | PAGE | 翻页按键 (IPU) | Key_Driver |
| PB6 | TFT_BL | TFT 背光 PWM (TIM4_CH1) | Tft_Driver |
| PB7 | F_DOWN | 频率减按键 (IPU) | Key_Driver |
| PB8 | F_UP | 频率加按键 (IPU) | Key_Driver |
| PB9 | ON/OFF | 启停按键 (IPU) | Key_Driver |
| PB10 | PowerContrl | 12V 动力电源闸 | main.c |
| PB11 | ESP8266EN | ESP8266 CH_PD | Esp8266_Driver |
| PB13 | LINA | TIM1_CH1N (PWM 左下管) | Pwm_Driver |
| PB14 | LINB | TIM1_CH2N (PWM 右下管) | Pwm_Driver |
| PB15 | BUZ | 有源蜂鸣器 (S8050) | Buzzer_Driver |

**重要**：不执行任何 TIM1 重映射（默认映射），不执行 SPI1 重映射。JTAG 禁用 (`GPIO_Remap_SWJ_JTAGDisable`) 释放 PB3/PB4/PB5/PA15，SWD (PA13/PA14) 保留。

## Module Map (V6.2)

```
Keil_Project/Hardware/
  Pwm_Driver      — TIM1 全桥默认映射, Up 计数, CH1=PWM1/CH2=PWM2, 1000ns 死区, 95~150kHz
  Adc_Driver      — ADC1+DMA1 CH8/CH9, 64 样本滑动窗口, 144241 周期互质采样, EMA 零点校准
  Tft_Driver      — ST7735S 128×160 SPI1, RGB565, 8×16 ASCII + 16×16 中文 UTF-8 混合显示
  Key_Driver      — 4 键 FSM (单击/双击/长按 3s), KEY_ID_ONOFF/F_UP/F_DOWN/PAGE 常量
  Led_Driver      — 6 LED 独立闪烁 (ON/OFF/SLOW/FAST), 系统心跳 500ms
  Buzzer_Driver   — PB15 GPIO, ON/BEEP(200/800ms)/OFF 三态
  Esp8266_Driver  — USART2 行缓冲, RST(PA1)+CH_PD(PB11) 非阻塞 3s 初始化
  Inverter_Control— 软启动 SS_IDLE→SWEEP→DONE→FAULT + 频率斜坡(容差±1kHz)
  Ui_Controller   — 6 态 TFT 中文界面, EMA 显示平滑, 5A 过流保护触发 Fault+蜂鸣器
System/
  Sys_Timer       — SysTick 1ms + DWT 周期计数器
User/
  Main.c          — 4 阶段启动 → while(1) 9 任务 + IWDG + __WFI
  App_Network     — 15s×3 重试, 500ms JSON 遥测, CMD:ON/OFF/SETFREQ 协议
  stm32f10x_it.c  — ISR: SysTick→Inc_Tick, USART2→Rx_Char, Fault×4→MOE关断
```

## Naming Convention

`Module_Name_Verb_Noun()` 帕斯卡+下划线。公开函数以模块名为前缀，静态变量 `s_xxx`，枚举类型 `Module_Name_Type`，枚举值 `MODULE_NAME_ENUM_VALUE`，宏常量 `MODULE_NAME_VALUE`。头文件保护 `MODULE_NAME_H`。

## Key Design Rules

### Scheduling
所有周期任务使用 `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值。`Sys_Timer_Delay_Ms()` 仅限初始化，**运行时绝对禁止阻塞延时**。

### PWM Baseline (不可改动)
- **计数模式**: `TIM_CounterMode_Up` (不可 CenterAligned，频率公式不同)
- **通道模式**: CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` (对角线交替导通)
- **输出极性**: `TIM_OCNPolarity_Low` (IR2103S LIN 低有效)
- **空闲电平**: `TIM_OCNIdleState_Set` (MOE 关断时下管关断)
- **死区**: 1000ns (`PWM_DRIVER_DEADTIME_NS`)
- **频率**: 95kHz~150kHz，原子更新 (UDIS→ARR+CCR→UG)
- **软启动**: 150k→100kHz, 200Hz/10ms，频率斜坡容差 ±1kHz

### Critical Sections
统一 PRIMASK 保存/恢复，绝不裸 `__disable_irq()` 或裸 `__enable_irq()`:
```c
uint32_t primask = __get_PRIMASK();
__disable_irq();
/* ... */
__set_PRIMASK(primask);
```

### Safety
- 4 个 Fault Handler 进入死循环前必须 `TIM_CtrlPWMOutputs(TIM1, DISABLE)`
- IWDG: LSI 40kHz/64, reload=1000 → 1.6s，主循环喂狗
- PB10 PowerContrl: 初始化立即拉低（关断 12V 功率级），仅在 `Inverter_Control_Soft_Start_Trigger()` 时拉高
- 过流 5A: `Ui_Controller_Task` 每 200ms 检查 → `Soft_Start_Fault()` + 蜂鸣器 BEEP

### TFT Display
横屏 160×128，MADCTL=0xC8，SPI1 Mode 0 18MHz 只写。8×16 ASCII 字体 + 56 字符 16×16 中文字库 (`TFT_CN_Font.h`)。`Tft_Driver_Show_CN_String()` 自动识别 UTF-8 三字节中文和单字节 ASCII 混合显示。

### TIM4 Backlight
TIM4 在 **APB1** 总线上（不是 APB2！），必须 `RCC_APB1Periph_CLOCKCmd(RCC_APB1Periph_TIM4, ENABLE)`。
