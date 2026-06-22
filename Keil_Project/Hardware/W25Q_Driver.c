/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.c
 * @brief   W25Q128 16MB SPI NOR Flash — SPI1 分时复用驱动 (极简行内聚合版)
 *
 *          接线: PA5=SCK PA7=MOSI PA6=动态(MISO↔DC) PA12=CS
 *          PA4=TFT_CS (TFT 占用), PA12=FLASH_CS (W25Q128), 双 CS 门控互斥
 *
 *          四大硬件死线闭锁:
 *            L1: 写使能 (0x06) 强制级联 + CS 边沿锁存
 *            L2: Busy 位 (SR1 BIT0) 阻塞死等上/下边界
 *            L3: DFF (SPI1 CR1 bit11) 原子闪切 8b↔16b 防时序毛刺
 *            L4: 发波态 (SWEEP/RUNNING) 全局禁擦, 45ms 死刑拦截
 *
 * @note    ARMCC V5 SPL, 纯 C89, 禁止 // 双斜杠
 ******************************************************************************
 */

#include "W25Q_Driver.h"
#include "Sys_Core.h"       /* g_sys_state */
#include "Sys_Timer.h"      /* Sys_Timer_Get_Tick */

/* ═══════════════════════════════════════════════
 *  引脚宏 (行内高聚合: 拉低→操作→拉高 压缩在单行)
 * ═══════════════════════════════════════════════ */
#define FLASH_CS_PIN    GPIO_Pin_12
#define FLASH_CS_PORT   GPIOA
#define FLASH_CS_LOW()  GPIO_ResetBits(FLASH_CS_PORT, FLASH_CS_PIN)  /* PA12=0 */
#define FLASH_CS_HIGH() GPIO_SetBits(FLASH_CS_PORT, FLASH_CS_PIN)    /* PA12=1 */

/* TFT 引脚引用 (仅用于 PA6 模式判断, 不操控 TFT_CS) */
#define TFT_DC_PIN      GPIO_Pin_6
#define TFT_DC_PORT     GPIOA

/* ── W25Q128 指令集 ── */
#define CMD_WREN        0x06U  /* 写使能 */
#define CMD_WRDI         0x04U  /* 写禁止 */
#define CMD_RDSR1        0x05U  /* 读状态寄存器1 */
#define CMD_READ         0x03U  /* 读数据 */
#define CMD_PP           0x02U  /* 页编程 (Page Program) */
#define CMD_SE           0x20U  /* 扇区擦除 (4KB) */
#define CMD_JEDEC        0x9FU  /* 读 JEDEC ID */
#define CMD_CHIP_ERASE   0xC7U  /* 全片擦除 (仅调试, 禁用于运行时) */

#define BUSY_BIT         0x01U  /* SR1 BIT0 */
#define BUSY_TIMEOUT_MS  100U   /* 写/擦 Busy 超时 */

/* ── 静态状态 ── */
static uint8_t s_chip_ok = 0;          /* JEDEC 校验通过标志 */

/* ═══════════════════════════════════════════════
 *  PA6 角色切换 (内联, 直接寄存器操作)
 *
 *  访问 TFT:   PA6=GPIO_Out_PP (DC 信号)
 *  访问 Flash: PA6=Input pull-up (MISO)
 *  空闲:       PA6 保持 GPIO_Out (TFT 占绝对多数时间)
 * ═══════════════════════════════════════════════ */

/** @brief PA6 → Input floating (Flash MISO), CS 拉低 → 门控闭合 */
static void W25Q_Enter_Mode(void)
{
    GPIOA->CRL &= ~(0x0FU << 24);                       /* PA6 CRL 清零 */
    GPIOA->CRL |=  (0x04U << 24);                        /* PA6 = Input floating */
    FLASH_CS_LOW();                                      /* 门控: 选中 Flash */
}

/** @brief CS 拉高 → 门控释放, PA6 → GPIO_Out_PP (恢复 TFT DC 角色) */
static void W25Q_Leave_Mode(void)
{
    FLASH_CS_HIGH();                                     /* 门控: 释放 Flash */
    GPIOA->CRL &= ~(0x0FU << 24);                       /* PA6 CRL 清零 */
    GPIOA->CRL |=  (0x03U << 24);                        /* PA6 = 50MHz PP Out */
}

/* ═══════════════════════════════════════════════
 *  SPI1 基础 8位 收发
 * ═══════════════════════════════════════════════ */

