/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — 实现 (V6.2 4 键版)
 * @note    PB9=ON/OFF, PB8=F_UP, PB7=F_DOWN, PB5=PAGE
 *          每键独立状态机: IDLE → DEBOUNCE → PRESS → WAIT_DOUBLE → LONG_HOLD
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_COUNT           4
#define KEY_DEBOUNCE_MS     10
#define KEY_LONG_PRESS_MS   3000
#define KEY_DOUBLE_WINDOW_MS 200
#define KEY_TASK_PERIOD_MS  10

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
    uint8_t          event;
    uint8_t          click_count;
} Key_Instance;

/* V6.2: PB9=ON/OFF, PB8=F_UP, PB7=F_DOWN, PB5=PAGE */
static Key_Instance s_keys[KEY_COUNT] = {
    { GPIOB, GPIO_Pin_9,  KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_8,  KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_7,  KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_5,  KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 }
};

void Key_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
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
            if (!pressed) {
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
    uint32_t primask;
    if (key_id >= KEY_COUNT) return KEY_DRIVER_EVENT_NONE;

    primask = __get_PRIMASK();
    __disable_irq();
    evt = (Key_Driver_Event)s_keys[key_id].event;
    s_keys[key_id].event = KEY_DRIVER_EVENT_NONE;
    __set_PRIMASK(primask);
    return evt;
}
