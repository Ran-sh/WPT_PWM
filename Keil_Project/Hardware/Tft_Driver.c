/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735 128x160 TFT 彩屏驱动 — SPI1+DMA (V5.0.1 全字库双路径)
 *
 *  Pinout (SPI1 TDM: TFT + W25Q128 share bus):
 *  +------------------------------------------------------------+
 *  |    STM32F103C8T6              ST7735 128x160 Green Tab      |
 *  |                                                             |
 *  |    PA5 --- SPI1_SCK ------------------> SCL  (18MHz)        |
 *  |    PA7 --- SPI1_MOSI ------------------> SDA  (shared data  |
 *  |    PA4 --- GPIO_PP --------------------> CS   (active low)  |
 *  |    PA6 --- GPIO_PP --------------------> DC   (cmd/data)    |
 *  |    PA0 --- GPIO_PP --------------------> RESET              |
 *  |    PA12 -- GPIO_PP --------------------> BL   (backlight ON/OFF)  |
 *  |                                                             |
 *  |    PA6 --- SPI1_MISO <------------------ W25Q128 DO (Flash  |
 *  |    PB12 -- GPIO_PP --------------------> W25Q128 /CS (Flas  |
 *  |                                                             |
 *  |    SPI Mode3 (CPOL=H, CPHA=2Edge), full-duplex              |
 *  |    160x128 landscape, MADCTL=0xA0, SetWin offset X+1 Y+2    |
 *  |    DMA1_Channel3 pixel pump, WrCmd/WrDat 8-bit polling      |
 *  |                                                             |
 *  |    PA6 dynamic: TFT(DC) <--> W25Q128(MISO)                  |
 *  |      DFF(SPI1_CR1 bit11): 8b(poll) <-> 16b(DMA) atomic swi  |
 *  +------------------------------------------------------------+
 *
 * @note    V4.3.2: Flash 20897 chars (CRC32) -> ROM 76 fallback
 *          SPLASH pure-code 8-frame fade-in (STM32 ROM, no W25Q)
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "TFT_Font_Data.h"
#include "W25Q_Driver.h"
#include "Sys_Timer.h"

extern uint32_t CRC32_Compute(const uint8_t *data, uint32_t len);

#define TFT_DRIVER_CS_PIN   GPIO_Pin_4
#define TFT_DRIVER_DC_PIN   GPIO_Pin_6
#define TFT_DRIVER_RST_PIN  GPIO_Pin_0
#define TFT_DRIVER_BL_PIN   GPIO_Pin_12
#define TFT_DRIVER_BL_PORT  GPIOA

#define TFT_CS_LOW()   GPIO_ResetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_CS_HIGH()  GPIO_SetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_DC_CMD()   GPIO_ResetBits(GPIOA, TFT_DRIVER_DC_PIN)
#define TFT_DC_DATA()  GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN)

#define TFT_DMA_MAX_PIXELS  65535

/* ── DMA 状态 ── */
static uint8_t s_dma_configured = 0;
static uint8_t  s_font_flash_valid = 0;     /* 1=Flash font header CRC32 valid */
static Font_Header g_font_header;           /* RAM-cached header 32B from W25Q */

/* ── 像素缓冲区 ── */
/* Max scaled char: 16×2=32 cols × 16×2=32 rows × 2 bytes = 2048 B for CN.
 * 1024 half-words fits MCU 20KB SRAM (~2KB). 1× scale uses 128 entries. */
static uint16_t s_dma_buf[1024];

/* ── Font scale (V4.5.0: pixel-doubling support) ── */
static uint8_t s_font_scale = 1;   /* 1=1x(8x16/16x16), 2=2x(16x32/32x32) */

/* ── Letter spacing (V4.5.0: inter-char gap, 0-3 pixels) ── */
static uint8_t s_letter_spacing = 0;

static Tft_Config g_tft_config = {1, 0, 0xFFFF, 0x0000};

static void Tft_Driver_Dly(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 9; i++) __NOP();
}

/* ═══════════════════════════════════════════════════════════════
 *  8位基础通信
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_Driver_WrCmd(uint8_t c)
{
    TFT_DC_CMD(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, c);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

static void Tft_Driver_WrDat(uint8_t d)
{
    TFT_DC_DATA(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, d);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

/* ═══════════════════════════════════════════════════════════════
 *  SPI 模式切换 — 8bit(命令) ↔ 16bit(像素 DMA)
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_SPI_8bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 &= ~SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE); /* 原子清 DFF */
}

static void Tft_SPI_16bit(void)
{
    SPI_Cmd(SPI1, DISABLE); SPI1->CR1 |= SPI_CR1_DFF; SPI_Cmd(SPI1, ENABLE);  /* 原子置 DFF */
}

/* ═══════════════════════════════════════════════════════════════
 *  DMA 一次性初始化
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Init(void)
{
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel3);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    dma.DMA_MemoryBaseAddr     = 0;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 0;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    s_dma_configured = 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  DMA 核心传输 (V4.5.0: 超时护底防硬锁)
 * ═══════════════════════════════════════════════════════════════ */

#define TFT_DMA_TIMEOUT_MS  200   /* DMA/SPI 忙等超时: 65535 半字@18MHz≈58ms, 200ms 护底安全 */

static void Tft_DMA_Transfer(const uint16_t* buf, uint32_t count, uint8_t inc_mem)
{
    uint32_t deadline;
    DMA_ClearFlag(DMA1_FLAG_TC3);  /* 清除上轮超时残留: TC 在 DISABLE 后仍保持置位 */
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->CMAR  = (uint32_t)buf;
    DMA1_Channel3->CNDTR = (uint16_t)count;
    if (inc_mem) DMA1_Channel3->CCR |= DMA_MemoryInc_Enable;
    else         DMA1_Channel3->CCR &= ~DMA_MemoryInc_Enable;
    DMA_Cmd(DMA1_Channel3, ENABLE);

    TFT_DC_DATA(); TFT_CS_LOW();
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    deadline = Sys_Timer_Get_Tick() + TFT_DMA_TIMEOUT_MS;
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET) {
        if (deadline - Sys_Timer_Get_Tick() < 0x80000000U) continue;  /* 未超时 */
        /* 超时: 紧急清理, 释放 SPI 总线, 防止系统硬锁 */
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        TFT_CS_HIGH();
        return;
    }
    DMA_ClearFlag(DMA1_FLAG_TC3);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    deadline = Sys_Timer_Get_Tick() + TFT_DMA_TIMEOUT_MS;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
        if (deadline - Sys_Timer_Get_Tick() < 0x80000000U) continue;
        TFT_CS_HIGH();  /* 超时强制释放 CS, 防止 SPI 总线永久占用 */
        return;
    }
    TFT_CS_HIGH();
}

