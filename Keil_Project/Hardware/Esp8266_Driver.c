/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.c
 * @brief   ESP8266 串口通信驱动 — 实现 (V6.2)
 * @note    V6.2: CH_PD=PB11 (EN), RST=PA1 (独立复位)
 *          非阻塞初始化: RST_LOW(1s) → RST_HIGH + CH_PD=1 → BOOT_WAIT(2s) → READY
 ******************************************************************************
 */

#include "Esp8266_Driver.h"
#include "Sys_Timer.h"

#define RX_BUF_SIZE         256
#define CH_PD_PIN           GPIO_Pin_11
#define CH_PD_PORT          GPIOB
#define RST_PIN             GPIO_Pin_1
#define RST_PORT            GPIOA

#define RST_LOW_MS          1000
#define BOOT_WAIT_MS        2000

typedef enum {
    ESP8266_DRIVER_INIT_STATE_IDLE = 0,
    ESP8266_DRIVER_INIT_STATE_HW_DONE,
    ESP8266_DRIVER_INIT_STATE_RST_LOW,
    ESP8266_DRIVER_INIT_STATE_BOOT_WAIT,
    ESP8266_DRIVER_INIT_STATE_READY
} Esp8266_Init_State;

static char    s_rx_buf[RX_BUF_SIZE];
static volatile uint16_t s_rx_index = 0;
static volatile uint8_t  s_rx_frame_flag = 0;

static Esp8266_Init_State s_init_state = ESP8266_DRIVER_INIT_STATE_IDLE;
static uint32_t           s_init_timer = 0;

static void Hardware_Configure(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    /* CH_PD 引脚 (PB11) + RST 引脚 (PA1) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    /* RST: PA1, 初始高电平 */
    gpio.GPIO_Pin   = RST_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RST_PORT, &gpio);
    GPIO_SetBits(RST_PORT, RST_PIN);

    /* CH_PD: PB11, 初始低电平 */
    gpio.GPIO_Pin   = CH_PD_PIN;
    GPIO_Init(CH_PD_PORT, &gpio);
    GPIO_ResetBits(CH_PD_PORT, CH_PD_PIN);

    /* USART2 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

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

void Esp8266_Driver_Start_Init(void)
{
    /* 清空旧帧缓冲, 防止 ESP 复位后残留数据被误消费 */
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        s_rx_buf[0]     = '\0';
        s_rx_index      = 0;
        s_rx_frame_flag = 0;
        __set_PRIMASK(primask);
    }

    Hardware_Configure();

    /* RST=0, CH_PD=0 → 硬件复位开始 */
    GPIO_ResetBits(RST_PORT, RST_PIN);
    s_init_timer = Sys_Timer_Get_Tick();
    s_init_state = ESP8266_DRIVER_INIT_STATE_RST_LOW;
}

void Esp8266_Driver_Init_Task(void)
{
    switch (s_init_state) {
        case ESP8266_DRIVER_INIT_STATE_IDLE:
        case ESP8266_DRIVER_INIT_STATE_READY:
            break;

        case ESP8266_DRIVER_INIT_STATE_HW_DONE:
            break;

        case ESP8266_DRIVER_INIT_STATE_RST_LOW:
            if (Sys_Timer_Get_Tick() - s_init_timer >= RST_LOW_MS) {
                /* RST=1, CH_PD=1 → 启动 ESP8266 */
                GPIO_SetBits(RST_PORT, RST_PIN);
                GPIO_SetBits(CH_PD_PORT, CH_PD_PIN);
                s_init_timer = Sys_Timer_Get_Tick();
                s_init_state = ESP8266_DRIVER_INIT_STATE_BOOT_WAIT;
            }
            break;

        case ESP8266_DRIVER_INIT_STATE_BOOT_WAIT:
            if (Sys_Timer_Get_Tick() - s_init_timer >= BOOT_WAIT_MS) {
                s_init_state = ESP8266_DRIVER_INIT_STATE_READY;
            }
            break;
    }
}

uint8_t Esp8266_Driver_Is_Ready(void)
{
    return (s_init_state == ESP8266_DRIVER_INIT_STATE_READY);
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
    } else if (!s_rx_frame_flag && s_rx_index < RX_BUF_SIZE - 1) {
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
    dst[len]      = '\0';
    s_rx_buf[0]   = '\0';
    s_rx_index    = 0;
    s_rx_frame_flag = 0;
    __set_PRIMASK(primask);
    return len;
}
