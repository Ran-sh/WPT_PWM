# V5.0 GPIO 引脚重映射 + 按键系统重构 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 STM32F103C8 固件从 V4.5.2 引脚映射迁移到 V5.0，包括 5 键系统、三灯系统、TFT背光GPIO化、W25Q128 CS 迁移

**Architecture:** 自底向上逐模块修改：LED驱动 → Flash CS → TFT背光 → 按键驱动 → Sys_Core电源控制 → Ui_Controller适配 → App_Network清理 → main.c注释

**Tech Stack:** ARMCC V5.06, STM32F103 SPL V3.5.0, Keil MDK-ARM V5, C89

**验证方式:** 每完成一个 Task 后运行 Keil 编译 (F7)，确认 0 错误 0 警告

---

### Task 1: Led_Driver — 三灯系统重构

**Files:**
- Modify: `Keil_Project/Hardware/Led_Driver.h`
- Modify: `Keil_Project/Hardware/Led_Driver.c`

**变更要点：**
- 移除 PA10(LED_COM), PA11(LED_POWER), PA12(LED_TEMP) 引用
- PB3 改为 POWER LED (纯 GPIO ON/OFF，不经过状态机)
- PA15 从心跳改为 STATUS LED (新增 `Led_Driver_Set_Status()`)
- 移除 `Led_Driver_Set_Com/Power/Temp/Pwm/System` 5 个废弃接口
- 保留 `Led_Driver_Set_WiFi()` 不变

- [ ] **Step 1: 重写 Led_Driver.h**

替换 `Keil_Project/Hardware/Led_Driver.h` 全部内容：

```c
/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.h
 * @brief   LED 指示灯驱动 — V5.0 (3 LED)
 * @note    PB4=WIFI, PB3=POWER(12V指示), PA15=STATUS(PWM状态)
 ******************************************************************************
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    LED_DRIVER_STATE_OFF  = 0,
    LED_DRIVER_STATE_ON   = 1,
    LED_DRIVER_STATE_SLOW = 2,   /* 500ms period blink */
    LED_DRIVER_STATE_FAST = 3    /* 200ms period blink */
} Led_Driver_State;

/** @brief Init 3 LED GPIOs + JTAG disable to free PB3/PB4 */
void Led_Driver_Init(void);
/** @brief Periodic LED drive (call from main loop, ~200ms) */
void Led_Driver_Task(void);

/** @brief Set WiFi status LED (PB4) */
void Led_Driver_Set_WiFi(Led_Driver_State state);
/** @brief Set POWER LED ON/OFF (PB3, direct GPIO — no state machine)
 *  @param on 1=12V active (ON), 0=12V off */
void Led_Driver_Set_Power(uint8_t on);
/** @brief Set STATUS LED (PA15) — PWM state indicator
 *  @param state OFF=idle/fault, SLOW=sweep, ON=running */
void Led_Driver_Set_Status(Led_Driver_State state);

#endif /* LED_DRIVER_H */
```

- [ ] **Step 2: 重写 Led_Driver.c**

替换 `Keil_Project/Hardware/Led_Driver.c` 全部内容：

