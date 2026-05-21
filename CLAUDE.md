# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Identity

Wireless Power Transfer PWM controller for STM32F103C8. Full-bridge resonant inverter with PFM power regulation, non-blocking soft-start frequency sweep, and OLED UI.

- **MCU**: STM32F103C8 (128KB Flash, Cortex-M3, 72MHz)
- **Toolchain**: Keil MDK V5.06 (ARMCC), SPL V3.5.0 only — **no HAL/LL/CubeMX**
- **Build**: Open `Project.uvprojx` in Keil uVision, click Build (F7). Output: `Objects/Project.hex`
- **Reference project**: `D:\Claude Code Project\WPT_PWM_NetAssistant_LAN_V1.0` — network-enabled variant with ESP8266 + App_Net

## Architecture

Three-layer design. Dependencies flow downward only:

```
User/          Application — main.c, stm32f10x_it.c
System/        System services — SysTimer (sole timebase)
Hardware/      Drivers — PWM, ADC, KEY, OLED, LED, UI
Library/       SPL V3.5.0 — read-only
Start/         Startup files — read-only
```

- Hardware modules: all internal variables/buffers/state-machines are `static`
- ISR-shared variables must be `volatile`; `.h` files never expose internals via `extern`
- Application layer may depend on System and Hardware; Hardware never depends on Application

## SysTimer Doctrine (Critical)

`System/SysTimer` is the **sole timebase** for the entire project. The tick counter is a private `static volatile uint32_t` incremented every 1ms by `SysTick_Handler`. All periodic tasks use the **timestamp-difference pattern**:

```c
static uint32_t last = 0;
if (SysTimer_GetTick() - last >= PERIOD_MS) {
    last = SysTimer_GetTick();
    // periodic work
}
```

Unsigned subtraction handles ~49.7-day wraparound correctly.

**Forbidden:**
- `Delay_ms()`/`Delay_us()` during runtime (blocks CPU)
- Business logic inside ISRs
- `extern uint8_t flag` for inter-module signaling
- `static` counters inside `SysTick_Handler`
- `System/Delay.h` — reprograms SysTick, conflicts with SysTimer

## Key Modules

### PWM (`Hardware/PWM.c`)

TIM1 full-bridge, complementary outputs with partial remap:

| Signal | Pin | Function |
|:---|:---|:---|
| CH1 | PA8 | Upper-left gate (HIN, active high) |
| CH1N | PA7 | Lower-left gate (LIN, active low) |
| CH2 | PA9 | Upper-right gate (HIN, active high) |
| CH2N | PB0 | Lower-right gate (LIN, active low) |

- CH1=PWM1, CH2=PWM2 → diagonal conduction, 50% fixed duty cycle
- PFM power control via frequency adjustment; dead-time set by `DEADTIME_NS` macro (1000ns)
- `DEADTIME_REG_VAL` computed at compile-time with assertion DTG ≤ 127 (BDTR linear range)
- **MOE is OFF after `PWM_Init()`** — safe power-on state
- Frequency hard limits: 95kHz–150kHz
- Shadow register atomic update via UDIS/UG sequence prevents period-distortion shoot-through

**Soft-start state machine** (non-blocking, 200Hz/step every 10ms, ~2.5s total):
```
SS_IDLE → SS_SWEEP (starts at 150kHz) → SS_DONE (100kHz steady)
              ↓
          SS_FAULT (overcurrent latch, reset by KEY0/KEY1)
```

**Rule**: Other modules must NEVER touch `TIM_Cmd`/`TIM_CtrlPWMOutputs`/`TIM1->ARR` directly — use `PWM_Enable()`/`PWM_Disable()`/`PWM_SetFrequency()`.

### ADC (`Hardware/ADC.c`)

ADC1 + DMA1_Channel1 dual-channel circular scan.
- PA0 = current (CC6920-10A hall sensor, VREF=3.30V, sensitivity=0.132V/A, offset=1.65V)
- PA1 = voltage (20:1 divider)
- `ADC_Filter_Task()`: 2ms period, 16-sample sliding window with O(1) running accumulator
- `Get_Real_Voltage()`/`Get_Real_Current()`: O(1) return of pre-computed values (32ms filter delay)

