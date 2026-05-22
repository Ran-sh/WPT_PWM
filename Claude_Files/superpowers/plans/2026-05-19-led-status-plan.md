# 五灯状态指示系统 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现五灯状态指示(PB3 WiFi/PB4 PWM/PB5 Ready/PC13心跳/PB1 EN)，不同系统状态以不同闪烁模式反映。

**Architecture:** LED.c 新增 PB3/PB4 引脚(JTAG重映射为GPIO)和三个状态更新函数(LED_Update_WiFi/PWM/Ready)，内部用时间戳差值法驱动闪烁。UI_Task 在 200ms 刷新周期内调用状态更新函数。

**Tech Stack:** STM32F103 SPL V3.5.0, ARMCC V5.06

---

## 文件结构

| 文件 | 操作 | 职责 |
|:---|:---|:---|
| `Hardware/LED.h` | 修改 | 新增状态枚举 + Update 函数声明 |
| `Hardware/LED.c` | 修改 | PB3/PB4 JTAG→GPIO + 闪烁状态机 + Update 函数 |
| `Hardware/UI.c` | 修改 | 200ms 刷新内调用 LED_Update_* |

---

### Task 1: LED.h — 新增状态枚举与 API

**Files:**
- Modify: `Hardware/LED.h`

- [ ] **Step 1: 替换文件内容**

```c
/**
 ******************************************************************************
 * @file    Hardware/LED.h
 * @brief   系统 LED 驱动 (PC13 + PB3/PB4/PB5) —— 公开接口
 * @note    PC13 心跳 (低有效) / PB3 WiFi (高有效) / PB4 PWM (高有效) / PB5 Ready (高有效)
 ******************************************************************************
 */

#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* ── LED 闪烁状态枚举 ── */
typedef enum {
    LED_OFF   = 0,   /* 熄灭         */
    LED_SLOW  = 1,   /* 慢闪 1Hz     */
    LED_FAST  = 2,   /* 快闪 5Hz     */
    LED_SOLID = 3    /* 常亮         */
} LedState_t;

void LED_Init(void);
void LED_Task(void);   /* PC13 心跳 500ms 翻转 */

/* 状态更新 (由 UI_Task 每 200ms 调用) */
void LED_Update_WiFi (LedState_t state);   /* PB3 */
void LED_Update_PWM  (LedState_t state);   /* PB4 */
void LED_Update_Ready(uint8_t   on_off);   /* PB5: 1=亮 0=灭 */

/* 瞬时操作 (可选, 如 WiFi 成功常亮 2s 后自动熄灭用) */
void LED_WiFi_ON(void);
void LED_WiFi_OFF(void);

#endif
```

- [ ] **Step 2: 提交**

```bash
git add Hardware/LED.h
git commit -m "feat: add LED status enums and Update API"
```

---

### Task 2: LED.c — JTAG 重映射 + 闪烁状态机

**Files:**
- Rewrite: `Hardware/LED.c`

- [ ] **Step 1: 写入完整 LED.c**

