# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `master` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V0.0` |
| **协议** | 纯本地控制 (无 WiFi) |
| **版本** | V1.0 |

其他分支: `LAN` (NetAssist 局域网) → `WPT_PWM_NetAssistant_LAN_V1.0`, `WAN` (巴法云 TCP V3.4) → `WPT_PWM_Bemfa_WAN_V2.0`, `ONENET` (OneNET MQTT V4.0 双脑架构) → `WPT_PWM_V3.0`

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支

**执行期间**: 全部权限自动通过，不中断等待用户确认。

### Git Push 网络配置

本机访问 GitHub 需通过 Windows 系统代理。git push 前自动配置：

```bash
# 读取 Windows 系统代理
PROXY=$(powershell -Command "(Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings').ProxyServer")
# 应用代理到 git
git config --global http.proxy http://$PROXY
git config --global https.proxy http://$PROXY
```

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Device Pack**: Keil.STM32F1xx_DFP.2.2.0
- **Project File**: `Project.uvprojx` — open in uVision to compile
- **Output**: `Objects/Project.hex` (HEX-80 format), `Objects/Project.axf` (debug)
- **Library**: STM32 Standard Peripheral Library (SPL) V3.5.0 in `Library/`
- **Startup**: `Start/startup_stm32f10x_md.s` (Cortex-M3 medium-density)

Compilation is done through the Keil IDE GUI. No CLI build script exists — the `Target 1.BAT` file is a batch output helper, not a build script.

## File Organization

- **Keil 编译源文件**: `Hardware/`, `System/`, `User/`, `Library/`, `Start/` — 路径不可移动
- **Claude 生成文件**: 全部放在 `Claude_Files/` 下 (`docs/`, `tools/`, `superpowers/`, `package.json`, `node_modules/`)
- **项目配置**: `CLAUDE.md` + `.claude/` 保留根目录

## Architecture: Three-Layer Separation

```
         ┌──────────────────────────┐
         │   应用层 (Application)    │  User/main.c
         ├──────────────────────────┤
         │   系统服务层 (System)      │  System/SysTimer.c
         ├──────────────────────────┤
         │   硬件驱动层 (Hardware)    │  Hardware/PWM, ADC, KEY, OLED, LED, UI
         ├──────────────────────────┤
         │   SPL 外设库 (Library)     │  Read-only, never modified
         └──────────────────────────┘
```

**Iron dependency rule**: Hardware layer only includes SPL headers and other Hardware headers. System layer can depend on Hardware. Application layer depends on both. Never the reverse.

## Library Doctrine (Critical)

- **SPL (V3.5.0) ONLY**. Any HAL/LL function (`HAL_UART_Transmit`, `HAL_GPIO_WritePin`, etc.) is forbidden.
- All SPL peripheral headers already enabled in `User/stm32f10x_conf.h`. New .c files just include the needed `stm32f10x_<periph>.h`.
- Pin configuration uses SPL structs: `GPIO_InitTypeDef`, `TIM_TimeBaseInitTypeDef`, `ADC_InitTypeDef`, etc.

## Scheduling: SysTimer Time-Stamp Diff

The entire system uses `System/SysTimer` as the single time base (SysTick at 1ms). The `stm32f10x_it.c` file is **purified** — `SysTick_Handler` contains only `SysTimer_IncTick()`. No `Flag_Task_xxx` variables exist anywhere.

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

`SysTimer_DelayMs()` is a busy-wait delay that blocks the CPU — use only during initialization. Runtime periodic tasks and timeouts must use the timestamp-diff pattern above.

**⚠️ `System/Delay.c` is deleted** — it directly reprograms SysTick registers and conflicts with `SysTimer`. Do not revive it.

## Key Modules

| Module | File | Role |
|:---|:---|:---|
| SysTimer | `System/SysTimer.c` | Global ms counter, `Init/IncTick/GetTick/DelayMs` |
| PWM | `Hardware/PWM.c` | TIM1 full-bridge, CH1+CH1N/CH2+CH2N, 1000ns dead-time (DEADTIME_NS macro), 50% locked duty, 95-150kHz PFM; non-blocking soft-start state machine (150k→100kHz, 200Hz/10ms step, ~2.5s); atomic `Inverter_SetState()` with irq guards; `Inverter_SoftStart_Trigger/Task/Stop/Fault/GetState/GetCurrentFreq` |
| ADC | `Hardware/ADC.c` | ADC1+DMA1 dual-channel scan (current PA0, voltage PA1); `ADC_Filter_Task` 2ms independent filter task (32ms response vs old 3.2s); `Get_Real_Voltage/Current` are O(1) returns of pre-computed values |
| KEY | `Hardware/KEY.c` | 7-state FSM, single-click/double-click detection, 10ms debounce; `KEY_Task()` with timestamp-diff scheduling; KEY0(PB12) / KEY1(PB13) |
| OLED | `Hardware/OLED.c` | SSD1306 128x64 0.96" over bit-banged I2C (PA11-SCL, PA12-SDA), 8x16 font; `OLED_Clear()` only on state transitions (rare); line overwrite avoids ~100ms I2C blocking; API: `ShowChar/ShowString/ShowNum/ShowFloatNum` |
| UI | `Hardware/UI.c` | Dual-page UI: Page 0 = control panel (actionable), Page 1 = monitor only (read-only); KEY0 single-click triggers soft-start directly; KEY1 stops or adjusts freq +1kHz; soft-start real-time frequency + progress bar; state-change auto-clear; `UI_GetBridgeState()` for external query |
| LED | `Hardware/LED.c` | PC13 heartbeat (500ms toggle via `LED_Task`), PB3 status (kept off), PB4 PWM status (fast blink=SS_SWEEP, slow blink=SS_DONE), PB5 Ready (on when Page0+IDLE/DONE, off on FAULT); power-on self-test: PB3/PB4/PB5 all on ~1s then off; JTAG disabled (SWD retained on PA13/PA14) to free PB3/PB4 as GPIO |

