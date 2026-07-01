/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735 128x160 TFT 彩屏驱动 — SPI1+DMA (V4.3.2 全字库双路径)
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
 *  |    PB6 --- TIM4_CH1 -------------------> BL   (backlight P  |
 *  |                                                             |
 *  |    PA6 --- SPI1_MISO <------------------ W25Q128 DO (Flash  |
 *  |    PA12 -- GPIO_PP --------------------> W25Q128 /CS (Flas  |
 *  |                                                             |
 *  |    SPI Mode3 (CPOL=H, CPHA=2Edge), full-duplex              |
 *  |    160x128 landscape, MADCTL=0xA0, SetWin offset X+1 Y+2    |
 *  |    DMA1_Channel3 pixel pump, WrCmd/WrDat 8-bit polling      |
 *  |                                                             |
 *  |    PA6 dynamic: TFT(DC) <--> W25Q128(MISO)                  |
 *  |      DFF(SPI1_CR1 bit11): 8b(poll) <-> 16b(DMA) atomic swi  |
 *  +------------------------------------------------------------+
 *
 * @note    V4.3.2: Flash V2 48B header (icon_table_offset)
 *          全字库 20897 字 + 31图标/54帧 -> 仅 Flash 读取
 *          ROM 仅 SPLASH: ASCII 95 + CN 4字 (无/线/充/电)
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
#define TFT_DRIVER_BL_PIN   GPIO_Pin_6

#define TFT_CS_LOW()   GPIO_ResetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_CS_HIGH()  GPIO_SetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_DC_CMD()   GPIO_ResetBits(GPIOA, TFT_DRIVER_DC_PIN)
#define TFT_DC_DATA()  GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN)

#define TFT_DMA_MAX_PIXELS  65535

/* ── DMA 状态 + Flash 字库缓存 ── */
static uint8_t s_dma_configured = 0;
static uint8_t  s_font_flash_valid = 0;     /* 1=Flash V2 font header CRC32 valid */
static Font_Header g_font_header;           /* RAM-cached header 48B from W25Q (V2) */

/* ── Icon Table RAM cache (Font_Init 一次加载) ── */
#define ICON_TABLE_MAX 32
typedef struct {
    uint16_t icon_id;
    uint16_t n_frames;
    uint16_t data_offset;   /* relative to g_font_header.icon_data_offset */
} IconEntry;
static IconEntry s_icon_table[ICON_TABLE_MAX];
static uint8_t   s_icon_count = 0;
static uint32_t  s_icon_data_base = 0;

/* ── 像素缓冲区 ── */
static uint16_t s_dma_buf[256];

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
 *  DMA 核心传输
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Transfer(const uint16_t* buf, uint32_t count, uint8_t inc_mem)
{
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->CMAR  = (uint32_t)buf;
    DMA1_Channel3->CNDTR = (uint16_t)count;
    if (inc_mem) DMA1_Channel3->CCR |= DMA_MemoryInc_Enable;
    else         DMA1_Channel3->CCR &= ~DMA_MemoryInc_Enable;
    DMA_Cmd(DMA1_Channel3, ENABLE);

    TFT_DC_DATA(); TFT_CS_LOW();
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);    /* 死等内存搬迁 */
    DMA_ClearFlag(DMA1_FLAG_TC3);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET); /* 死等移位寄存器吐尽 */
    TFT_CS_HIGH();
}

/* ═══════════════════════════════════════════════════════════════
 *  像素泵送 — 双重空闲死等, 防切帧地雷
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Fill(uint32_t pixel_count, uint16_t color)
{
    if (pixel_count == 0) return;
    if (pixel_count > TFT_DMA_MAX_PIXELS) pixel_count = TFT_DMA_MAX_PIXELS;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(&color, pixel_count, 0);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET); /* 死等末帧泵尽 */
    Tft_SPI_8bit();
}

