/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735 1.8寸 128x160 TFT — SPI1+DMA 全硬件加速版 V11
 *          PA5=SCK PA7=MOSI PA4=CS PA6=DC PA0=RST PB6=BL
 *          SPI1 Mode3 (CPOL=High, CPHA=2Edge), 横屏 160x128, MADCTL=0xA0
 *          DMA1_Channel3 用于全部像素传输 (Fill + Blit), WrCmd/WrDat 8位轮询
 * @note    V11: 字符/图标全部改为 buffer-build → DMA 发送, 消除逐像素 WrD16
 *              填充=MINC=0(同色泵送), 位图=MINC=1(缓冲区自增)
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "TFT_Font.h"
#include "TFT_CN_Font.h"
#include "TFT_Img.h"

#define TFT_DRIVER_CS_PIN   GPIO_Pin_4
#define TFT_DRIVER_DC_PIN   GPIO_Pin_6
#define TFT_DRIVER_RST_PIN  GPIO_Pin_0
#define TFT_DRIVER_BL_PIN   GPIO_Pin_6

#define TFT_CS_LOW()   GPIO_ResetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_CS_HIGH()  GPIO_SetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_DC_CMD()   GPIO_ResetBits(GPIOA, TFT_DRIVER_DC_PIN)
#define TFT_DC_DATA()  GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN)

/* DMA 传输上限: 单次最多 65535 个 16位像素 (约 128KB), ST7735 160x128=20480 足够 */
#define TFT_DMA_MAX_PIXELS  65535

/* ── DMA 状态 ── */
static uint8_t s_dma_configured = 0;

/* ── 像素缓冲区: 16×16 中文/图标 = 256 像素, static 避免大栈帧 ── */
static uint16_t s_dma_buf[256];

static void dly(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 9; i++) __NOP();
}

/* ═══════════════════════════════════════════════════════════════
 *  8位基础通信 (保持轮询, 仅用于命令字节和寄存器数据)
 * ═══════════════════════════════════════════════════════════════ */

/* 写 SPI 命令字节 (DC=0) */
static void WrCmd(uint8_t c)
{
    TFT_DC_CMD(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, c);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

/* 写 SPI 数据字节 (DC=1) — 仅用于命令参数 (寄存器配置), 不用于像素 */
static void WrDat(uint8_t d)
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
    SPI_Cmd(SPI1, DISABLE);
    SPI1->CR1 &= ~SPI_CR1_DFF;
    SPI_Cmd(SPI1, ENABLE);
}

static void Tft_SPI_16bit(void)
{
    SPI_Cmd(SPI1, DISABLE);
    SPI1->CR1 |= SPI_CR1_DFF;
    SPI_Cmd(SPI1, ENABLE);
}

/* ═══════════════════════════════════════════════════════════════
 *  DMA 一次性初始化 (只配 RCC + 外设基地址, CCR 按传输模式配置)
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Init(void)
{
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel3);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    dma.DMA_MemoryBaseAddr     = 0;               /* 运行时动态设置 */
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 0;               /* 运行时动态设置 */
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;   /* 默认自增, Fill 时覆盖 */
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    s_dma_configured = 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  DMA 核心传输 — DC=DATA, CS=LOW, 等 TC3, 拉高 CS
 *  inc_mem=0 → 同色填充 (CMAR 指向单色变量)
 *  inc_mem=1 → 缓冲区自增 (CMAR 指向像素数组)
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_DMA_Transfer(const uint16_t* buf, uint32_t count, uint8_t inc_mem)
{
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->CMAR  = (uint32_t)buf;
    DMA1_Channel3->CNDTR = (uint16_t)count;
    if (inc_mem)
        DMA1_Channel3->CCR |=  DMA_MemoryInc_Enable;
    else
        DMA1_Channel3->CCR &= ~DMA_MemoryInc_Enable;
    DMA_Cmd(DMA1_Channel3, ENABLE);

    TFT_DC_DATA();
    TFT_CS_LOW();
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);
    DMA_ClearFlag(DMA1_FLAG_TC3);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));  /* 等末帧发完 */
    TFT_CS_HIGH();
}

/* ── 单色填充: 同一颜色值 DMA 泵送 (MINC=0) ── */
static void Tft_DMA_Fill(uint32_t pixel_count, uint16_t color)
{
    if (pixel_count == 0) return;
    if (pixel_count > TFT_DMA_MAX_PIXELS) pixel_count = TFT_DMA_MAX_PIXELS;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(&color, pixel_count, 0);
    Tft_SPI_8bit();
}