```c
/**
 ******************************************************************************
 * @file    Hardware/Led_Driver.c
 * @brief   LED 指示灯驱动 — V5.0 (3 LEDs)
 *
 *  Pinout (JTAG disabled -> PB3/PB4/PA15 freed as GPIO):
 *  +----------------------------------------------------------+
 *  |                    STM32F103C8T6                          |
 *  |                                                           |
 *  |    PA15 --- GPIO_PP --- LED_STATUS (yellow) PWM indicator |
 *  |              OFF=idle/fault  SLOW=sweep  ON=running       |
 *  |                                                           |
 *  |    PB4  --- GPIO_PP --- LED_WIFI   (blue)  WiFi status    |
 *  |              ON=online  SLOW=reconnect  OFF=offline       |
 *  |                                                           |
 *  |    PB3  --- GPIO_PP --- LED_POWER  (green) 12V indicator  |
 *  |              ON=12V enabled  OFF=12V disabled             |
 *  |                                                           |
 *  |    Each LED: GPIO -> R (220 ohm) -> LED anode -> GND      |
 *  +----------------------------------------------------------+
 *
 * @note    V5.0: PA10/PA11 removed, PA12->TFT_BL, PB3=POWER LED
 *          STATUS LED replaces old heartbeat — reflects PWM state
 ******************************************************************************
 */

#include "Led_Driver.h"
#include "Sys_Timer.h"

#define LED_DRIVER_WIFI_PIN    GPIO_Pin_4   /* PB4 — WiFi status */
#define LED_DRIVER_POWER_PIN   GPIO_Pin_3   /* PB3 — 12V power indicator */
#define LED_DRIVER_STATUS_PIN  GPIO_Pin_15  /* PA15 — PWM state indicator */

#define LED_DRIVER_PORT_A      GPIOA
#define LED_DRIVER_PORT_B      GPIOB

#define LED_DRIVER_BLINK_SLOW_PERIOD_MS   500
#define LED_DRIVER_BLINK_FAST_PERIOD_MS   200

/* ── Static state ── */
static Led_Driver_State s_wifi_state   = LED_DRIVER_STATE_OFF;
static Led_Driver_State s_status_state = LED_DRIVER_STATE_OFF;
static uint8_t          s_power_on     = 0;

static uint32_t s_wifi_last   = 0;
static uint32_t s_status_last = 0;

/* ── Per-pin blink driver ── */
static void Drive_Pin(GPIO_TypeDef* port, uint16_t pin,
                      Led_Driver_State state, uint32_t* p_last)
{
    uint32_t now = Sys_Timer_Get_Tick();

    switch (state) {
        case LED_DRIVER_STATE_ON:
            GPIO_SetBits(port, pin);
            break;
        case LED_DRIVER_STATE_OFF:
            GPIO_ResetBits(port, pin);
            break;
        case LED_DRIVER_STATE_SLOW:
            if (now - *p_last >= LED_DRIVER_BLINK_SLOW_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin,
                    (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
        case LED_DRIVER_STATE_FAST:
            if (now - *p_last >= LED_DRIVER_BLINK_FAST_PERIOD_MS) {
                *p_last = now;
                GPIO_WriteBit(port, pin,
                    (BitAction)!GPIO_ReadOutputDataBit(port, pin));
            }
            break;
    }
}

void Led_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    /* PA15 = STATUS LED */
    cfg.GPIO_Pin = LED_DRIVER_STATUS_PIN;
    GPIO_Init(LED_DRIVER_PORT_A, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);

    /* PB3 = POWER, PB4 = WIFI */
    cfg.GPIO_Pin = LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN;
    GPIO_Init(LED_DRIVER_PORT_B, &cfg);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);

    /* Power-on self-test: all ON 500ms (SysTimer not yet ready, busy-wait) */
    GPIO_SetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);
    GPIO_SetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
    { volatile uint32_t i; for (i = 0; i < 175000; i++) __NOP(); }  /* ~500ms @72MHz */
    GPIO_ResetBits(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN);
    GPIO_ResetBits(LED_DRIVER_PORT_B,
        LED_DRIVER_POWER_PIN | LED_DRIVER_WIFI_PIN);
}

void Led_Driver_Task(void)
{
    Drive_Pin(LED_DRIVER_PORT_B, LED_DRIVER_WIFI_PIN,  s_wifi_state,  &s_wifi_last);
    Drive_Pin(LED_DRIVER_PORT_A, LED_DRIVER_STATUS_PIN, s_status_state, &s_status_last);

    /* POWER LED: direct GPIO follow s_power_on (no state machine) */
    if (s_power_on)
        GPIO_SetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);
    else
        GPIO_ResetBits(LED_DRIVER_PORT_B, LED_DRIVER_POWER_PIN);
}

void Led_Driver_Set_WiFi(Led_Driver_State state)   { s_wifi_state   = state; }
void Led_Driver_Set_Power(uint8_t on)               { s_power_on     = on;    }
void Led_Driver_Set_Status(Led_Driver_State state)  { s_status_state = state; }
```

- [ ] **Step 3: 编译验证**

在 Keil IDE 中按 F7 编译。预期 0 错误 0 警告。Led_Driver 消费者（App_Network, Ui_Controller, Sys_Core）会有编译错误——这是预期的，后续 Task 逐一修复。

- [ ] **Step 4: Commit**

```bash
git add Keil_Project/Hardware/Led_Driver.c Keil_Project/Hardware/Led_Driver.h
git commit -m "refactor: V5.0 — Led_Driver 三灯系统 (移除 COM/POWER/TEMP/PWM, STATUS替代心跳)"
```

---

### Task 2: W25Q_Driver — Flash CS 从 PA12 迁至 PB12

**Files:**
- Modify: `Keil_Project/Hardware/W25Q_Driver.c:45-48`

- [ ] **Step 1: 修改 CS 引脚宏**

在 `Keil_Project/Hardware/W25Q_Driver.c` 第 45-48 行，将：

```c
#define FLASH_CS_PIN    GPIO_Pin_12
#define FLASH_CS_PORT   GPIOA
```

改为：

```c
#define FLASH_CS_PIN    GPIO_Pin_12
#define FLASH_CS_PORT   GPIOB
```

其余所有逻辑（BSRR原子翻转, Enter_Mode/Leave_Mode, CS_Pulse）通过宏引用自动适配，零额外改动。

- [ ] **Step 2: 更新接线图注释**

在 `Keil_Project/Hardware/W25Q_Driver.c` 文件头部接线图中，将：

```
|    PA12 --- GPIO_PP --------------------> /CS  (GPIO gated  |
```

改为：

```
|    PB12 --- GPIO_PP --------------------> /CS  (GPIO gated  |
```

- [ ] **Step 3: 编译验证**

F7 编译。预期 0 错误 0 警告。

- [ ] **Step 4: Commit**

```bash
git add Keil_Project/Hardware/W25Q_Driver.c
git commit -m "refactor: V5.0 — W25Q128 CS PA12→PB12"
```

---

### Task 3: Tft_Driver — TFT 背光 GPIO 化 (PB6→PA12)

**Files:**
- Modify: `Keil_Project/Hardware/Tft_Driver.c`

**变更要点：**
- 移除 TIM4_CH1 硬件 PWM 背光
- TFT_BL 改为 PA12 GPIO 推挽输出
- `Tft_Driver_Set_Backlight()` 改为 GPIO ON/OFF
- 去掉 Tft_Driver_Init 中 L2 Flash CS 钳位（PA12 现在是 BL 不是 CS）

- [ ] **Step 1: 修改 TFT_BL 引脚宏**

在 `Keil_Project/Hardware/Tft_Driver.c` 第 43 行，将：

```c
#define TFT_DRIVER_BL_PIN   GPIO_Pin_6
```

改为：

```c
#define TFT_DRIVER_BL_PIN   GPIO_Pin_12
#define TFT_DRIVER_BL_PORT  GPIOA
```

- [ ] **Step 2: 重写 Tft_Driver_Init 中的 BL 和 Flash CS 初始化**