/* ═══════════════════════════════════════════════════════════════
 *  像素泵送 — 双重空闲死等, 防切帧地雷
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Fill(uint32_t pixel_count, uint16_t color)
{
    uint32_t deadline;
    if (pixel_count == 0) return;
    if (pixel_count > TFT_DMA_MAX_PIXELS) pixel_count = TFT_DMA_MAX_PIXELS;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(&color, pixel_count, 0);
    deadline = Sys_Timer_Get_Tick() + TFT_DMA_TIMEOUT_MS;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
        if (deadline - Sys_Timer_Get_Tick() < 0x80000000U) continue;
        break;  /* 超时护底, 强制切回 8bit */
    }
    Tft_SPI_8bit();
}

static void Tft_DMA_Send(const uint16_t* buf, uint32_t pixel_count)
{
    uint32_t deadline;
    if (pixel_count == 0) return;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(buf, pixel_count, 1);
    deadline = Sys_Timer_Get_Tick() + TFT_DMA_TIMEOUT_MS;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
        if (deadline - Sys_Timer_Get_Tick() < 0x80000000U) continue;
        break;  /* 超时护底, 强制切回 8bit */
    }
    Tft_SPI_8bit();
}

/* ═══════════════════════════════════════════════════════════════
 *  SetWin
 * ═══════════════════════════════════════════════════════════════ */

static void SetWin(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += 1; xe += 1;
    ys += 2; ye += 2;
    Tft_Driver_WrCmd(0x2A);
    Tft_Driver_WrDat((uint8_t)(xs >> 8)); Tft_Driver_WrDat((uint8_t)xs);
    Tft_Driver_WrDat((uint8_t)(xe >> 8)); Tft_Driver_WrDat((uint8_t)xe);
    Tft_Driver_WrCmd(0x2B);
    Tft_Driver_WrDat((uint8_t)(ys >> 8)); Tft_Driver_WrDat((uint8_t)ys);
    Tft_Driver_WrDat((uint8_t)(ye >> 8)); Tft_Driver_WrDat((uint8_t)ye);
    Tft_Driver_WrCmd(0x2C);
}