/* ── 缓冲区发送: 像素数组 DMA 泵送 (MINC=1) ── */
static void Tft_DMA_Send(const uint16_t* buf, uint32_t pixel_count)
{
    if (pixel_count == 0) return;
    if (!s_dma_configured) Tft_DMA_Init();

    Tft_SPI_16bit();
    Tft_DMA_Transfer(buf, pixel_count, 1);
    Tft_SPI_8bit();
}

/* 设置 ST7735 绘图窗口 (CASET+RASET+RAMWR), 含横屏面板物理偏移 X+1/Y+2 */
static void SetWin(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += 1; xe += 1;  /* 横屏 X偏移=1 */
    ys += 2; ye += 2;  /* 横屏 Y偏移=2 */
    WrCmd(0x2A);
    WrDat((uint8_t)(xs >> 8)); WrDat((uint8_t)xs);
    WrDat((uint8_t)(xe >> 8)); WrDat((uint8_t)xe);
    WrCmd(0x2B);
    WrDat((uint8_t)(ys >> 8)); WrDat((uint8_t)ys);
    WrDat((uint8_t)(ye >> 8)); WrDat((uint8_t)ye);
    WrCmd(0x2C);
}

/** ═══════════════════════════════════════════════
 *  ST7735 Green Tab 已验证初始化
 * ═══════════════════════════════════════════════ */

void Tft_Driver_Init(void)
{
    GPIO_InitTypeDef  gpio;
    SPI_InitTypeDef   spi;
    TIM_TimeBaseInitTypeDef  tim_base;
    TIM_OCInitTypeDef        oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    /* SCK=PA5, MOSI=PA7 */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* CS=PA4, DC=PA6, RST=PA0 */
    gpio.GPIO_Pin  = TFT_DRIVER_CS_PIN | TFT_DRIVER_DC_PIN | TFT_DRIVER_RST_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);

    /* BL=PB6, TIM4_CH1 */
    gpio.GPIO_Pin  = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);

    TFT_CS_HIGH();

    /* SPI1: Mode 3 (CPOL=High, CPHA=2Edge), 18MHz (72M/4) — 中景园 ST7735 */
    SPI_StructInit(&spi);
    spi.SPI_Direction  = SPI_Direction_1Line_Tx;
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
    dly(100000);   /* 100ms */
    GPIO_SetBits(GPIOA, TFT_DRIVER_RST_PIN);
    dly(120000);   /* 120ms */

    /* ═══════════════════════════════════════════
     *  ST7735 Green Tab 已验证 init
     * ═══════════════════════════════════════════ */

    WrCmd(0x11);   /* SLPOUT */
    dly(120000);   /* 120ms */

    WrCmd(0x3A);   /* COLMOD: RGB565 — 中景园放在 NORON 之后 */
    WrDat(0x05);

    WrCmd(0xB1);   /* FRMCTR1 */
    WrDat(0x05); WrDat(0x3C); WrDat(0x3C);
    WrCmd(0xB2);   /* FRMCTR2 */
    WrDat(0x05); WrDat(0x3C); WrDat(0x3C);
    WrCmd(0xB3);   /* FRMCTR3 */
    WrDat(0x05); WrDat(0x3C); WrDat(0x3C);
    WrDat(0x05); WrDat(0x3C); WrDat(0x3C);

    WrCmd(0xB4);   /* INVCTR */
    WrDat(0x03);

    WrCmd(0xC0);   /* PWCTR1 */
    WrDat(0x28); WrDat(0x08); WrDat(0x04);
    WrCmd(0xC1);   /* PWCTR2 */
    WrDat(0xC0);
    WrCmd(0xC2);   /* PWCTR3 */
    WrDat(0x0D); WrDat(0x00);
    WrCmd(0xC3);   /* PWCTR4 */
    WrDat(0x8D); WrDat(0x2A);
    WrCmd(0xC4);   /* PWCTR5 */
    WrDat(0x8D); WrDat(0xEE);
    WrCmd(0xC5);   /* VMCTR1 */
    WrDat(0x1A);

    /* ═══════════════════════════════════════════════════════════════
     *  MADCTL (0x36): 横屏 160x128, (0,0)=左上角
     *
     *  当前: 0xA0 (MY=1,MX=0,MV=1) — 方向3 反向横屏
     *  备选: 0x70 (MY=0,MX=1,MV=1) — 方向1 横屏 (若上下颠倒可用)
     *  BGR注意: 设0x08后红蓝颠倒, 保持bit3=0, 不用BGR
     *  ═══════════════════════════════════════════════════════════════ */
    WrCmd(0x36);
    WrDat(0xA0);

    /* Gamma (+) */
    WrCmd(0xE0);
    WrDat(0x04);WrDat(0x22);WrDat(0x07);WrDat(0x0A);
    WrDat(0x2E);WrDat(0x30);WrDat(0x25);WrDat(0x2A);
    WrDat(0x28);WrDat(0x26);WrDat(0x2E);WrDat(0x3A);
    WrDat(0x00);WrDat(0x01);WrDat(0x03);WrDat(0x13);

    /* Gamma (-) */
    WrCmd(0xE1);
    WrDat(0x04);WrDat(0x16);WrDat(0x06);WrDat(0x0D);
    WrDat(0x2D);WrDat(0x26);WrDat(0x23);WrDat(0x27);
    WrDat(0x27);WrDat(0x25);WrDat(0x2D);WrDat(0x3B);
    WrDat(0x00);WrDat(0x01);WrDat(0x04);WrDat(0x13);

    WrCmd(0x13);   /* NORON */

    WrCmd(0x3A);   /* COLMOD: RGB565 */
    WrDat(0x05);

    WrCmd(0x29);   /* DISPON */
    dly(50000);    /* 50ms */

    Tft_Driver_Clear(TFT_COLOR_BLACK);
}

