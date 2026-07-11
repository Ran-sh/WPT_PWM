/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — V4.5.2 (4 keys)
 *
 *  Pinout (4 keys, all IPU pull-up, press = LOW):
 *  +----------------------------------------------------------+
 *  |                      STM32F103C8T6                        |
 *  |                                                           |
 *  |    PB8  --- IPU ---+--- Key --- GND    F_UP    (频率+)      |
 *  |    PB7  --- IPU ---+--- Key --- GND    F_DOWN  (频率-)      |
 *  |    PB5  --- IPU ---+--- Key --- GND    PAGE   (确定/启停)   |
 *  |    PB9  --- IPU ---+--- Key --- GND    ON     (返回)         |
 *  |                                                           |
 *  |    Per-key FSM: IDLE -> DEBOUNCE(10ms) -> PRESS           |
 *  |      -> WAIT_DOUBLE(200ms) -> LONG(3s)                    |
 *  |    Batch read: Key_Driver_Read_Batch merges critical sec  |
 *  +----------------------------------------------------------+
 *
 * @note    PB5=PAGE(确定/启停), PB9=ON(返回), PB8=F_UP, PB7=F_DOWN
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_DRIVER_COUNT               4
#define KEY_DRIVER_DEBOUNCE_MS         10
#define KEY_DRIVER_RELEASE_DEBOUNCE_MS 12   /* rising-edge debounce for release */
#define KEY_DRIVER_LONG_PRESS_MS       3000
#define KEY_DRIVER_DOUBLE_WINDOW_MS    200
#define KEY_DRIVER_TASK_PERIOD_MS      10

typedef enum {
    KEY_DRIVER_FSM_IDLE = 0,
    KEY_DRIVER_FSM_DEBOUNCE,
    KEY_DRIVER_FSM_PRESS,
    KEY_DRIVER_FSM_RELEASE_DEBOUNCE,  /* rising-edge debounce on release */
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
    uint8_t             flags;          /* bit0=no_double: skip double-click window */
} Key_Driver_Instance;

/* PB5=确定(启停), PB8=频率+, PB7=频率-, PB9=返回 */
/* flags bit0=no_double: 0=allow double-click, 1=fire CLICK immediately on release */
static Key_Driver_Instance s_keys[KEY_DRIVER_COUNT] = {
    { GPIOB, GPIO_Pin_5, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_8, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_7, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 },
    { GPIOB, GPIO_Pin_9, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0, 0 }
};

void Key_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    cfg.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &cfg);
}

/**
 * @brief  配置按键选项
 * @param  key_id    按键编号 (0=PAGE/确定, 1=F_UP, 2=F_DOWN, 3=ON/返回)
 * @param  no_double 1=跳过双击检测, 释放去抖后立即发 CLICK (启停键用)
 */
void Key_Driver_Configure(uint8_t key_id, uint8_t no_double)
{
    if (key_id < KEY_DRIVER_COUNT) {
        if (no_double) s_keys[key_id].flags |= 0x01;
        else           s_keys[key_id].flags &= ~0x01;
    }
}

/* 单键 FSM — 含释放沿去抖 + no_double 快速路径
 *   IDLE → DEBOUNCE(10ms) → PRESS
 *     → RELEASE_DEBOUNCE(12ms) → [no_double? → CLICK → IDLE]
 *                              → [else → WAIT_DOUBLE(200ms) → CLICK/DOUBLE_CLICK → IDLE]
 *     → LONG(3s) → LONG_PRESS (按住不松触发) */
static void Update_Fsm(Key_Driver_Instance* key)
{
    uint8_t  pressed     = (GPIO_ReadInputDataBit(key->port, key->pin) == Bit_RESET);
    uint8_t  no_double   = (key->flags & 0x01);
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
                    key->timer = Sys_Timer_Get_Tick();  /* reset for long-press timer */
                } else {
                    key->state = KEY_DRIVER_FSM_IDLE;   /* glitch — discard */
                }
            }
            break;

        case KEY_DRIVER_FSM_PRESS:
            if (pressed) {
                /* held — check for long-press */
                if (elapsed >= KEY_DRIVER_LONG_PRESS_MS) {
                    key->event       = KEY_DRIVER_EVENT_LONG_PRESS;
                    key->click_count = 0;
                    key->state       = KEY_DRIVER_FSM_LONG;
                }
            } else {
                /* released — enter release debounce (Bug 2: was trusting raw release) */
                key->state = KEY_DRIVER_FSM_RELEASE_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            }
            break;

        case KEY_DRIVER_FSM_RELEASE_DEBOUNCE:
            if (elapsed >= KEY_DRIVER_RELEASE_DEBOUNCE_MS) {
                if (pressed) {
                    /* bounced back low — go to press debounce */
                    key->state = KEY_DRIVER_FSM_DEBOUNCE;
                    key->timer = Sys_Timer_Get_Tick();
                } else {
                    /* truly released — fast path for critical keys (Bug 1) */
                    if (no_double) {
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
                /* second press detected → debounce it */
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            } else if (elapsed >= KEY_DRIVER_DOUBLE_WINDOW_MS) {
                /* timeout — fire single or double click */
                key->event = (key->click_count >= 2)
                    ? KEY_DRIVER_EVENT_DOUBLE_CLICK
                    : KEY_DRIVER_EVENT_CLICK;
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_IDLE;
            }
            break;

        case KEY_DRIVER_FSM_LONG:
            /* wait for release before allowing new events */
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

void Key_Driver_Get_All_Events(Key_Driver_Event out[4])
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    {
        uint8_t i;
        for (i = 0; i < 4; i++) {
            out[i] = (Key_Driver_Event)s_keys[i].event;
            s_keys[i].event = KEY_DRIVER_EVENT_NONE;
        }
    }
    __set_PRIMASK(primask);
}