/* ═══════════════════════════════════════════════════════════════
 *  Tft_Driver_Init — 开机三级锁死 (V4.3.0r2 深水区重构)
 *
 *  L1: AFIO+JTAG禁用 绝对第一行, 净化时钟图层
 *  L2: Flash CS(PA12) 强推挽锁高, 封杀开机对灌短路
 *  L3: PA6(共享引脚) ODR 显式初始高, 防飘移
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Init(void)
{
    GPIO_InitTypeDef  gpio;
    SPI_InitTypeDef   spi;
    /* ══ L1: 绝对第一行 — AFIO+JTAG 统合接管, 净化时钟图层 ══ */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); /* 释放 PB3/PB4, 封杀毛刺 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    /* SCK=PA5, MOSI=PA7 (SPI1 共享: TFT + W25Q128) */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* CS=PA4, DC=PA6, RST=PA0 (PA6 动态切换: TFT=GPIO_Out DC, Flash=Input MISO) */
    gpio.GPIO_Pin  = TFT_DRIVER_CS_PIN | TFT_DRIVER_DC_PIN | TFT_DRIVER_RST_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);
    GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN);              /* L3: PA6 ODR 显式锁高, 防飘移 */

    /* BL=PA12, GPIO ON/OFF */
    gpio.GPIO_Pin   = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TFT_DRIVER_BL_PORT, &gpio);
    GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);  /* start OFF */

    TFT_CS_HIGH();

    /* SPI1: Mode 3, 全双工, 18MHz */
    SPI_StructInit(&spi);
    spi.SPI_Direction  = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode       = SPI_Mode_Master;
    spi.SPI_DataSize   = SPI_DataSize_8b;
    spi.SPI_CPOL       = SPI_CPOL_High;
    spi.SPI_CPHA       = SPI_CPHA_2Edge;
    spi.SPI_NSS        = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;  /* 72/4=18MHz, 原始配置 */
    spi.SPI_FirstBit   = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* ── 硬件复位 ── */
    GPIO_ResetBits(GPIOA, TFT_DRIVER_RST_PIN);
    Tft_Driver_Dly(100000);
    GPIO_SetBits(GPIOA, TFT_DRIVER_RST_PIN);
    Tft_Driver_Dly(120000);

    /* ═══════════════════════════════════════════
     *  ST7735 Green Tab 已验证 init
     * ═══════════════════════════════════════════ */

    Tft_Driver_WrCmd(0x11); Tft_Driver_Dly(120000);      /* SLPOUT */

    Tft_Driver_WrCmd(0x3A); Tft_Driver_WrDat(0x05);       /* COLMOD: RGB565 */

    Tft_Driver_WrCmd(0xB1);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrCmd(0xB2);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrCmd(0xB3);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);

    Tft_Driver_WrCmd(0xB4); Tft_Driver_WrDat(0x03);       /* INVCTR */

    Tft_Driver_WrCmd(0xC0);
    Tft_Driver_WrDat(0x28); Tft_Driver_WrDat(0x08); Tft_Driver_WrDat(0x04);
    Tft_Driver_WrCmd(0xC1); Tft_Driver_WrDat(0xC0);
    Tft_Driver_WrCmd(0xC2); Tft_Driver_WrDat(0x0D); Tft_Driver_WrDat(0x00);
    Tft_Driver_WrCmd(0xC3); Tft_Driver_WrDat(0x8D); Tft_Driver_WrDat(0x2A);
    Tft_Driver_WrCmd(0xC4); Tft_Driver_WrDat(0x8D); Tft_Driver_WrDat(0xEE);
    Tft_Driver_WrCmd(0xC5); Tft_Driver_WrDat(0x1A);

    Tft_Driver_WrCmd(0x36); Tft_Driver_WrDat(0xA0);        /* MADCTL */  /* Gamma (+) */
    Tft_Driver_WrCmd(0xE0);
    Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x22);Tft_Driver_WrDat(0x07);Tft_Driver_WrDat(0x0A);
    Tft_Driver_WrDat(0x2E);Tft_Driver_WrDat(0x30);Tft_Driver_WrDat(0x25);Tft_Driver_WrDat(0x2A);
    Tft_Driver_WrDat(0x28);Tft_Driver_WrDat(0x26);Tft_Driver_WrDat(0x2E);Tft_Driver_WrDat(0x3A);
    Tft_Driver_WrDat(0x00);Tft_Driver_WrDat(0x01);Tft_Driver_WrDat(0x03);Tft_Driver_WrDat(0x13);   /* Gamma (-) */
    Tft_Driver_WrCmd(0xE1);
    Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x16);Tft_Driver_WrDat(0x06);Tft_Driver_WrDat(0x0D);
    Tft_Driver_WrDat(0x2D);Tft_Driver_WrDat(0x26);Tft_Driver_WrDat(0x23);Tft_Driver_WrDat(0x27);
    Tft_Driver_WrDat(0x27);Tft_Driver_WrDat(0x25);Tft_Driver_WrDat(0x2D);Tft_Driver_WrDat(0x3B);
    Tft_Driver_WrDat(0x00);Tft_Driver_WrDat(0x01);Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x13);

    Tft_Driver_WrCmd(0x13);                                 /* NORON */

    Tft_Driver_WrCmd(0x3A); Tft_Driver_WrDat(0x05);        /* COLMOD: RGB565 */

    Tft_Driver_WrCmd(0x29); Tft_Driver_Dly(50000);         /* DISPON */

    Tft_Driver_Clear(TFT_COLOR_BLACK);
}

/* ===============================================================
 *  Tft_Driver_Font_Init — 字库初始化 (W25Q_Driver_Init 之后调用)
 *  读取 Flash 字库头, CRC32 校验通过则启用全字库 20897 字,
 *  否则 s_font_flash_valid 保持 0 → ROM 76 字自动回退
 * ============================================================= */

