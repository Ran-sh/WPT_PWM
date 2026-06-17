/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — 实现 (V4.2.0 4 键版)
 * @note    PB9=ON/OFF, PB8=F_UP, PB7=F_DOWN, PB5=PAGE
 *          每键独立状态机: IDLE → DEBOUNCE → PRESS → WAIT_DOUBLE → LONG
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_DRIVER_COUNT            4
#define KEY_DRIVER_DEBOUNCE_MS      10
#define KEY_DRIVER_LONG_PRESS_MS    3000
#define KEY_DRIVER_DOUBLE_WINDOW_MS 200
#define KEY_DRIVER_TASK_PERIOD_MS   10

typedef enum {
    KEY_DRIVER_FSM_IDLE = 0,
    KEY_DRIVER_FSM_DEBOUNCE,
    KEY_DRIVER_FSM_PRESS,
    KEY_DRIVER_FSM_WAIT_DOUBLE,
    KEY_DRIVER_FSM_LONG
} Key_Driver_Fsm_State;

typedef struct {
    GPIO_TypeDef*      port;
    uint16_t           pin;
    Key_Driver_Fsm_State state;
    uint32_t           timer;
    uint8_t            event;
    uint8_t            click_count;
} Key_Driver_Instance;

/* PB9=启停, PB8=频率+, PB7=频率-, PB5=翻页 */
static Key_Driver_Instance s_keys[KEY_DRIVER_COUNT] = {
    { GPIOB, GPIO_Pin_9, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_8, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_7, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_5, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 }
};

void Key_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    cfg.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    cfg.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &cfg);
}

/* 单键去抖 FSM: IDLE→DEBOUNCE(10ms)→PRESS(释放→WAIT_DOUBLE(200ms)判定单击/双击; 按住→LONG(3s)) */
static void Update_Fsm(Key_Driver_Instance* key)
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
            if (Sys_Timer_Get_Tick() - key->timer >= KEY_DRIVER_DEBOUNCE_MS) {
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
            } else if (Sys_Timer_Get_Tick() - key->timer >= KEY_DRIVER_LONG_PRESS_MS) {
                key->event = KEY_DRIVER_EVENT_LONG_PRESS;  /* 长按 3s 触发 */
                key->click_count = 0;
                key->state = KEY_DRIVER_FSM_LONG;
            }
            break;

        case KEY_DRIVER_FSM_WAIT_DOUBLE:
            if (pressed) {
                key->state = KEY_DRIVER_FSM_DEBOUNCE;
                key->timer = Sys_Timer_Get_Tick();
            } else if (Sys_Timer_Get_Tick() - key->timer >= KEY_DRIVER_DOUBLE_WINDOW_MS) {
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
                key->state = KEY_DRIVER_FSM_IDLE;
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

Key_Driver_Event Key_Driver_Get_Event(uint8_t key_id)
{
    Key_Driver_Event evt;
    uint32_t primask;
    if (key_id >= KEY_DRIVER_COUNT) return KEY_DRIVER_EVENT_NONE;

    primask = __get_PRIMASK();
    __disable_irq();
    evt = (Key_Driver_Event)s_keys[key_id].event;
    s_keys[key_id].event = KEY_DRIVER_EVENT_NONE;
    __set_PRIMASK(primask);
    return evt;
}
