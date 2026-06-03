# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **协议** | OneNET MQTT 物模型 (Dual-MCU 架构) + ST7735S TFT 彩屏 |
| **版本** | V6.2 |

其他分支: `master` (V0.0 基版) → `WPT_PWM_V0.0`, `1.0LAN` (NetAssist 局域网) → `WPT_PWM_NetAssistant_LAN_V1.0`, `2.0WAN` (巴法云 TCP) → `WPT_PWM_Bemfa_WAN_V2.0`, `3.0ONENET` (OneNET MQTT V6.1 OLED) → `WPT_PWM_ONENET_V3.0`

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支 (4.0TFT)

**执行期间**: 全部权限自动通过，不中断等待用户确认。

## Naming Convention (V6.0)

全部模块统一采用 `Module_Name_Action_Object()` 帕斯卡+下划线命名:

- 公开函数: `Module_Name_Verb_Noun()` — 如 `Tft_Driver_Show_String()`, `Adc_Driver_Get_Voltage()`
- 静态变量: `s_module_description` — 如 `s_ui_state`, `s_rx_frame_flag`
- 类型/枚举: `Module_Name_Type` — 如 `Ui_Controller_State`, `Inverter_Control_Soft_Start_State`
- 枚举值: `MODULE_NAME_ENUM_VALUE` — 全大写+下划线+模块前缀, 如 `LED_DRIVER_STATE_ON`, `INVERTER_CONTROL_SS_STATE_DONE`, `UI_CONTROLLER_STATE_READY`
- 宏常量: `MODULE_NAME_VALUE` — 全大写+下划线+模块前缀, 如 `ADC_DRIVER_VREF_MCU`, `APP_NETWORK_MAX_RETRIES`
- 静态函数: 建议加模块前缀, 如 `Ui_Controller_Draw_Running()`
- 头文件保护: `MODULE_NAME_H` (无前导下划线, 避免 C 保留标识符)

### 编码规范 (Code Design Rules)

**状态机**
- 禁止用隐式 bool/int 标志组合表达状态 — 必须用显式 `typedef enum`
- 每个状态机有命名的枚举类型 + 单一状态变量, 不准用 `s_flag1`+`s_flag2` 拼凑
- 状态转换集中在一个 Task 函数内, 用 `switch` 分发

**调度**
- 所有周期任务用 `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值模式
- `Sys_Timer_Delay_Ms()` 仅限初始化阶段, 运行时绝对禁止阻塞延时
- 初始化序列也优先用非阻塞状态机
- 主循环末尾 `__WFI()` 休眠, SysTick 唤醒, 不空转

**模块架构**
- 每个模块 `.h` 只放公开接口, `.c` 放全部实现 + 静态变量
- `.c` 内部函数一律 `static`, 不准跨模块 `extern` 访问私有变量
- 不允许 `#include ".c"` 文件
- 模块内部辅助函数建议加模块前缀避免与 SPL 库函数冲突
- 头文件保护: `MODULE_NAME_H`, 禁止双下划线前缀 (`__NAME_H` 是 C 保留字)

**OOP 在 C 中的实践**
- 相关变量封装到 struct 中, 避免多个分散的 static 变量
- 状态机用 struct 打包 (状态 + 定时器 + 上下文)
- 一个 `.c` 只管理自己定义的结构体, 外部通过函数接口访问

**分层依赖**
- Hardware → System → Application, 严格单向
- 应用层不直接操作寄存器, 不绕过硬件抽象层

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
- `TIM_CounterMode_Up` — 不可改为 CenterAligned (频率公式不同, 两路 CH1=PWM1+CH2=PWM2 配合 Up 计数实现对角线交替导通)
- CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` — 两路不同模式, 桥间产生差分电压; 同模式则桥间电压为零
- `TIM_OCNPolarity_Low` — IR2103S LIN 为低有效, 不可改为 High
- `TIM_OCNIdleState_Set` — MOE 关断时下管必须关断 (LIN=HIGH), 不可改为 Reset
- **不执行任何 TIM1 重映射** (使用默认映射: PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N)
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
│  • TFT/KEY/LED/BUZ 人机交互   │    │  • 串口 JSON ↔ STM32 透传    │
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
cd ONENETapp && git add -A && git commit -m "..." && git push && git push origin gh-pages:master && cd ..

# Railway
cd Railway_Deploy && git add -A && git commit -m "..." && git push && cd ..
```

## Build System

### STM32 (Keil MDK)
- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Project File**: `Keil_Project/Project.uvprojx`
- **Output**: `Keil_Project/Objects/Project.hex` (HEX-80)
- **Library**: SPL V3.5.0 in `Keil_Project/Library/` — read-only, never modified
- No CLI build — compilation through Keil IDE GUI.

