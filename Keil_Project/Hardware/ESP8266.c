/**
 ******************************************************************************
 * @file    Hardware/ESP8266.c
 * @brief   ESP8266-01 WiFi 模块底层驱动
 * @note    存放路径: 项目根目录\Hardware\
 *          依赖: STM32F10x 标准外设库 (SPL), 绝不允许使用 HAL/LL 库
 *          硬件接口: USART2 (PA2-TX, PA3-RX), 波特率 115200
 *          通信协议: AT 指令透传模式
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
static volatile uint8_t  s_FrameReady = 0;      /* 内部帧就绪标志 (ISR 置位, 主循环消费, 必须 volatile 防编译器缓存) */

/* 外部可见标志 —— main.c 通过 ESP8266_GetRxFlag() 轮询 */
volatile uint8_t g_ESP8266_RxFrameFlag = 0;

/* WaitResponse 轮询回调 (用于 OLED 点动画等) */
static void (*s_WaitCallback)(void) = NULL;
void ESP8266_SetWaitCallback(void (*cb)(void)) { s_WaitCallback = cb; }

/* ═══════════════════════════════════════════════════════════════
 *              硬件初始化: USART2 + GPIO + NVIC
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  初始化 USART2 外设，用于与 ESP8266-01 通信
 * @note   引脚映射:
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
     *  ESP8266 硬件深度复位 (解决多次联网必须重上电 VCC 的问题)
     * ═══════════════════════════════════════════════════════════════
     *
     *  根因: 透传模式下 ESP8266 固件状态机卡死, 短脉冲 CH_PD 无法
     *        彻底清除内部状态。仅靠 CH_PD 硬件复位不够, 必须加软件
     *        AT+RST 双重复位。
     *
     *  步骤:
     *    1. CH_PD 拉低 1000ms — 确保内部电容完全放电
     *    2. CH_PD 拉高, 等待 2000ms — 固件冷启动 + RF 校准
     *    3. 发送 AT+RST — 软件复位固件状态机 (清除透传/残留连接)
     *    4. 等待 "ready" 响应 — 确认 ESP8266 完全就绪
     *
     *  此序列确保每次联网都是完全干净的起点, 绝不依赖上一次的残留状态。
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

    /*
     * ── 8. 透传退出 + AT+RST 双软件复位 ──
     * CH_PD 硬件复位后, 部分模块仍可能卡在透传模式 (ESP8266
     * 通过 IO 引脚漏电维持部分电路)。两步软件复位确保干净起点:
     *   8a. 发送 "+++" 退出透传 (须前后各 1s 静默, 不加 \r\n)
     *   8b. 发送 AT+RST 软复位固件, 等待 "ready"
     */
    SysTimer_DelayMs(1000);                  /* +++ 前静默 1s */
    ESP8266_SendString("+++");              /* 退出透传, 不带 \r\n */
    SysTimer_DelayMs(1000);                  /* +++ 后静默 1s */
    ESP8266_ClearRxBuffer();

    ESP8266_SendString("AT+RST\r\n");
    {
        uint32_t rst_elapsed = 0;
        uint8_t  rst_ok = 0;
        while (rst_elapsed < 5000)   /* 软件复位最多等 5s */
        {
            if (s_FrameReady)
            {
                USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
                s_FrameReady = 0;
                rst_ok = (strstr(s_RxBuf, "ready") != NULL);
                USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
                if (rst_ok) break;
            }
            SysTimer_DelayMs(10);
            rst_elapsed += 10;
        }
    }
    ESP8266_ClearRxBuffer();  /* 清除复位过程中的垃圾数据 */
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
 *         遇到 '\n' (0x0A) 时认为一帧结束，置位 s_FrameReady 和
 *         g_ESP8266_RxFrameFlag，通知 main.c 主循环取走数据。
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
        s_FrameReady          = 1;
        g_ESP8266_RxFrameFlag = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *              应答匹配引擎 (带超时和错误检测)
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  轮询等待 ESP8266 返回期望的应答字符串
 * @param  expect    : 期望在接收缓冲区中匹配的字符串 (如 "OK")
 * @param  timeout_ms: 最大等待时间 (毫秒)
 * @retval 1 = 收到期望应答
 *         0 = 超时 / 收到 ERROR / 收到 FAIL
 * @note   每次 s_FrameReady 置位 (即收到一个以 '\n' 结尾的帧) 时
 *         检查一次全缓冲区；检查期间短暂关闭 USART2 RXNE 中断，
 *         防止 ISR 在 strstr 扫描期间修改缓冲区导致指针越界。
 */
static uint8_t ESP8266_WaitResponse(const char *expect, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;

    while (elapsed < timeout_ms)
    {
        if (s_FrameReady)
        {
            uint8_t has_error, has_expect;

            /* ── 临界区: 先关中断, 再消费标志, 最后统一恢复 ── */
            USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
            s_FrameReady = 0;

            has_error  = (strstr(s_RxBuf, "ERROR") != NULL ||
                          strstr(s_RxBuf, "FAIL")  != NULL);
            has_expect = (strstr(s_RxBuf, expect) != NULL);

            USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

            if (has_error)  return 0;
            if (has_expect) return 1;
        }
        if (s_WaitCallback) s_WaitCallback();   /* OLED 点动画等 */
        SysTimer_DelayMs(10);
        elapsed += 10;
    }
    return 0;  /* 超时 */
}