static void Tft_DMA_Send(const uint16_t* buf, uint32_t pixel_count)
{
    if (pixel_count == 0) return;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(buf, pixel_count, 1);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET); /* 死等末帧泵尽 */
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
    TIM_TimeBaseInitTypeDef  tim_base;
    TIM_OCInitTypeDef        oc;

    /* ══ L1: 绝对第一行 — AFIO+JTAG 统合接管, 净化时钟图层 ══ */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); /* 释放 PB3/PB4, 封杀毛刺 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    /* ══ L2: Flash CS 无条件前置锁死, 封杀开机对灌短路 ══ */
    gpio.GPIO_Pin   = GPIO_Pin_12; gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOA, &gpio);
    GPIO_SetBits(GPIOA, GPIO_Pin_12);                    /* CS=H → W25Q128 高阻悬空 */

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

    /* BL=PB6, TIM4_CH1 */
    gpio.GPIO_Pin  = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);

    TFT_CS_HIGH();

    /* SPI1: Mode 3, 全双工, 18MHz */
    SPI_StructInit(&spi);
    spi.SPI_Direction  = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode       = SPI_Mode_Master;
    spi.SPI_DataSize   = SPI_DataSize_8b;
    spi.SPI_CPOL       = SPI_CPOL_High;
    spi.SPI_CPHA       = SPI_CPHA_2Edge;
    spi.SPI_NSS        = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    spi.SPI_FirstBit   = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* TIM4 CH1 背光 1kHz */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler = 71;
    tim_base.TIM_Period    = 999;
    TIM_TimeBaseInit(TIM4, &tim_base);
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 999;
    TIM_OC1Init(TIM4, &oc);
    TIM_Cmd(TIM4, ENABLE);

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
 *  V2: 48B header CRC32 覆盖 0x0C->0x2F, 解析 icon_table 缓存
 *  CRC32 有效则启用 Flash 全字库 20897 字 + 31 图标/54 帧
 * ============================================================= */

void Tft_Driver_Font_Init(void)
{
    uint32_t crc_stored; uint32_t crc_computed;
    s_font_flash_valid = 0; s_icon_count = 0; s_icon_data_base = 0;

    W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)&g_font_header, sizeof(Font_Header));
    if (g_font_header.magic != FONT_MAGIC) return;
    if (g_font_header.version < 2) return;                   /* V2+ 才含 icon 字段 */

    crc_stored = g_font_header.crc32; g_font_header.crc32 = 0;
    crc_computed = CRC32_Compute((uint8_t*)&g_font_header + 0x0C, 36);
    g_font_header.crc32 = crc_stored;
    if (crc_stored != crc_computed) return;

    s_font_flash_valid = 1;

    /* ── Parse Icon Table from Flash ── */
    {
        uint8_t  ihdr[16];  /* 16B icon table header */
        uint8_t  ientry[8]; /* 8B per entry */
        uint16_t i, n_entries, n_frames, io_id, io_frames, io_doff;
        W25Q_Driver_Read(g_font_header.icon_table_offset, ihdr, 16);
        n_entries = ihdr[0] | ((uint16_t)ihdr[1] << 8);
        n_frames  = ihdr[2] | ((uint16_t)ihdr[3] << 8);
        (void)n_frames;  /* total frames count, unused at runtime */

        if (n_entries > ICON_TABLE_MAX) n_entries = ICON_TABLE_MAX;
        s_icon_count = (uint8_t)n_entries;
        s_icon_data_base = g_font_header.icon_data_offset;

        for (i = 0; i < n_entries; i++) {
            W25Q_Driver_Read(g_font_header.icon_table_offset + 16 + (uint32_t)i * 8,
                             ientry, 8);
            io_id     = ientry[0] | ((uint16_t)ientry[1] << 8);
            io_frames = ientry[2] | ((uint16_t)ientry[3] << 8);
            io_doff   = ientry[4] | ((uint16_t)ientry[5] << 8);
            s_icon_table[i].icon_id     = io_id;
            s_icon_table[i].n_frames    = io_frames;
            s_icon_table[i].data_offset = io_doff;
        }
    }
}

/* 公开字库状态查询 (供 Sys_Startup_Screen 显示) */
uint8_t Tft_Driver_Is_Font_Flash_Valid(void)
{
    return s_font_flash_valid;
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
    TIM_SetCompare1(TIM4, ((uint16_t)v * 999) / 255);
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

static void Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg, uint16_t* out)
{
    uint8_t b;
    for (b = 0; b < 8; b++)
        out[b] = (byte_val & (0x01 << b)) ? fg : bg;
}

