/**
 ******************************************************************************
 * @file    Hardware/Key_Driver.c
 * @brief   五键扫描与按键事件驱动 — V5.0.2
 *
 *  硬件连接（全部使用内部上拉，按下时为低电平）:
 *  +----------------------------------------------------------+
 *  |                      STM32F103C8T6                        |
 *  |                                                           |
 *  |    PB9  --- KEY0 --- GND    电源开关                       |
 *  |    PB8  --- KEY1 --- GND    返回                           |
 *  |    PB7  --- KEY2 --- GND    上移或增加                     |
 *  |    PB6  --- KEY3 --- GND    下移或减少                     |
 *  |    PB5  --- KEY4 --- GND    确定或启停                     |
 *  |                                                           |
 *  |    单键状态机：消抖10ms，双击窗口200ms，长按阈值3s        |
 *  |    五键事件在同一临界区内批量读取，避免跨键竞争           |
 *  +----------------------------------------------------------+
 *
 * @note    KEY0的电源事件由系统核心层优先消费，不传递给页面逻辑。
 ******************************************************************************
 */

#include "Key_Driver.h"
#include "Sys_Timer.h"

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

/* 五个按键依次连接PB9、PB8、PB7、PB6和PB5。 */
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
 * @brief  配置指定按键允许产生的扩展事件
 * @param  key_id 按键编号：0为电源，1为返回，2为上移，3为下移，4为确定
 * @param  config 双击与长按能力标志的按位组合
 */
void Key_Driver_Configure(uint8_t key_id, uint8_t config)
{
    if (key_id < KEY_DRIVER_COUNT) {
        s_keys[key_id].flags = (uint8_t)(config &
            (KEY_DRIVER_CFG_DOUBLE_ENABLE | KEY_DRIVER_CFG_LONG_ENABLE));
    }
}

/* 单键状态机分别处理单击、双击和长按路径。 */
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
    uint32_t primask;

    /* 调用方未提供事件数组时保留现有事件，避免误清除尚未消费的按键。 */
    if (out == 0) return;

    primask = __get_PRIMASK();
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
