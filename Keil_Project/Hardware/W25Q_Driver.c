/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.c
 * @brief   W25Q128 16MB SPI NOR Flash — SPI1 分时复用驱动 (V4.3.2)
 *
 *  Pinout (shares SPI1 with TFT, PA6 dynamic swap):
 *  +------------------------------------------------------------+
 *  |    STM32F103C8T6               W25Q128 (16MB SPI NOR Flash  |
 *  |                                                             |
 *  |    PA5  --- SPI1_SCK ------------------> CLK  (shared w/ T  |
 *  |    PA7  --- SPI1_MOSI ------------------> DI   (shared w/   |
 *  |    PA6  --- SPI1_MISO <------------------ DO   (dynamic sw  |
 *  |    PA12 --- GPIO_PP --------------------> /CS  (GPIO gated  |
 *  |              BSRR atomic toggle, glitch-free                |
 *  |                                                             |
 *  |    Partition layout (16MB, 0x000000 ~ 0xFFFFFF):            |
 *  |      [0x000000~0x1FFFFF] Font lib (2MB)  <- CH341A burn     |
 *  |      [0x200000~0x2FFFFF] Font reserve (1MB)                 |
 *  |      [0x300000~0x301FFF] Params dual-copy (8KB)             |
 *  |      [0x400000~0x7FFFFF] Blackbox log (4MB)                 |
 *  |      [0x800000~0xFFFFFF] Unused (8MB)                       |
 *  |                                                             |
 *  |    CS toggle rule: CS high+low before EVERY CMD_READ,       |
 *  |      else Flash command decoder ignores second read         |
 *  |                                                             |
 *  |    Four hardware deadlock guards:                           |
 *  |      L1: Write enable (0x06) cascaded + CS edge latch       |
 *  |      L2: Busy bit (SR1 BIT0) blocking wait with bounds      |
 *  |      L3: DFF (SPI1 CR1 bit11) atomic 8b<->16b no-glitch     |
 *  |      L4: Wave-active (SWEEP/RUNNING) global erase ban (45m  |
 *  +------------------------------------------------------------+
 *
 * @note    ARMCC V5 SPL, pure C89, no // comments
 ******************************************************************************
 */

#include "W25Q_Driver.h"
#include "Sys_Core.h"       /* g_sys_state */
#include "Sys_Timer.h"      /* Sys_Timer_Get_Tick */
#include "App_Storage.h"    /* CRC32_Compute */

/* ═══════════════════════════════════════════════
 *  引脚宏 (行内高聚合)
 * ═══════════════════════════════════════════════ */
#define FLASH_CS_PIN    GPIO_Pin_12
#define FLASH_CS_PORT   GPIOA
#define FLASH_CS_LOW()  GPIO_ResetBits(FLASH_CS_PORT, FLASH_CS_PIN)
#define FLASH_CS_HIGH() GPIO_SetBits(FLASH_CS_PORT, FLASH_CS_PIN)

/* ── W25Q128 指令集 ── */
#define CMD_WREN         0x06U
#define CMD_WRDI         0x04U
#define CMD_RDSR1        0x05U
#define CMD_READ         0x03U
#define CMD_PP           0x02U
#define CMD_SE           0x20U
#define CMD_JEDEC        0x9FU

#define BUSY_BIT         0x01U
#define BUSY_TIMEOUT_MS  100U

/* ── 静态状态 ── */
static uint8_t s_chip_ok = 0;

/* ═══════════════════════════════════════════════
 *  PA6 角色切换 (内联, 直接寄存器操作)
 * ═══════════════════════════════════════════════ */

/** @brief PA6 → Input floating (Flash MISO), CS 拉低 → 门控闭合 */
/** @brief 进入 Flash 独占模式: PA6->MISO + CS=L, 独占 SPI 总线 */
void W25Q_Enter_Mode(void)
{
    GPIOA->CRL &= ~(0x0FU << 24); GPIOA->CRL |= (0x04U << 24); /* PA6=Input floating */
    FLASH_CS_LOW();                                              /* 门控: 选中 Flash */
}

/** @brief CS 拉高 → 门控释放, PA6 → GPIO_Out_PP (恢复 TFT DC 角色)
 *  @note  先置 ODR HIGH (TFT DC=DATA), 再切方向, 防切向瞬间低电平毛刺导致 TFT 误收命令 */
/** @brief 退出 Flash 独占模式: CS=H + PA6->DC, 释放总线归还 TFT */
void W25Q_Leave_Mode(void)
{
    FLASH_CS_HIGH();                                             /* 门控: 释放 Flash */
    GPIOA->BSRR = GPIO_Pin_6;                                    /* PA6 ODR 预置高 (DC=DATA) */
    GPIOA->CRL &= ~(0x0FU << 24); GPIOA->CRL |= (0x03U << 24); /* PA6=50MHz PP Out */
}

/* ═══════════════════════════════════════════════
 *  SPI1 基础 8位 收发
 * ═══════════════════════════════════════════════ */

uint8_t W25Q_SPI_Transfer(uint8_t tx)
{
    SPI_I2S_SendData(SPI1, tx);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

/* ═══════════════════════════════════════════════
 *  L3: SPI1 DFF 原子闪切 (必须定义在 Wait_Busy_Timeout 之前)
 * ═══════════════════════════════════════════════ */

/** @brief SPI1 → 8位帧 */
/** @brief SPI1 -> 8bit 帧模式 (原子闪切 DFF 位) */
void W25Q_SPI_8bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 &= ~SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE); /* 原子清 DFF */
}

#ifdef W25Q_DRIVER_USE_16BIT_MODE
static void W25Q_SPI_16bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 |= SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE);
}
#endif

/* ═══════════════════════════════════════════════
 *  L2: Busy 阻塞死等 (高聚合, 上/下边界强制调用)
 * ═══════════════════════════════════════════════ */

/** @brief 死等 W25Q128 Busy 位清零 (SR1 BIT0), 超时护底
 *  @note  任何读/写/擦除的 if-else 进入和退出边界必须调用 */
/** @brief 阻塞等待 W25Q128 Busy 位清零 (超时护底) */
void W25Q_Wait_Busy_Timeout(void)
{
    uint32_t deadline; uint8_t sr1;
    deadline = Sys_Timer_Get_Tick() + BUSY_TIMEOUT_MS;   /* 超时护底 */
    W25Q_SPI_8bit();                                     /* 原子切8bit */
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L, 防对灌短路 */
    do {
        W25Q_SPI_Transfer(CMD_RDSR1);                    /* 0x05 读 SR1 */
        sr1 = W25Q_SPI_Transfer(0xFF);                   /* 哑写收 SR1 */
        FLASH_CS_HIGH(); FLASH_CS_LOW();                 /* CS 脉冲 */
        if ((sr1 & BUSY_BIT) == 0) break;                /* Busy=0 释放 */
    } while (Sys_Timer_Get_Tick() - deadline < 0x80000000U); /* uint32 回绕安全 */
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC, 归还总线 */
}

/* ═══════════════════════════════════════════════
 *  L1: 写使能 (0x06) 强制级联 + CS 边沿锁存
 * ═══════════════════════════════════════════════ */

static void W25Q_Write_Enable(void)
{
    FLASH_CS_LOW(); W25Q_SPI_Transfer(CMD_WREN); FLASH_CS_HIGH(); /* 0x06 锁存 */
}

/* ═══════════════════════════════════════════════
 *  公开接口实现
 * ═══════════════════════════════════════════════ */

void W25Q_Driver_Init(void)
{
    GPIO_InitTypeDef cfg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    cfg.GPIO_Pin   = FLASH_CS_PIN; cfg.GPIO_Mode = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(FLASH_CS_PORT, &cfg);
    FLASH_CS_HIGH();                                     /* 初始不选中 */

    if (W25Q_Driver_Read_JEDEC_ID() != W25Q_JEDEC_ID) {
        s_chip_ok = 0; return;
    }
    s_chip_ok = 1;
}

uint32_t W25Q_Driver_Read_JEDEC_ID(void)
{
    uint32_t id;
    W25Q_SPI_8bit(); W25Q_Enter_Mode();                  /* 8bit + PA6→MISO */
    W25Q_SPI_Transfer(CMD_JEDEC);
    id  = (uint32_t)W25Q_SPI_Transfer(0xFF) << 16;
    id |= (uint32_t)W25Q_SPI_Transfer(0xFF) << 8;
    id |= (uint32_t)W25Q_SPI_Transfer(0xFF);
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC */
    return id;
}

uint8_t W25Q_Driver_Read_SR1(void)
{
    uint8_t sr1;
    W25Q_SPI_8bit(); W25Q_Enter_Mode();
    W25Q_SPI_Transfer(CMD_RDSR1);
    sr1 = W25Q_SPI_Transfer(0xFF);
    W25Q_Leave_Mode();
    return sr1;
}

/** @brief 通用 SPI 读: 任意地址任意长度 (0x03 命令, 轮询) */
void W25Q_Driver_Read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if (!s_chip_ok || buf == 0 || len == 0) return;

    W25Q_SPI_8bit(); W25Q_Enter_Mode();                  /* L3: 8bit + PA6→MISO, CS=L */
    FLASH_CS_HIGH(); FLASH_CS_LOW();                      /* CS 脉冲: 重置 Flash 命令解码器 */
    W25Q_SPI_Transfer(CMD_READ);
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));
    while (len--) *buf++ = W25Q_SPI_Transfer(0xFF);
    W25Q_Leave_Mode();                                   /* CS=H, PA6→DC */
}