```c
/**
 ******************************************************************************
 * @file    Hardware/LED.c
 * @brief   系统 LED 驱动 (PC13 心跳 + PB3 WiFi + PB4 PWM + PB5 Ready)
 * @note    PB3=JTDO→GPIO, PB4=JNTRST→GPIO, 需禁用 JTAG 保留 SWD
 *          PB3/PB4/PB5 高电平亮, PC13 低电平亮
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"

/* ── 闪烁参数 ── */
#define LED_SLOW_MS   500    /* 慢闪半周期 */
#define LED_FAST_MS   100    /* 快闪半周期 */

/* ── 私有状态变量 ── */
static LedState_t s_wifi_state  = LED_OFF;
static LedState_t s_pwm_state   = LED_OFF;
static uint8_t    s_ready_on    = 0;

void LED_Init(void)
{
    /* ── 时钟 ── */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);

    /*
     * JTAG 重映射: 禁用 JTAG→PB3/PB4 变 GPIO, SWD (PA13/PA14) 保留
     * ST-Link 用 SWD, 不影响下载调试
     */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* ── PC13 心跳 (低有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);   /* 高=灭 */

    /* ── PB3 WiFi (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_3);  /* 低=灭 */

    /* ── PB4 PWM (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_4);

    /* ── PB5 Ready (高有效) ── */
    gpio.GPIO_Pin = GPIO_Pin_5;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_5);    /* 初始亮=可操作 */
}

/* ═══════════════════════════════════════════════════════════════
 *  PC13 心跳 (500ms 翻转, 慢闪 1Hz)
 * ═══════════════════════════════════════════════════════════════ */

void LED_Task(void)
{
    static uint32_t last = 0;

    if (SysTimer_GetTick() - last >= 500)
    {
        last = SysTimer_GetTick();
        GPIO_WriteBit(GPIOC, GPIO_Pin_13,
            (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13)));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  通用闪烁驱动 (时间戳差值法)
 * ═══════════════════════════════════════════════════════════════ */

/* 每引脚独立时间戳, 避免 PB3/PB4 互窜闪烁节拍 */
static uint32_t s_wifi_last   = 0;
static uint32_t s_pwm_last    = 0;

static void LED_Drive(GPIO_TypeDef *port, uint16_t pin, LedState_t state,
                      uint32_t *p_last)
{
    uint32_t half_period;

    switch (state)
    {
        case LED_OFF:
            GPIO_ResetBits(port, pin);   /* 低=灭 */
            return;

        case LED_SOLID:
            GPIO_SetBits(port, pin);     /* 高=亮 */
            return;

        case LED_SLOW:
            half_period = LED_SLOW_MS;
            break;

        case LED_FAST:
            half_period = LED_FAST_MS;
            break;

        default:
            return;
    }

    if (SysTimer_GetTick() - *p_last >= half_period)
    {
        *p_last = SysTimer_GetTick();
        GPIO_WriteBit(port, pin,
            (BitAction)(1 - GPIO_ReadOutputDataBit(port, pin)));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */

void LED_Update_WiFi(LedState_t state)
{
    s_wifi_state = state;
    if (state == LED_OFF || state == LED_SOLID)
        LED_Drive(GPIOB, GPIO_Pin_3, state, &s_wifi_last);
}

void LED_Update_PWM(LedState_t state)
{
    s_pwm_state = state;
    if (state == LED_OFF || state == LED_SOLID)
        LED_Drive(GPIOB, GPIO_Pin_4, state, &s_pwm_last);
}

void LED_Update_Ready(uint8_t on_off)
{
    s_ready_on = on_off;
    if (on_off)
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
    else
        GPIO_ResetBits(GPIOB, GPIO_Pin_5);
}

void LED_WiFi_ON(void)  { GPIO_SetBits(GPIOB, GPIO_Pin_3); }
void LED_WiFi_OFF(void) { GPIO_ResetBits(GPIOB, GPIO_Pin_3); }

/*
 * 闪烁 Task: 由 UI_Task 内部调用 (非独立任务)
 * 处理 PB3/PB4 的慢闪/快闪, PB5 为静态电平无需驱动
 */
void LED_Status_Task(void)
{
    /* PB3 WiFi 闪烁 */
    if (s_wifi_state == LED_SLOW || s_wifi_state == LED_FAST)
        LED_Drive(GPIOB, GPIO_Pin_3, s_wifi_state, &s_wifi_last);

    /* PB4 PWM 闪烁 */
    if (s_pwm_state == LED_SLOW || s_pwm_state == LED_FAST)
        LED_Drive(GPIOB, GPIO_Pin_4, s_pwm_state, &s_pwm_last);
}
```

- [ ] **Step 2: 提交**

```bash
git add Hardware/LED.c
git commit -m "feat: add PB3/PB4/PB5 LED status with JTAG remap"
```

---

### Task 3: LED.h — 补充 LED_Status_Task 声明

**Files:**
- Modify: `Hardware/LED.h`