在 `Keil_Project/Hardware/Tft_Driver.c` 第 248-303 行区域：

**删除** L2 Flash CS 钳位代码块 (第 256-259 行)：
```c
    /* ══ L2: Flash CS 无条件前置锁死, 封杀开机对灌短路 ══ */
    gpio.GPIO_Pin   = GPIO_Pin_12; gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOA, &gpio);
    GPIO_SetBits(GPIOA, GPIO_Pin_12);                    /* CS=H → W25Q128 高阻悬空 */
```

**删除** TIM4 时钟使能 (第 254 行)：
```c
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
```
改为不使能 TIM4。

**修改** BL 初始化 (第 273-276 行)，从 AF_PP 改为 Out_PP：

将：
```c
    /* BL=PB6, TIM4_CH1 */
    gpio.GPIO_Pin  = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);
```

改为：
```c
    /* BL=PA12, GPIO ON/OFF */
    gpio.GPIO_Pin   = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TFT_DRIVER_BL_PORT, &gpio);
    GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);  /* 初始灭 */
```

**删除** TIM4 PWM 配置块 (第 293-303 行)：
```c
    /* TIM4 CH1 背光 1kHz */
    TIM_TimeBaseStructInit(&tim_base);
    ...
    TIM_Cmd(TIM4, ENABLE);
```

同时删除函数开头不再需要的局部变量 `tim_base` 和 `oc` 声明 (第 245-246 行)。

- [ ] **Step 3: 重写 Tft_Driver_Set_Backlight**

将 `Keil_Project/Hardware/Tft_Driver.c` 第 429-432 行：

```c
void Tft_Driver_Set_Backlight(uint8_t v)
{
    TIM_SetCompare1(TIM4, ((uint16_t)v * 999) / 255);
}
```

改为：

```c
void Tft_Driver_Set_Backlight(uint8_t v)
{
    if (v > 0)
        GPIO_SetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
    else
        GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
}
```

- [ ] **Step 4: 更新文件头部接线图**

将 Tft_Driver.c 头部注释中：

```
|    PB6 --- TIM4_CH1 -------------------> BL   (backlight P  |
```

改为：

```
|    PA12 -- GPIO_PP --------------------> BL   (backlight ON/OFF) |
```

- [ ] **Step 5: 编译验证**

F7 编译。预期 0 错误 0 警告。

- [ ] **Step 6: Commit**

```bash
git add Keil_Project/Hardware/Tft_Driver.c
git commit -m "refactor: V5.0 — TFT_BL GPIO化 PB6→PA12, 去TIM4 PWM"
```

---

### Task 4: Key_Driver — 4→5 键 + ID 重命名 + 双击配置

**Files:**
- Modify: `Keil_Project/Hardware/Key_Driver.h`
- Modify: `Keil_Project/Hardware/Key_Driver.c`

**变更要点：**
- KEY_DRIVER_COUNT 4→5
- KEY0(PB9)=电源, KEY1(PB8)=返回, KEY2(PB7)=UP, KEY3(PB6)=DOWN, KEY4(PB5)=确定
- `no_double` bit flag → `config` 字段: CLICK_ONLY / WITH_DOUBLE
- KEY0/KEY4=CLICK_ONLY, KEY1/KEY2/KEY3=WITH_DOUBLE

- [ ] **Step 1: 重写 Key_Driver.h**

替换 `Keil_Project/Hardware/Key_Driver.h` 全部内容：

```c
/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.h
 * @brief   按键驱动 — V5.0 (5 键)
 * @note    PB9=KEY0(电源), PB8=KEY1(返回), PB7=KEY2(UP), PB6=KEY3(DOWN), PB5=KEY4(确定)
 *          全部 GPIO IPU, 低电平按下, 10ms 去抖 + FSM 状态机
 ******************************************************************************
 */

#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    KEY_DRIVER_EVENT_NONE        = 0,
    KEY_DRIVER_EVENT_CLICK       = 1,
    KEY_DRIVER_EVENT_DOUBLE_CLICK = 2,
    KEY_DRIVER_EVENT_LONG_PRESS  = 3
} Key_Driver_Event;

/* V5.0 5-key ID */
#define KEY_DRIVER_ID_POWER     0   /* PB9 — KEY0 电源开关 */
#define KEY_DRIVER_ID_BACK      1   /* PB8 — KEY1 返回 */
#define KEY_DRIVER_ID_UP        2   /* PB7 — KEY2 UP/加 */
#define KEY_DRIVER_ID_DOWN      3   /* PB6 — KEY3 DOWN/减 */
#define KEY_DRIVER_ID_CONFIRM   4   /* PB5 — KEY4 确定/启停 */

/* Config flags */
#define KEY_DRIVER_CFG_CLICK_ONLY   0x01  /* skip double-click, fire CLICK on release */
#define KEY_DRIVER_CFG_WITH_DOUBLE  0x00  /* allow double-click detection */

/** @brief Init 5 keys GPIO (all PBx, IPU) */
void             Key_Driver_Init(void);
/** @brief Configure key behavior
 *  @param key_id  Key index (0-4)
 *  @param config  KEY_DRIVER_CFG_CLICK_ONLY or KEY_DRIVER_CFG_WITH_DOUBLE */
void             Key_Driver_Configure(uint8_t key_id, uint8_t config);
/** @brief Periodic key FSM drive (call every 10ms) */
void             Key_Driver_Task(void);
/** @brief Batch read 5 key events (single critical section)
 *  @param out[5] Key events array (0=POWER 1=BACK 2=UP 3=DOWN 4=CONFIRM) */
void             Key_Driver_Get_All_Events(Key_Driver_Event out[5]);

#endif /* KEY_DRIVER_H */
```