static void Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg, uint16_t* out)
{
    uint8_t b;
    for (b = 0; b < 8; b++) {
        out[b]     = (lo & (0x01 << b)) ? fg : bg;
        out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  ASCII 渲染 — 双路径: Flash 流式读取 / ROM 字模回退
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t idx; uint16_t* p;

    if (line >= TFT_LINE_COUNT || col >= TFT_CHAR_PER_LINE) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    idx = (uint8_t)(ch - 32);

    if (s_font_flash_valid) {
        /* ── Flash 路径: 流式读取 16 行 (每行 1B), 不经 RAM 缓存 ── */
        uint8_t row; uint32_t base;
        SetWin(col * 8, line * 16, col * 8 + 7, line * 16 + 15);
        base = g_font_header.ascii_offset + (uint32_t)idx * 16;  /* idx = ch-32, 0..94 */
        p = s_dma_buf;
        for (row = 0; row < 16; row++) {
            uint8_t byte_val;
            W25Q_Driver_Read(base + (uint32_t)row, &byte_val, 1);
            Decode_Char_Row(byte_val, fg, bg, p + row * 8);
        }
        Tft_DMA_Send(s_dma_buf, 128);
        return;
    }

    {
        /* ── ROM 回退: 片内 TFT_FONT_8X16 字模 ── */
        uint8_t r; uint16_t x0, y0;
        x0 = col  * TFT_FONT_WIDTH;
        y0 = line * TFT_FONT_HEIGHT;
        SetWin(x0, y0, x0 + TFT_FONT_WIDTH - 1, y0 + TFT_FONT_HEIGHT - 1);
        p = s_dma_buf;
        for (r = 0; r < 16; r++)
            Decode_Char_Row(TFT_FONT_8X16[idx][r], fg, bg, p + r * 8);
        Tft_DMA_Send(s_dma_buf, 128);
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
 *  中文渲染 — 双路径: Flash 二分查找 (20902字) / ROM 4字回退 (SPLASH)
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t Tft_Is_UTF8_CN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

static void Tft_Driver_CN_Draw(uint8_t ln, uint8_t col, const uint8_t *utf8,
                               uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;

    if (ln >= TFT_LINE_COUNT || col + 1 >= TFT_CHAR_PER_LINE) return;

    if (s_font_flash_valid) {
        /* ── Flash 路径: UTF-8 → Unicode → 二分查找 → 流式读取 16 行 ── */
        uint32_t unicode; uint32_t data_offset; uint32_t base;
        unicode  = ((uint32_t)(utf8[0] & 0x0F) << 12);
        unicode |= ((uint32_t)(utf8[1] & 0x3F) << 6);
        unicode |= ((uint32_t)(utf8[2] & 0x3F));
        data_offset = W25Q_Font_Index_Binary_Search((uint16_t)unicode, &g_font_header);
        if (data_offset == 0xFFFFFFFFUL) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg);
            return;
        }
        base = g_font_header.cjk_data_offset + (uint32_t)data_offset;
        SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
        p = s_dma_buf;
        for (row = 0; row < 16; row++) {
            uint8_t lo_hi[2];
            W25Q_Driver_Read(base + (uint32_t)row * 2, lo_hi, 2);
            Decode_CN_Row(lo_hi[0], lo_hi[1], fg, bg, p + row * 16);
        }
        Tft_DMA_Send(s_dma_buf, 256);
    } else {
        /* ── ROM 回退: CN_INDEX 线性查找 (4字: 无/线/充/电) ── */
        uint8_t g_idx; uint8_t i;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx == 0xFF) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg);
            return;
        }
        SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
        p = s_dma_buf;
        for (row = 0; row < 16; row++)
            Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                           fg, bg, p + row * 16);
        Tft_DMA_Send(s_dma_buf, 256);
    }
}

