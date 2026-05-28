/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.c
 * @brief   ESP8266 串口通信驱动 — 实现
 ******************************************************************************
 */

#include "Esp8266_Driver.h"
#include "Sys_Timer.h"

#define RX_BUF_SIZE         256
#define CH_PD_PIN           GPIO_Pin_1
#define CH_PD_PORT          GPIOB
#define CH_PD_RESET_MS      1000
#define CH_PD_BOOT_MS       2000

static char    s_rx_buf[RX_BUF_SIZE];
static volatile uint16_t s_rx_index = 0;
static volatile uint8_t  s_rx_frame_flag = 0;
static uint8_t           s_ready = 0;

void Esp8266_Driver_Init(void)
{
    GPIO_InitTypeDef      gpio;
    USART_InitTypeDef     usart;
    NVIC_InitTypeDef      nvic;

    /* CH_PD 控制引脚 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin   = CH_PD_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(CH_PD_PORT, &gpio);

    /* 硬件复位: 拉低 1000ms → 拉高 → 等 2000ms 冷启动 */
    GPIO_ResetBits(CH_PD_PORT, CH_PD_PIN);
    Sys_Timer_Delay_Ms(CH_PD_RESET_MS);
    GPIO_SetBits(CH_PD_PORT, CH_PD_PIN);
    Sys_Timer_Delay_Ms(CH_PD_BOOT_MS);

    /* USART2 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;  /* TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_3;  /* RX */
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

    s_ready = 1;
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
    /* 双分隔符兼容: \r 和 \n 都视为帧尾 */
    if (ch == '\r' || ch == '\n') {
        if (s_rx_index > 0) {
            s_rx_buf[s_rx_index] = '\0';
            s_rx_frame_flag = 1;
        }
    } else if (s_rx_index < RX_BUF_SIZE - 1) {
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
    __disable_irq();
    s_rx_buf[0]     = '\0';
    s_rx_index      = 0;
    s_rx_frame_flag = 0;
    __enable_irq();
}

uint16_t Esp8266_Driver_Copy_Rx_Frame(char* dst, uint16_t max_len)
{
    uint16_t len = 0;
    if (max_len < 2) return 0;
    __disable_irq();
    while (s_rx_buf[len] && len < max_len - 1) {
        dst[len] = s_rx_buf[len];
        len++;
    }
    dst[len]      = '\0';
    s_rx_buf[0]   = '\0';
    s_rx_index    = 0;
    s_rx_frame_flag = 0;
    __enable_irq();
    return len;
}

uint8_t Esp8266_Driver_Is_Ready(void)
{
    return s_ready;
}
