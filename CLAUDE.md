# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `ONENET` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_ONENET_V3.0` |
| **协议** | OneNET MQTT 物模型 (Dual-MCU 架构) |
| **版本** | V4.0 |

其他分支: `master` (V0.0 基版) → `WPT_PWM_V0.0`, `WAN` (巴法云 TCP) → `WPT_PWM_Bemfa_WAN_V2.0`, `LAN` (NetAssist 局域网) → `WPT_PWM_NetAssistant_LAN_V1.0`

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支 (ONENET)

**执行期间**: 全部权限自动通过，不中断等待用户确认。

## Architecture: Dual-MCU (V4.0)

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • PWM 发波 + PFM 调功        │    │  • WiFiManager 网页配网       │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET MQTT 物模型连云     │
│  • KEY/OLED/LED 人机交互      │    │  • 串口 JSON ↔ STM32 透传    │
│  • 纯 JSON 串口透传           │    │  • WiFi/MQTT 自动重连         │
│  • 软启动扫频 + 过流保护       │    │                              │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)          │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000}  │
           │◄──────────────────────────────────┤
           │  CMD:ON\n  或  CMD:OFF\n           │
```

**Iron rule**: STM32 never sends AT commands. ESP8266 never touches PWM/ADC. Communication is pure text JSON over USART2 at 115200bps.

## Build System

### STM32 (Keil MDK)
- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Device Pack**: Keil.STM32F1xx_DFP.2.2.0
- **Project File**: `Keil_Project/Project.uvprojx` — open in uVision to compile
- **Output**: `Keil_Project/Objects/Project.hex` (HEX-80), `Keil_Project/Objects/Project.axf` (debug)
- **Library**: STM32 Standard Peripheral Library (SPL) V3.5.0 in `Keil_Project/Library/`
- **Startup**: `Keil_Project/Start/startup_stm32f10x_md.s`

No CLI build — compilation through Keil IDE GUI. `Keil_Project/Target 1.BAT` is a post-build helper only.

### ESP8266 (Arduino IDE)
- **IDE**: Arduino IDE 1.8.x or 2.x
- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU
- **File**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- **Libraries**: ESP8266WiFi, PubSubClient (Nick O'Leary), ArduinoJson v7 (Benoit Blanchon), WiFiManager (tzapu)

## File Organization

```
WPT_PWM_V3.0/
├── Arduino_Project/          ← Arduino 固件工程 (平台无关)
├── Keil_Project/            ← Keil MDK STM32 固件工程 (所有 C 源码)
│   ├── Hardware/            ← 硬件驱动层 (ESP8266, PWM, ADC, KEY, OLED, LED, UI)
│   ├── System/              ← 系统服务层 (SysTimer)
│   ├── User/                ← 应用层 (main.c, App_Net.c, stm32f10x_conf.h, stm32f10x_it.c)
│   ├── Library/             ← SPL V3.5.0 外设库 (只读，绝不修改)
│   ├── Start/               ← 启动文件 (startup_stm32f10x_md.s, system_stm32f10x.c)
│   ├── Project.uvprojx      ← Keil uVision 工程文件
│   ├── Target 1.BAT         ← 编译后处理脚本
│   └── keilkill.bat         ← 清理脚本
├── Claude_Files/             ← Claude Code 生成文件 (docs/, tools/, node_modules/)
├── CLAUDE.md                ← 项目指令 (本文件)
├── README.md                ← GitHub 首页 README
└── .claude/                 ← Claude Code 配置
```

- **Arduino_Project/**: Arduino 版本固件，平台无关的独立工程
- **Keil_Project/**: Keil MDK-ARM STM32 固件工程，所有 C 源文件、SPL 库、启动文件均在此目录下。后文所有 `Hardware/`、`System/`、`User/`、`Library/`、`Start/` 路径均相对于 `Keil_Project/`
- **Claude_Files/**: Claude Code 自动生成的文档、工具脚本、配置文件
- **项目配置**: `CLAUDE.md` + `.claude/` 保留根目录

## 目录架构铁律 (Directory Architecture)

**绝对不允许在根目录下新建任何文件夹。** 三个主文件夹的职责和规则如下：

| 文件夹 | 用途 | 规则 |
|:---|:---|:---|
| `Keil_Project/` | Keil MDK 固件工程 | **绝对不允许更改内部架构**: 文件夹名字和数量必须与参考一致 (Hardware/ System/ User/ Library/ Start/ DebugConfig/)，只允许修改或创建 `.c` `.h` 文件 |
| `Claude_Files/` | Claude Code 生成文件 | 可自由新建文件和文件夹 (docs/ tools/ superpowers/ 等) |
| `Arduino_Project/` | Arduino 固件工程 | 可自由新建文件和文件夹 |

**需要新建文件夹时**：必须获得用户同意，由用户手动创建后，读取新架构并更新到 CLAUDE.md。

**使用新软件时**：提醒用户在根目录下新建相应文件夹 (如 `PlatformIO_Project/`)，用户创建后再写入规则。

## Architecture: STM32 Three-Layer Separation

```
         ┌──────────────────────────┐
         │   应用层 (Application)    │  User/main.c, User/App_Net.c
         ├──────────────────────────┤
         │   系统服务层 (System)      │  System/SysTimer.c
         ├──────────────────────────┤
         │   硬件驱动层 (Hardware)    │  Hardware/ESP8266, PWM, ADC, KEY, OLED, etc.
         ├──────────────────────────┤
         │   SPL 外设库 (Library)     │  Read-only, never modified
         └──────────────────────────┘