- [ ] **Step 2: 重写 Key_Driver.c**

替换 `Keil_Project/Hardware/Key_Driver.c` 全部内容：

```c
/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — V5.0 (5 keys)
 *
 *  Pinout (5 keys, all IPU pull-up, press = LOW):
 *  +----------------------------------------------------------+
 *  |                      STM32F103C8T6                        |
 *  |                                                           |
 *  |    PB9  --- IPU ---+--- Key --- GND    KEY0  (电源开关)    |
 *  |    PB8  --- IPU ---+--- Key --- GND    KEY1  (返回)        |
 *  |    PB7  --- IPU ---+--- Key --- GND    KEY2  (UP/加)      |
 *  |    PB6  --- IPU ---+--- Key --- GND    KEY3  (DOWN/减)    |
 *  |    PB5  --- IPU ---+--- Key --- GND    KEY4  (确定/启停)   |
 *  |                                                           |
 *  |    Per-key FSM: IDLE -> DEBOUNCE(10ms) -> PRESS           |
 *  |      -> WAIT_DOUBLE(200ms) -> LONG(3s)                    |
 *  |    Batch read: Key_Driver_Get_All_Events merges critical   |
 *  +----------------------------------------------------------+
 *
 * @note    V5.0: 5 keys, KEY0=power hardware switch (handled by Sys_Core)
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_DRIVER_COUNT               5
#define KEY_DRIVER_DEBOUNCE_MS         10
#define KEY_DRIVER_RELEASE_DEBOUNCE_MS 12
#define KEY_DRIVER_LONG_PRESS_MS       3000
#define KEY_DRIVER_DOUBLE_WINDOW_MS    200
#define KEY_DRIVER_TASK_PERIOD_MS      10

typedef enum {
    KEY_DRIVER_FSM_IDLE = 0,
    KEY_DRIVER_FSM_DEBOUNCE,
    KEY_DRIVER_FSM_PRESS,
    KEY_DRIVER_FSM_RELEASE_DEBOUNCE,
    KEY_DRIVER_FSM_WAIT_DOUBLE,
    KEY_DRIVER_FSM_LONG
} Key_Driver_Fsm_State;

typedef struct {
    GPIO_TypeDef*       port;
    uint16_t            pin;
    Key_Driver_Fsm_State state;
    uint32_t            timer;
    uint8_t             event;
    uint8_t             click_count;
    uint8_t             flags;          /* bit0=click_only: 1=skip double-click */
} Key_Driver_Instance;

/* KEY0=PB9, KEY1=PB8, KEY2=PB7, KEY3=PB6, KEY4=PB5 */
static Key_Driver_Instance s_keys[KEY_DRIVER_COUNT] = {
    { GPIOB, GPIO_Pin_9, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_8, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_7, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_6, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_5, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 }
};

void Key_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    cfg.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &cfg);
}

/**
 * @brief  Configure key behavior
 * @param  key_id   Key index (0=POWER, 1=BACK, 2=UP, 3=DOWN, 4=CONFIRM)
 * @param  config   KEY_DRIVER_CFG_CLICK_ONLY or KEY_DRIVER_CFG_WITH_DOUBLE
 */
void Key_Driver_Configure(uint8_t key_id, uint8_t config)
{
    if (key_id < KEY_DRIVER_COUNT) {
        if (config & KEY_DRIVER_CFG_CLICK_ONLY)
            s_keys[key_id].flags |= 0x01;
        else
            s_keys[key_id].flags &= ~0x01;
    }
}

/* Single-key FSM — with release-edge debounce + click_only fast path
 *   IDLE -> DEBOUNCE(10ms) -> PRESS
 *     -> RELEASE_DEBOUNCE(12ms) -> [click_only? -> CLICK -> IDLE]
 *                                -> [else -> WAIT_DOUBLE(200ms) -> CLICK/DOUBLE_CLICK -> IDLE]
 *     -> LONG(3s) -> LONG_PRESS (held) */
static void Update_Fsm(Key_Driver_Instance* key)
{
    uint8_t  pressed     = (GPIO_ReadInputDataBit(key->port, key->pin) == Bit_RESET);
    uint8_t  click_only  = (key->flags & 0x01);
    uint32_t elapsed     = Sys_Timer_Get_Tick() - key->timer;

    switch (key->state) {
        case KEY_DRIVER_FSM_IDLE:
            if (pressed) {
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            }
            break;

        case KEY_DRIVER_FSM_DEBOUNCE:
            if (elapsed >= KEY_DRIVER_DEBOUNCE_MS) {
                if (pressed) {
                    key->state = KEY_DRIVER_FSM_PRESS;
                    key->timer = Sys_Timer_Get_Tick();
                } else {
                    key->state = KEY_DRIVER_FSM_IDLE;
                }
            }
            break;

        case KEY_DRIVER_FSM_PRESS:
            if (pressed) {
                if (elapsed >= KEY_DRIVER_LONG_PRESS_MS) {
                    key->event       = KEY_DRIVER_EVENT_LONG_PRESS;
                    key->click_count = 0;
                    key->state       = KEY_DRIVER_FSM_LONG;
                }
            } else {
                key->state = KEY_DRIVER_FSM_RELEASE_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            }
            break;

        case KEY_DRIVER_FSM_RELEASE_DEBOUNCE:
            if (elapsed >= KEY_DRIVER_RELEASE_DEBOUNCE_MS) {
                if (pressed) {
                    key->state = KEY_DRIVER_FSM_DEBOUNCE;
                    key->timer = Sys_Timer_Get_Tick();
                } else {
                    if (click_only) {
                        key->event       = KEY_DRIVER_EVENT_CLICK;
                        key->click_count = 0;
                        key->state       = KEY_DRIVER_FSM_IDLE;
                    } else {
                        key->click_count++;
                        key->state = KEY_DRIVER_FSM_WAIT_DOUBLE;
                        key->timer = Sys_Timer_Get_Tick();
                    }
                }
            }
            break;

        case KEY_DRIVER_FSM_WAIT_DOUBLE:
            if (pressed) {
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            } else if (elapsed >= KEY_DRIVER_DOUBLE_WINDOW_MS) {
                key->event = (key->click_count >= 2)
                    ? KEY_DRIVER_EVENT_DOUBLE_CLICK
                    : KEY_DRIVER_EVENT_CLICK;
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_IDLE;
            }
            break;

        case KEY_DRIVER_FSM_LONG:
            if (!pressed) {
                key->click_count = 0;
                key->state       = KEY_DRIVER_FSM_IDLE;
            }
            break;
    }
}

void Key_Driver_Task(void)
{
    static uint32_t last = 0;
    uint8_t i;

    if (Sys_Timer_Get_Tick() - last < KEY_DRIVER_TASK_PERIOD_MS) return;
    last = Sys_Timer_Get_Tick();

    for (i = 0; i < KEY_DRIVER_COUNT; i++) {
        Update_Fsm(&s_keys[i]);
    }
}

void Key_Driver_Get_All_Events(Key_Driver_Event out[5])
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    {
        uint8_t i;
        for (i = 0; i < 5; i++) {
            out[i] = (Key_Driver_Event)s_keys[i].event;
            s_keys[i].event = KEY_DRIVER_EVENT_NONE;
        }
    }
    __set_PRIMASK(primask);
}
```