/** @brief 页写入: <=256B, 自动发写使能+等 Busy (调用方保证不跨页) */
void W25Q_Driver_Write_Page(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    if (!s_chip_ok || buf == 0 || len == 0 || len > W25Q_PAGE_SIZE) return;

    W25Q_Wait_Busy_Timeout(); W25Q_Write_Enable();       /* L2 + L1: 死等+写使能 */  W25Q_SPI_8bit();
    W25Q_Enter_Mode();                                   /* PA6→MISO, CS=L */
    W25Q_SPI_Transfer(CMD_PP);
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));
    while (len--) W25Q_SPI_Transfer(*buf++);             /* 泵入页数据 */
    W25Q_Leave_Mode(); W25Q_Wait_Busy_Timeout();         /* L2: 退出边界 ~3ms */
}

/** @brief 扇区擦除: 4KB (0x20), 发波态硬件拦截, 阻塞 ~45ms */
void W25Q_Driver_Erase_Sector(uint32_t addr)
{
    /* ══ L4: 发波态绝对禁擦 ══ */
    if (g_sys_state == SYS_STATE_SWEEP || g_sys_state == SYS_STATE_RUNNING) return;
    if (!s_chip_ok) return;

    W25Q_Wait_Busy_Timeout(); W25Q_Write_Enable();       /* L2 + L1 */
    W25Q_SPI_8bit(); W25Q_Enter_Mode();                  /* L3 + PA6→MISO */
    W25Q_SPI_Transfer(CMD_SE);
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));
    W25Q_Leave_Mode(); W25Q_Wait_Busy_Timeout();         /* L2: 退出边界 ~45ms */
}