```

**Iron dependency rule**: Hardware layer only includes SPL headers and other Hardware headers. System layer can depend on Hardware. Application layer depends on both. Never the reverse.

## Library Doctrine (Critical)

- **SPL (V3.5.0) ONLY**. Any HAL/LL function (`HAL_UART_Transmit`, `HAL_GPIO_WritePin`, etc.) is forbidden.
- All SPL peripheral headers already enabled in `User/stm32f10x_conf.h`. New .c files just include the needed `stm32f10x_<periph>.h`.
- Pin configuration uses SPL structs: `GPIO_InitTypeDef`, `USART_InitTypeDef`, `TIM_TimeBaseInitTypeDef`, etc.

## Scheduling: SysTimer Time-Stamp Diff

The entire system uses `System/SysTimer` as the single time base (SysTick at 1ms). The `stm32f10x_it.c` file is **purified**—`SysTick_Handler` contains only `SysTimer_IncTick()`. No `Flag_Task_xxx` variables exist anywhere.

**Every periodic task uses this exact pattern:**

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (SysTimer_GetTick() - last >= PERIOD_MS) {
        last = SysTimer_GetTick();
        // business logic
    }
}
```

All `static uint32_t last` variables in task functions are per-function private and rely on unsigned integer underflow for overflow safety (works for ~49.7 days of uptime).

**Forbidden patterns**: `Delay_ms()` in runtime code, `Flag_Task_xxx` in ISRs, `static` counters inside `SysTick_Handler`.

`SysTimer_DelayMs()` is a busy-wait delay that blocks the CPU — use only during initialization (e.g., waiting for ESP8266 CH_PD reset in `ESP8266_Init`). Runtime periodic tasks and timeouts must use the timestamp-diff pattern above.

**⚠️ `System/Delay.c` is deprecated** — it directly reprograms SysTick registers and conflicts with `SysTimer`. No module includes it. Do not revive it.

## Key Modules

