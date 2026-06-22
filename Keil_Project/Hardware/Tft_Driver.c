/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735 128×160 TFT — SPI1+DMA V4.3.0 (W25Q128 Flash 字库, 深水区重构)
 *          PA5=SCK PA7=MOSI PA4=CS PA6=DC PA0=RST PB6=BL PA12=FLASH_CS
 *          SPI1 Mode3 (CPOL=High, CPHA=2Edge), 横屏 160x128, MADCTL=0xA0
 *          DMA1_Channel3 用于全部像素传输 (Fill + Blit), WrCmd/WrDat 8位轮询
 * @note    V4.3.0r2: 开机三级锁死 — Flash CS 封杀/AFIO 统合接管/DMA 双重空闲拦截
 *                 字模来源: W25Q128 SPI Flash (PA6 动态 DC/MISO)
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "W25Q_Driver.h"
#include "App_Storage.h"

#define TFT_DRIVER_CS_PIN   GPIO_Pin_4
#define TFT_DRIVER_DC_PIN   GPIO_Pin_6
#define TFT_DRIVER_RST_PIN  GPIO_Pin_0
#define TFT_DRIVER_BL_PIN   GPIO_Pin_6

#define TFT_CS_LOW()   GPIO_ResetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_CS_HIGH()  GPIO_SetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_DC_CMD()   GPIO_ResetBits(GPIOA, TFT_DRIVER_DC_PIN)
#define TFT_DC_DATA()  GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN)

#define TFT_DMA_MAX_PIXELS  65535

/* ── DMA 状态 ── */
static uint8_t s_dma_configured = 0;

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
 *  ASCII 渲染 — Flash 字模
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t r, glyph_row[16]; uint16_t* p;
    uint16_t x0, y0;

    if (line >= TFT_LINE_COUNT || col >= TFT_CHAR_PER_LINE) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    x0 = col  * TFT_FONT_WIDTH;
    y0 = line * TFT_FONT_HEIGHT;
    SetWin(x0, y0, x0 + TFT_FONT_WIDTH - 1, y0 + TFT_FONT_HEIGHT - 1);

    App_Storage_Read_ASCII((uint8_t)ch, glyph_row);

    p = s_dma_buf;
    for (r = 0; r < 16; r++)
        Decode_Char_Row(glyph_row[r], fg, bg, p + r * 8);

    Tft_DMA_Send(s_dma_buf, 128);
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
 *  中文渲染 — UTF-8 → Unicode → Flash 全字库 O(1)
 * ═══════════════════════════════════════════════════════════════ */

static uint16_t Tft_UTF8_To_Unicode(const uint8_t *u8)
{
    return (uint16_t)(((uint16_t)(u8[0] & 0x0F) << 12) |
                      ((uint16_t)(u8[1] & 0x3F) << 6)  |
                       (uint16_t)(u8[2] & 0x3F));
}

static uint8_t Tft_Is_UTF8_CN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

static void Tft_Driver_CN_Draw_Flash(uint8_t ln, uint8_t col, const uint8_t *utf8,
                                     uint16_t fg, uint16_t bg)
{
    uint8_t row, glyph[32]; uint16_t* p;
    uint16_t cp;

    if (ln >= TFT_LINE_COUNT || col + 1 >= TFT_CHAR_PER_LINE) return;

    cp = Tft_UTF8_To_Unicode(utf8);
    SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);

    App_Storage_Read_Glyph(cp, glyph);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(glyph[row * 2], glyph[row * 2 + 1], fg, bg, p + row * 16);

    Tft_DMA_Send(s_dma_buf, 256);
}

void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,
                                uint16_t fg, uint16_t bg)
{
    while (*s && col < TFT_CHAR_PER_LINE) {
        if (Tft_Is_UTF8_CN((uint8_t)*s) && *(s+1) && *(s+2)) {
            Tft_Driver_CN_Draw_Flash(ln, col, (const uint8_t*)s, fg, bg);
            col += 2; s += 3;
        } else {
            Tft_Driver_Show_Char(ln, col, *s, fg, bg);
            col++; s++;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  WiFi 图标 — Flash 读取
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg)
{
    uint8_t row, icon_buf[32]; uint16_t* p;
    if (frame > 3) frame = 3;

    SetWin(x, y, x + 15, y + 15);

    W25Q_Driver_Read(FONT_CJK_BASE + (FONT_CJK_COUNT * FONT_CHAR_BYTES)
                     + ((uint32_t)frame * 32U), icon_buf, 32);
    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(icon_buf[row * 2], icon_buf[row * 2 + 1], fg, bg, p + row * 16);

    Tft_DMA_Send(s_dma_buf, 256);
}

void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],
                                  uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;

    SetWin(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(data[row * 2], data[row * 2 + 1], fg, bg, p + row * 16);

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
