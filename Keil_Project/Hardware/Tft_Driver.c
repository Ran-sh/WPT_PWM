/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735 1.8寸 128x160 TFT — 已验证初始化 (Green Tab)
 *          PA5=SCK PA7=MOSI PA4=CS PA6=DC PA0=RST PB6=BL
 *          SPI1 Mode3 (CPOL=High, CPHA=2Edge), 横屏 160x128, MADCTL=0xA0
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "TFT_Font.h"
#include "TFT_CN_Font.h"

#define TFT_DRIVER_CS_PIN   GPIO_Pin_4
#define TFT_DRIVER_DC_PIN   GPIO_Pin_6
#define TFT_DRIVER_RST_PIN  GPIO_Pin_0
#define TFT_DRIVER_BL_PIN   GPIO_Pin_6

#define TFT_CS_LOW()   GPIO_ResetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_CS_HIGH()  GPIO_SetBits(GPIOA, TFT_DRIVER_CS_PIN)
#define TFT_DC_CMD()   GPIO_ResetBits(GPIOA, TFT_DRIVER_DC_PIN)
#define TFT_DC_DATA()  GPIO_SetBits(GPIOA, TFT_DRIVER_DC_PIN)

static void dly(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 9; i++) __NOP();
}

/* 写 SPI 命令字节 (DC=0) */
static void WrCmd(uint8_t c)
{
    TFT_DC_CMD(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, c);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

/* 写 SPI 数据字节 (DC=1) */
static void WrDat(uint8_t d)
{
    TFT_DC_DATA(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, d);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

/* 写 RGB565 16 位像素 (DC=1, MSB first) */
static void WrD16(uint16_t d)
{
    TFT_DC_DATA(); TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, (uint8_t)(d >> 8));
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    SPI_I2S_SendData(SPI1, (uint8_t)d);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
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

    // 只在 reset 后发 SLPOUT, 不先发 SWRESET — 中景园参考代码
    // WrCmd(0x01);   /* SWRESET — 和参考代码一样跳过 */
    // dly(150000);

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
    uint32_t n, total = (uint32_t)TFT_WIDTH * TFT_HEIGHT;
    SetWin(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    for (n = 0; n < total; n++) WrD16(color);
}

void Tft_Driver_Set_Backlight(uint8_t v)
{
    TIM_SetCompare1(TIM4, ((uint16_t)v * 999) / 255);
}

void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t n, total;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH  - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    total = (uint32_t)w * h;
    SetWin(x, y, x + w - 1, y + h - 1);
    for (n = 0; n < total; n++) WrD16(color);
}

/* ═══════════════════════════════════
 *  ASCII 8x16 字符
 * ═══════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t r, b;
    uint16_t x0, y0;

    if (line >= TFT_LINE_COUNT || col >= TFT_CHAR_PER_LINE) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    x0 = col  * TFT_FONT_WIDTH;
    y0 = line * TFT_FONT_HEIGHT;
    /* 像素坐标直接写入 */
    SetWin(x0, y0, x0 + TFT_FONT_WIDTH - 1, y0 + TFT_FONT_HEIGHT - 1);

    for (r = 0; r < 16; r++) {
        uint8_t byte = TFT_FONT_8X16[ch - 32][r];
        for (b = 0; b < 8; b++)
            WrD16((byte & (0x01 << b)) ? fg : bg);  /* LSB */
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

/* ═══════════════════════════════════
 *  数字
 * ═══════════════════════════════════ */

/* 计算 10^e (用于十进制数字取位) */
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
 *  中英文混合
 * ═══════════════════════════════════ */

/* 在 CN_INDEX 中查找 UTF-8 三字节中文, 返回索引 0..72 或 0xFF(未找到) */
static uint8_t Cnlk(const char* u8)
{
    uint8_t i;
    for (i = 0; i < CN_CHAR_COUNT; i++)
        if (CN_INDEX[i*3]==u8[0] && CN_INDEX[i*3+1]==u8[1] && CN_INDEX[i*3+2]==u8[2])
            return i;
    return 0xFF;
}

static void CnDr(uint8_t ln, uint8_t col, uint8_t idx, uint16_t fg, uint16_t bg)
{
    uint8_t row, bi, bit;
    if (ln >= TFT_LINE_COUNT || col + 1 >= TFT_CHAR_PER_LINE) return;
    SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
    for (row = 0; row < 16; row++)
        for (bi = 0; bi < 2; bi++)
            for (bit = 0; bit < 8; bit++)
                WrD16((CN_FONT_16X16[idx][row * 2 + bi] & (0x01 << bit)) ? fg : bg);
}

/* 判定字节是否为 UTF-8 中文首字节 (0xE0~0xEF) */
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
