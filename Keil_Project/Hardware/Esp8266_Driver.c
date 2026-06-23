/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.c
 * @brief   ESP8266 串口通信驱动 — 实现 (V4.2.0 冷启动强化版)
 * @note    CH_PD=PB11 (EN), RST=PA1 (独立复位)
 *          简化时序: GPIO仅配一次 → 拉高CH_PD供电 → RST复位脉冲 → 等待启动
 ******************************************************************************
 */

#include "Esp8266_Driver.h"
#include "Sys_Timer.h"

#define ESP8266_DRIVER_RX_BUF_SIZE      256
#define ESP8266_DRIVER_RX_RING_SIZE      3   /* 双帧缓冲: 防止 ESP 连续发送多条帧时丢弃后续帧 */
#define ESP8266_DRIVER_CH_PD_PIN        GPIO_Pin_11
#define ESP8266_DRIVER_CH_PD_PORT       GPIOB
#define ESP8266_DRIVER_RST_PIN          GPIO_Pin_1
#define ESP8266_DRIVER_RST_PORT         GPIOA

#define ESP8266_DRIVER_RST_PULSE_MS     100    /* RST拉低脉冲宽度 */
#define ESP8266_DRIVER_BOOT_WAIT_MS     4000   /* 释放RST后等待固件启动 */

typedef enum {
    ESP8266_DRIVER_INIT_IDLE = 0,       /* 未初始化 */
    ESP8266_DRIVER_INIT_HW_READY,       /* 硬件已配, 等待Start_Init触发 */
    ESP8266_DRIVER_INIT_RST_PULSE,      /* RST拉低脉冲中 */
    ESP8266_DRIVER_INIT_BOOT_WAIT,      /* RST释放, 等待固件加载 */
    ESP8266_DRIVER_INIT_READY           /* 就绪 */
} Esp8266_Driver_Init_State;

static char    s_rx_buf[ESP8266_DRIVER_RX_BUF_SIZE];
static volatile uint16_t s_rx_index = 0;
static volatile uint8_t  s_rx_frame_flag = 0;

static Esp8266_Driver_Init_State s_init_state = ESP8266_DRIVER_INIT_IDLE;
static uint32_t                  s_init_timer = 0;
static uint8_t                   s_hw_configured = 0;  /* 硬件仅配一次 */

/* ── 内部: 仅配置 GPIO (只执行一次) ── */
static void Esp8266_Driver_Config_GPIO_Once(void)
{
    GPIO_InitTypeDef gpio;

    if (s_hw_configured) return;
    s_hw_configured = 1;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    /* RST: PA1, 初始高 — 不用复位时保持高 */
    gpio.GPIO_Pin   = ESP8266_DRIVER_RST_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP8266_DRIVER_RST_PORT, &gpio);
    GPIO_SetBits(ESP8266_DRIVER_RST_PORT, ESP8266_DRIVER_RST_PIN);

    /* CH_PD/EN: PB11, 初始低 — 默认断电 */
    gpio.GPIO_Pin   = ESP8266_DRIVER_CH_PD_PIN;
    GPIO_Init(ESP8266_DRIVER_CH_PD_PORT, &gpio);
    GPIO_ResetBits(ESP8266_DRIVER_CH_PD_PORT, ESP8266_DRIVER_CH_PD_PIN);
}

/* ── 内部: 配置 USART2 (只执行一次) ── */
static void Esp8266_Driver_Config_USART_Once(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA2=TX(AF_PP), PA3=RX(IN_FLOATING) */
    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_3;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_WordLength          = USART_WordLength_8b;
    USART_Init(USART2, &usart);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    NVIC_Init(&nvic);
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */

void Esp8266_Driver_Start_Init(void)
{
    /* 硬件只配一次, 避免反复 USART_Init 干扰正在接收的数据 */
    Esp8266_Driver_Config_GPIO_Once();
    Esp8266_Driver_Config_USART_Once();

    /* 清空接收缓冲 */
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        s_rx_buf[0]     = '\0';
        s_rx_index      = 0;
        s_rx_frame_flag = 0;
        __set_PRIMASK(primask);
    }

    /* 先给 CH_PD 供电, 让模块上电 (此时 RST 已经是高, 模块会开始启动但马上会被拉低复位) */
    GPIO_SetBits(ESP8266_DRIVER_CH_PD_PORT, ESP8266_DRIVER_CH_PD_PIN);
    { volatile uint32_t i; for (i = 0; i < 7000; i++) __NOP(); } /* 建立稳定供电腹地, 等待 VCC 充放电建立 */

    /* 给一个干净的硬件复位脉冲: 拉低 RST */
    GPIO_ResetBits(ESP8266_DRIVER_RST_PORT, ESP8266_DRIVER_RST_PIN);
    s_init_timer = Sys_Timer_Get_Tick();
    s_init_state = ESP8266_DRIVER_INIT_RST_PULSE;
}

