/**
 ******************************************************************************
 * @file    Hardware/KEY.c
 * @brief   物理按键驱动 —— 单击/双击识别
 * @note    存放路径: 项目根目录\Hardware\
 *          硬件接口:
 *            KEY0 → PB12 (内部上拉, 按下接 GND, 释放悬空 = 3.3V)
 *            KEY1 → PB13 (内部上拉, 按下接 GND, 释放悬空 = 3.3V)
 *          调用周期: KEY_Scan_All() 由 KEY_Task() 通过 SysTimer 时间戳差值法每 10ms 调用
 *                    所有时序依赖此 10ms 节拍 (不可更改!)
 *
 *          状态机设计:
 *            IDLE → DEBOUNCE → PRESSED → WAIT_RELEASE
 *                                         ├→ WAIT_DOUBLE (200ms 窗口)
 *                                         │    ├→ 再次按下 → WAIT_DOUBLE_REL → IDLE (双击!)
 *                                         │    └→ 超时无按压 → IDLE (单击!)
 *                                         └→ PRESSED (仍按住)
 *
 *          事件投递: 单击=1, 双击=2, 读取后自动清零 (阅后即焚机制)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "KEY.h"
#include "SysTimer.h"

/* ── 按键数量 (物理接线数) ── */
#define KEY_COUNT 2

/*
 * 按键状态机 —— 七态模型
 *
 *    IDLE ──(电平=低)──→ DEBOUNCE ──(仍低)──→ PRESSED
 *                                                     │
 *      ←── 投递单击=1 ── IDLE ←──(超时200ms)── WAIT_DOUBLE ←──(电平变高)── WAIT_RELEASE
 *                                                     │ ↑
 *      ←── 投递双击=2 ── IDLE ←── WAIT_DOUBLE_REL ←──┘ (再次按下又释放)
 *
 *  核心思路:
 *  - 第一次释放后不立刻判单击，而是进入 WAIT_DOUBLE 态开 200ms 时间窗
 *  - 窗口期内如检测到第二次按下→释放，则判双击
 *  - 窗口期超时且无第二次按下，确认单击
 */
typedef enum {
    KEY_STATE_IDLE = 0,         /* 空闲态: 等待首次按下 */
    KEY_STATE_DEBOUNCE,         /* 去抖态: 确认不是毛刺 */
    KEY_STATE_PRESSED,          /* 已按下: 等待释放 */
    KEY_STATE_WAIT_RELEASE,     /* 确认释放: 第一次松手 */
    KEY_STATE_WAIT_DOUBLE,      /* 双击窗口: 200ms 内等待第二次按下 */
    KEY_STATE_WAIT_DOUBLE_REL   /* 等待第二次松手: 确认双击完成 */
} KeyState_t;

/*
 * 按键控制块
 *
 * 字段说明:
 *   Port/ Pin : GPIO 端口和引脚号
 *   State     : 当前状态机所处状态 (见 KeyState_t)
 *   EventFlag : 0=无事件, 1=单击, 2=双击 (由扫描函数投递, 主循环通过 KEY_Get_Event 读取)
 *   Timer     : 双击窗口倒计时计数器 (单位: 10ms, 由 SysTick 每周期递进)
 */
typedef struct {
    GPIO_TypeDef* Port;
    uint16_t      Pin;
    KeyState_t    State;
    uint8_t       EventFlag;
    uint16_t      Timer;
} Button_t;

/*
 * 按键实例数组
 *
 * [0] → KEY0 (PB12): 软开关机 / 翻页 (双击)
 * [1] → KEY1 (PB13): 调频率 / 功能调节 (单击)
 */
static Button_t Key_List[KEY_COUNT] = {
    {GPIOB, GPIO_Pin_12, KEY_STATE_IDLE, 0, 0},
    {GPIOB, GPIO_Pin_13, KEY_STATE_IDLE, 0, 0}
};

/**
 * @brief  按键 GPIO 初始化
 * @note   PB12 / PB13 均配置为内部上拉输入 (IPU)
 *         按下时引脚被拉至 GND (电平=0), 释放时内部上拉至 3.3V (电平=1)
 */
void KEY_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_Init_Structure;
    GPIO_Init_Structure.GPIO_Mode  = GPIO_Mode_IPU;           /* 内部上拉输入: 默认高电平 */
    GPIO_Init_Structure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init_Structure.GPIO_Pin   = GPIO_Pin_12 | GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_Init_Structure);
}