### ESP8266 (Arduino IDE)
- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU
- **File**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- **Libraries**: ESP8266WiFi, PubSubClient, ArduinoJson v7, WiFiManager

## STM32 Module Map (V6.2)

```
Keil_Project/
├── Hardware/
│   ├── Tft_Driver        ← ST7735S 128×160 SPI 彩屏 (PA5/PA7/PA4/PA6/PA0, PB6背光)
│   ├── Led_Driver        ← 6 LED 驱动 (PA15心跳, PB3/PB4/PA10/PA11/PA12 状态灯)
│   ├── Buzzer_Driver     ← 有源蜂鸣器驱动 (PB15, NPN S8050)
│   ├── Pwm_Driver        ← TIM1 全桥 PWM 默认映射 (95-150kHz, 1000ns死区)
│   ├── Inverter_Control  ← 软启动状态机 + 频率斜坡 (应用层, 依赖 Pwm_Driver)
│   ├── Adc_Driver        ← ADC1+DMA1 双通道 (PB0=CH8电流, PB1=CH9电压) + 64样本滤波
│   ├── Key_Driver        ← 4 按键 FSM (PB5/PB7/PB8/PB9), 10ms去抖
│   ├── Esp8266_Driver    ← USART2 115200 异步收发, CH_PD(PB11)/RST(PA1) 非阻塞初始化
│   └── Ui_Controller     ← TFT界面状态机 + 按键分发 + LED联动 + 蜂鸣器+ 过流保护
├── System/
│   └── Sys_Timer         ← SysTick 1ms + DWT 周期计数器 (亚毫秒定时)
└── User/
    ├── Main.c            ← main() 入口, 4阶段启动 + 非阻塞主循环
    ├── App_Network       ← 联网管理 + 遥测门控 + 指令接收
    └── stm32f10x_it.c    ← ISR (SysTick→Sys_Timer_Inc_Tick, USART2→Esp8266_Driver_Rx_Char)
```

**Layer dependency**: Hardware → System → Application. Never reverse.

## Key Design Rules

### Scheduling: Timestamp-Diff (Non-Blocking)
```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
        last = Sys_Timer_Get_Tick();
        // business logic
    }
}
```
`Sys_Timer_Delay_Ms()` is deprecated — use non-blocking state machines even for initialization sequences. `System/Delay.c` is deleted, do not revive.

### IWDG Watchdog + Power Saving

IWDG: LSI 40kHz, prescaler 64, reload 1000 → ~1.6s timeout. `IWDG_ReloadCounter()` in main loop. Any task hang triggers auto-reset.
`__WFI()` at loop end: idle current ~30mA → ~5mA. SysTick ISR wakes CPU every 1ms.

### ADC Anti-Aliasing
采样周期 144241 CPU cycles 与 100kHz PWM (720 cycles) 互质 → 720 个不同相位均匀覆盖。64 样本滑动窗口 (128ms) 收敛至 DC 分量。自动零点校准: READY 状态首次采集 50 样本取平均, 后续 EMA 追踪。

### PWM Safety
- MOE off at boot, only enabled by `Inverter_Control_Soft_Start_Trigger()`
- Atomic ARR/CCR update via UDIS→write→UG→clear UDIS
- `Inverter_SetState()` saves/restores PRIMASK (never unconditionally enables IRQ)
- Fault handlers disable TIM1 outputs before infinite loop

### HardFault Protection
All fault handlers (`HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler`) call `TIM_CtrlPWMOutputs(TIM1, DISABLE)` before `while(1)` to prevent bridge shoot-through on CPU crash.

### Overcurrent Protection
`Ui_Controller_Task` 在 SWEEPING/RUNNING 状态每 200ms 检测电流 > 5.0A, 触发 `Inverter_Control_Soft_Start_Fault()` → MOE 关断 + SS_FAULT 锁存。仅 KEY0 可复位。蜂鸣器鸣响告警。

### Library Doctrine
- **SPL V3.5.0 ONLY**. No HAL/LL functions.
- Internal functions prefixed with module name to avoid SPL name clashes (e.g., `Tft_SPI_Init` not `SPI_Init`).

## Pin Mapping (STM32F103C8 LQFP-48) — V6.2 TFT

### 左侧排针