- [ ] **Step 3: 编译验证**

F7 编译。预期 Key_Driver 本身 0 错误。消费者（Sys_Core, Ui_Controller）会有编译错误——这是预期的。

- [ ] **Step 4: Commit**

```bash
git add Keil_Project/Hardware/Key_Driver.c Keil_Project/Hardware/Key_Driver.h
git commit -m "refactor: V5.0 — Key_Driver 4→5键, ID重命名, WITH_DOUBLE配置"
```

---

### Task 5: Sys_Core — 新增电源控制 + 移除 PB10 自动逻辑

**Files:**
- Modify: `Keil_Project/User/Sys_Core.h`
- Modify: `Keil_Project/User/Sys_Core.c`

**变更要点：**
- 新增 `Sys_Power_Control_Handle()` 处理 KEY0 事件
- 移除 `Sys_Safety_Task()` 中 PB10 电压自动阈值逻辑
- 各 `Sys_Run_*()` 中调用 `Sys_Power_Control_Handle()`, 将 KEY0 消费后清零
- KEY4 长按全局清 WiFi 逻辑从 Ui_Controller 移到此处 (KEY4=ID 4 的长按事件)

- [ ] **Step 1: 修改 Sys_Core.h**

在 `Keil_Project/User/Sys_Core.h` 的公开接口区域（`Sys_Safety_Reset_EMA` 声明之后）新增：

```c
/** @brief Handle KEY0 power toggle + coordinate PB10/PWM/POWER LED
 *  @param ke array of 5 key events from Key_Driver_Get_All_Events
 *  @note  Consumes ke[KEY_DRIVER_ID_POWER] after processing */
void Sys_Power_Control_Handle(Key_Driver_Event ke[5]);
```

同时在文件头部添加 `#include "Key_Driver.h"` 或前置声明 `Key_Driver_Event`。推荐在 `#include "stm32f10x.h"` 后加 `#include "Key_Driver.h"`。

- [ ] **Step 2: 修改 Sys_Core.c — 头部新增 include 和接线图更新**

在文件头部注释中，将接线表按键/灯区更新为：

```
 *    -- Keys --
 *    PB9=KEY0(电源) PB8=KEY1(返回) PB7=KEY2(UP) PB6=KEY3(DOWN) PB5=KEY4(确定)
 *    -- LEDs --
 *    PA15=STATUS(PWM指示) PB4=WIFI PB3=POWER(12V)
 *    -- Power control --
 *    PB10=PowerCtrl (KEY0 manual toggle, HIGH=12V enable)
```

- [ ] **Step 3: 修改 Sys_Hardware_Init — Key 配置**

将 `Sys_Hardware_Init()` 中的 Key_Driver_Configure 调用（第 103-104 行）：

```c
    Key_Driver_Configure(KEY_DRIVER_ID_PAGE, 1);
```

改为：

```c
    Key_Driver_Configure(KEY_DRIVER_ID_POWER,   KEY_DRIVER_CFG_CLICK_ONLY);
    Key_Driver_Configure(KEY_DRIVER_ID_BACK,    KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_UP,      KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_DOWN,    KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_CONFIRM, KEY_DRIVER_CFG_CLICK_ONLY);
```

- [ ] **Step 4: 修改 Sys_Safety_Task — 移除 PB10 自动逻辑**

删除 `Sys_Safety_Task()` 中 PB10 自动电压阈值控制块（第 205-214 行）：

```c
    /* PB10 电源控制 */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > SYS_SAFETY_POWER_V);
        ...
    }
```

同时删除不再需要的 `#define SYS_SAFETY_POWER_V 12.0f`（第 162 行）——如果该宏仅在此处使用的话。

- [ ] **Step 5: 新增 Sys_Power_Control_Handle 函数**