void Tft_Driver_Font_Init(void)
{
    uint32_t crc_stored; uint32_t crc_computed;
    W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)&g_font_header, sizeof(Font_Header));
    s_font_flash_valid = 0;
    if (g_font_header.magic == FONT_MAGIC) {
        crc_stored = g_font_header.crc32; g_font_header.crc32 = 0;
        crc_computed = CRC32_Compute((uint8_t*)&g_font_header + 0x0C, 20);
        g_font_header.crc32 = crc_stored;
        s_font_flash_valid = (crc_stored == crc_computed) ? 1 : 0;
    }
}

/* 公开字库状态查询 (供 Sys_Startup_Screen 显示) */
uint8_t Tft_Driver_Is_Font_Flash_Valid(void)
{
    return s_font_flash_valid;
}

/* ═══════════════════════════════════════════════════════════════
 *  V4.5.0 Tft_Config public API
 * ═══════════════════════════════════════════════════════════════ */

const Tft_Config* Tft_Driver_Get_Config(void)
{
    g_tft_config.font_scale     = s_font_scale;
    g_tft_config.letter_spacing = s_letter_spacing;
    return &g_tft_config;
}

void Tft_Driver_Set_Font_Scale(uint8_t scale)
{
    if (scale < 1) scale = 1;
    if (scale > 2) scale = 2;
    s_font_scale = scale;
    g_tft_config.font_scale = scale;
}

uint8_t Tft_Driver_Get_Font_Scale(void)
{
    return s_font_scale;
}

void Tft_Driver_Set_Letter_Spacing(uint8_t sp)
{
    if (sp > 6) sp = 6;
    s_letter_spacing = sp;
    g_tft_config.letter_spacing = sp;
}

uint8_t Tft_Driver_Get_Letter_Spacing(void)
{
    return s_letter_spacing;
}

/* ═══════════════════════════════════════════════════════════════
 *  基础绘图
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Clear(uint16_t color)
{
    SetWin(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    Tft_DMA_Fill((uint32_t)TFT_WIDTH * TFT_HEIGHT, color);
}

void Tft_Driver_Set_Backlight(uint8_t v)
{
    if (v > 0)
        GPIO_SetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
    else
        GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
}

void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t total;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH  - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    total = (uint32_t)w * h;
    if (total == 0) return;
    SetWin(x, y, x + w - 1, y + h - 1);
    Tft_DMA_Fill(total, color);
}

void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    Tft_Driver_Fill_Rect(x, y, w, h, TFT_COLOR_BLACK);
}

/* ═══════════════════════════════════════════════════════════════
 *  位图解码器 (原样保留, LSB-first)
 * ═══════════════════════════════════════════════════════════════ */

static void Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg,
                           uint16_t* out, uint8_t scale)
{
    uint8_t b;
    if (scale == 1) {
        for (b = 0; b < 8; b++)
            out[b] = (byte_val & (0x01 << b)) ? fg : bg;
    } else {
        /* 2x pixel doubling: each source pixel becomes 2×1 output pixels */
        for (b = 0; b < 8; b++) {
            uint16_t c = (byte_val & (0x01 << b)) ? fg : bg;
            out[b * 2]     = c;
            out[b * 2 + 1] = c;
        }
    }
}