void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,
                                uint16_t fg, uint16_t bg)
{
    while (*s && col < TFT_CHAR_PER_LINE) {
        if (Tft_Is_UTF8_CN((uint8_t)*s) && *(s+1) && *(s+2)) {
            Tft_Driver_CN_Draw(ln, col, (const uint8_t*)s, fg, bg);
            col += 2; s += 3;
        } else {
            Tft_Driver_Show_Char(ln, col, *s, fg, bg);
            col++; s++;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Icon 绘制 — Flash V2 优先, ROM 3 图标回退 (WIFI/MQTT 消极态)
 * ═══════════════════════════════════════════════════════════════ */

/** @brief 统一图标绘制: Flash V2 优先, ROM 3图标回退 (WIFI_OFF/WIFI_REMOVE/MQTT_NO)
 *  @param icon_id 0-30, see ICON_ID_* defines in Tft_Driver.h
 *  @param frame   0..n_frames-1, clamped internally
 *  @param x,y     top-left TFT pixel coordinates
 *  @param fg,bg   foreground/background RGB565 colors
 *  @retval 1=success, 0=not found
 *  @note   Flash 正常 -> 31 图标全量; Flash 无效 -> ROM 全量回退 (WiFi 4+6+2, MQTT 4+6, Star 1)
uint8_t Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y,
    uint8_t icon_id, uint8_t frame, uint16_t fg, uint16_t bg)
{
    uint8_t row;
    uint16_t *p;
    const uint8_t *rom_data = 0;

    /* ── Flash 优先 ── */
    if (s_font_flash_valid && s_icon_count > 0) {
        uint8_t  i;
        uint8_t  found = 0;
        uint16_t n_frames = 0;
        uint16_t data_off  = 0;
        for (i = 0; i < s_icon_count; i++) {
            if (s_icon_table[i].icon_id == icon_id) {
                n_frames = s_icon_table[i].n_frames;
                data_off = s_icon_table[i].data_offset;
                found = 1;
                break;
            }
        }
        if (found && n_frames > 0) {
            uint32_t frame_addr;
            uint8_t  frame_data[32];
            if (frame >= n_frames) frame = (uint8_t)(n_frames - 1);
            SetWin(x, y, x + 15, y + 15);
            p = s_dma_buf;
            frame_addr = s_icon_data_base + (uint32_t)data_off + (uint32_t)frame * 32U;
            W25Q_Driver_Read(frame_addr, frame_data, 32);
            for (row = 0; row < 16; row++)
                Decode_CN_Row(frame_data[row * 2], frame_data[row * 2 + 1],
                              fg, bg, p + row * 16);
            Tft_DMA_Send(s_dma_buf, 256);
            return 1;
        }
    }

    /* ── ROM 回退: 全量 WiFi/MQTT/Star 图标 ── */
    switch (icon_id) {
        case ICON_ID_WIFI_SIGNAL:
            if (frame > 3) frame = 3; rom_data = WIFI_ICON[frame];     break;
        case ICON_ID_WIFI_CONNECT_ANIM:
            if (frame > 5) frame = 5; rom_data = WIFI_CONNECT_ANIM[frame]; break;
        case ICON_ID_WIFI_OFF:       rom_data = WIFI_OFF_ICON;         break;
        case ICON_ID_WIFI_REMOVE:    rom_data = WIFI_REMOVE_ICON;      break;
        case ICON_ID_MQTT_BASE:      rom_data = MQTT_ICON;             break;
        case ICON_ID_MQTT_YES:       rom_data = MQTT_YES_ICON;         break;
        case ICON_ID_MQTT_NO:        rom_data = MQTT_NO_ICON;          break;
        case ICON_ID_MQTT_ANIM:
            if (frame > 5) frame = 5; rom_data = MQTT_ANIM[frame];      break;
        case ICON_ID_STAR:           rom_data = ICON_STAR;             break;
        default: return 0;
    }
    (void)frame;
    SetWin(x, y, x + 15, y + 15);
    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(rom_data[row * 2], rom_data[row * 2 + 1], fg, bg, p + row * 16);
    Tft_DMA_Send(s_dma_buf, 256);
    return 1;
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
 *  使用字库: ROM ASCII 95 + CN 4字, SPLASH 不依赖 Flash
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

    /* 版本号: 右下角暗灰 (Row 7 col14, 屏亮后第一个字渐亮时出现) */
    Tft_Driver_Show_String(7, 14, "V4.3.2", 0x3186U, TFT_COLOR_BLACK);

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