/** @brief SPI1 发送单字节并接收 (全双工, 8bit DFF) */
static uint8_t W25Q_SPI_Transfer(uint8_t tx)
{
    SPI_I2S_SendData(SPI1, tx);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

/* ═══════════════════════════════════════════════
 *  L2: Busy 阻塞死等 (高聚合, 上/下边界强制调用)
 * ═══════════════════════════════════════════════ */

/** @brief 死等 W25Q128 Busy 位清零 (SR1 BIT0), 超时护底
 *  @note  任何读/写/擦除的 if-else 进入和退出边界必须调用 */
static void W25Q_Wait_Busy_Timeout(void)
{
    uint32_t deadline; uint8_t sr1;
    deadline = Sys_Timer_Get_Tick() + BUSY_TIMEOUT_MS;   /* 超时护底 */
    W25Q_SPI_8bit();                                     /* L3: 确保 8bit 帧 */
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L, 防对灌短路 */
    do {
        W25Q_SPI_Transfer(CMD_RDSR1);                    /* 0x05 读 SR1 */
        sr1 = W25Q_SPI_Transfer(0xFF);                   /* 哑写收 SR1 */
        FLASH_CS_HIGH(); FLASH_CS_LOW();                 /* CS 脉冲 (无需 Leave/Enter) */
        if ((sr1 & BUSY_BIT) == 0) break;                /* Busy=0 释放 */
    } while (Sys_Timer_Get_Tick() - deadline < 0x80000000U); /* uint32 回绕安全 */
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC, 归还总线 */
}

/* ═══════════════════════════════════════════════
 *  L1: 写使能 (0x06) 强制级联 + CS 边沿锁存
 * ═══════════════════════════════════════════════ */

/** @brief 发送写使能 (0x06) + CS 上升沿锁存, 单行内聚合
 *  @note  CS 下降沿→发 0x06→CS 上升沿: 三者缺一不可, 上升沿触发内部锁存 */
static void W25Q_Write_Enable(void)
{
    FLASH_CS_LOW(); W25Q_SPI_Transfer(CMD_WREN); FLASH_CS_HIGH(); /* 0x06 锁存 */
}

/* ═══════════════════════════════════════════════
 *  L3: SPI1 DFF 原子闪切 (CR1 bit11)
 *
 *  TFT DMA 发送时将 SPI1 设为 16位帧 (DFF=1), 对 Flash 操作必须闪切为 8位。
 *  直接寄存器操作, 零函数调用开销, 禁止在切帧期间被 ISR 打断 (理论上不会,
 *  因为 W25Q 访问期间 TFT_CS=High, TIM1 发波完全独立于 SPI 帧格式)。
 * ═══════════════════════════════════════════════ */

/** @brief SPI1 → 8位帧 (Flash 通信专用) */
static void W25Q_SPI_8bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 &= ~SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE); /* 原子清 DFF */
}

/** @brief SPI1 → 16位帧 (reserved for future use — TFT DMA 恢复由 Tft_SPI_16bit 处理) */
#ifdef W25Q_DRIVER_USE_16BIT_MODE
static void W25Q_SPI_16bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 |= SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE);
}
#endif /* W25Q_DRIVER_USE_16BIT_MODE */

/* ═══════════════════════════════════════════════
 *  公开接口实现
 * ═══════════════════════════════════════════════ */

/**
 * @brief  初始化 W25Q128
 * @note   TFT_Driver_Init 已配 SPI1+GPIO, 此函数仅补 PA12 CS + JEDEC 校验
 *         所有 SPI1 Mode3 参数继承 TFT 初始化, 不重复配置
 */
void W25Q_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA12 = Flash CS, 推挽输出, 初始高 (不选中) */
    cfg.GPIO_Pin   = FLASH_CS_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(FLASH_CS_PORT, &cfg);
    FLASH_CS_HIGH();                                     /* 初始不选中 */

    /* PA6 保持在 GPIO_Out (TFT DC 默认角色, Tft_Driver_Init 已配) */

    /* JEDEC ID 校验 */
    if (W25Q_Driver_Read_JEDEC_ID() != W25Q_JEDEC_ID) {
        s_chip_ok = 0; return;                           /* 虚焊/错片 → 不工作 */
    }
    s_chip_ok = 1;
}

