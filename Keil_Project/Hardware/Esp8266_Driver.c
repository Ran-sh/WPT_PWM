/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.c
 * @brief   ESP8266 串口通信驱动 — V5.1.0
 *
 *  硬件连接（USART2与控制引脚）:
 *  +------------------------------------------------------------+
 *  |    STM32F103C8T6                      ESP8266               |
 *  |                                                             |
 *  |    PA2  --- USART2_TX  ----------------->  RXD              |
 *  |    PA3  --- USART2_RX  <-----------------  TXD              |
 *  |    PA1  --- GPIO_PP    ----------------->  RST              |
 *  |    PB11 --- GPIO_PP    ----------------->  CH_PD / EN       |
 *  |                                                             |
 *  |    串口参数：115200位/秒，8位数据，无校验，1位停止位       |
 *  |    通信内容：纯JSON透传，STM32不发送AT指令                 |
 *  |    启动顺序：使能模块，等待100ms，复位100ms，再等待启动    |
 *  |    接收路径：串口中断写入三个256字节环形缓冲槽             |
 *  |    发送路径：中断发送队列，并检查发送完成标志              |
 *  +------------------------------------------------------------+
 *
 * @note    PB11控制模块使能，PA1控制复位，PA2和PA3用于串口通信。
 ******************************************************************************
 */

#include "Esp8266_Driver.h"
#include "Sys_Timer.h"

#define ESP8266_DRIVER_RX_BUF_SIZE      256
#define ESP8266_DRIVER_RX_RING_SIZE      3   /* 三槽缓冲可承接ESP连续发送的多条数据帧 */
#define ESP8266_DRIVER_TX_BUF_SIZE      256U
#define ESP8266_DRIVER_CH_PD_PIN        GPIO_Pin_11
#define ESP8266_DRIVER_CH_PD_PORT       GPIOB
#define ESP8266_DRIVER_RST_PIN          GPIO_Pin_1
#define ESP8266_DRIVER_RST_PORT         GPIOA

#define ESP8266_DRIVER_RST_PULSE_MS     100    /* 复位引脚低电平脉冲宽度 */
#define ESP8266_DRIVER_BOOT_WAIT_MS     4000   /* 释放复位引脚后的最长启动等待时间 */

typedef enum {
    ESP8266_DRIVER_INIT_IDLE = 0,       /* 未初始化 */
    ESP8266_DRIVER_INIT_HW_READY,       /* 硬件已配置，等待启动请求 */
    ESP8266_DRIVER_INIT_RST_PULSE,      /* 正在输出复位低电平脉冲 */
    ESP8266_DRIVER_INIT_BOOT_WAIT,      /* 已释放复位，等待固件启动 */
    ESP8266_DRIVER_INIT_READY           /* 就绪 */
} Esp8266_Driver_Init_State;

/* 三槽环形缓冲用于承接ESP连续发送的数据帧。
 * 中断写入当前槽，主循环按顺序读取最早的数据帧。
 * 待处理帧计数大于零表示至少存在一帧可供读取。 */
static char    s_rx_buf[ESP8266_DRIVER_RX_RING_SIZE][ESP8266_DRIVER_RX_BUF_SIZE];
static volatile uint16_t s_rx_index = 0;
static volatile uint8_t  s_rx_ring_wr = 0;  /* 中断当前写入的槽位 */
static volatile uint8_t  s_rx_ring_rd = 0;  /* 主循环下次读取的槽位 */
static volatile uint8_t  s_rx_frame_count = 0;  /* 等待主循环处理的帧数 */
static uint8_t s_tx_buf[ESP8266_DRIVER_TX_BUF_SIZE];
static volatile uint16_t s_tx_head = 0U;
static volatile uint16_t s_tx_tail = 0U;
static volatile uint32_t s_tx_full_count = 0U;

