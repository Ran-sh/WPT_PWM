# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `2.0WAN` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_Bemfa_WAN_V2.0` |
| **协议** | 巴法云 TCP 创客云 (Bemfa Cloud) |
| **版本** | V3.5 |

其他分支: `master` (V0.0 基版) → `WPT_PWM_V0.0`, `1.0LAN` (NetAssist 局域网 V3.4) → `WPT_PWM_NetAssistant_LAN_V1.0`, `3.0ONENET` (OneNET MQTT V6.1 OLED) → `WPT_PWM_ONENET_V3.0`, `4.0TFT` (OneNET MQTT V6.2 TFT彩屏) → `WPT_PWM_V4.0_ONENET_TFT`

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支 (2.0WAN)

**执行期间**: 全部权限自动通过，不中断等待用户确认。

## 目录架构铁律 (Directory Architecture)

**绝对不允许在根目录下新建任何文件夹。** 三个主文件夹的职责和规则如下：

| 文件夹 | 用途 | 规则 |
|:---|:---|:---|
| `Keil_Project/` | Keil MDK 固件工程 | **绝对不允许更改内部架构**: 文件夹名字和数量必须与参考一致 (Hardware/ System/ User/ Library/ Start/ DebugConfig/)，只允许修改或创建 `.c` `.h` 文件 |
| `Claude_Files/` | Claude Code 生成文件 | 可自由新建文件和文件夹 (docs/ tools/ superpowers/ 等) |
| `Arduino_Project/` | Arduino 固件工程 | 可自由新建文件和文件夹 |

**需要新建文件夹时**：必须获得用户同意，由用户手动创建后，读取新架构并更新到 CLAUDE.md。

**使用新软件时**：提醒用户在根目录下新建相应文件夹，用户创建后再写入规则。


## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Device Pack**: Keil.STM32F1xx_DFP.2.2.0
- **Project File**: `Keil_Project/Project.uvprojx` — open in uVision to compile
- **Output**: `Keil_Project/Objects/Project.hex` (HEX-80 format), `Keil_Project/Objects/Project.axf` (debug)
- **Library**: STM32 Standard Peripheral Library (SPL) V3.5.0 in `Keil_Project/Library/`
- **Startup**: `Keil_Project/Start/startup_stm32f10x_md.s` (Cortex-M3 medium-density)

Compilation is done through the Keil IDE GUI. No CLI build script exists—the `Keil_Project/Target 1.BAT` file is a batch output helper, not a build script.

## File Organization

- **Keil 编译源文件**: `Hardware/`, `System/`, `User/`, `Keil_Project/Library/`, `Keil_Project/Start/` — 路径不可移动
- **Claude 生成文件**: 全部放在 `Claude_Files/` 下 (`docs/`, `tools/`, `superpowers/`, `package.json`, `node_modules/`)
- **项目配置**: `CLAUDE.md` + `.claude/` 保留根目录

## Architecture: Three-Layer Separation

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

`SysTimer_DelayMs()` is a busy-wait delay that blocks the CPU — use only during initialization (e.g., waiting for ESP8266 AT responses in `App_Net_Init`). Runtime periodic tasks and timeouts must use the timestamp-diff pattern above.

**⚠️ `System/Delay.c` is deprecated** — it directly reprograms SysTick registers and conflicts with `SysTimer`. No module includes it. Do not revive it.

## Key Modules

| Module | File | Role |
|:---|:---|:---|
| SysTimer | `System/SysTimer.c` | Global ms counter, `Init/IncTick/GetTick/DelayMs` |
| ESP8266 | `Hardware/ESP8266.c` | Async USART2 receiver, AT command state machine, TCP transparent mode; PB1 CH_PD/EN pin with 1000ms deep reset + AT+RST software reset; `ESP8266_SetWaitCallback` hook for AT-progress dot animation |
| PWM | `Hardware/PWM.c` | TIM1 full-bridge, CH1+CH1N/CH2+CH2N, 1000ns dead-time (DEADTIME_NS macro), 50% locked duty, 95-150kHz PFM; non-blocking soft-start state machine (150k→100kHz, 200Hz/10ms step, ~2.5s); atomic `Inverter_SetState()` with irq guards; `Inverter_SoftStart_Trigger/Task/Stop/GetState/GetCurrentFreq` |
| ADC | `Hardware/ADC.c` | ADC1+DMA1 dual-channel scan (current PA0, voltage PA1); `ADC_Filter_Task` 2ms independent filter task (32ms response vs old 3.2s); `Get_Real_Voltage/Current` are O(1) returns of pre-computed float values; floating-point division preserves fractional ADC precision |
| KEY | `Hardware/KEY.c` | 7-state FSM, single-click/double-click detection, 10ms debounce |
| OLED | `Hardware/OLED.c` | SSD1315 128x64 0.96" 4-pin over bit-banged I2C (PA11-SCL, PA12-SDA), 8x16 font; `OLED_Clear()` only on state transitions (rare); daily refresh uses 16-char full-line overwrite to avoid ~100ms I2C blocking; `pow10_lut[10]` lookup table for fast number display |
| UI | `Hardware/UI.c` | Dual-page UI (control panel + monitor mode); KEY0 triggers WiFi connect then soft-start; KEY1 stops; soft-start real-time frequency + progress bar display; state-change auto-clear |
| LED | `Hardware/LED.c` | PC13 heartbeat (500ms toggle) + PB3 WiFi (slow→connecting, fast→connecting, **solid→connected**) + PB4 PWM (blink) + PB5 Ready (on/off); `LED_Init`/`LED_Task` |
| App_Net | `User/App_Net.c` | **V3.5 Bemfa Cloud**: config macros in `App_Net.h` for branch diff; `Bemfa_Subscribe()` injected in both blocking + non-blocking connect paths; **cmd=2 telemetry** envelope at 2000ms (1Hz rate limit); CMD:ON/CMD:OFF protocol; CLOSED→immediate shutdown + wifi reset; **silent watchdog removed** (Bemfa is silent by default, CLOSED frame suffices); `snprintf` for buffer safety |