在 `Sys_Safety_Task()` 函数之后（约第 223 行之后）新增：

```c
/**
 * @brief  Handle KEY0 power toggle — hardware power switch
 * @note   KEY0 click: toggle PB10(12V) + POWER LED(PB3)
 *         Power ON  -> PB10 HIGH + POWER LED ON
 *         Power OFF -> force PWM stop + PB10 LOW + POWER LED OFF + STATUS LED OFF
 *         PWM restart requires KEY4 explicit action after power-on
 */
void Sys_Power_Control_Handle(Key_Driver_Event ke[5])
{
    if (ke[KEY_DRIVER_ID_POWER] != KEY_DRIVER_EVENT_CLICK) return;

    /* Read current PB10 state */
    if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_10) == Bit_RESET) {
        /* Power ON: enable 12V, light POWER LED */
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
        Led_Driver_Set_Power(1);
    } else {
        /* Power OFF: force stop PWM, disable 12V, extinguish POWER LED */
        Inverter_Control_Soft_Start_Stop();
        if (g_sys_state == SYS_STATE_RUNNING || g_sys_state == SYS_STATE_SWEEP) {
            g_sys_state = SYS_STATE_IDLE;
        }
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        Led_Driver_Set_Power(0);
        Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
    }

    /* Consume KEY0 event — do not propagate to UI */
    ke[KEY_DRIVER_ID_POWER] = KEY_DRIVER_EVENT_NONE;
}
```

需要在 Sys_Core.c 顶部新增 include：
```c
#include "Led_Driver.h"
```

（如果还没有的话）

- [ ] **Step 6: 更新 STATUS LED 在各 Sys_Run_*() 中**

在每个状态函数的开头（或 Led_Tick 之前），根据 `g_sys_state` 设置 STATUS LED：

`Sys_Run_Idle()` 函数开头新增：
```c
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
```

`Sys_Run_Sweep()` 函数开头新增：
```c
    Led_Driver_Set_Status(LED_DRIVER_STATE_SLOW);
```

`Sys_Run_Running()` 函数开头新增：
```c
    Led_Driver_Set_Status(LED_DRIVER_STATE_ON);
```

`Sys_Run_Fault()` 函数开头新增：
```c
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
```

同时从 `Sys_Post_Init()` 中删除 `Led_Driver_Set_System(1)`（第 117 行）——该函数已移除。

- [ ] **Step 7: 修改 Sys_Post_Init — 移除废弃 LED 调用**

删除 `Sys_Post_Init()` 第 117 行的：
```c
    Led_Driver_Set_System(1);
```

- [ ] **Step 8: 编译验证**

F7 编译。预期 0 错误 0 警告。

- [ ] **Step 9: Commit**

```bash
git add Keil_Project/User/Sys_Core.c Keil_Project/User/Sys_Core.h
git commit -m "feat: V5.0 — Sys_Power_Control_Handle + STATUS LED + 去PB10自动"
```

---

### Task 6: Ui_Controller — 键 ID 适配 + KEY1 双击主菜单 + LED 精简

**Files:**
- Modify: `Keil_Project/Hardware/Ui_Controller.c`

**变更要点：**
- `Handle_Keys_by_Page` 签名从 `(k0,k1,k2,k3)` 改为 `(k1,k2,k3,k4)` (只传 UI 键，不含 KEY0)
- k1=BACK(返回), k2=UP, k3=DOWN, k4=CONFIRM(确定)
- KEY1 双击 → 直接回主菜单
- `Update_Leds` 移除所有废弃调用，仅保留 `Led_Driver_Set_WiFi()`
- `Ui_Controller_Task` 中 `ke[4]`→`ke[5]`, 调用流程适配

- [ ] **Step 1: 修改 Update_Leds — 只保留 WiFi LED**

将 `Update_Leds()` 函数 (第 1444-1471 行) 替换为：

```c
/* ================================================================
 *  LED Update — V5.0: only WiFi LED managed by UI
 *  STATUS LED handled by Sys_Core per g_sys_state
 *  POWER LED handled by Sys_Power_Control_Handle per PB10
 * ================================================================ */
static void Update_Leds(void)
{
    uint8_t cs = App_Network_Get_Connect_Status();

    if (cs == APP_NETWORK_CONN_ONLINE)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
    else if (cs == APP_NETWORK_CONN_WIFI || cs == APP_NETWORK_CONN_MQTT)
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
    else if (App_Network_Is_Offline())
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
    else
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
}
```

- [ ] **Step 2: 修改 Handle_Keys_by_Page — 签名 + 键 ID 映射**

将函数签名 (第 1476-1478 行) 从：

```c
static void Handle_Keys_by_Page(Ui_Page page,
                                Key_Driver_Event k0, Key_Driver_Event k1,
                                Key_Driver_Event k2, Key_Driver_Event k3)
```

改为：

```c
static void Handle_Keys_by_Page(Ui_Page page,
                                Key_Driver_Event k1, Key_Driver_Event k2,
                                Key_Driver_Event k3, Key_Driver_Event k4)
```

**键映射变更** (旧→新):
- 旧 k0(PAGE/确定) → 新 k4(CONFIRM/确定)
- 旧 k1(F_UP) → 新 k2(UP/加)
- 旧 k2(F_DOWN) → 新 k3(DOWN/减)
- 旧 k3(ON/返回) → 新 k1(BACK/返回)

在整个函数体内进行以下替换：
- `k0` → `k4` (确定键)
- `k1` → `k2` (UP键)
- `k2` → `k3` (DOWN键)
- `k3` → `k1` (返回键)