/* ═══════════════════════════════════
 *  基础绘图
 * ═══════════════════════════════════ */

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

/* ═══════════════════════════════════════════════════════════════
 *  位图→缓冲区解码辅助函数
 *  字体格式: 8x16 ASCII LSB-first, 16x16 中文 LSB-first, 每字节 bit0=左
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 将 8x16 LSB-first 字模的一行解码为 8 个 RGB565 像素
 * @param byte_val  字模行字节 (bit0=最左像素)
 * @param fg, bg    前景色/背景色
 * @param out       输出缓冲区 (至少 8 个 uint16_t)
 */
static void Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg, uint16_t* out)
{
    uint8_t b;
    for (b = 0; b < 8; b++)
        out[b] = (byte_val & (0x01 << b)) ? fg : bg;
}

/**
 * @brief 将 16x16 LSB-first 字模的 2 字节行解码为 16 个 RGB565 像素
 * @param lo, hi    字模行低字节/高字节 (lo=左8列, hi=右8列)
 * @param fg, bg    前景色/背景色
 * @param out       输出缓冲区 (至少 16 个 uint16_t)
 */
static void Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg, uint16_t* out)
{
    uint8_t b;
    for (b = 0; b < 8; b++) {
        out[b]     = (lo & (0x01 << b)) ? fg : bg;
        out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
    }
}

/* ═══════════════════════════════════
 *  ASCII 8x16 字符 — DMA 版
 * ═══════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t r;
    uint16_t* p;
    uint16_t x0, y0;

    if (line >= TFT_LINE_COUNT || col >= TFT_CHAR_PER_LINE) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    x0 = col  * TFT_FONT_WIDTH;
    y0 = line * TFT_FONT_HEIGHT;
    SetWin(x0, y0, x0 + TFT_FONT_WIDTH - 1, y0 + TFT_FONT_HEIGHT - 1);

    /* 逐行解码 → s_dma_buf, 16行 × 8像素 = 128 半字 */
    p = s_dma_buf;
    for (r = 0; r < 16; r++)
        Decode_Char_Row(TFT_FONT_8X16[(uint8_t)ch - 32][r], fg, bg, p + r * 8);

    Tft_DMA_Send(s_dma_buf, 128);
}

/* ═══════════════════════════════════
 *  ASCII 字符串 — 逐字 DMA (统一 fg/bg, 每字 128px DMA 一次)
 * ═══════════════════════════════════ */

void Tft_Driver_Show_String(uint8_t line, uint8_t col, const char* s,
                            uint16_t fg, uint16_t bg)
{
    while (*s && col < TFT_CHAR_PER_LINE) {
        Tft_Driver_Show_Char(line, col, *s, fg, bg);
        col++; s++;
    }
}