static void Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg,
                          uint16_t* out, uint8_t scale)
{
    uint8_t b;
    if (scale == 1) {
        for (b = 0; b < 8; b++) {
            out[b]     = (lo & (0x01 << b)) ? fg : bg;
            out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
        }
    } else {
        /* 2x pixel doubling: each 16-wide source row becomes 32-wide */
        for (b = 0; b < 8; b++) {
            uint16_t cl = (lo & (0x01 << b)) ? fg : bg;
            uint16_t ch = (hi & (0x01 << b)) ? fg : bg;
            out[b * 2]     = cl;
            out[b * 2 + 1] = cl;
            out[b * 2 + 16] = ch;
            out[b * 2 + 17] = ch;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  ASCII 渲染 — 双路径: Flash 流式读取 / ROM 字模回退
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t idx; uint16_t* p;
    uint8_t char_w = TFT_FONT_WIDTH  * s_font_scale;  /* 8 or 16 */
    uint8_t char_h = TFT_FONT_HEIGHT * s_font_scale;  /* 16 or 32 */
    uint8_t row;
    uint16_t line_h_scaled = (uint16_t)TFT_FONT_HEIGHT * s_font_scale;

    if (line * TFT_FONT_HEIGHT + char_h > TFT_HEIGHT) return;
    if (col * TFT_FONT_WIDTH  + char_w > TFT_WIDTH) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    idx = (uint8_t)(ch - 32);

    if (s_font_flash_valid) {
        uint32_t base;
        uint8_t ascii_rows[16];
        SetWin(col * TFT_FONT_WIDTH, line * line_h_scaled,
               col * TFT_FONT_WIDTH + char_w - 1,
               line * line_h_scaled + char_h - 1);
        base = g_font_header.ascii_offset + (uint32_t)idx * 16;
        W25Q_Driver_Read(base, ascii_rows, 16);  /* 一次读 16B, 替代 16 次单字节读 */
        p = s_dma_buf;
        if (s_font_scale == 1) {
            for (row = 0; row < 16; row++) {
                Decode_Char_Row(ascii_rows[row], fg, bg, p + row * 8, 1);
            }
            Tft_DMA_Send(s_dma_buf, 128);
        } else {
            /* 2x: duplicate each row */
            for (row = 0; row < 16; row++) {
                uint8_t r;
                Decode_Char_Row(ascii_rows[row], fg, bg, p, 2);
                /* copy doubled row to second line */
                for (r = 0; r < 16; r++) p[32 + r] = p[r];
                p += 32;
            }
            Tft_DMA_Send(s_dma_buf, 16 * 32);  /* 16 rows × 16 cols = 256 → 512 B */
        }
        return;
    }

    {
        uint16_t x0, y0;
        x0 = col * TFT_FONT_WIDTH;
        y0 = line * line_h_scaled;
        SetWin(x0, y0, x0 + char_w - 1, y0 + char_h - 1);
        p = s_dma_buf;
        if (s_font_scale == 1) {
            for (row = 0; row < 16; row++)
                Decode_Char_Row(TFT_FONT_8X16[idx][row], fg, bg, p + row * 8, 1);
            Tft_DMA_Send(s_dma_buf, 128);
        } else {
            for (row = 0; row < 16; row++) {
                uint8_t r;
                Decode_Char_Row(TFT_FONT_8X16[idx][row], fg, bg, p, 2);
                for (r = 0; r < 16; r++) p[32 + r] = p[r];
                p += 32;
            }
            Tft_DMA_Send(s_dma_buf, 16 * 32);
        }
    }
}

void Tft_Driver_Show_String(uint8_t line, uint8_t col, const char* s,
                            uint16_t fg, uint16_t bg)
{
    while (*s && col < TFT_CHAR_PER_LINE) {
        Tft_Driver_Show_Char(line, col, *s, fg, bg);
        col++; s++;
    }
}

static uint32_t Tft_Driver_Pw(uint32_t e) { uint32_t r = 1; while (e--) r *= 10; return r; }

void Tft_Driver_Show_Num(uint8_t ln, uint8_t col, uint32_t v,
                         uint8_t len, uint16_t fg, uint16_t bg)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        Tft_Driver_Show_Char(ln, col + i,
            (char)('0' + (v / Tft_Driver_Pw(len - 1 - i)) % 10), fg, bg);
}

void Tft_Driver_Show_Float(uint8_t ln, uint8_t col, float v,
                           uint8_t il, uint8_t fl, uint16_t fg, uint16_t bg)
{
    uint32_t ip, fp, p10 = Tft_Driver_Pw(fl);
    uint8_t i;
    if (v < 0) { Tft_Driver_Show_Char(ln, col, '-', fg, bg); col++; v = -v; }
    v += 0.5f / (float)p10;
    ip = (uint32_t)v;
    fp = (uint32_t)((v - (float)ip) * (float)p10);
    for (i = 0; i < il; i++)
        Tft_Driver_Show_Char(ln, col + i,
            (char)('0' + (ip / Tft_Driver_Pw(il - 1 - i)) % 10), fg, bg);
    Tft_Driver_Show_Char(ln, col + il, '.', fg, bg);
    for (i = 0; i < fl; i++)
        Tft_Driver_Show_Char(ln, col + il + 1 + i,
            (char)('0' + (fp / Tft_Driver_Pw(fl - 1 - i)) % 10), fg, bg);
}

/* ═══════════════════════════════════════════════════════════════
 *  中文渲染 — 双路径: Flash 二分查找 (6763字) / ROM 线性回退 (76字)
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t Tft_Is_UTF8_CN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

static void Tft_Driver_CN_Draw(uint8_t ln, uint8_t col, const uint8_t *utf8,
                               uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;
    uint8_t char_w = (uint16_t)TFT_FONT_WIDTH * 2 * s_font_scale;  /* 16 or 32 */
    uint8_t char_h = (uint16_t)TFT_FONT_HEIGHT * s_font_scale;     /* 16 or 32 */
    uint16_t buf_x_start = col * TFT_FONT_WIDTH;
    uint16_t buf_y_start = ln  * ((uint16_t)TFT_FONT_HEIGHT * s_font_scale);

    if (buf_y_start + char_h > TFT_HEIGHT) return;
    if (buf_x_start + char_w > TFT_WIDTH) return;

    SetWin(buf_x_start, buf_y_start, buf_x_start + char_w - 1, buf_y_start + char_h - 1);
    p = s_dma_buf;

    /* ROM 优先: 检查 CN_INDEX (4 字: 无/线/充/电, SPLASH 专用) */
    {
        uint8_t g_idx; uint8_t i;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx != 0xFF) {
            if (s_font_scale == 1) {
                for (row = 0; row < 16; row++)
                    Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                                   fg, bg, p + row * 16, 1);
                Tft_DMA_Send(s_dma_buf, 256);
            } else {
                for (row = 0; row < 16; row++) {
                    uint8_t r;
                    Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                                   fg, bg, p, 2);
                    for (r = 0; r < 32; r++) p[64 + r] = p[r];
                    p += 64;
                }
                Tft_DMA_Send(s_dma_buf, 16 * 64);
            }
            return;
        }
    }

    /* Flash 路径: ROM 未命中 → W25Q128 全字库 20897 字 */
    if (s_font_flash_valid) {
        uint32_t unicode; uint32_t data_offset; uint32_t base;
        uint8_t cn_glyph[32];
        unicode  = ((uint32_t)(utf8[0] & 0x0F) << 12);
        unicode |= ((uint32_t)(utf8[1] & 0x3F) << 6);
        unicode |= ((uint32_t)(utf8[2] & 0x3F));
        data_offset = W25Q_Font_Index_Binary_Search((uint16_t)unicode, &g_font_header);
        if (data_offset == 0xFFFFFFFFUL) {
            Tft_DMA_Fill((uint32_t)char_w * char_h, bg);
            return;
        }
        base = g_font_header.cjk_data_offset + (uint32_t)data_offset;
        W25Q_Driver_Read(base, cn_glyph, 32);
        if (s_font_scale == 1) {
            for (row = 0; row < 16; row++) {
                Decode_CN_Row(cn_glyph[row * 2], cn_glyph[row * 2 + 1], fg, bg, p + row * 16, 1);
            }
            Tft_DMA_Send(s_dma_buf, 256);
        } else {
            for (row = 0; row < 16; row++) {
                uint8_t r;
                Decode_CN_Row(cn_glyph[row * 2], cn_glyph[row * 2 + 1], fg, bg, p, 2);
                for (r = 0; r < 32; r++) p[64 + r] = p[r];
                p += 64;
            }
            Tft_DMA_Send(s_dma_buf, 16 * 64);
        }
        return;
    }

    /* 无 Flash → 空白 (ROM 已检索, 未命中) */
    Tft_DMA_Fill((uint32_t)char_w * char_h, bg);
}

