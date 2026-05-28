/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — 实现
 * @note    每键独立状态机: IDLE → DEBOUNCE → PRESS → WAIT_DOUBLE → LONG_PRESS
 *          Key_Driver_Task 按 10ms 节拍轮询, Key_Driver_Get_Event 消费事件
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_COUNT           2
#define KEY_DEBOUNCE_MS     10
#define KEY_LONG_PRESS_MS   3000
#define KEY_DOUBLE_WINDOW_MS 200
#define KEY_TASK_PERIOD_MS  10

/* ── 按键状态机 ── */
typedef enum {
    KEY_DRIVER_FSM_IDLE = 0,
    KEY_DRIVER_FSM_DEBOUNCE,
    KEY_DRIVER_FSM_PRESS,
    KEY_DRIVER_FSM_WAIT_DOUBLE,
    KEY_DRIVER_FSM_LONG_HOLD
} Key_Fsm_State;

typedef struct {
    GPIO_TypeDef*    port;
    uint16_t         pin;
    Key_Fsm_State    state;
    uint32_t         timer;
    uint8_t          event;        /* 待消费事件: Key_Driver_Event */
    uint8_t          click_count;
} Key_Instance;

static Key_Instance s_keys[KEY_COUNT] = {
    { GPIOB, GPIO_Pin_12, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_13, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 }
};

void Key_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin  = GPIO_Pin_12 | GPIO_Pin_13;
    cfg.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &cfg);
}

static void Key_Fsm_Update(Key_Instance* key)
{
    uint8_t pressed = (GPIO_ReadInputDataBit(key->port, key->pin) == Bit_RESET);

    switch (key->state) {
        case KEY_DRIVER_FSM_IDLE:
            if (pressed) {
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            }
            break;

        case KEY_DRIVER_FSM_DEBOUNCE:
            if (Sys_Timer_Get_Tick() - key->timer >= KEY_DEBOUNCE_MS) {
                if (pressed) {
                    key->state = KEY_DRIVER_FSM_PRESS;
                    key->timer = Sys_Timer_Get_Tick();
                } else {
                    key->state = KEY_DRIVER_FSM_IDLE;
                }
            }
            break;

        case KEY_DRIVER_FSM_PRESS:
            if (!pressed) {  /* 释放 */
                key->click_count++;
                key->state = KEY_DRIVER_FSM_WAIT_DOUBLE;
                key->timer = Sys_Timer_Get_Tick();
            } else if (Sys_Timer_Get_Tick() - key->timer >= KEY_LONG_PRESS_MS) {
                key->event = KEY_DRIVER_EVENT_LONG_PRESS;
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_LONG_HOLD;
            }
            break;

        case KEY_DRIVER_FSM_WAIT_DOUBLE:
            if (pressed) {
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            } else if (Sys_Timer_Get_Tick() - key->timer >= KEY_DOUBLE_WINDOW_MS) {
                key->event = (key->click_count >= 2) ? KEY_DRIVER_EVENT_DOUBLE_CLICK : KEY_DRIVER_EVENT_CLICK;
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_IDLE;
            }
            break;

        case KEY_DRIVER_FSM_LONG_HOLD:
            if (!pressed) {
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_IDLE;
            }
            break;
    }
}

void Key_Driver_Task(void)
{
    static uint32_t last = 0;
    uint8_t i;

    if (Sys_Timer_Get_Tick() - last < KEY_TASK_PERIOD_MS) return;
    last = Sys_Timer_Get_Tick();

    for (i = 0; i < KEY_COUNT; i++) {
        Key_Fsm_Update(&s_keys[i]);
    }
}

Key_Driver_Event Key_Driver_Get_Event(uint8_t key_id)
{
    Key_Driver_Event evt;
    if (key_id >= KEY_COUNT) return KEY_DRIVER_EVENT_NONE;

    __disable_irq();
    evt = (Key_Driver_Event)s_keys[key_id].event;
    s_keys[key_id].event = KEY_DRIVER_EVENT_NONE;
    __enable_irq();
    return evt;
}