/* ═══════════════════════════════════
 *  数字 — 委托 Show_Char
 * ═══════════════════════════════════ */

static uint32_t pw(uint32_t e) { uint32_t r = 1; while (e--) r *= 10; return r; }

void Tft_Driver_Show_Num(uint8_t ln, uint8_t col, uint32_t v,
                         uint8_t len, uint16_t fg, uint16_t bg)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        Tft_Driver_Show_Char(ln, col + i,
            (char)('0' + (v / pw(len - 1 - i)) % 10), fg, bg);
}

void Tft_Driver_Show_Float(uint8_t ln, uint8_t col, float v,
                           uint8_t il, uint8_t fl, uint16_t fg, uint16_t bg)
{
    uint32_t ip, fp, p10 = pw(fl);
    uint8_t i;
    if (v < 0) { Tft_Driver_Show_Char(ln, col, '-', fg, bg); col++; v = -v; }
    v += 0.5f / (float)p10;
    ip = (uint32_t)v;
    fp = (uint32_t)((v - (float)ip) * (float)p10);
    for (i = 0; i < il; i++)
        Tft_Driver_Show_Char(ln, col + i,
            (char)('0' + (ip / pw(il - 1 - i)) % 10), fg, bg);
    Tft_Driver_Show_Char(ln, col + il, '.', fg, bg);
    for (i = 0; i < fl; i++)
        Tft_Driver_Show_Char(ln, col + il + 1 + i,
            (char)('0' + (fp / pw(fl - 1 - i)) % 10), fg, bg);
}

/* ═══════════════════════════════════
 *  中英文混合 — DMA 版
 * ═══════════════════════════════════ */

static uint8_t Cnlk(const char* u8)
{
    uint8_t i;
    for (i = 0; i < CN_CHAR_COUNT; i++)
        if (CN_INDEX[i*3]==u8[0] && CN_INDEX[i*3+1]==u8[1] && CN_INDEX[i*3+2]==u8[2])
            return i;
    return 0xFF;
}

/**
 * @brief  绘制 16x16 中文字符 — DMA 版
 * @note   字模 32 字节/字, 2字节/行×16行, LSB-first
 *         每行: lo(左8列) + hi(右8列) → 16 像素, 16行×16px=256 像素, DMA 一次
 */
static void CnDr(uint8_t ln, uint8_t col, uint8_t idx, uint16_t fg, uint16_t bg)
{
    uint8_t row;
    uint16_t* p;
    const uint8_t* glyph;  /* 32 字节字模 */

    if (ln >= TFT_LINE_COUNT || col + 1 >= TFT_CHAR_PER_LINE) return;

    SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);

    glyph = CN_FONT_16X16[idx];
    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(glyph[row * 2], glyph[row * 2 + 1], fg, bg, p + row * 16);

    Tft_DMA_Send(s_dma_buf, 256);
}