具体替换区域：
1. 第 1487 行: `if (k1 == KEY_DRIVER_EVENT_CLICK)` → `if (k2 == KEY_DRIVER_EVENT_CLICK)` (UP 键)
2. 第 1517 行: `if (k2 == KEY_DRIVER_EVENT_CLICK)` → `if (k3 == KEY_DRIVER_EVENT_CLICK)` (DOWN 键)
3. 第 1550 行: `if (k0 == KEY_DRIVER_EVENT_CLICK)` → `if (k4 == KEY_DRIVER_EVENT_CLICK)` (确定键)
4. 第 1634 行: `if (k3 == KEY_DRIVER_EVENT_CLICK)` → `if (k1 == KEY_DRIVER_EVENT_CLICK)` (返回键)

- [ ] **Step 3: 新增 KEY1 双击回主菜单**

在返回键的 CLICK 处理 (原第 1634-1658 行区域) 中，新增双击处理。在 CLICK 处理之前添加：

```c
    /* BACK (k1): double-click -> jump to MAIN MENU directly */
    if (k1 == KEY_DRIVER_EVENT_DOUBLE_CLICK) {
        s_page = UI_PAGE_MAIN_MENU;
        s_menu_cursor = 0;
        s_setting_cursor = 0;
        return;  /* skip CLICK processing below */
    }

    /* BACK (k1): single click -> return to previous page */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        switch (page) {
            ...
```

同时需要在函数开头新增 early return 结构——如果双击事件触发，直接跳主菜单并 return，不再处理后续 CLICK。

- [ ] **Step 4: 修改 Handle_Settings_Keys 和子页面处理函数**

`Handle_Setting_Keys` (第 1698-1699 行): 签名从 `(k0,k1,k2,k3)` 改为 `(k1,k2,k3,k4)`，对应键映射同 Step 2。

`Handle_Lang_Keys` (第 1779-1780 行): 同上。

`Handle_Icons_Keys` (第 1910-1911 行): 同上。

`Handle_Spacing_Keys` (第 2023-2024 行): 同上。

`Handle_BL_Sub_Keys` (第 2085-2086 行): 同上。

`Handle_BL_Manual_Keys` (第 2130-2131 行): 同上。

每个函数内部的键变量映射：
- `k0` (ok/确定) → `k4`
- `k1` (up) → `k2`
- `k2` (down) → `k3`
- `k3` (back) → `k1`

- [ ] **Step 5: 修改 Ui_Controller_Task — 数组 + 调用流程**

将 Phase 3 中的键数组 (第 2453-2476 行)：

从：
```c
        Key_Driver_Event ke[4];
        Key_Driver_Get_All_Events(ke);
        /* global long-press: clear WiFi */
        if (ke[0] == KEY_DRIVER_EVENT_LONG_PRESS) {
            ...
        }
        /* Settings key dispatch */
        if (!Handle_Settings_Keys(s_page, ke[0], ke[1], ke[2], ke[3])) {
            Handle_Keys_by_Page(s_page, ke[0], ke[1], ke[2], ke[3]);
        }
```

改为：
```c
        Key_Driver_Event ke[5];
        Key_Driver_Get_All_Events(ke);

        /* KEY0 (POWER) handled by Sys_Core — Ui_Controller never sees it */
        Sys_Power_Control_Handle(ke);

        /* KEY4 long-press: clear WiFi — must fire BEFORE Settings intercept */
        if (ke[KEY_DRIVER_ID_CONFIRM] == KEY_DRIVER_EVENT_LONG_PRESS) {
            Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
            if (!(ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE)) {
                Esp8266_Driver_Send_String("CMD:CLEAR\n");
                App_Network_Manual_Disconnect();
                s_no_wifi_mode = 1;
                if (s_page != UI_PAGE_WIFI_SETUP && s_page != UI_PAGE_FAULT) {
                    s_page = UI_PAGE_WIFI_SETUP;
                }
                Reset_EMA();
            }
        }
        /* Settings key dispatch (k1=BACK, k2=UP, k3=DOWN, k4=CONFIRM) */
        if (!Handle_Settings_Keys(s_page, ke[KEY_DRIVER_ID_BACK],
                                   ke[KEY_DRIVER_ID_UP],
                                   ke[KEY_DRIVER_ID_DOWN],
                                   ke[KEY_DRIVER_ID_CONFIRM])) {
            Handle_Keys_by_Page(s_page, ke[KEY_DRIVER_ID_BACK],
                                ke[KEY_DRIVER_ID_UP],
                                ke[KEY_DRIVER_ID_DOWN],
                                ke[KEY_DRIVER_ID_CONFIRM]);
        }
```

- [ ] **Step 6: 更新 Update_Leds 调用点**

第 2541 行: `Update_Leds(s_page)` → `Update_Leds()`
第 2590 行: `Update_Leds(s_page)` → `Update_Leds()`

- [ ] **Step 7: 编译验证**

F7 编译。预期 0 错误 0 警告。

- [ ] **Step 8: Commit**

```bash
git add Keil_Project/Hardware/Ui_Controller.c
git commit -m "refactor: V5.0 — Ui_Controller 键ID适配 + KEY1双击主菜单 + LED精简"
```

---

### Task 7: App_Network — 移除 Led_Driver_Set_Com 调用

**Files:**
- Modify: `Keil_Project/User/App_Network.c`

- [ ] **Step 1: 确认无 Led_Driver_Set_Com 调用**

从之前的 grep 结果看，App_Network.c 中只使用了 `Led_Driver_Set_WiFi()`，没有 `Led_Driver_Set_Com()` 调用。但需要确认 `Led_Driver_Set_Power()` 也没有被调用。