- [ ] **Step 1: 在 LED.h 末尾 `#endif` 前补一行**

```c
void LED_Status_Task(void);  /* 闪烁驱动, UI_Task 内调用 */
```

- [ ] **Step 2: 提交**

```bash
git add Hardware/LED.h
git commit -m "feat: expose LED_Status_Task"
```

---

### Task 4: UI.c — 200ms 刷新内调用 LED_Update

**Files:**
- Modify: `Hardware/UI.c`

- [ ] **Step 1: 在 need_refresh 块末尾追加 LED 状态更新**

在 `UI_Task` 函数中, `need_refresh` 判定之后、按键处理之前, 加入:

```c
if (need_refresh) {
    /* ── LED 状态指示更新 ── */
    SoftStart_State_t ss = Inverter_SoftStart_GetState();

    /* PB3 WiFi */
    if (!wifi_connected) {
        LED_Update_WiFi(LED_SLOW);           /* 待联网: 慢闪 */
    } else {
        LED_Update_WiFi(LED_OFF);            /* 已联网: 灭 */
    }

    /* PB4 PWM */
    if (ss == SS_SWEEP || ss == SS_DONE) {
        LED_Update_PWM(LED_SLOW);            /* PWM 工作: 慢闪 */
    } else {
        LED_Update_PWM(LED_OFF);             /* 关断: 灭 */
    }

    /* PB5 Ready: 可操作时亮, 忙时灭 */
    uint8_t ready = wifi_connected
                 && (ss == SS_IDLE || ss == SS_DONE)
                 && (1);  /* 联网成功后立刻可操作 */
    LED_Update_Ready(ready);

    LED_Status_Task();  /* 驱动闪烁状态机 */
}
```

- [ ] **Step 2: 将 `ss` 变量的获取从 later 提上来**

`UI_Task` 中原本的 `SoftStart_State_t ss = Inverter_SoftStart_GetState();` 移到 `need_refresh` 之前（如果还没在那里），确保 LED 更新和按键分发都能用。

- [ ] **Step 3: 联网中快闪**

在 KEY0 触发联网的路径中（`if (key0_event == 1 && !wifi_connected)` 块），调用 `LED_Update_WiFi(LED_FAST);` 在 `App_Net_Init()` 之前，以及成功后 `LED_Update_WiFi(LED_SOLID);` 然后延时 2s 后 `LED_Update_WiFi(LED_OFF);`:

```c
if (key0_event == 1) {
    uint8_t ret;
    LED_Update_WiFi(LED_FAST);              /* 联网中快闪 */
    OLED_Clear();
    OLED_ShowString(1, 1, "[Control Mode] ");
    OLED_ShowString(2, 1, "WiFi Connecting");
    OLED_ShowString(3, 1, "Please wait...");
    OLED_ShowString(4, 1, "                ");
    ret = App_Net_Init();
    if (ret == 0) {
        wifi_connected = 1;
        LED_Update_WiFi(LED_SOLID);          /* 成功常亮 */
        SysTimer_DelayMs(2000);
        LED_Update_WiFi(LED_OFF);            /* 2s 后熄灭 */
    }
    /* 失败: wifi_connected 保持 0, 回到 LED_SLOW */
    OLED_Clear();
    need_refresh = 0;
    break;
}
```

- [ ] **Step 4: 提交**

```bash
git add Hardware/UI.c
git commit -m "feat: integrate LED status indicators into UI"
```

---

### Task 5: 编译验证

- [ ] **Step 1: Keil uVision 编译 Project.uvprojx**

预期: 0 errors, 0 warnings。

- [ ] **Step 2: 烧录测试验证**

- 上电: PC13 慢闪, PB5 常亮 (待联网, 可操作), PB3 慢闪
- 按 KEY0: PB3 快闪→常亮2s→灭, PB5 灭→亮
- 再按 KEY0: PB4 慢闪 (扫频+运行)
- 按 KEY1 关断: PB4 灭

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "fix: compile and verify LED status system"
```