static Esp8266_Driver_Init_State s_init_state = ESP8266_DRIVER_INIT_IDLE;
static uint32_t                  s_init_timer = 0;
static uint8_t                   s_hw_configured = 0;  /* 硬件仅配一次 */

/* 内部硬件配置：控制引脚只初始化一次。 */
static void Esp8266_Driver_Config_GPIO_Once(void)
{
    GPIO_InitTypeDef gpio;

    if (s_hw_configured) return;
    s_hw_configured = 1;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    /* PA1为复位引脚，空闲时保持高电平。 */
    gpio.GPIO_Pin   = ESP8266_DRIVER_RST_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP8266_DRIVER_RST_PORT, &gpio);
    GPIO_SetBits(ESP8266_DRIVER_RST_PORT, ESP8266_DRIVER_RST_PIN);

    /* PB11为模块使能引脚，初始化时保持低电平。 */
    gpio.GPIO_Pin   = ESP8266_DRIVER_CH_PD_PIN;
    GPIO_Init(ESP8266_DRIVER_CH_PD_PORT, &gpio);
    GPIO_ResetBits(ESP8266_DRIVER_CH_PD_PORT, ESP8266_DRIVER_CH_PD_PIN);
}

/* 内部硬件配置：USART2只初始化一次。 */
static void Esp8266_Driver_Config_USART_Once(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA2配置为复用推挽发送，PA3配置为浮空输入接收。 */
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

    USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    NVIC_Init(&nvic);
}

/* ==============================================================
 *  公开接口
 * ============================================================== */