## Startup Flow

```
上电 → PWM_Init(MOE=OFF) → OLED_Init → LED_Init → ADC_DMA_Init → KEY_Init
     → SysTimer_Init
     → 主循环 while(1):
         KEY_Task | ADC_Filter_Task | UI_Task | Inverter_SoftStart_Task | LED_Task

扫频: KEY0 单击(SS_IDLE) → Trigger(150kHz) → Task 10ms/步 → 100kHz DONE (~2.5s)
关断: KEY0(SS_DONE) / KEY1(SS_SWEEP) → Stop → SS_IDLE
调频: KEY1(SS_DONE) → freq +1kHz (100k~150k 封顶，不绕回)
故障: Overcurrent → SS_FAULT 锁存, KEY0/KEY1 复位
每次 Trigger 都是全新扫频 150k→100k
```

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | Function | Connected To |
|:---|:---|:---|
| PA0 | ADC_CH0 | Current sensor (CC6920-10A) |
| PA1 | ADC_CH1 | Voltage divider (20:1) |
| PA7 | TIM1_CH1N | Half-bridge left low-side |
| PA8 | TIM1_CH1 | Half-bridge left high-side |
| PA9 | TIM1_CH2 | Half-bridge right high-side |
| PA11 | GPIO (OD) | OLED SCL |
| PA12 | GPIO (OD) | OLED SDA |
| PA13 | SWDIO | Debug (retained) |
| PA14 | SWCLK | Debug (retained) |
| PB0 | TIM1_CH2N | Half-bridge right low-side |
| PB3 | GPIO (PP) | Status LED (ex-JTDO) |
| PB4 | GPIO (PP) | PWM status LED (ex-JNTRST) |
| PB5 | GPIO (PP) | Ready LED |
| PB12 | GPIO (IPU) | KEY0 (单击: Trigger/Stop/故障复位, 双击: 切页) |
| PB13 | GPIO (IPU) | KEY1 (单击: Stop SS_SWEEP时 / +1kHz调频 SS_DONE时 / 故障复位) |
| PC13 | GPIO (PP) | Heartbeat LED (active-low) |

## PWM Safety Rules

### Dead-Time Macro

```c
#define DEADTIME_NS  1000   // PWM.h — 修改此值调整死区
// DeadTime 寄存器值由编译期宏自动换算:
//   TDTS = 13.889ns @ 72MHz CKD_DIV1
//   DEADTIME_REG_VAL = ((DEADTIME_NS) * 72 + 500) / 1000   [四舍五入]
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

`PWM_SetFrequency` uses `TIM_CR1_UDIS` → write ARR+CCR1+CCR2 → `TIM_EGR_UG` → clear UDIS to atomically load all shadow registers. This prevents Update Event between writes causing new-period-with-old-duty-cycle magnetic saturation.

### MOE Power-On Safe State

`PWM_Init()` enables the counter but keeps MOE off. No bridge output until `Inverter_SoftStart_Trigger()` explicitly enables MOE. `PWM_Disable()` shuts MOE first, then the counter — preventing narrow-pulse shoot-through during shutdown.

### Frequency Hard Limits

`PWM_SetFrequency` clamps at 95kHz (absolute minimum, capacitive region = dead MOSFETs) and 150kHz (maximum). Period ticks forced to even numbers for anti-bias (positive/negative half-cycle symmetry).

### ADC Calibration Timing

After `ADC_Cmd(ENABLE)`, a short stabilization delay (~2μs) is required before `ADC_ResetCalibration` per STM32 reference manual (t_STAB >= 2 ADC cycles). Without it, calibration includes power-up noise causing reference drift.

### ADC Filtering

`Get_Real_Voltage()` and `Get_Real_Current()` use 16-sample moving average with O(1) running accumulator to suppress 100kHz EMI noise. Filter response: 32ms (16 * 2ms `ADC_Filter_Task` period). `ADC_Filter_Task` runs independently from UI/other callers via 2ms timestamp-diff.

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
| `Claude_Files/docs/embedded-architect-system-prompt.md` | Skill definition (also at `~/.claude/skills/embedded-architect/SKILL.md`); coding standards reference |

## Key Build Targets / Variants

- **Target 1**: Main application (flash to STM32 via ST-Link or serial bootloader)
- `Claude_Files/tools/generate_docx.js`: Node.js script to batch-convert `Claude_Files/docs/*.md` → `Claude_Files/docs/*.docx` with branded formatting
- `Claude_Files/docs/embedded-architect-system-prompt.md`: The project's skill definition (also installed at `~/.claude/skills/embedded-architect/SKILL.md`). Contains coding standards, scheduling doctrine, document version control rules, and the auto-diff document update workflow. When Claude needs a refresher on project conventions, read this file.