| 排针 | 引脚 | 网络名 | 功能 | 配置 |
|:---|:---|:---|:---|:---|
| 1 | VBAT | VBAT | 备用电池（悬空） | — |
| 2 | PC13 | C13 | 预留悬空 | — |
| 3 | PC14 | C14 | 预留悬空 | — |
| 4 | PC15 | C15 | 预留悬空 | — |
| 5 | PA0 | 1.8TFT_RES | TFT 复位 | GPIO PP |
| 6 | PA1 | ESP8266RST | ESP8266 复位 | GPIO PP |
| 7 | PA2 | ESP8266RX | ESP8266 接收 (USART2_TX) | AF_PP |
| 8 | PA3 | ESP8266TX | ESP8266 发送 (USART2_RX) | IN_FLOATING |
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

### 右侧排针

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

### JTAG/SWD 注意事项

PB3/PB4/PB5/PA15 被 JTAG 默认占用，需要在固件中禁用 JTAG：
`GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`
SWD (PA13/PA14) 保留，ST-Link 可正常调试和烧录。

## V6.2 vs V6.1 关键差异

| 项目 | V6.1 (3.0ONENET) | V6.2 (4.0TFT) |
|:---|:---|:---|
| 显示屏 | SSD1315 OLED I2C (PA11/PA12) | ST7735S TFT SPI (PA5/PA7/PA4/PA6/PA0, PB6背光) |
| TIM1 重映射 | PartialRemap (PA7=CH1N, PB0=CH2N) | **默认映射** (PB13=CH1N, PB14=CH2N) |
| ADC 电流 | PA0 (CH0) | PB0 (CH8) |
| ADC 电压 | PA1 (CH1) | PB1 (CH9) |
| 按键 | 2 (PB12/PB13) | 4 (PB5/PB7/PB8/PB9) |
| LED | 4 (PC13+PB3/PB4/PB5) | 6 (PA15+PB3/PB4/PA10/PA11/PA12) |
| 蜂鸣器 | 无 | 有源 2.7kHz (PB15, S8050) |
| 电源控制 | 无 | PowerContrl (PB10) 12V 闸控制 |
| ESP8266 RST | 无独立控制 | PA1 独立复位引脚 |
| JTAG 禁用 | GPIO_Remap_SWJ_JTAGDisable | GPIO_Remap_SWJ_JTAGDisable (额外释放 PB5/PA15) |

## Startup Flow (V6.2)

```
上电 → Pwm_Driver_Init(MOE=OFF) → Tft_Driver_Init → Led_Driver_Init
     → Buzzer_Driver_Init → Adc_Driver_Init → Key_Driver_Init
     → Sys_Timer_Init (SysTick + DWT) → IWDG_Init (1.6s 超时)
     → DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP (调试安全)
     → App_Network_Start_Connect (ESP 非阻塞初始化, 自动清空RX缓冲)
     → while(1):
         Key_Driver_Task | Adc_Driver_Filter_Task | Ui_Controller_Task
         | App_Network_Task | Inverter_Control_Soft_Start_Task
         | Inverter_Control_Freq_Ramp_Task | Led_Driver_Task
         | Buzzer_Driver_Task | IWDG_ReloadCounter | __WFI
```

## ESP8266 Firmware (V5.1)

单文件 `ESP8266_MQTT_Firmware.ino`, 注释分段架构:

| 段 | 命名空间 | 职责 |
|:---|:---|:---|
| 配置区 | `#define` | 所有可调参数 |
| 连接状态机 | `MQTT_CONN_STATE_*` 枚举 | IDLE→WIFI→MQTT→ONLINE→FAILED 显式状态 |
| MQTT 模块 | `Mqtt_Task_*` | 双 Broker 连接 + OneNET 物模型收发 |
| 串口模块 | `Serial_Parse_*` | 非阻塞行读取 + 前缀匹配防协议误触发 |

关键改进: `Str_Starts_With()` 前缀匹配替代 `strstr()` 子串搜索, 防止 `STATUS:ONLINE` 嵌入 JSON 字符串时误触发。

## Documentation Output

- 每个 `.md` 文档在 `Claude_Files/docs/` 有配对的 `.docx`
- `.docx` 生成: `cd Claude_Files && npm install && node Claude_Files/tools/generate_docx.js "Claude_Files/docs/<name>.md"`
- 代码变更后版本号自增 (逻辑改动 +0.1, 新模块 +1.0, 纯格式日期刷新)
- "更新文档"时先 diff 再决定是否重写; 无变更则输出 "没有任何文件变化，无需更新"

### Docs Directory

| Document | Purpose |
|:---|:---|
| `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` | 完整全指南 |
| `Claude_Files/docs/embedded-architect-system-prompt.md` | Skill 定义 (同步至 `~/.claude/skills/embedded-architect/SKILL.md`) |