| Module | File | Role |
|:---|:---|:---|
| SysTimer | `System/SysTimer.c` | Global ms counter, `Init/IncTick/GetTick/DelayMs` |
| ESP8266 | `Hardware/ESP8266.c` | V4.0 Dual-MCU: USART2 async receiver (115200), PB1 CH_PD/EN 1000ms hardware reset only (no AT commands); `ESP8266_SendString/CopyRxFrame/GetRxFlag/IsReady` for pure JSON serial passthrough |
| PWM | `Hardware/PWM.c` | TIM1 full-bridge, CH1+CH1N/CH2+CH2N, 1000ns dead-time (DEADTIME_NS macro), 50% locked duty, 95-150kHz PFM; non-blocking soft-start state machine (150k→100kHz, 200Hz/10ms step, ~2.5s); atomic `Inverter_SetState()` with irq guards; `Inverter_SoftStart_Trigger/Task/Stop/GetState/GetCurrentFreq` |
| ADC | `Hardware/ADC.c` | ADC1+DMA1 dual-channel scan (current PA0, voltage PA1); `ADC_Filter_Task` 2ms independent filter task (32ms response); `Get_Real_Voltage/Current` are O(1) returns of pre-computed values |
| KEY | `Hardware/KEY.c` | 7-state FSM, single-click/double-click detection, 10ms debounce |
| OLED | `Hardware/OLED.c` | SSD1315 128x64 0.96" 4-pin over bit-banged I2C (PA11-SCL, PA12-SDA), 8x16 font; `OLED_Clear()` only on state transitions (rare); daily refresh uses 16-char full-line overwrite |
| UI | `Hardware/UI.c` | Dual-page UI (control panel + monitor mode); KEY0 triggers HW init then soft-start; KEY1 stops; soft-start real-time frequency + progress bar display; state-change auto-clear |
| LED | `Hardware/LED.c` | PC13 heartbeat (500ms toggle) + PB3 WiFi (LED_SOLID/slow blink) + PB4 PWM (blink) + PB5 Ready (on/off); `LED_Init`/`LED_Task` |
| App_Net | `User/App_Net.c` | **V4.0 Dual-MCU**: only 3 public APIs; `App_Net_Init()` → ESP8266_Init (~3s); `App_Net_Task()` → JSON telemetry `{"V":xx,"I":xx,"F":xx}\n` every 2000ms + `strstr` parse CMD:ON/OFF/F_UP/F_DOWN; `App_Net_IsConnected()` delegates to `ESP8266_IsReady()` |

## Startup Flow (V4.0)

```
上电 → PWM_Init(MOE=OFF) → OLED_Init → LED_Init → ADC_DMA → KEY
     → SysTimer_Init
     → OLED "Wireless Charge"
     → 主循环 while(1):
         KEY_Task  |  ADC_Filter_Task  |  UI_Task  |  App_Net_Task
         |  Inverter_SoftStart_Task  |  LED_Task

硬件初始化: KEY0 单击 → App_Net_Init(阻塞~3s, CH_PD复位) → s_WiFiConnected=1
            OLED 显示 "HW Init..." → "WiFi: READY"
扫频: KEY0 单击(SS_IDLE) → Trigger(150kHz) → Task 10ms/步 → 100kHz DONE (~2.5s)
关断: KEY1(SS_SWEEP) / KEY0(SS_DONE) / 云端 "CMD:OFF" → Stop → SS_IDLE
调频: KEY1(SS_DONE) → freq +1kHz (循环 100k~150k)
每次 ON 都是全新扫频 150k→100k
```

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | Function | Connected To |
|:---|:---|:---|
| PA0 | ADC_CH0 | Current sensor (CC6920-10A) |
| PA1 | ADC_CH1 | Voltage divider (20:1) |
| PA2 | USART2_TX | ESP8266 RXD |
| PA3 | USART2_RX | ESP8266 TXD |
| PA7 | TIM1_CH1N | Half-bridge left low-side |
| PA8 | TIM1_CH1 | Half-bridge left high-side |
| PA9 | TIM1_CH2 | Half-bridge right high-side |
| PA11 | GPIO (OD) | OLED SCL |
| PA12 | GPIO (OD) | OLED SDA |
| PB0 | TIM1_CH2N | Half-bridge right low-side |
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN (1000ms low reset → high enable) |
| PB3 | GPIO (PP) | WiFi LED (active-high, JTAG disabled) |
| PB4 | GPIO (PP) | PWM LED (active-high, JTAG disabled) |
| PB5 | GPIO (PP) | Ready LED (active-high) |
| PB12 | GPIO (IPU) | KEY0 (单击: HW初始化/Trigger, 双击: 切页) |
| PB13 | GPIO (IPU) | KEY1 (单击: Stop SS_SWEEP时 / +1kHz调频 SS_DONE时) |
| PC13 | GPIO (PP) | Heartbeat LED (active-low) |

**Critical hardware note**: ESP8266 requires independent 3.3V LDO (≥500mA, e.g., AMS1117-3.3) with 100μF+0.1μF decoupling. STM32 dev board's onboard 3.3V regulator cannot supply ESP8266 WiFi bursts (~300mA). ESP8266 RST pin: connect to 3.3V via 10kΩ pull-up. ESP8266 GPIO0: pull low during firmware upload.

### ESP8266 CH_PD Hardware Reset (V4.0)

**每次 `ESP8266_Init()` 执行 CH_PD 硬件复位:**