执行检查后，如果确实没有调用，此 Task 为零改动——直接跳过。

- [ ] **Step 2: 编译验证**

F7 编译。预期 0 错误 0 警告。

- [ ] **Step 3: Commit** (如果需要改动)

```bash
# 仅当有改动时执行
git add Keil_Project/User/App_Network.c
git commit -m "refactor: V5.0 — App_Network 清理废弃 LED 调用"
```

---

### Task 8: main.c — 接线表注释更新

**Files:**
- Modify: `Keil_Project/User/main.c`

- [ ] **Step 1: 更新接线表**

将 `main.c` 文件头部接线表（第 7-29 行）替换为 V5.0 版本：

```c
 *  系统总接线表 (全部使用引脚, 48 脚 LQFP):
 *  +------------------------------------------------------------+
 *  |   引脚  功能               引脚  功能                       |
 *  |   ----  -----------------  ----  -----------------          |
 *  |    PA0   TFT_RST            PB0   ADC_CH8  (电流 CC6920BSO) |
 *  |    PA1   ESP8266 RST        PB1   ADC_CH9  (电压分压)       |
 *  |    PA2   USART2_TX          PB3   LED_POWER (绿, 12V指示)   |
 *  |    PA3   USART2_RX          PB4   LED_WIFI  (蓝, WiFi状态)  |
 *  |    PA4   TFT_CS             PB5   KEY4 (IPU, 确定/启停)     |
 *  |    PA5   SPI1_SCK           PB6   KEY3 (IPU, DOWN/减)       |
 *  |    PA6   TFT_DC/Flash MISO  PB7   KEY2 (IPU, UP/加)         |
 *  |    PA7   SPI1_MOSI          PB8   KEY1 (IPU, 返回)          |
 *  |    PA8   TIM1_CH1 (HINA)    PB9   KEY0 (IPU, 电源开关)      |
 *  |    PA9   TIM1_CH2 (HINB)    PB10  PowerCtrl (KEY0 手动)     |
 *  |    PA12  TFT_BL (GPIO)      PB11  ESP8266 EN                |
 *  |    PA15  LED_STATUS (黄)    PB12  W25Q128_CS                |
 *  |                              PB13  TIM1_CH1N (LINA)          |
 *  |                              PB14  TIM1_CH2N (LINB)          |
 *  |                              PB15  Buzzer (有源蜂鸣器)       |
 *  |                                                             |
 *  |    电源: VDD=3.3V, VDDA=3.3V, VBAT=3.3V                     |
 *  |    时钟: HSE=8MHz -> PLL=72MHz (SYSCLK)                     |
 *  |    JTAG 禁用: PB3/PB4/PA15 释放为 GPIO                      |
 *  |    看门狗: IWDG 1.6s, 调试自动暂停                          |
 *  +------------------------------------------------------------+
```

- [ ] **Step 2: 更新版本号和 @brief**

将 `@brief` 行从 `V4.5.2` 改为 `V5.0`。

- [ ] **Step 3: 编译验证**

F7 编译，全项目最终验证。预期 **0 错误 0 警告**。

- [ ] **Step 4: Commit**

```bash
git add Keil_Project/User/main.c
git commit -m "docs: V5.0 — main.c 接线表更新"
```

---

### Task 9: 全项目最终编译 + 清理

- [ ] **Step 1: 清理编译产物**

```bash
cmd.exe /c Keil_Project\keilkill.bat
```

- [ ] **Step 2: Keil F7 全量编译**

在 Keil IDE 中 Rebuild All (或 F7)。确认 **0 错误 0 警告**。

- [ ] **Step 3: 检查 git status**

```bash
git status
```

确认仅有预期的 9 个文件被修改，无编译产物残留。

- [ ] **Step 4: 最终 Commit (如有遗漏)**

```bash
git add -A
git commit -m "chore: V5.0 — 全项目编译验证通过, 清理编译产物"
```

---

## 任务依赖图

```
Task 1 (Led_Driver) ──┐
Task 2 (W25Q_Driver)  │
Task 3 (Tft_Driver)   ├──> Task 5 (Sys_Core) ──> Task 6 (Ui_Controller) ──> Task 8 (main.c) ──> Task 9 (验证)
Task 4 (Key_Driver)  ──┘                        Task 7 (App_Network) ──────┘
```

Task 1-4 无依赖，可并行。Task 5 依赖 1+4。Task 6 依赖 4+5。Task 7 独立。Task 8 依赖全部。Task 9 收尾。

**建议顺序执行**（而非并行），因为每个 Task 都可能导致后续消费者编译报错，需要逐个修复。

---

## 文件改动汇总

| 文件 | 改动行数 (估计) | 类型 |
|------|----------------|------|
| `Led_Driver.h` | ~45 行 (全量重写) | API 简化 |
| `Led_Driver.c` | ~160 行 (全量重写) | 实现重写 |
| `W25Q_Driver.c` | ~3 行 | 宏修改 |
| `Tft_Driver.c` | ~30 行 | 去 TIM4 + BL GPIO 化 |
| `Key_Driver.h` | ~50 行 (全量重写) | API 重构 |
| `Key_Driver.c` | ~200 行 (全量重写) | 5 键扩展 |
| `Sys_Core.h` | ~5 行 | 新增声明 |
| `Sys_Core.c` | ~60 行 | 新增函数 + 删旧逻辑 |
| `Ui_Controller.c` | ~80 行 | 键 ID 适配 + LED 精简 |
| `App_Network.c` | ~0 行 | 无改动 |
| `main.c` | ~30 行 | 注释更新 |
| **合计** | **~660 行** | |