static uint8_t IsCN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,
                                uint16_t fg, uint16_t bg)
{
    while (*s && col < TFT_CHAR_PER_LINE) {
        if (IsCN((uint8_t)*s)) {
            uint8_t idx = Cnlk(s);
            if (idx != 0xFF) { CnDr(ln, col, idx, fg, bg); col += 2; s += 3; }
            else { col += 2; s += 3; }
        } else {
            Tft_Driver_Show_Char(ln, col, *s, fg, bg);
            col++; s++;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  WIFI 信号图标 — DMA 版 (16x16, 256 像素一次 DMA)
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg)
{
    uint8_t row;
    const uint8_t* data;
    uint16_t* p;

    if (frame > 3) frame = 3;
    data = WIFI_ICON[frame];

    SetWin(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(data[row * 2], data[row * 2 + 1], fg, bg, p + row * 16);

    Tft_DMA_Send(s_dma_buf, 256);
}

/* ═══════════════════════════════════════════════════════════════
 *  单帧图标 — DMA 版 (16x16, 256 像素一次 DMA)
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],
                                  uint16_t fg, uint16_t bg)
{
    uint8_t row;
    uint16_t* p;

    SetWin(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Decode_CN_Row(data[row * 2], data[row * 2 + 1], fg, bg, p + row * 16);

    Tft_DMA_Send(s_dma_buf, 256);
}

/* ════════════════════════════════════════════════════════
 *  4×8 微型字库 — PCtoLCD2002 行主序 LSB-first 宋体
 *  字符: 0 1 2 3 4 5 6 7 8 9 . - V F C k H z  W 空格
 *  共 19 字符 × 8 字节 = 152 字节
 *  位序: 低4位有效, bit0=最左像素 (LSB-first)
 * ════════════════════════════════════════════════════════ */
static const uint8_t FONT_4X8[19][8] = {
    /* [0] '0' */ {0x00,0x00,0x02,0x05,0x05,0x05,0x02,0x00},
    /* [1] '1' */ {0x00,0x00,0x06,0x04,0x04,0x04,0x0E,0x00},
    /* [2] '2' */ {0x00,0x00,0x07,0x05,0x02,0x01,0x07,0x00},
    /* [3] '3' */ {0x00,0x00,0x07,0x04,0x02,0x04,0x07,0x00},
    /* [4] '4' */ {0x00,0x00,0x04,0x06,0x05,0x0F,0x0E,0x00},
    /* [5] '5' */ {0x00,0x00,0x07,0x07,0x04,0x04,0x07,0x00},
    /* [6] '6' */ {0x00,0x00,0x06,0x07,0x05,0x05,0x07,0x00},
    /* [7] '7' */ {0x00,0x00,0x06,0x04,0x04,0x04,0x04,0x00},
    /* [8] '8' */ {0x00,0x00,0x07,0x05,0x02,0x05,0x07,0x00},
    /* [9] '9' */ {0x00,0x00,0x07,0x05,0x07,0x04,0x03,0x00},
    /*[10] '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00},
    /*[11] '-' */ {0x00,0x00,0x00,0x00,0x0F,0x00,0x00,0x00},
    /*[12] 'V' */ {0x00,0x00,0x09,0x09,0x06,0x06,0x00,0x00},
    /*[13] 'F' */ {0x00,0x00,0x06,0x06,0x06,0x06,0x07,0x00},
    /*[14] 'C' */ {0x00,0x00,0x06,0x01,0x01,0x01,0x06,0x00},
    /*[15] 'k' */ {0x00,0x02,0x02,0x06,0x06,0x06,0x02,0x00},
    /*[16] 'H' */ {0x00,0x00,0x06,0x06,0x06,0x06,0x06,0x00},
    /*[17] 'z' */ {0x00,0x00,0x00,0x06,0x06,0x06,0x06,0x00},
    /*[18] ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/**
 * @brief  将 ASCII 字符映射到 FONT_4X8 索引
 * @param  ch  待映射字符
 * @retval 0..18 FONT_4X8 索引, 不支持→回退空格(18)
 */
static uint8_t Map_4x8_Idx(char ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    switch (ch) {
        case '.': return 10;
        case '-': return 11;
        case 'V': return 12;
        case 'F': return 13;
        case 'C': return 14;
        case 'k': return 15;
        case 'H': return 16;
        case 'z': return 17;
        case ' ': return 18;
    }
    return 18;
}

/**
 * @brief  在 TFT 像素坐标绘制 4×8 微型字符串
 * @param  x, y   像素坐标 (0≤x≤156, 0≤y≤120)
 * @param  s      字符串 (仅支持 FONT_4X8 字符集)
 * @param  fg, bg 前景色/字体色
 * @note   每字符 4px宽 × 8px高, 间距2px, 每字符步进 6px
 *         用 4px 宽字符可在R=55弧外标注20+个数字
 */
void Tft_Driver_Show_4x8_String_Pixel(uint16_t x, uint16_t y,
                                       const char* s,
                                       uint16_t fg, uint16_t bg)
{
    uint8_t row, b, idx;
    uint16_t* p;

    while (*s && x + 4 <= TFT_WIDTH) {
        idx = Map_4x8_Idx(*s);

        SetWin(x, y, x + 3, y + 7);

        /* 逐行解码 → s_dma_buf: 8行 × 4像素 = 32 半字 */
        p = s_dma_buf;
        for (row = 0; row < 8; row++) {
            uint8_t byte_val = FONT_4X8[idx][row];
            for (b = 0; b < 4; b++)
                *p++ = (byte_val & (0x01 << b)) ? fg : bg;
        }

        Tft_DMA_Send(s_dma_buf, 32);

        x += 6;  /* 4px char + 2px spacing */
        s++;
    }
}