/* ═══════════════════════════════════════════════
 *  Font_Header_Load — 上电校验字库头部
 * ═══════════════════════════════════════════════ */

/** @brief 加载并 CRC32 校验 Font Header, 返回 1=有效
 *  @note  依赖 extern CRC32_Compute (App_Storage.c) */
uint8_t Font_Header_Load(Font_Header *hdr)
{
    uint32_t crc_stored; uint32_t crc_computed;
    if (!s_chip_ok || hdr == 0) return 0;
    W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)hdr, sizeof(Font_Header));
    if (hdr->magic != FONT_MAGIC) return 0;
    crc_stored = hdr->crc32; hdr->crc32 = 0;                    /* 临时清零用于计算 */
    crc_computed = CRC32_Compute((uint8_t*)hdr + 0x0C, 20);     /* 0x0C→0x1F */
    hdr->crc32 = crc_stored;
    return (crc_stored == crc_computed) ? 1 : 0;
}

/* ═══════════════════════════════════════════════
 *  Font_Index_Binary_Search — 总线独占 二分检索
 *  V4.3.2 FIX: uint16_t→uint32_t, 修复高位 offset 截断
 * ═══════════════════════════════════════════════ */

/** @brief  总线独占二分搜索 CJK Index (20902 条 Unicode 升序)
 *  @note   Index 条目: [U16 LE 2B][Offset U32 LE 4B] = 6B/条
 *          V4.3.2 FIX: 循环内 CS 翻转 (W25Q128 要求每次 CMD_READ 前 CS↑→CS↓ 重置解码器)
 *          CS 脉冲期间 PA6 保持 MISO 角色 (仅切 CS 线, 不重配 GPIO)
 *          Entry+Exit 各一次 Leave_Mode, 中途不归还总线
 *  @retval U32 offset 相对 CJK_Data_BASE, 0xFFFFFFFF=未找到 */