### KEY (`Hardware/KEY.c`)

Seven-state FSM, single/double-click detection, 10ms debounce, 200ms double-click window.

| Key | Pin | Click | Double-click |
|:---|:---|:---|:---|
| KEY0 | PB12 (IPU) | SS_IDLE→Trigger / SS_DONE→Stop / SS_FAULT→Reset | Toggle UI page |
| KEY1 | PB13 (IPU) | SS_SWEEP→Stop / SS_DONE→+1kHz / SS_FAULT→Reset | — |

`KEY_Task()`: 10ms timestamp-diff. `KEY_Get_Event()`: consume-on-read (0=none, 1=click, 2=double).

### LED (`Hardware/LED.c`)

JTAG disabled (SWD retained on PA13/PA14) to free PB3/PB4 as GPIO.

| LED | Pin | Active | Behavior |
|:---|:---|:---|:---|
| Heartbeat | PC13 | Low | 500ms toggle via `LED_Task()` |
| Status | PB3 | High | Kept off (WiFi indicator in reference, unused in local variant) |
| PWM | PB4 | High | Fast blink=SS_SWEEP, slow blink=SS_DONE, off otherwise |
| Ready | PB5 | High | On when Page0 + IDLE or DONE; off on FAULT |

Power-on self-test: PB3/PB4/PB5 all on for ~1s then off. `LED_Status_Task()` drives blinking, called from `UI_Task()`.

### OLED (`Hardware/OLED.c`)

SSD1306 128×64, software I2C on PA11(SCL)/PA12(SDA), open-drain, slave address 0x78. Font: 8×16 ASCII (`OLED_Font.h`), 4 lines × 16 chars.

- `OLED_Clear()` is slow (~100ms) — only called on state transitions or page switches
- Routine 200ms refresh uses 16-char full-line overwrites without clearing

### UI (`Hardware/UI.c`)

Dual-page, 200ms refresh. Page 0 = control panel (actionable), Page 1 = monitor only (read-only). Handles key dispatch and LED state updates. Interface matches reference project (`UI_SetBridgeState`, `UI_GetBridgeState`, `UI_SetWiFiConnected` retained for compatibility).

## Main Loop

```c
while (1) {
    KEY_Task();                  // 10ms
    ADC_Filter_Task();           // 2ms
    UI_Task();                   // 200ms + LED status
    Inverter_SoftStart_Task();   // 10ms sweep step
    LED_Task();                  // 500ms heartbeat
}
```

No delays, no blocking. Each task self-regulates via timestamp-diff.

## stm32f10x_it.c

`SysTick_Handler` has exactly one line: `SysTimer_IncTick();`. No static counters, no `Flag_Task_*`, no peripheral interrupt handlers.

## Pin Allocation

| Pin | Function | Alt Function |
|:---|:---|:---|
| PA0 | Current sense (ADC1_CH0) | — |
| PA1 | Voltage sense (ADC1_CH1) | — |
| PA7 | TIM1_CH1N | PWM lower-left |
| PA8 | TIM1_CH1 | PWM upper-left |
| PA9 | TIM1_CH2 | PWM upper-right |
| PA11 | I2C SCL (OLED) | GPIO open-drain |
| PA12 | I2C SDA (OLED) | GPIO open-drain |
| PA13 | SWDIO | Debug (retained) |
| PA14 | SWCLK | Debug (retained) |
| PB0 | TIM1_CH2N | PWM lower-right |
| PB3 | Status LED | GPIO (ex-JTDO) |
| PB4 | PWM status LED | GPIO (ex-JNTRST) |
| PB5 | Ready LED | GPIO |
| PB12 | KEY0 | GPIO IPU |
| PB13 | KEY1 | GPIO IPU |
| PC13 | Heartbeat LED | GPIO (active low) |