## Startup Flow (V3.5)

```
上电 → PWM_Init(MOE=OFF) → OLED_Init → LED_Init → ADC_DMA → KEY
     → SysTimer_Init
     → OLED "Wireless Charge"
     → 主循环 while(1):
         KEY_Task  |  UI_Task  |  App_Net_Task  |  Inverter_SoftStart_Task  |  LED_Task

联网: KEY0 单击 → App_Net_Init(阻塞20~30s, 返回0=成功/1~6=失败) → IDLE 待机
      失败 → OLED 错误码 3 秒 → 自动回 "Press KEY0 WiFi" → 可按 KEY0 重试
扫频: KEY0 单击 → Trigger(150kHz) → Task 10ms/步 → 100kHz DONE (~2.5s)
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
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN (100ms low reset → high enable) |
| PB5 | GPIO (PP) | Ready LED (active-high) |
| PB12 | GPIO (IPU) | KEY0 (单击: 联网/Trigger, 双击: 切页) |
| PB13 | GPIO (IPU) | KEY1 (单击: Stop SS_SWEEP时 / +1kHz调频 SS_DONE时) |
| PC13 | GPIO (PP) | Heartbeat LED (active-low) |
**Critical hardware note**: ESP8266 requires independent 3.3V LDO (≥500mA, e.g., AMS1117-3.3) with 100μF+0.1μF decoupling. STM32 dev board's onboard 3.3V regulator cannot supply ESP8266 WiFi bursts (~300mA). ESP8266 RST pin: connect to 3.3V via 10kΩ pull-up.

### ⚠️ ESP8266 双重复位机制 (禁止简化)

**每次调用 `ESP8266_Init()` 必须执行以下双重复位, 否则多次联网后 ESP8266 卡死, 必须重上电 VCC 才能恢复:**

```
1. 硬件复位: CH_PD(PB1) 拉低 1000ms → 拉高, 等待 2000ms 冷启动
2. 软件复位: 发送 AT+RST, 等待 "ready" 响应 (最多 5s)
3. 清除缓冲区垃圾数据
```

**根因**: 透传模式下 ESP8266 固件状态机卡死后, 仅靠 CH_PD 硬件复位无法彻底清除内部状态。必须硬件+软件双重复位才能保证每次联网都是干净起点。**绝不允许为了"加快启动"而缩短 CH_PD 延时或跳过 AT+RST。**

## USART2 / ESP8266 ISR Rules

`USART2_IRQHandler` **must** check `USART_FLAG_ORE` before `USART_IT_RXNE`. On STM32F103, an overrun with RXNEIE enabled also triggers the ISR, and if not cleared, the ISR locks up. Clear ORE by reading SR then DR.

All ISR-shared variables (`s_RxIndex`, `s_FrameReady`, `g_ESP8266_RxFrameFlag`) must be `volatile`.

**Critical section pattern** — any function that reads `s_RxBuf` via `strstr` must wrap the access. **V3.2**: Use `ESP8266_CopyRxFrame()` (atomic copy + clear in single critical section, preserves tail bytes for TCP粘包) or `ESP8266_BufferContains(needle)` (critical section strstr). Never call `USART_ITConfig(USART2, ...)` from outside ESP8266.c — all USART register access is encapsulated.

`ESP8266_SendString` waits for TXE only — the final TC check was removed because USART2 is full-duplex and TXE guarantees the byte is in the shift register.

`ESP8266` no longer has local delay functions. It includes `SysTimer.h` and uses `SysTimer_DelayMs()` exclusively.

The frame delimiter in `ESP8266_RxChar` matches **both** `\r` (0x0D) and `\n` (0x0A) as a defensive dual-delimiter design — compatible with `\r`/`\n`/`\r\n` from any TCP endpoint.

`ESP8266_GetRxBuffer` returns `const char*` — callers must not write to the buffer.

`ESP8266_ClearRxBuffer` and `ESP8266_ClearRxFlag` both fully clear buffer (`s_RxBuf[0]='\0'`), reset index (`s_RxIndex=0`), and clear both flags (`s_FrameReady=0`, `g_ESP8266_RxFrameFlag=0`) inside a critical section. No stale "ON"/"OFF" commands persist.

`g_ESP8266_RxFrameFlag` is no longer `extern` in the header — all external access goes through `ESP8266_GetRxFlag()`.

**`App_Net_Task` TXE hang prevention**: `s_WiFiConnected` flag guards all USART2 access in `App_Net_Task`. The flag is set only after async networking succeeds (`NET_SUCCESS`). Before networking, `App_Net_Task` returns immediately — no `ESP8266_SendString` calls on uninitialized USART2.

**Remote command protocol**: Bemfa Cloud delivers `CMD:ON` / `CMD:OFF` wrapped in `cmd=2&...&msg=` envelope. The `strstr` parser catches the command substring regardless of envelope — no envelope parsing needed. `CLOSED` frame triggers immediate `Inverter_SoftStart_Stop()` + wifi reset, entirely non-blocking.

**AT progress dot animation**: `ESP8266_SetWaitCallback(AT_DotAnim)` registers a callback called every ~10ms from `ESP8266_WaitResponse`'s polling loop. The callback updates OLED line 3 with cycling dots (200ms throttle). Cleared after the AT sequence completes.

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
All runtime `s_ss_state` writes MUST go through `Inverter_SetState()`. SS_FAULT state added: entered via `Inverter_SoftStart_Fault()`, exited only by KEY0/KEY1.

### Preload + UDIS Atomic ARR/CCR Update

`PWM_Init` MUST call before `TIM_Cmd`:
```c
TIM_ARRPreloadConfig(TIM1, ENABLE);
TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
```
Without preload, runtime ARR/CCR changes can cause cycle distortion and shoot-through.

`PWM_SetFrequency` uses `TIM_CR1_UDIS`→write ARR+CCR1+CCR2→`TIM_EGR_UG`→clear UDIS to atomically load all shadow registers. This prevents Update Event between writes causing new-period-with-old-duty-cycle magnetic saturation.

### ESP8266 Connection Loss Detection (V3.4)

**WAN branch**: Silent watchdog removed. Bemfa Cloud is silent by default (no keepalive, no periodic data). The `CLOSED` frame from ESP8266 on TCP disconnect is the sole offline detection mechanism.

**LAN branch** (NetAssist 局域网) still uses a 15s silent watchdog. **Master branch** (V0.0) has no networking.

The `s_LastRxTick` variable, `ESP8266_GetLastRxTime()` function, and `ESP8266_SILENT_TIMEOUT` macro were removed from the WAN branch codebase.

**Coverage matrix (WAN)**:

| Scenario | Trigger | Action |
|:---|:---|:---|
| TCP正常断开 | `CLOSED` frame | Immediate PWM off |
| ESP8266掉电 | CLOSED on TCP RST | PWM off + wifi reset |
| STM32掉电后上电 | `PWM_Init(MOE=OFF)` | Safe at boot |

### ADC Filtering (Bug #5 Fix)

`Get_Real_Voltage()` and `Get_Real_Current()` use 16-sample moving average to suppress 100kHz EMI noise. DMA provides instantaneous raw values; filter outputs clean DC readings.

## Active Configuration

WiFi credentials and server IP are defined as macros in `User/App_Net.h` (moved from `.c` for branch differentiation). The current WAN branch working config (as of 2026-05-21):

```c
#define WIFI_SSID       "Xsyy"
#define WIFI_PASSWORD   "**********"
#define SERVER_IP       "tcp.bemfa.com"          // 巴法云 TCP 域名
#define SERVER_PORT     8344                    // 巴法云 TCP 端口
#define BEMFA_UID       "382d6976a7f647bb856143e0b32eb9d3"
#define BEMFA_TOPIC     "CG42x7TF6006"
```

**LAN branch** uses NetAssist TCP with PC IP:8080. **Master branch** (V0.0) has no networking. Only `App_Net.h` differs between WAN and LAN branches.

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
| `Claude_Files/docs/巴法云WAN远程联调操作指南.md` | Bemfa Cloud WAN remote debugging guide |
| `Claude_Files/docs/embedded-architect-system-prompt.md` | Skill definition (also at `~/.claude/skills/embedded-architect/SKILL.md`); coding standards reference |

## Key Build Targets / Variants

- **Target 1**: Main application (flash to STM32 via ST-Link or serial bootloader)
- `Claude_Files/tools/generate_docx.js`: Node.js script to batch-convert `Claude_Files/docs/*.md` → `Claude_Files/docs/*.docx` with branded formatting