void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,
                                uint16_t fg, uint16_t bg)
{
    uint8_t as_char_w = TFT_FONT_WIDTH  * s_font_scale;  /* ASCII width: 8 or 16 */
    uint8_t cn_char_w = TFT_FONT_WIDTH  * 2 * s_font_scale; /* CN width: 16 or 32 */
    uint8_t max_x = TFT_WIDTH - as_char_w;

    /* V4.5.0: pure pixel-gap spacing.  Each char drawn at pixel position cur_x,
     *   then cur_x += char_w + s_letter_spacing.  Background fill bridges the gap. */
    {
        uint16_t cur_x = (uint16_t)col * TFT_FONT_WIDTH;
        while (*s && cur_x <= max_x) {
            if (Tft_Is_UTF8_CN((uint8_t)*s) && *(s+1) && *(s+2)) {
                if (cur_x + cn_char_w > TFT_WIDTH) break;
                /* Redraw spacer region as BG first, then draw glyph */
                if (s_letter_spacing > 0)
                    Tft_Driver_Fill_Rect(cur_x, (uint16_t)ln * (TFT_FONT_HEIGHT * s_font_scale),
                        s_letter_spacing, (uint16_t)TFT_FONT_HEIGHT * s_font_scale, bg);
                Tft_Driver_CN_Draw(ln, (uint8_t)(cur_x / TFT_FONT_WIDTH), (const uint8_t*)s, fg, bg);
                cur_x += cn_char_w + s_letter_spacing;
                s += 3;
            } else {
                if (cur_x + as_char_w > TFT_WIDTH) break;
                if (s_letter_spacing > 0)
                    Tft_Driver_Fill_Rect(cur_x, (uint16_t)ln * (TFT_FONT_HEIGHT * s_font_scale),
                        s_letter_spacing, (uint16_t)TFT_FONT_HEIGHT * s_font_scale, bg);
                Tft_Driver_Show_Char(ln, (uint8_t)(cur_x / TFT_FONT_WIDTH), *s, fg, bg);
                cur_x += as_char_w + s_letter_spacing;
                s++;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  WiFi 图标 — 片内 ROM 读取
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;
    if (frame > 3) frame = 3;

    SetWin(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(WIFI_ICON[frame][row * 2], WIFI_ICON[frame][row * 2 + 1],
                       fg, bg, p + row * 16, 1);

    Tft_DMA_Send(s_dma_buf, 256);
}

void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],
                                  uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;

    SetWin(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(data[row * 2], data[row * 2 + 1], fg, bg, p + row * 16, 1);

    Tft_DMA_Send(s_dma_buf, 256);
}

/* ═══════════════════════════════════════════════════════════════
 *  5×10 微型数字 — 片内保留 (120B, 零 Flash 等待)
 * ═══════════════════════════════════════════════════════════════ */

static const uint8_t FONT_5X10[12][10] = {
    {0x00,0x00,0x06,0x09,0x09,0x09,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x04,0x06,0x04,0x04,0x04,0x04,0x0E,0x00},
    {0x00,0x00,0x0E,0x0A,0x08,0x04,0x04,0x02,0x0E,0x00},
    {0x00,0x00,0x0E,0x0A,0x08,0x04,0x08,0x0A,0x0E,0x00},
    {0x00,0x00,0x08,0x0C,0x0A,0x09,0x1F,0x08,0x1C,0x00},
    {0x00,0x00,0x0E,0x02,0x0E,0x08,0x08,0x0A,0x06,0x00},
    {0x00,0x00,0x0E,0x01,0x07,0x09,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x0E,0x08,0x08,0x04,0x04,0x04,0x04,0x00},
    {0x00,0x00,0x06,0x09,0x09,0x06,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x06,0x09,0x09,0x09,0x0F,0x0C,0x07,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static uint8_t Map_5x10_Idx(char ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch == '.') return 10;
    return 11;
}

void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,
                                        const char* s,
                                        uint16_t fg, uint16_t bg)
{
    uint8_t row, b, idx; uint16_t* p;

    while (*s && x + 5 <= TFT_WIDTH) {
        idx = Map_5x10_Idx(*s);
        SetWin(x, y, x + 4, y + 9);

        p = s_dma_buf;
        for (row = 0; row < 10; row++) {
            uint8_t byte_val = FONT_5X10[idx][row];
            for (b = 0; b < 5; b++)
                *p++ = (byte_val & (0x01 << b)) ? fg : bg;
        }

        Tft_DMA_Send(s_dma_buf, 50);
        x += 7; s++;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Icon By ID — 按编号从 Flash 读取 16x16 图标 (V4.4.0: ROM 不再存储 20 图标)
 * ═══════════════════════════════════════════════════════════════ */

uint8_t Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y, uint8_t icon_id,
                                   uint8_t frame, uint16_t fg, uint16_t bg)
{
    /* V4.4.0: 图标数据在 W25Q128 Flash 中 (via Font_Header icon_table).
     *  无 Flash 时: WIFI/MQTT/ICON_STAR (id 0-8) 可走 ROM 回退.
     *  其他 id 11-30 仅 Flash 可用. */
    /* Note: 此函数签名已改为有 frame 参数, 与 .h 一致.
     *  Flash 路径通过 W25Q_Read icon_table→data 实现.
     *  为快速修复, 先实现 Flash-only 路径, ROM 回退仅保留 STAR/WIFI/MQTT. */
    uint8_t row; uint16_t* p;

    if (icon_id > 34) return 0;

    SetWin(x, y, x + 15, y + 15);
    p = s_dma_buf;

    /* ROM 回退: WIFI (0-3), MQTT (4-7), STAR (8) — ROM 优先 */
    switch (icon_id) {
        case 0:  /* WIFI_SIGNAL: 按 frame 选信号强度帧 */
            if (frame > 3) frame = 3;
            for (row = 0; row < 16; row++)
                Decode_CN_Row(WIFI_ICON[frame][row*2], WIFI_ICON[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        case 1:  /* WIFI_CONNECT_ANIM: 按 frame 选动画帧 */
            if (frame > 5) frame = 5;
            for (row = 0; row < 16; row++)
                Decode_CN_Row(WIFI_CONNECT_ANIM[frame][row*2], WIFI_CONNECT_ANIM[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        case 2:  /* WIFI_OFF — 带×的静态图标 */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(WIFI_OFF_ICON[row*2], WIFI_OFF_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        case 3:  /* WIFI_REMOVE — 带减号的静态图标 */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(WIFI_REMOVE_ICON[row*2], WIFI_REMOVE_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        case 4:  /* MQTT_ICON 静态 */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(MQTT_ICON[row*2], MQTT_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256); return 1;
        case 5:  /* MQTT_YES 静态 */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(MQTT_YES_ICON[row*2], MQTT_YES_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256); return 1;
        case 6:  /* MQTT_NO 静态 */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(MQTT_NO_ICON[row*2], MQTT_NO_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256); return 1;
        case 7:  /* MQTT_ANIM 6帧 */
            if (frame > 5) frame = 5;
            for (row = 0; row < 16; row++)
                Decode_CN_Row(MQTT_ANIM[frame][row*2], MQTT_ANIM[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256); return 1;
        case 8:  /* ICON_STAR */
            for (row = 0; row < 16; row++)
                Decode_CN_Row(ICON_STAR[row*2], ICON_STAR[row*2+1], fg, bg, p + row*16, 1);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        default: break;  /* ID 9-34: 仅 Flash */
    }

    /* Flash path: ID 9+ 或无 ROM 的图标 */
    if (s_font_flash_valid) {
        /* Read icon frame from Flash icon_data area
         * Flash layout (from generate_font.py OFF_ICONS):
         *   0x000700: Icon Table Header 16B
         *   0x000710: Icon Entries 31×8B = 248B
         *   0x000808: Icon Bitmap Data
         * Entry 8B: [id:2][n_frames:2][data_offset:2][reserved:2] */
        uint32_t icon_idx_base, icon_data_base, data_offset, frame_offset;
        uint8_t n_frames, icon_entry[8], icon_glyph[32];
        icon_idx_base = 0x000710U;  /* icon entries start */
        /* Read 8-byte entry: [id:2][n_frames:2][data_offset:2][reserved:2] */
        W25Q_Driver_Read(icon_idx_base + (uint32_t)icon_id * 8, icon_entry, 8);
        n_frames     = icon_entry[2] | (uint16_t)icon_entry[3] << 8;
        data_offset  = (uint32_t)icon_entry[4] | (uint32_t)icon_entry[5] << 8;
        icon_data_base = 0x000808U;  /* icon bitmap data start */
        if (frame >= n_frames) frame = n_frames - 1;
        frame_offset = icon_data_base + data_offset + (uint32_t)frame * 32;
        W25Q_Driver_Read(frame_offset, icon_glyph, 32);  /* 一次读 32B */
        for (row = 0; row < 16; row++) {
            Decode_CN_Row(icon_glyph[row * 2], icon_glyph[row * 2 + 1], fg, bg, p + row * 16, 1);
        }
        Tft_DMA_Send(s_dma_buf, 256);
        return 1;
    }

    /* 无 Flash 且无 ROM → 空白 */
    { uint8_t i; for (i = 0; i < 256; i++) p[i] = bg; }
    Tft_DMA_Send(s_dma_buf, 256);
    return 0;
}

/* ===============================================================
 *  SPLASH 开机动画 — 纯代码实现 (V4.3.2)
 *
 *  布局 (160×128):
 *  +----------------------------------+
 *  |                           V4.3.2|  Row 7 col14: 版本号, 右下角暗灰
 *  |                                  |
 *  |     无  线  充  电               |  逐字出现, 间隔空格, 亮黄
 *  |                                  |
 *  |          W  P  T                 |  逐字出现, 亮青
 *  +----------------------------------+
 *
 *  动画 (总长约 4.8s):
 *    Phase 1 (1200ms): 背光渐亮 0->248, 8帧x150ms
 *    Phase 2 (3200ms): 4字x8帧x50ms = 400ms/字, 每字渐亮
 *    Hold   (400ms):   全亮定格
 *
 *  使用字库: ROM 76 字 — 无 线 充 电, 不依赖 Flash
 * ============================================================= */

void Tft_Driver_Show_Splash(void)
{
    uint8_t i, bl, col;
    /* UTF-8: 无 U+65E0  线 U+7EBF  充 U+5145  电 U+7535 */
    const char s_cn[4][4] = {
        "\xe6\x97\xa0", "\xe7\xba\xbf", "\xe5\x85\x85", "\xe7\x94\xb5"
    };

    Tft_Driver_Set_Backlight(0);
    Tft_Driver_Clear(TFT_COLOR_BLACK);

    /* Phase 1: 背光渐亮 0->248, 8帧x150ms=1200ms */
    for (i = 1; i <= 8; i++) {
        bl = (uint8_t)(i * 248 / 8);
        Tft_Driver_Set_Backlight(bl);
        Sys_Timer_Delay_Ms(150);
    }

    /* 版本号: 右下角暗灰 */
    Tft_Driver_Show_String(7, 14, "V5.0.1", 0x3186U, TFT_COLOR_BLACK);

    /* Phase 2: 两行同时逐字点亮, 每字 8帧渐亮x50ms=400ms, 4字=1600ms */
    for (col = 0; col < 4; col++) {
        uint16_t cn_fg, wpt_fg;
        uint8_t  sub;

        for (sub = 0; sub < 8; sub++) {
            /* 当前字颜色: 从暗到亮 (0x3186 暗 -> 0xFFE0 亮黄 / 0x031F 暗 -> 0x07FF 亮青) */
            cn_fg  = 0x31BEU + (uint16_t)((0xFFE0U - 0x31BEU) * (sub + 1) / 8);
            wpt_fg = 0x031FU + (uint16_t)((0x07FFU - 0x031FU) * (sub + 1) / 8);

            /* 之前已点亮的字保持全亮 */
            if (col > 0) {
                uint8_t prev;
                for (prev = 0; prev < col; prev++)
                    Tft_Driver_Show_CN_String(2, (uint8_t)(5 + prev * 2),
                        s_cn[prev], 0xFFE0U, TFT_COLOR_BLACK);
                for (prev = 0; prev < col && prev < 3; prev++)
                    Tft_Driver_Show_String(5, (uint8_t)(8 + prev),
                        "WPT" + prev, 0x07FFU, TFT_COLOR_BLACK);
            }

            /* 当前字: 渐亮 */
            Tft_Driver_Show_CN_String(2, (uint8_t)(5 + col * 2),
                s_cn[col], cn_fg, TFT_COLOR_BLACK);

            if (col < 3)
                Tft_Driver_Show_String(5, (uint8_t)(8 + col),
                    "WPT" + col, wpt_fg, TFT_COLOR_BLACK);

            Sys_Timer_Delay_Ms(50);
        }
    }

    /* Phase 3: 全亮定格 400ms */
    Sys_Timer_Delay_Ms(400);
}
