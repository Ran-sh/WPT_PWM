/**
 ******************************************************************************
 * @file    Hardware/ESP8266.c
 * @brief   ESP8266-01 WiFi 模块底层驱动
 * @note    存放路径: 项目根目录\Hardware\
 *          依赖: STM32F10x 标准外设库 (SPL), 绝不允许使用 HAL/LL 库
 *          硬件接口: USART2 (PA2-TX, PA3-RX), 波特率 115200
 *          通信协议: 纯 JSON 透传 (Dual-MCU 架构, ESP8266 端运行 Arduino MQTT 固件, 不经 AT 指令)
 *
 *          模块化隔离: 所有缓冲区及内部状态变量均使用 static 私有化，
 *          外部只能通过公开接口访问，杜绝跨模块篡改。
 ******************************************************************************
 */

#include "ESP8266.h"
#include "SysTimer.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════
 *                    私有静态变量 (模块隔离)
 * ═══════════════════════════════════════════════════════════════ */
static char     s_RxBuf[ESP8266_RX_BUF_SIZE];  /* 接收环形缓冲区 */
static volatile uint16_t s_RxIndex = 0;         /* 缓冲区写入游标 (ISR 写入, 主循环读取, 必须 volatile) */

/* 帧就绪标志 —— main.c 通过 ESP8266_GetRxFlag() 轮询 */
static volatile uint8_t g_ESP8266_RxFrameFlag = 0;

static uint8_t s_ready = 0;  /* ESP8266_Init() 成功完成后置 1 */

/* ═══════════════════════════════════════════════════════════════
 *              硬件初始化: USART2 + GPIO + NVIC
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  初始化 USART2 外设，用于与 ESP8266-01 通信
 * @note   Dual-MCU 架构: ESP8266 端运行 Arduino MQTT 固件,
 *         STM32 侧仅需 CH_PD 硬件复位 + USART2 初始化,
 *         无需 AT 指令 (AT+RST / +++).
 *         引脚映射:
 *           PA2 → USART2_TX  (复用推挽输出, 50MHz)
 *           PA3 → USART2_RX  (浮空输入)
 *         中断配置:
 *           使能 USART_IT_RXNE 接收中断，每收到 1 字节触发一次
 *           NVIC 抢占优先级 = 1, 子优先级 = 1
 */
void ESP8266_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* ── 0. 重试保护: 先关 USART2 和中断, 从干净状态开始 ── */
    USART_Cmd(USART2, DISABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
    USART_DeInit(USART2);   /* 复位 USART2 所有寄存器到默认值 */

    /* ── 1. 开启外设时钟 (GPIOB 用于 CH_PD/EN 引脚) ── */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* ── 1b. 配置 ESP8266 使能引脚: PB1 → CH_PD/EN ── */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /*
     * ═══════════════════════════════════════════════════════════════
     *  ESP8266 硬件深度复位
     * ═══════════════════════════════════════════════════════════════
     *
     *  V4.0 Dual-MCU: ESP8266 运行 Arduino 固件, 无需 AT 指令。
     *  仅保留 CH_PD 硬件复位确保模块冷启动。
     *
     *  步骤:
     *    1. CH_PD 拉低 1000ms — 确保内部电容完全放电
     *    2. CH_PD 拉高, 等待 2000ms — 固件冷启动 + RF 校准
     */
    GPIO_ResetBits(GPIOB, GPIO_Pin_1);   /* CH_PD = 0 → 模块完全断电 */
    SysTimer_DelayMs(1000);              /* 1000ms 深度放电 */
    GPIO_SetBits(GPIOB, GPIO_Pin_1);     /* CH_PD = 1 → 模块冷启动 */
    SysTimer_DelayMs(2000);              /* 等待固件启动 + RF 校准 */

    /* ── 2. 配置 TX 引脚: PA2 (复用推挽输出) ── */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ── 3. 配置 RX 引脚: PA3 (浮空输入) ── */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ── 4. USART2 参数: 115200 bps, 8 数据位, 1 停止位, 无校验 ── */
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    /* ── 5. 使能接收中断 USART_IT_RXNE (每个字节触发) ── */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    /* ── 6. NVIC 中断优先级配置 ── */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* ── 7. 使能 USART2 ── */
    USART_Cmd(USART2, ENABLE);

    ESP8266_ClearRxBuffer();
    s_ready = 1;
}

/* ═══════════════════════════════════════════════════════════════
 *                    阻塞式发送
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  向 ESP8266 阻塞发送完整字符串
 * @param  str: 以 '\0' 结尾的字符串指针
 * @note   逐字节等待 TXE (发送数据寄存器空) 标志置位后写入，
 *         全部字节发送完毕后等待 TC (发送完成) 标志确保数据完全移出。
 */
void ESP8266_SendString(const char *str)
{
    while (*str)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);  /* 等发送寄存器空 */
        USART_SendData(USART2, (uint16_t)(*str++));
    }
    /* TXE 已确保最后一个字节写入 DR，USART 全双工 — 无需等 TC 清空移位寄存器 */
}