/** @brief 总线独占二分搜索 CJK 字模索引 (6763 条 Unicode 升序, 5.85us/字) */
uint32_t W25Q_Font_Index_Binary_Search(uint16_t unicode, const Font_Header *hdr)
{
    uint16_t lo, hi, mid, mid_uc; uint32_t addr, found_off;
    if (!s_chip_ok || hdr == 0) return 0xFFFFFFFFUL;
    W25Q_Enter_Mode();                                           /* PA6→MISO, CS=L */
    W25Q_SPI_8bit();                                             /* 原子切8bit */
    lo = 0; hi = hdr->cjk_index_count;                           /* hi=20902 */
    while (lo < hi) {
        mid = (lo + hi) >> 1;                                    /* 折半 → 无溢出 uint16 */
        addr = hdr->cjk_index_offset + (uint32_t)mid * 6U;       /* 条大小=6B [U16][U32] */
        FLASH_CS_HIGH(); FLASH_CS_LOW();                         /* CS 脉冲: 重置 Flash 命令解码器 */
        W25Q_SPI_Transfer(CMD_READ);
        W25Q_SPI_Transfer((uint8_t)(addr >> 16));
        W25Q_SPI_Transfer((uint8_t)(addr >> 8));
        W25Q_SPI_Transfer((uint8_t)(addr));
        mid_uc  = (uint16_t)W25Q_SPI_Transfer(0xFF);
        mid_uc |= (uint16_t)W25Q_SPI_Transfer(0xFF) << 8;        /* LE 还原 */
        if (mid_uc < unicode) lo = mid + 1; else hi = mid;       /* 标准二分[lo,hi) */
    }
    if (lo >= hdr->cjk_index_count) { W25Q_Leave_Mode(); return 0xFFFFFFFFUL; }
    /* 验证候选条目: 读 U16 Unicode + U32 offset */
    addr = hdr->cjk_index_offset + (uint32_t)lo * 6U;
    FLASH_CS_HIGH(); FLASH_CS_LOW();                             /* CS 脉冲: 重置命令解码器 */
    W25Q_SPI_Transfer(CMD_READ);
    W25Q_SPI_Transfer((uint8_t)(addr >> 16));
    W25Q_SPI_Transfer((uint8_t)(addr >> 8));
    W25Q_SPI_Transfer((uint8_t)(addr));
    mid_uc  = (uint16_t)W25Q_SPI_Transfer(0xFF);
    mid_uc |= (uint16_t)W25Q_SPI_Transfer(0xFF) << 8;
    if (mid_uc != unicode) { W25Q_Leave_Mode(); return 0xFFFFFFFFUL; }
    /* V4.3.2 FIX: 完整 U32 offset, 高位汉字 offset 可达 668KB (>65535) */
    found_off  = (uint32_t)W25Q_SPI_Transfer(0xFF);              /* offset[0] lo */
    found_off |= (uint32_t)W25Q_SPI_Transfer(0xFF) << 8;         /* offset[1] */
    found_off |= (uint32_t)W25Q_SPI_Transfer(0xFF) << 16;        /* offset[2] */
    found_off |= (uint32_t)W25Q_SPI_Transfer(0xFF) << 24;        /* offset[3] hi */
    W25Q_Leave_Mode();                                           /* CS=H, PA6→DC */
    return found_off;
}