/**
 * @brief  按键批量扫描 (需由 SysTick_Handler 每 10ms 调用一次)
 * @note   核心时序:
 *         1. 读取引脚电平 → 驱动状态机运转
 *         2. 双击判定窗口 = Timer 计数到 20 (= 200ms / 10ms)
 *         3. 消抖只需连续两个周期确认 (IDLE→DEBOUNCE→PRESSED)
 *
 *         【严重警告】调用间隔必须严格 10ms!
 *         如果间隔漂移, 双击窗口 200ms 会失真, 用户体验会极差。
 */
void KEY_Scan_All(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++)
    {
        /* 读取当前引脚电平: 0=按下(GND), 1=释放(3.3V) */
        uint8_t level = GPIO_ReadInputDataBit(Key_List[i].Port, Key_List[i].Pin);

        switch (Key_List[i].State)
        {
            /* ── 空闲态: 检测到低电平 → 进入去抖 ── */
            case KEY_STATE_IDLE:
                if (level == 0) Key_List[i].State = KEY_STATE_DEBOUNCE;
                break;

            /* ── 去抖态: 连续两次确认低电平才是真按下 ── */
            case KEY_STATE_DEBOUNCE:
                if (level == 0) Key_List[i].State = KEY_STATE_PRESSED;
                else            Key_List[i].State = KEY_STATE_IDLE;  /* 毛刺, 忽略 */
                break;

            /* ── 已按下: 等待释放 ── */
            case KEY_STATE_PRESSED:
                if (level == 1) Key_List[i].State = KEY_STATE_WAIT_RELEASE;
                break;

            /* ── 确认第一次释放: 进入双击窗口期 ── */
            case KEY_STATE_WAIT_RELEASE:
                if (level == 1) {
                    /* 第一次真正松手了! 开启 200ms 倒计时窗口, 等等看会不会按第二次 */
                    Key_List[i].State = KEY_STATE_WAIT_DOUBLE;
                    Key_List[i].Timer = 0;
                } else {
                    /* 又检测到低电平, 回到按下状态 (可能是抖动) */
                    Key_List[i].State = KEY_STATE_PRESSED;
                }
                break;

            /* ── 双击窗口期: 守株待兔等第二次按下 ── */
            case KEY_STATE_WAIT_DOUBLE:
                Key_List[i].Timer++;  /* 每 10ms 累加 1 */

                if (level == 0) {
                    /* 200ms 窗口内又按下了! 这就是双击! 等待最终释放 */
                    Key_List[i].State = KEY_STATE_WAIT_DOUBLE_REL;
                }
                else if (Key_List[i].Timer >= 20) {
                    /* 过了 200ms (20 × 10ms) 还没按第二下, 确认只是单击 */
                    Key_List[i].EventFlag = 1;     /* 投递事件: 单击 */
                    Key_List[i].State = KEY_STATE_IDLE;
                }
                break;

            /* ── 第二次按下后等待释放: 松手即双击完成 ── */
            case KEY_STATE_WAIT_DOUBLE_REL:
                if (level == 1) {
                    /* 第二次彻底松手, 投递双击事件! */
                    Key_List[i].EventFlag = 2;     /* 投递事件: 双击 */
                    Key_List[i].State = KEY_STATE_IDLE;
                }
                break;
        }
    }
}

/**
 * @brief  读取按键事件 (阅后即焚)
 * @param  key_id: 按键编号 (0 = KEY0/PB12, 1 = KEY1/PB13)
 * @retval 0 = 无事件, 1 = 单击, 2 = 双击
 * @note   读取后内部标志自动清零, 保证同一事件不会被重复消费。
 *         若 key_id 超出范围返回 0, 不做越界访问。
 */
uint8_t KEY_Get_Event(uint8_t key_id)
{
    if (key_id >= KEY_COUNT) return 0;

    uint8_t event = Key_List[key_id].EventFlag;
    Key_List[key_id].EventFlag = 0;  /* 阅后即焚: 消费后立刻清零 */
    return event;
}

/**
 * @brief  非阻塞按键扫描任务 (时间戳差值法, 每 10ms 执行一次)
 * @note   替代原先在 SysTick_Handler 中直接调用 KEY_Scan_All() 的方案。
 *         由 main.c 主循环高频调用, 内部自动保证 10ms 间隔。
 *         uint32_t 无符号减法保证了溢出安全。
 */
void KEY_Task(void)
{
    static uint32_t last_scan = 0;

    if (SysTimer_GetTick() - last_scan >= 10)
    {
        last_scan = SysTimer_GetTick();
        KEY_Scan_All();
    }
}