/* ═══════════════════════════════════════════════════════════════
 *                  异步接收引擎 (USART2_IRQHandler 调用)
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  字符注入 —— 由 USART2_IRQHandler 在中断上下文中调用
 * @param  ch: 刚收到的 8 位数据
 * @note   每收到一个字符追加到 s_RxBuf 末尾；
 *         遇到 '\n' (0x0A) 时认为一帧结束，
 *         置位 g_ESP8266_RxFrameFlag 通知 main.c 主循环取走数据。
 *
 *         缓冲区溢出保护策略：
 *         当写入游标达到缓冲区末尾时，自动将后半段数据前移覆盖前半段，
 *         丢弃最早的 50% 旧数据，确保最新收到的帧完整保留。
 */
void ESP8266_RxChar(uint8_t ch)
{
    /*
     * 缓冲区溢出保护: 必须为 '\0' 留一个字节 (s_RxBuf[s_RxIndex+1])
     * 因此判据为 size-2 (511→510), 否则写入 \0 时越界覆写相邻内存
     */
    if (s_RxIndex >= ESP8266_RX_BUF_SIZE - 2)
    {
        memmove(s_RxBuf,
                s_RxBuf + (ESP8266_RX_BUF_SIZE / 2),
                ESP8266_RX_BUF_SIZE / 2);
        s_RxIndex = ESP8266_RX_BUF_SIZE / 2;
    }

    /* ── 追加字符并保持字符串闭合 ── */
    s_RxBuf[s_RxIndex++] = (char)ch;
    s_RxBuf[s_RxIndex]   = '\0';

    /* ── \r 或 \n 视为帧结束符 (防御性双分隔符设计, 兼容 \r / \n / \r\n) ── */
    if (ch == '\n' || ch == '\r')
    {
        g_ESP8266_RxFrameFlag = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *                    公开访问函数
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  查询是否有完整帧到达 (供 main.c 轮询)
 * @retval 1 = 有新帧, 0 = 无新帧
 */
uint8_t ESP8266_GetRxFlag(void)
{
    return g_ESP8266_RxFrameFlag;
}

/**
 * @brief  获取接收缓冲区只读指针
 * @retval 指向 s_RxBuf 首地址的指针
 * @note   调用者不应直接修改缓冲区内容，使用 ESP8266_ClearRxBuffer 清空
 */
const char* ESP8266_GetRxBuffer(void)
{
    return s_RxBuf;
}

/**
 * @brief  完全清空接收缓冲区及所有标志
 * @note   在发送 AT 指令前调用，避免残留数据干扰应答匹配
 */
void ESP8266_ClearRxBuffer(void)
{
    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);  /* 临界区: 防止 ISR 在清零期间写入 */
    s_RxBuf[0]            = '\0';   /* 仅截断, strstr 遇到 \0 即停止, 无需清零全缓冲 */
    s_RxIndex             = 0;
    g_ESP8266_RxFrameFlag = 0;
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
}

/**
 * @brief  原子帧读取: 拷贝第一帧并保留尾部未处理字节 (防 TCP 粘包)
 * @param  dst: 目标数组
 * @param  max_len: 最大拷贝长度 (含 '\0')
 * @retval 实际拷贝的字节数 (不含 '\0')
 * @note   仅消费第一个帧, 其后的字节 (如粘包的 "CMD:O") 前移到缓冲区头部,
 *         不丢失已接收但未处理的后续数据
 */
uint16_t ESP8266_CopyRxFrame(char *dst, uint16_t max_len)
{
    uint16_t len = 0;
    uint16_t consumed;

    if (max_len < 2) return 0;  /* 防止 uint16 下溢 */

    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);

    /* 拷贝到第一个 \r 或 \n 为止 (一帧) */
    while (len < max_len - 1 && s_RxBuf[len] != '\0') {
        dst[len] = s_RxBuf[len];
        if (s_RxBuf[len] == '\r' || s_RxBuf[len] == '\n') {
            len++;
            break;  /* 帧结束 */
        }
        len++;
    }
    dst[len] = '\0';
    consumed = len;

    /*
     * 保留尾部未处理字节: 如果 ISR 在 \r\n 之后又写入了新数据
     * (如 "CMD:ON\r\nCMD:OFF\r\n" 粘包), 将后半截移到缓冲区头部,
     * 下一轮轮询时标志仍置位, 可继续处理
     */
    if (consumed < s_RxIndex) {
        uint16_t tail = s_RxIndex - consumed;
        memmove(s_RxBuf, s_RxBuf + consumed, tail);
        s_RxIndex = tail;
        s_RxBuf[s_RxIndex] = '\0';
        /* 帧标志保持置位: 消费了一帧, 后面还有 */
    } else {
        s_RxBuf[0] = '\0';
        s_RxIndex  = 0;
        g_ESP8266_RxFrameFlag = 0;
    }

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    return len;
}

uint8_t ESP8266_IsReady(void)
{
    return s_ready;
}