```
1. CH_PD(PB1) 拉低 1000ms — 模块完全放电
2. CH_PD(PB1) 拉高, 等待 2000ms — 冷启动 + RF 校准
3. USART2 初始化 + 清缓冲区
```

V4.0 下 ESP8266 运行 Arduino 固件，不再有 AT 指令。已删除 AT+RST 软件复位和 "+++" 透传退出逻辑。

## USART2 / ESP8266 ISR Rules

`USART2_IRQHandler` **must** check `USART_FLAG_ORE` before `USART_IT_RXNE`. On STM32F103, an overrun with RXNEIE enabled also triggers the ISR, and if not cleared, the ISR locks up. Clear ORE by reading SR then DR.

All ISR-shared variables (`s_RxIndex`, `g_ESP8266_RxFrameFlag`) must be `static volatile`.

**Critical section pattern** — use `ESP8266_CopyRxFrame()` (atomic copy + clear in single critical section, preserves tail bytes). Never call `USART_ITConfig(USART2, ...)` from outside ESP8266.c — all USART register access is encapsulated.

`ESP8266_SendString` waits for TXE only — the final TC check was removed because USART2 is full-duplex and TXE guarantees the byte is in the shift register.

`ESP8266` no longer has local delay functions. It includes `SysTimer.h` and uses `SysTimer_DelayMs()` exclusively.

The frame delimiter in `ESP8266_RxChar` matches **both** `\r` (0x0D) and `\n` (0x0A) as a defensive dual-delimiter design — compatible with `\r`/`\n`/`\r\n` from any source.

`ESP8266_GetRxBuffer` returns `const char*` — callers must not write to the buffer.

`ESP8266_ClearRxBuffer` fully clears buffer (`s_RxBuf[0]='\0'`), resets index (`s_RxIndex=0`), and clears the frame flag (`g_ESP8266_RxFrameFlag=0`) inside a critical section.

`g_ESP8266_RxFrameFlag` is `static` within ESP8266.c — all external access goes through `ESP8266_GetRxFlag()`.

**`App_Net_Task` TXE hang prevention**: `ESP8266_IsReady()` flag guards all USART2 access in `App_Net_Task`. Before hardware init (KEY0 trigger), `App_Net_Task` returns immediately — no `ESP8266_SendString` calls on uninitialized USART2.

**ADC calibration timing**: After `ADC_Cmd(ENABLE)`, a short stabilization delay (~2μs) is required before `ADC_ResetCalibration` per STM32 reference manual (t_STAB ≥ 2 ADC cycles). Without it, calibration includes power-up noise causing reference drift.

## PWM Safety Rules

### Dead-Time Macro

```c
#define DEADTIME_NS  1000   // PWM.h — 修改此值调整死区
// DeadTime 寄存器值由编译期宏自动换算:
//   DEADTIME_REG_VAL = ((DEADTIME_NS) * 72 + 500) / 1000
//   断言 <= 127 (BDTR 线性段 DTG[7:5]=0xx)
```

### Soft-Start State Machine (Non-Blocking)

```c
typedef enum { SS_IDLE=0, SS_SWEEP=1, SS_DONE=2, SS_FAULT=3 } SoftStart_State_t;

void Inverter_SoftStart_Trigger(void);   // SS_IDLE → SS_SWEEP, 开 MOE
void Inverter_SoftStart_Task(void);      // 主循环调用, 10ms 时间戳步进
void Inverter_SoftStart_Stop(void);      // 关 MOE → SS_IDLE
void Inverter_SoftStart_Fault(void);     // 紧急关断 → SS_FAULT (仅 KEY0/KEY1 可复位)
```

### Atomic State Transition

```c
static void Inverter_SetState(SoftStart_State_t new_state) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_ss_state = new_state;
    __set_PRIMASK(primask);  // restore previous IRQ state, never unconditionally enable
}
```
All runtime `s_ss_state` writes MUST go through `Inverter_SetState()`.

### Preload + UDIS Atomic ARR/CCR Update

`PWM_Init` MUST call before `TIM_Cmd`:
```c
TIM_ARRPreloadConfig(TIM1, ENABLE);
TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
```
Without preload, runtime ARR/CCR changes can cause cycle distortion and shoot-through.