/* ═══════════════════════════════════════════════════════════════
 *               极速联网状态机 (6 步 AT 指令建立透传)
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  执行 6 步 AT 指令序列，建立 TCP 透传连接
 * @param  ssid : WiFi 热点名称 (不超过 32 字符)
 * @param  pwd  : WiFi 密码 (不超过 64 字符)
 * @param  ip   : 目标服务器 IPv4 地址 (点分十进制字符串, 如 "192.168.1.100")
 * @param  port : 目标服务器端口号 (如 8080)
 * @retval 0 = 成功进入透传模式，此后 USART2 即为透明数据通道
 *         1 = 模块无 AT 响应 (检查供电 / 接线 / 波特率)
 *         2 = CWMODE=1 设置失败
 *         3 = WiFi 连接失败 (检查 SSID / 密码 / 信号强度)
 *         4 = TCP 连接失败 (检查 IP / 端口 / 防火墙)
 *         5 = CIPMODE=1 设置失败
 *         6 = CIPSEND 启动失败
 */
uint8_t ESP8266_ConnectToServer(const char *ssid, const char *pwd,
                                 const char *ip, uint16_t port)
{
    char cmdBuf[160];  /* 160 字节足够容纳最长 SSID(32) + 最长密码(64) + AT指令开销 */

    /* ──────────────────────────────────────────────────────────
     * Step 1: 确认模块 AT 指令就绪
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    ESP8266_SendString("AT\r\n");
    if (!ESP8266_WaitResponse("OK", ESP8266_CMD_TIMEOUT))
        return 1;  /* 模块无响应 */

    /* ──────────────────────────────────────────────────────────
     * Step 2: 设置 Station (客户端) 模式
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    ESP8266_SendString("AT+CWMODE=1\r\n");
    if (!ESP8266_WaitResponse("OK", ESP8266_CMD_TIMEOUT))
        return 2;

    /* ──────────────────────────────────────────────────────────
     * Step 3: 连接 WiFi 热点 (含 DHCP, 耗时较长)
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    sprintf(cmdBuf, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    ESP8266_SendString(cmdBuf);
    if (!ESP8266_WaitResponse("OK", ESP8266_WIFI_TIMEOUT))
        return 3;

    /* ──────────────────────────────────────────────────────────
     * Step 4: 建立 TCP 连接到目标服务器
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    sprintf(cmdBuf, "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", ip, port);
    ESP8266_SendString(cmdBuf);
    if (!ESP8266_WaitResponse("OK", ESP8266_TCP_TIMEOUT))
        return 4;

    /* ──────────────────────────────────────────────────────────
     * Step 5: 开启透传模式
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    ESP8266_SendString("AT+CIPMODE=1\r\n");
    if (!ESP8266_WaitResponse("OK", ESP8266_CMD_TIMEOUT))
        return 5;

    /* ──────────────────────────────────────────────────────────
     * Step 6: 启动透传发送
     *         注意: AT+CIPSEND 的应答是 ">" 不带换行符，
     *         因此不能依赖帧标志，需要直接轮询缓冲区内容。
     * ────────────────────────────────────────────────────────── */
    ESP8266_ClearRxBuffer();
    ESP8266_SendString("AT+CIPSEND\r\n");
    {
        uint32_t elapsed = 0;
        uint8_t  got_prompt = 0;
        while (elapsed < ESP8266_CMD_TIMEOUT)
        {
            got_prompt = ESP8266_BufferContains(">");
            if (got_prompt) break;
            SysTimer_DelayMs(10);
            elapsed += 10;
        }
        if (elapsed >= ESP8266_CMD_TIMEOUT)
            return 6;
    }

    return 0;  /* 透传通道已建立 */
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
 * @brief  清除帧就绪标志 (main.c 处理完当前帧后调用)
 */
void ESP8266_ClearRxFlag(void)
{
    ESP8266_ClearRxBuffer();   /* 与 ClearRxBuffer 完全一致, 统一实现 */
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
    s_FrameReady          = 0;
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
     * 下一轮轮询时 s_FrameReady 仍为 1, 可继续处理
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
        s_FrameReady          = 0;
        g_ESP8266_RxFrameFlag = 0;
    }

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    return len;
}

/**
 * @brief  临界区内检查缓冲区是否包含指定字符串
 * @note   封装 USART 中断管理, 调用方无需直接操作 USART2 寄存器
 */
uint8_t ESP8266_BufferContains(const char *needle)
{
    const char *rx = ESP8266_GetRxBuffer();
    uint8_t found;
    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
    found = (strstr(rx, needle) != NULL);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    return found;
}
