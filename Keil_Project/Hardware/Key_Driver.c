/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   按键驱动 — V4.3.2 (4 keys)
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

/* PB5=确定(启停), PB8=频率+, PB7=频率-, PB9=返回 */
static Key_Driver_Instance s_keys[KEY_DRIVER_COUNT] = {
    { GPIOB, GPIO_Pin_5, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_8, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_7, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 },
    { GPIOB, GPIO_Pin_9, KEY_DRIVER_FSM_IDLE, 0, KEY_DRIVER_EVENT_NONE, 0 }
};

/** @brief 初始化 4 键 GPIO: PB5/PB7/PB8/PB9 全部 IPU 上拉 */
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

/** @brief 周期扫描 4 键 FSM (每 10ms), 自动去抖+单击/双击/长按判定 */
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

/** @brief 批量读取 4 键事件 (单次临界区, 阅后即焚, 减少 IRQ 抖动) */
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
