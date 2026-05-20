# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Device Pack**: Keil.STM32F1xx_DFP.2.2.0
- **Project File**: `Project.uvprojx` — open in uVision to compile
- **Output**: `Objects/Project.hex` (HEX-80 format), `Objects/Project.axf` (debug)
- **Library**: STM32 Standard Peripheral Library (SPL) V3.5.0 in `Library/`
- **Startup**: `Start/startup_stm32f10x_md.s` (Cortex-M3 medium-density)

Compilation is done through the Keil IDE GUI. No CLI build script exists—the `Target 1.BAT` file is a batch output helper, not a build script.

## File Organization

- **Keil 编译源文件**: `Hardware/`, `System/`, `User/`, `Library/`, `Start/` — 路径不可移动
- **Claude 生成文件**: 全部放在 `claude_code/` 下 (`docs/`, `docx/`, `tools/`, `superpowers/`, `package.json`, `node_modules/`)
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
| ESP8266 | `Hardware/ESP8266.c` | Async USART2 receiver, AT command state machine, TCP transparent mode; PB1 CH_PD/EN pin with 1000ms deep reset + AT+RST software reset; `ESP8266_SetWaitCallback` hook for AT-progress dot animation; `ESP8266_GetLastRxTime()` for silent watchdog; 15s `ESP8266_SILENT_TIMEOUT` for offline detection |
| PWM | `Hardware/PWM.c` | TIM1 full-bridge, CH1+CH1N/CH2+CH2N, 1000ns dead-time (DEADTIME_NS macro), 50% locked duty, 95-150kHz PFM; non-blocking soft-start state machine (150k→100kHz, 200Hz/10ms step, ~2.5s); atomic `Inverter_SetState()` with irq guards; `Inverter_SoftStart_Trigger/Task/Stop/GetState/GetCurrentFreq` |
| ADC | `Hardware/ADC.c` | ADC1+DMA1 dual-channel scan (current PA0, voltage PA1); `ADC_Filter_Task` 2ms independent filter task (32ms response vs old 3.2s); `Get_Real_Voltage/Current` are O(1) returns of pre-computed values |
| KEY | `Hardware/KEY.c` | 7-state FSM, single-click/double-click detection, 10ms debounce |
| OLED | `Hardware/OLED.c` | OLED1315 128x64 0.96" 4-pin over bit-banged I2C (PA11-SCL, PA12-SDA), 8x16 font; `OLED_Clear()` only on state transitions (rare); daily refresh uses 16-char full-line overwrite to avoid ~100ms I2C blocking |
| UI | `Hardware/UI.c` | Dual-page UI (control panel + monitor mode); KEY0 triggers WiFi connect then soft-start; KEY1 stops; soft-start real-time frequency + progress bar display; state-change auto-clear |
| LED | `Hardware/LED.c` | PC13 heartbeat (500ms toggle task) + PB5/PE5 dual-color external LED (active-low); `LED_Init`/`LED_Task` |
| App_Net | `User/App_Net.c` | V3.2 async 9-state AT FSM (NET_IDLE→NET_STEP_AT→...→NET_SUCCESS/FAIL), KEY1 cancelable, 3-retry auto-fallback; `s_WiFiConnected` single authority source + USART2 ready gate; JSON telemetry (1s, skipped during SS_SWEEP); **CMD:ON/CMD:OFF** protocol (not bare ON/OFF); CLOSED→immediate `Inverter_SoftStart_Stop` + reset wifi state (non-blocking); **V3.3 silent watchdog**: 15s no RX data → `Inverter_SoftStart_Stop` + `s_WiFiConnected=0`; `snprintf` for JSON buffer safety |

## Startup Flow (V3.3)

