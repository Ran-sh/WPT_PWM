/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — V5.0.1 (5 keys)
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
 *  |    Batch read: Key_Driver_Get_All_Events in critical sec   |
 *  +----------------------------------------------------------+
 *
 * @note    V5.0: 5 keys, KEY0=power hardware switch (handled by Sys_Core)
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

#define KEY_DRIVER_COUNT               5U
#define KEY_DRIVER_DEBOUNCE_MS         10U
#define KEY_DRIVER_RELEASE_DEBOUNCE_MS 12U
#define KEY_DRIVER_LONG_PRESS_MS       3000U
#define KEY_DRIVER_DOUBLE_WINDOW_MS    200U
#define KEY_DRIVER_TASK_PERIOD_MS      10U

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
    uint8_t             flags;
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
 * @param  config   Independent DOUBLE_ENABLE/LONG_ENABLE capability bits
 */
void Key_Driver_Configure(uint8_t key_id, uint8_t config)
{
    if (key_id < KEY_DRIVER_COUNT) {
        s_keys[key_id].flags = (uint8_t)(config &
            (KEY_DRIVER_CFG_DOUBLE_ENABLE | KEY_DRIVER_CFG_LONG_ENABLE));
    }
}

/* Single-key FSM with independent double-click and long-press paths. */
static void Key_Driver_Update_Fsm(Key_Driver_Instance* key)
{
    uint8_t pressed;
    uint8_t double_enabled;
    uint8_t long_enabled;
    uint32_t elapsed;

    pressed = (GPIO_ReadInputDataBit(key->port, key->pin) == Bit_RESET) ?
              1U : 0U;
    double_enabled = (key->flags & KEY_DRIVER_CFG_DOUBLE_ENABLE) != 0U ?
                     1U : 0U;
    long_enabled = (key->flags & KEY_DRIVER_CFG_LONG_ENABLE) != 0U ?
                   1U : 0U;
    elapsed = Sys_Timer_Get_Tick() - key->timer;

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
                if (long_enabled != 0U &&
                    elapsed >= KEY_DRIVER_LONG_PRESS_MS) {
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
                    if (double_enabled == 0U) {
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
        Key_Driver_Update_Fsm(&s_keys[i]);
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