void Esp8266_Driver_Start_Init(void)
{
    /* 硬件只配置一次，避免重复初始化串口干扰正在接收的数据。 */
    Esp8266_Driver_Config_GPIO_Once();
    Esp8266_Driver_Config_USART_Once();

    /* 清空环形接收缓冲区和帧计数。 */
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        s_rx_buf[0][0]  = '\0';
        s_rx_buf[1][0]  = '\0';
        s_rx_buf[2][0]  = '\0';
        s_rx_index      = 0;
        s_rx_ring_wr    = 0;
        s_rx_ring_rd    = 0;
        s_rx_frame_count = 0;
        s_tx_head       = 0U;
        s_tx_tail       = 0U;
        USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
        __set_PRIMASK(primask);
    }

    /* 先使能模块并等待电源稳定，再输出一个完整的硬件复位脉冲。 */
    GPIO_SetBits(ESP8266_DRIVER_CH_PD_PORT, ESP8266_DRIVER_CH_PD_PIN);
    { volatile uint32_t i; for (i = 0; i < 7000; i++) __NOP(); } /* 为模块电源稳定预留短暂建立时间 */

    /* 拉低复位引脚，开始输出硬件复位脉冲。 */
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
                /* 释放复位引脚，让模块开始启动。 */
                GPIO_SetBits(ESP8266_DRIVER_RST_PORT, ESP8266_DRIVER_RST_PIN);
                s_init_timer = Sys_Timer_Get_Tick();
                s_init_state = ESP8266_DRIVER_INIT_BOOT_WAIT;
            }
            break;

        case ESP8266_DRIVER_INIT_BOOT_WAIT:
            /* 等待时间到达上限后进入就绪状态。 */
            if (Sys_Timer_Get_Tick() - s_init_timer >= ESP8266_DRIVER_BOOT_WAIT_MS) {
                s_init_state = ESP8266_DRIVER_INIT_READY;
            }
            /* 串口已经收到数据，说明模块固件已启动，可提前进入就绪状态。 */
            {
                uint32_t primask = __get_PRIMASK();
                __disable_irq();
                if (s_rx_frame_count > 0) {
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

Esp8266_Driver_Tx_Result Esp8266_Driver_Send_String(const char* str)
{
    uint16_t length;
    uint16_t head;
    uint16_t tail;
    uint16_t used;
    uint16_t free_count;
    uint16_t i;
    uint32_t primask;

    if (str == 0) return ESP8266_DRIVER_TX_INVALID;
    length = 0U;
    while (str[length] != '\0') {
        if (length >= (ESP8266_DRIVER_TX_BUF_SIZE - 1U)) {
            return ESP8266_DRIVER_TX_INVALID;
        }
        length++;
    }
    if (length == 0U) return ESP8266_DRIVER_TX_INVALID;

    primask = __get_PRIMASK();
    __disable_irq();
    head = s_tx_head;
    tail = s_tx_tail;
    if (head >= tail) {
        used = head - tail;
    } else {
        used = (uint16_t)(ESP8266_DRIVER_TX_BUF_SIZE - tail + head);
    }
    free_count = (uint16_t)(ESP8266_DRIVER_TX_BUF_SIZE - 1U - used);
    if (length > free_count) {
        s_tx_full_count++;
        __set_PRIMASK(primask);
        return ESP8266_DRIVER_TX_FULL;
    }

    for (i = 0U; i < length; i++) {
        s_tx_buf[head] = (uint8_t)str[i];
        head++;
        if (head >= ESP8266_DRIVER_TX_BUF_SIZE) head = 0U;
    }
    s_tx_head = head;
    USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
    __set_PRIMASK(primask);
    return ESP8266_DRIVER_TX_OK;
}

uint32_t Esp8266_Driver_Get_Tx_Full_Count(void)
{
    return s_tx_full_count;
}

void Esp8266_Driver_Tx_Ready_ISR(void)
{
    uint16_t tail;

    tail = s_tx_tail;
    if (tail == s_tx_head) {
        USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
        return;
    }

    USART_SendData(USART2, s_tx_buf[tail]);
    tail++;
    if (tail >= ESP8266_DRIVER_TX_BUF_SIZE) tail = 0U;
    s_tx_tail = tail;
    if (tail == s_tx_head) {
        USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
    }
}

void Esp8266_Driver_Rx_Char(uint8_t ch)
{
    uint8_t wr = s_rx_ring_wr;
    if (ch == '\r' || ch == '\n') {
        if (s_rx_index > 0 && s_rx_frame_count < ESP8266_DRIVER_RX_RING_SIZE) {
            s_rx_buf[wr][s_rx_index] = '\0';
            s_rx_index = 0;
            s_rx_frame_count++;
            /* 将写入位置推进到下一个空闲槽。 */
            s_rx_ring_wr = (wr + 1) % ESP8266_DRIVER_RX_RING_SIZE;
        }
    } else if (s_rx_frame_count < ESP8266_DRIVER_RX_RING_SIZE
               && s_rx_index < ESP8266_DRIVER_RX_BUF_SIZE - 1) {
        s_rx_buf[wr][s_rx_index++] = ch;
    }
}

/**
 * @brief  在同一临界区内完成帧检查、复制和消费
 * @note   每次读取最早进入三槽环形缓冲区的数据帧，并推进读取位置。
 *         合并检查与复制操作，可以避免中断在两步之间写入新帧造成静默丢失。
 * @retval 实际帧长；返回0表示当前没有可用数据帧
 */
uint16_t Esp8266_Driver_Try_Copy_Rx_Frame(char* dst, uint16_t max_len)
{
    uint16_t len = 0;
    uint32_t primask; uint8_t rd;
    /* 输出缓冲区无效时不得进入临界区，更不能写入字符串结束符。 */
    if (dst == 0 || max_len < 2U) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_rx_frame_count == 0) {
        __set_PRIMASK(primask);
        return 0;
    }
    rd = s_rx_ring_rd;
    while (s_rx_buf[rd][len] && len < max_len - 1) {
        dst[len] = s_rx_buf[rd][len];
        len++;
    }
    dst[len]            = '\0';
    s_rx_buf[rd][0]     = '\0';
    s_rx_ring_rd        = (rd + 1) % ESP8266_DRIVER_RX_RING_SIZE;
    s_rx_frame_count--;
    __set_PRIMASK(primask);
    return len;
}