```
上电 → PWM_Init(MOE=OFF) → OLED_Init → HardLED → ADC_DMA → KEY
     → SysTimer_Init
     → OLED "Wireless Charge"
     → 主循环 while(1):
         KEY_Task  |  UI_Task  |  App_Net_Task  |  Inverter_SoftStart_Task  |  HardLED_Task

联网: KEY0 单击 → App_Net_Init(阻塞20~30s, 返回0=成功/1~6=失败) → IDLE 待机
      失败 → OLED 错误码 3 秒 → 自动回 "Press KEY0 WiFi" → 可按 KEY0 重试
扫频: KEY0 单击 → Trigger(150kHz) → Task 10ms/步 → 100kHz DONE (~2.5s)
关断: KEY1(SS_SWEEP) / KEY0(SS_DONE) / PC "OFF" → Stop → SS_IDLE
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
| PB5 | GPIO (PP) | LED_DS0 (active-low) |
| PB12 | GPIO (IPU) | KEY0 (单击: 联网/Trigger, 双击: 切页) |
| PB13 | GPIO (IPU) | KEY1 (单击: Stop SS_SWEEP时 / +1kHz调频 SS_DONE时) |
| PC13 | GPIO (PP) | HardLED (active-low) |
| PE5 | GPIO (PP) | LED_DS1 (active-low) |

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

The frame delimiter in `ESP8266_RxChar` must match **both** `\r` (0x0D) and `\n` (0x0A). NetAssist's default "send" may only append `\r`.

`ESP8266_GetRxBuffer` returns `const char*` — callers must not write to the buffer.

`ESP8266_ClearRxBuffer` and `ESP8266_ClearRxFlag` both fully clear buffer (`s_RxBuf[0]='\0'`), reset index (`s_RxIndex=0`), and clear both flags (`s_FrameReady=0`, `g_ESP8266_RxFrameFlag=0`) inside a critical section. No stale "ON"/"OFF" commands persist.

`g_ESP8266_RxFrameFlag` is no longer `extern` in the header — all external access goes through `ESP8266_GetRxFlag()`.

**`App_Net_Task` TXE hang prevention**: `s_WiFiConnected` flag guards all USART2 access in `App_Net_Task`. The flag is set only after async networking succeeds (`NET_SUCCESS`). Before networking, `App_Net_Task` returns immediately — no `ESP8266_SendString` calls on uninitialized USART2.

**Remote command protocol**: PC must send `CMD:ON` / `CMD:OFF` (not bare `ON`/`OFF`). This prevents false triggers from "JSON", "CONNECT" etc. When ESP8266 sends "CLOSED" (physical disconnect), `App_Net_Task` immediately calls `Inverter_SoftStart_Stop()` then resets `wifi_connected` — entirely non-blocking, no `SysTimer_DelayMs`.

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

### ESP8266 Silent Watchdog (V3.3)

**Problem**: When ESP8266 module loses power independently (MCU still running), USART2 goes silent — no `CLOSED` frame is sent. PWM continues outputting with no remote shutdown capability.

**Solution**: `ESP8266_RxChar()` records `s_LastRxTick = SysTimer_GetTick()` on every received byte. `App_Net_Task` checks each iteration:

```c
if (SysTimer_GetTick() - ESP8266_GetLastRxTime() > ESP8266_SILENT_TIMEOUT) {
    Inverter_SoftStart_Stop();  // immediate PWM shutdown
    s_WiFiConnected = 0;        // UI returns to "Press KEY0 WiFi"
}
```

`ESP8266_SILENT_TIMEOUT = 15000` (15 seconds). On Cortex-M3, aligned 32-bit `s_LastRxTick` read/write is atomic — no critical section needed. `ESP8266_Init()` seeds the timestamp so the first 15s window starts with a fresh value.

**Coverage matrix**:

| Scenario | Trigger | Action |
|:---|:---|:---|
| TCP正常断开 | `CLOSED` frame | Immediate PWM off (existing) |
| ESP8266掉电 | 15s RX silence | PWM off + wifi reset |
| ESP8266卡死 | 15s RX silence | PWM off + wifi reset |
| STM32掉电后上电 | `PWM_Init(MOE=OFF)` | Safe at boot |
| PC长期不发指令 | TCP ACK刷新时间戳 | No false trigger |

### ADC Filtering (Bug #5 Fix)

`Get_Real_Voltage()` and `Get_Real_Current()` use 16-sample moving average to suppress 100kHz EMI noise. DMA provides instantaneous raw values; filter outputs clean DC readings.

## Active Configuration

WiFi credentials and server IP are defined as macros in `User/App_Net.c`. When the user provides a new `ipconfig` output, update `SERVER_IP` to the active WLAN adapter's IPv4. The current working config (as of 2026-05-16):

```c
#define WIFI_SSID       "Xsyy"
#define WIFI_PASSWORD   "**********"
#define SERVER_IP       "192.168.31.254"
#define SERVER_PORT     8080
```

The NetAssist tool is at `D:\Assistant\netassist5.0.13`. Configure as TCP Server with the PC's IPv4 and port 8080.

## Documentation Output

- Every `.md` document in `claude_code/docs/` must have a paired `.docx` with identical body content
- `.docx` structure: Section 1 (cover, no page#), Section 2 (TOC with Roman numerals), Section 3 (body, Arabic page# starting at 1)
- Regenerate `.docx` files from `claude_code/` directory: `npm install && node claude_code/tools/generate_docx.js "claude_code/docs/<filename>.md"`
- Documents require a version control header with change log. On code changes, auto-increment version (logic change → +0.1, new module → +1.0, formatting only → date refresh).
- When user says "更新文档": scan all .c/.h, diff against documented state, report changes before rewriting. If no changes: output "没有任何文件变化，无需更新" and exit.

### Docs Directory

| Document | Purpose |
|:---|:---|
| `claude_code/docs/软件架构与开发者指南.md` | Primary architecture and developer guide |
| `claude_code/docs/PC端联调操作指南.md` | PC-side joint debugging with NetAssist |
| `claude_code/docs/LabVIEW上位机构建指南.md` | LabVIEW host-side application build guide |
| `claude_code/docs/embedded-architect-system-prompt.md` | Skill definition (also at `~/.claude/skills/embedded-architect/SKILL.md`); coding standards reference |

## Key Build Targets / Variants

- **Target 1**: Main application (flash to STM32 via ST-Link or serial bootloader)
- `claude_code/tools/deploy_netassist.ps1`: PowerShell script that downloads and launches NetAssist TCP debugger to `D:\NetAssist\`
- `claude_code/tools/generate_docx.js`: Node.js script to batch-convert `claude_code/docs/*.md` → `claude_code/docs/*.docx` with branded formatting
- `claude_code/docs/embedded-architect-system-prompt.md`: The project's skill definition (also installed at `~/.claude/skills/embedded-architect/SKILL.md`). Contains coding standards, scheduling doctrine, document version control rules, and the auto-diff document update workflow. When Claude needs a refresher on project conventions, read this file.