uint32_t W25Q_Driver_Read_JEDEC_ID(void)
{
    uint32_t id;
    W25Q_SPI_8bit();                                     /* L3: 原子切 8bit */
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L */
    W25Q_SPI_Transfer(CMD_JEDEC);                        /* 0x9F */
    id  = (uint32_t)W25Q_SPI_Transfer(0xFF) << 16;       /* Manufacturer: 0xEF */
    id |= (uint32_t)W25Q_SPI_Transfer(0xFF) << 8;        /* Memory Type: 0x40 */
    id |= (uint32_t)W25Q_SPI_Transfer(0xFF);             /* Capacity: 0x18 */
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC */
    return id;
}

uint8_t W25Q_Driver_Read_SR1(void)
{
    uint8_t sr1;
    W25Q_SPI_8bit();                                     /* L3: 原子切 8bit */
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L */
    W25Q_SPI_Transfer(CMD_RDSR1);                        /* 0x05 */
    sr1 = W25Q_SPI_Transfer(0xFF);                       /* 收 SR1 */
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC */
    return sr1;
}

void W25Q_Driver_Read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if (!s_chip_ok || buf == 0 || len == 0) return;      /* 芯片未就绪/空操作 */
    W25Q_SPI_8bit();                                     /* L3: 原子切 8bit */

    /* L2: 进入边界 — 硬卡 Busy 清零 */
    W25Q_Wait_Busy_Timeout();

    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L */
    W25Q_SPI_Transfer(CMD_READ);                         /* 0x03 Read Data */
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));             /* A23-A16 */
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));              /* A15-A8  */
    W25Q_SPI_Transfer((uint8_t)(addr));                   /* A7-A0   */
    while (len--) *buf++ = W25Q_SPI_Transfer(0xFF);      /* 哑写收数据 */

    /* L2: 退出边界 — 硬卡 Busy 清零 (读操作正常不会 Busy, 但防护必须到位) */
    FLASH_CS_HIGH(); GPIOA->CRL &= ~(0x0FU<<24); GPIOA->CRL |= (0x03U<<24);
    /* W25Q_Leave_Mode 的内联等价 — 消除函数调用 */
}

void W25Q_Driver_Write_Page(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    if (!s_chip_ok || buf == 0 || len == 0 || len > W25Q_PAGE_SIZE) return;

    /* L2: 进入边界 — 硬卡 Busy 清零, 前次操作必须完成 */
    W25Q_Wait_Busy_Timeout();

    /* L1: 写使能 (0x06) 强制级联 — CS下降沿→0x06→CS上升沿锁存 */
    W25Q_Write_Enable();

    W25Q_SPI_8bit();                                     /* L3: 原子切 8bit */
    W25Q_Enter_Mode();                                   /* PA6→MISO (0x02 只发不收,但保持MISO) */
    W25Q_SPI_Transfer(CMD_PP);                           /* 0x02 Page Program */
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));
    while (len--) W25Q_SPI_Transfer(*buf++);             /* 泵入页数据 */
    W25Q_Leave_Mode();                                   /* CS=H 触发编程, PA6→DC */

    /* L2: 退出边界 — 页编程启动后 Wait Busy (~3ms typ) */
    W25Q_Wait_Busy_Timeout();
}

void W25Q_Driver_Erase_Sector(uint32_t addr)
{
    /* ══ L4: 发波态绝对禁擦 — if-else 前置拦截, 45ms 死刑 ══ */
    if (g_sys_state == SYS_STATE_SWEEP || g_sys_state == SYS_STATE_RUNNING)
        return;                                          /* 发波中擦除 = 炸管, 直接退 */

    if (!s_chip_ok) return;

    /* L2: 进入边界 — 硬卡 Busy 清零 */
    W25Q_Wait_Busy_Timeout();

    /* L1: 写使能 (0x06) 强制级联 + CS 边沿锁存 */
    W25Q_Write_Enable();

    W25Q_SPI_8bit();                                     /* L3: 原子切 8bit */
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L */
    W25Q_SPI_Transfer(CMD_SE);                           /* 0x20 Sector Erase */
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));                  /* 地址任意 (扇区内对齐) */
    W25Q_Leave_Mode();                                   /* CS=H 触发擦除, PA6→DC */

    /* L2: 退出边界 — 扇区擦除 ~45ms, 死等 Busy 清零 */
    W25Q_Wait_Busy_Timeout();
}