void Esp8266_Driver_Init_Task(void)
{
    switch (s_init_state) {
        case ESP8266_DRIVER_INIT_IDLE:
        case ESP8266_DRIVER_INIT_HW_READY:
        case ESP8266_DRIVER_INIT_READY:
            break;

        case ESP8266_DRIVER_INIT_RST_PULSE:
            if (Sys_Timer_Get_Tick() - s_init_timer >= ESP8266_DRIVER_RST_PULSE_MS) {
                /* 释放 RST, 模块启动 */
                GPIO_SetBits(ESP8266_DRIVER_RST_PORT, ESP8266_DRIVER_RST_PIN);
                s_init_timer = Sys_Timer_Get_Tick();
                s_init_state = ESP8266_DRIVER_INIT_BOOT_WAIT;
            }
            break;

        case ESP8266_DRIVER_INIT_BOOT_WAIT:
            /* 到达超时 → 就绪 */
            if (Sys_Timer_Get_Tick() - s_init_timer >= ESP8266_DRIVER_BOOT_WAIT_MS) {
                s_init_state = ESP8266_DRIVER_INIT_READY;
            }
            /* 提前完成: ESP 已在串口发数据 → 说明固件已启动, 立即就绪 */
            {
                uint32_t primask = __get_PRIMASK();
                __disable_irq();
                if (s_rx_frame_flag) {
                    s_init_state = ESP8266_DRIVER_INIT_READY;
                }
                __set_PRIMASK(primask);
            }
            break;
    }
}

uint8_t Esp8266_Driver_Is_Ready(void)
{
    return (s_init_state == ESP8266_DRIVER_INIT_READY);
}

void Esp8266_Driver_Send_String(const char* str)
{
    while (*str) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, *str++);
    }
}

void Esp8266_Driver_Rx_Char(uint8_t ch)
{
    if (ch == '\r' || ch == '\n') {
        if (s_rx_index > 0 && !s_rx_frame_flag) {
            s_rx_buf[s_rx_index] = '\0';
            s_rx_frame_flag = 1;
        }
    } else if (!s_rx_frame_flag && s_rx_index < ESP8266_DRIVER_RX_BUF_SIZE - 1) {
        s_rx_buf[s_rx_index++] = ch;
    }
}

uint8_t Esp8266_Driver_Get_Rx_Flag(void)
{
    return s_rx_frame_flag;
}

const char* Esp8266_Driver_Get_Rx_Buffer(void)
{
    return s_rx_buf;
}

void Esp8266_Driver_Clear_Rx_Buffer(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_rx_buf[0]     = '\0';
    s_rx_index      = 0;
    s_rx_frame_flag = 0;
    __set_PRIMASK(primask);
}

uint16_t Esp8266_Driver_Copy_Rx_Frame(char* dst, uint16_t max_len)
{
    uint16_t len = 0;
    uint32_t primask;
    if (max_len < 2) return 0;
    primask = __get_PRIMASK();
    __disable_irq();
    while (s_rx_buf[len] && len < max_len - 1) {
        dst[len] = s_rx_buf[len];
        len++;
    }
    dst[len]        = '\0';
    s_rx_buf[0]     = '\0';
    s_rx_index      = 0;
    s_rx_frame_flag = 0;
    __set_PRIMASK(primask);
    return len;
}

/**
 * @brief  原子检查+复制: flag-check + frame-copy + clear 在同一临界区内完成
 * @note   消除 Get_Rx_Flag()→Copy_Rx_Frame() 之间的中断窗口:
 *         若检测到帧标志后 ISR 又写入新帧, 旧原子方案会在 Copy 内清标志,
 *         导致新帧标志也一并被清零, 帧静默丢失。
 *         本函数将"判断+复制"合一, 返回 0 (无帧) 或有效帧长, 调用方无需先调 Get_Rx_Flag。
 */
uint16_t Esp8266_Driver_Try_Copy_Rx_Frame(char* dst, uint16_t max_len)
{
    uint16_t len = 0;
    uint32_t primask;
    if (max_len < 2) return 0;
    primask = __get_PRIMASK();
    __disable_irq();
    if (!s_rx_frame_flag) {
        __set_PRIMASK(primask);
        return 0;
    }
    while (s_rx_buf[len] && len < max_len - 1) {
        dst[len] = s_rx_buf[len];
        len++;
    }
    dst[len]        = '\0';
    s_rx_buf[0]     = '\0';
    s_rx_index      = 0;
    s_rx_frame_flag = 0;
    __set_PRIMASK(primask);
    return len;
}