`PWM_SetFrequency` uses `TIM_CR1_UDIS`→write ARR+CCR1+CCR2→`TIM_EGR_UG`→clear UDIS to atomically load all shadow registers. This prevents Update Event between writes causing new-period-with-old-duty-cycle magnetic saturation.

### Connection Loss Detection (V4.0)

ESP8266 runs independent Arduino firmware — WiFi and MQTT reconnection are handled entirely by the ESP8266 side (non-blocking, every 5s). STM32 has no connection monitoring responsibility.

**Coverage matrix:**

| Scenario | Trigger | Action |
|:---|:---|:---|
| ESP8266 断线 | ESP8266 自管理重连 | STM32 无感, 继续发送 JSON |
| ESP8266 掉电 | USART2 无数据 | STM32 继续正常工作 |
| STM32 掉电后上电 | `PWM_Init(MOE=OFF)` | Safe at boot |

### ADC Filtering

`Get_Real_Voltage()` and `Get_Real_Current()` use 16-sample moving average to suppress 100kHz EMI noise. DMA provides instantaneous raw values; filter outputs clean DC readings.

## ESP8266 Arduino Firmware

**File**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`

**Libraries**: ESP8266WiFi + PubSubClient + ArduinoJson v7 + WiFiManager (tzapu)

**Features**:
- **WiFiManager 网页配网**: 首次上电开热点 `STM32_WPT_Config` (无密码), 手机连上配网, 凭据存闪存
- **OneNET MQTT 物模型**: 属性上报 `$sys/.../thing/property/post`, 属性设置 `$sys/.../thing/property/set`
- **非阻塞重连**: 每 5s 检查 WiFi+MQTT, 自动重连
- **JSON 转换**: STM32 `{"V":x,"I":x,"F":x}` → OneNET `{"id":"123","version":"1.0","params":{...}}`
- **指令下行**: 解析 OneNET `Switch`(布尔) → `CMD:ON\n` / `CMD:OFF\n`; `FreqAdd`/`FreqSub`(布尔) → `CMD:F_UP\n` / `CMD:F_DOWN\n`
- **属性回复**: 收到属性设置后发布 `set_reply` 应答 (解决"响应超时")
- **点动复位**: FreqAdd/FreqSub 触发后立刻 POST 写回 false, 网页端开关自动弹回

**Config macros** in `.ino`:
- `MQTT_SERVER` / `MQTT_PORT` — OneNET MQTT 地址 (`mqtts.heclouds.com:1883`)
- `ONENET_PRODUCT_ID` / `ONENET_DEVICE_NAME` / `ONENET_TOKEN` — 设备凭证
- `MQTT_TOPIC_PROPERTY_POST` / `MQTT_TOPIC_PROPERTY_SET` / `MQTT_TOPIC_PROPERTY_SET_REPLY` — 物模型主题

## Documentation Output

- Every `.md` document in `Claude_Files/docs/` must have a paired `.docx` with identical body content
- `.docx` structure: Section 1 (cover, no page#), Section 2 (TOC with Roman numerals), Section 3 (body, Arabic page# starting at 1)
- Regenerate `.docx` files from `Claude_Files/` directory: `npm install && node Claude_Files/tools/generate_docx.js "Claude_Files/docs/<filename>.md"`
- Documents require a version control header with change log. On code changes, auto-increment version (logic change → +0.1, new module → +1.0, formatting only → date refresh).
- When user says "更新文档": scan all .c/.h, diff against documented state, report changes before rewriting. If no changes: output "没有任何文件变化，无需更新" and exit.

### Docs Directory

| Document | Purpose |
|:---|:---|
| `Claude_Files/docs/软件架构与开发者指南.md` | Primary architecture and developer guide |
| `Claude_Files/docs/双脑架构V4.0验证与烧录指南.md` | Dual-MCU V4.0 verification, STM32 serial test, ESP8266 flashing guide |
| `Claude_Files/docs/embedded-architect-system-prompt.md` | Skill definition (also at `~/.claude/skills/embedded-architect/SKILL.md`); coding standards reference |

## Key Build Targets / Variants

- **Target 1**: Main application (flash to STM32 via ST-Link or serial bootloader)
- `Claude_Files/tools/generate_docx.js`: Node.js script to batch-convert `Claude_Files/docs/*.md` → `Claude_Files/docs/*.docx` with branded formatting
