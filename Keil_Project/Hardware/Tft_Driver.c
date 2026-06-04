/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735S 128×160 TFT 彩色显示驱动 — 实现 (V6.2)
 * @note    SPI1 硬件驱动, Mode 0 (CPOL=0, CPHA=0)
 *          PA5=SCK, PA7=MOSI (SPI1 默认映射), PA4=CS (软件NSS), PA6=DC, PA0=RST
 *          PB6=TIM4_CH1 背光 PWM 调光
 *          横屏模式: 160×128, RGB565
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "TFT_Font.h"

/* ── 硬件引脚宏 ── */
#define TFT_CS_PORT     GPIOA
#define TFT_CS_PIN      GPIO_Pin_4
#define TFT_DC_PORT     GPIOA
#define TFT_DC_PIN      GPIO_Pin_6
#define TFT_RST_PORT    GPIOA
#define TFT_RST_PIN     GPIO_Pin_0
#define TFT_BL_PORT     GPIOB
#define TFT_BL_PIN      GPIO_Pin_6

#define TFT_CS_LOW()    GPIO_ResetBits(TFT_CS_PORT, TFT_CS_PIN)
#define TFT_CS_HIGH()   GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN)
#define TFT_DC_CMD()    GPIO_ResetBits(TFT_DC_PORT, TFT_DC_PIN)   /* DC=0: 命令 */
#define TFT_DC_DATA()   GPIO_SetBits(TFT_DC_PORT, TFT_DC_PIN)     /* DC=1: 数据 */

/* ── 内部函数 ── */
static void Tft_SPI_Init(void);
static void Tft_Write_Command(uint8_t cmd);
static void Tft_Write_Data(uint8_t data);
static void Tft_Write_Data_16(uint16_t data);
static void Tft_Set_Address_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static uint32_t Tft_Int_Pow(uint32_t base, uint32_t exp);

/* ═══════════════════════════════════════════════════════════════
 *  SPI1 底层配置
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_SPI_Init(void)
{
    GPIO_InitTypeDef  gpio;
    SPI_InitTypeDef   spi;
    TIM_TimeBaseInitTypeDef  tim_base;
    TIM_OCInitTypeDef        oc;

    /* SPI1 + GPIO 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);  /* TIM4 在 APB1 总线上 */

    /* PA5=SCK, PA7=MOSI (SPI1 默认映射, 不重映射) */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* CS(PA4), DC(PA6), RST(PA0) — GPIO 推挽输出 */
    gpio.GPIO_Pin   = TFT_CS_PIN | TFT_DC_PIN | TFT_RST_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);

    /* BL(PB6) — TIM4_CH1 背光 PWM 调光 */
    gpio.GPIO_Pin   = TFT_BL_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);

    TFT_CS_HIGH();

    /* SPI1 配置: Mode 0 (CPOL=0, CPHA=0), Master, 18MHz (72M/4) */
    SPI_StructInit(&spi);
    spi.SPI_Direction      = SPI_Direction_1Line_Tx;  /* 只写 */
    spi.SPI_Mode           = SPI_Mode_Master;
    spi.SPI_DataSize       = SPI_DataSize_8b;
    spi.SPI_CPOL           = SPI_CPOL_Low;
    spi.SPI_CPHA           = SPI_CPHA_1Edge;
    spi.SPI_NSS            = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;  /* 72M/4 = 18MHz */
    spi.SPI_FirstBit       = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial  = 7;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* ── 背光 PWM: TIM4_CH1, 1kHz, PB6 ── */
    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler   = 71;      /* 72MHz/(71+1) = 1MHz */
    tim_base.TIM_Period      = 999;     /* 1MHz/(999+1) = 1kHz */
    tim_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &tim_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode     = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 999;  /* 默认全亮 (100%) */
    TIM_OC1Init(TIM4, &oc);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

/* ═══════════════════════════════════════════════════════════════
 *  SPI 收发
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_Write_Command(uint8_t cmd)
{
    TFT_DC_CMD();
    TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, cmd);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

static void Tft_Write_Data(uint8_t data)
{
    TFT_DC_DATA();
    TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

static void Tft_Write_Data_16(uint16_t data)
{
    TFT_DC_DATA();
    TFT_CS_LOW();
    SPI_I2S_SendData(SPI1, (uint8_t)(data >> 8));
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    SPI_I2S_SendData(SPI1, (uint8_t)(data & 0xFF));
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
    TFT_CS_HIGH();
}

/* ═══════════════════════════════════════════════════════════════
 *  ST7735S 初始化序列
 * ═══════════════════════════════════════════════════════════════ */

static void Tft_Hardware_Reset(void)
{
    /* 硬件复位: RST=0 延迟 → RST=1 */
    GPIO_ResetBits(TFT_RST_PORT, TFT_RST_PIN);
    { volatile uint32_t i; for (i = 0; i < 100000; i++) __NOP(); }  /* ~10ms */
    GPIO_SetBits(TFT_RST_PORT, TFT_RST_PIN);
    { volatile uint32_t i; for (i = 0; i < 100000; i++) __NOP(); }  /* ~10ms */
}

static void Tft_Set_Address_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* CASET: 列地址 */
    Tft_Write_Command(0x2A);
    Tft_Write_Data_16(x1 + 1);   /* ST7735S COLSTART = 1 (off-by-1) */
    Tft_Write_Data_16(x2 + 1);

    /* RASET: 行地址 */
    Tft_Write_Command(0x2B);
    Tft_Write_Data_16(y1 + 26);  /* ST7735S ROWSTART = 26 (off-by-26) */
    Tft_Write_Data_16(y2 + 26);

    /* RAMWR: 写内存 */
    Tft_Write_Command(0x2C);
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Init(void)
{
    Tft_SPI_Init();
    Tft_Hardware_Reset();

    /* ── ST7735S 初始化序列 (横屏 160×128) ── */

    Tft_Write_Command(0x11);  /* SLEEP OUT */
    { volatile uint32_t i; for (i = 0; i < 1000000; i++) __NOP(); }  /* ~120ms */

    Tft_Write_Command(0x36);  /* MADCTL: 内存数据访问控制 */
    Tft_Write_Data(0xC8);     /* MY=1, MX=1, MV=1 → 横屏, RGB 顺序 */

    Tft_Write_Command(0x3A);  /* COLMOD: 像素格式 */
    Tft_Write_Data(0x05);     /* 16-bit/pixel (RGB565) */

    /* ── Porch 设置 (ST7735S 标准值) ── */
    Tft_Write_Command(0xB1);  /* FRMCTR1: 帧速率控制 */
    Tft_Write_Data(0x01);
    Tft_Write_Data(0x2C);
    Tft_Write_Data(0x2D);

    Tft_Write_Command(0xB2);  /* FRMCTR2 */
    Tft_Write_Data(0x01);
    Tft_Write_Data(0x2C);
    Tft_Write_Data(0x2D);

    Tft_Write_Command(0xB3);  /* FRMCTR3 */
    Tft_Write_Data(0x01);
    Tft_Write_Data(0x2C);
    Tft_Write_Data(0x2D);
    Tft_Write_Data(0x01);
    Tft_Write_Data(0x2C);
    Tft_Write_Data(0x2D);

    Tft_Write_Command(0xB4);  /* INVCTR: 列反转 */
    Tft_Write_Data(0x07);

    /* ── 电源设置 ── */
    Tft_Write_Command(0xC0);  /* PWCTR1 */
    Tft_Write_Data(0xA2);
    Tft_Write_Data(0x02);
    Tft_Write_Data(0x84);

    Tft_Write_Command(0xC1);  /* PWCTR2 */
    Tft_Write_Data(0xC5);

    Tft_Write_Command(0xC2);  /* PWCTR3 */
    Tft_Write_Data(0x0A);
    Tft_Write_Data(0x00);

    Tft_Write_Command(0xC3);  /* PWCTR4 */
    Tft_Write_Data(0x8A);
    Tft_Write_Data(0x2A);

    Tft_Write_Command(0xC4);  /* PWCTR5 */
    Tft_Write_Data(0x8A);
    Tft_Write_Data(0xEE);

    Tft_Write_Command(0xC5);  /* VMCTR1: VCOM 控制 */
    Tft_Write_Data(0x0E);

    /* ── Gamma 校正 ── */
    Tft_Write_Command(0xE0);  /* GMCTRP1: Gamma (+) */
    Tft_Write_Data(0x02); Tft_Write_Data(0x1C); Tft_Write_Data(0x07);
    Tft_Write_Data(0x12); Tft_Write_Data(0x37); Tft_Write_Data(0x32);
    Tft_Write_Data(0x29); Tft_Write_Data(0x2D); Tft_Write_Data(0x29);
    Tft_Write_Data(0x25); Tft_Write_Data(0x2B); Tft_Write_Data(0x39);
    Tft_Write_Data(0x00); Tft_Write_Data(0x01); Tft_Write_Data(0x03);
    Tft_Write_Data(0x10);

    Tft_Write_Command(0xE1);  /* GMCTRN1: Gamma (-) */
    Tft_Write_Data(0x03); Tft_Write_Data(0x1D); Tft_Write_Data(0x07);
    Tft_Write_Data(0x06); Tft_Write_Data(0x2E); Tft_Write_Data(0x2C);
    Tft_Write_Data(0x29); Tft_Write_Data(0x2D); Tft_Write_Data(0x2E);
    Tft_Write_Data(0x2E); Tft_Write_Data(0x37); Tft_Write_Data(0x3F);
    Tft_Write_Data(0x00); Tft_Write_Data(0x00); Tft_Write_Data(0x02);
    Tft_Write_Data(0x10);

    Tft_Write_Command(0x13);  /* NORON: 正常显示模式 */
    Tft_Write_Command(0x20);  /* INVOFF: 非反转 */
    Tft_Write_Command(0x29);  /* DISPON: 开启显示 */

    Tft_Driver_Clear(TFT_COLOR_BLACK);
}

void Tft_Driver_Clear(uint16_t color)
{
    uint16_t x, y;
    Tft_Set_Address_Window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    for (y = 0; y < TFT_HEIGHT; y++)
        for (x = 0; x < TFT_WIDTH; x++)
            Tft_Write_Data_16(color);
}

void Tft_Driver_Set_Backlight(uint8_t brightness)
{
    /* 0~255 映射到 TIM4 CCR1 (0~999) */
    uint16_t pulse = ((uint16_t)brightness * 999) / 255;
    TIM_SetCompare1(TIM4, pulse);
}

void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t cx, cy;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

    Tft_Set_Address_Window(x, y, x + w - 1, y + h - 1);
    for (cy = 0; cy < h; cy++)
        for (cx = 0; cx < w; cx++)
            Tft_Write_Data_16(color);
}

/* ═══════════════════════════════════════════════════════════════
 *  字符/字符串显示
 * ═══════════════════════════════════════════════════════════════ */

void Tft_Driver_Show_Char(uint8_t line, uint8_t column, char ch,
                          uint16_t fg_color, uint16_t bg_color)
{
    uint8_t row, col_byte, bit;
    uint16_t x_base, y_base;

    if (line >= TFT_LINE_COUNT || column >= TFT_CHAR_PER_LINE) return;
    if ((uint8_t)ch < ' ' || (uint8_t)ch > '~') return;

    x_base = column * TFT_FONT_WIDTH;
    y_base = line * TFT_FONT_HEIGHT;

    Tft_Set_Address_Window(x_base, y_base,
                           x_base + TFT_FONT_WIDTH - 1,
                           y_base + TFT_FONT_HEIGHT - 1);

    for (row = 0; row < TFT_FONT_HEIGHT; row++) {
        col_byte = TFT_FONT_8X16[ch - ' '][row];
        for (bit = 0; bit < TFT_FONT_WIDTH; bit++) {
            if (col_byte & (0x80 >> bit))
                Tft_Write_Data_16(fg_color);
            else
                Tft_Write_Data_16(bg_color);
        }
    }
}

void Tft_Driver_Show_String(uint8_t line, uint8_t column, const char* str,
                            uint16_t fg_color, uint16_t bg_color)
{
    uint8_t i;
    for (i = 0; str[i] != '\0'; i++) {
        if (column + i >= TFT_CHAR_PER_LINE) break;
        Tft_Driver_Show_Char(line, (uint8_t)(column + i), str[i], fg_color, bg_color);
    }
}

static uint32_t Tft_Int_Pow(uint32_t base, uint32_t exp)
{
    uint32_t result = 1;
    while (exp--) result *= base;
    return result;
}

void Tft_Driver_Show_Num(uint8_t line, uint8_t column, uint32_t num,
                         uint8_t len, uint16_t fg_color, uint16_t bg_color)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        Tft_Driver_Show_Char(line, (uint8_t)(column + i),
                             (char)(num / Tft_Int_Pow(10, len - i - 1) % 10 + '0'),
                             fg_color, bg_color);
}

void Tft_Driver_Show_Float(uint8_t line, uint8_t column, float num,
                           uint8_t int_len, uint8_t fract_len,
                           uint16_t fg_color, uint16_t bg_color)
{
    uint32_t int_part, fract_part;
    uint32_t pow10 = Tft_Int_Pow(10, fract_len);
    uint8_t  i, col = column;

    if (num < 0.0f) {
        Tft_Driver_Show_Char(line, col, '-', fg_color, bg_color);
        num = -num;
        col++;
    }

    num += 0.5f / (float)pow10;
    int_part   = (uint32_t)num;
    fract_part = (uint32_t)((num - (float)int_part) * (float)pow10);

    for (i = 0; i < int_len; i++)
        Tft_Driver_Show_Char(line, (uint8_t)(col + i),
                             (char)(int_part / Tft_Int_Pow(10, int_len - i - 1) % 10 + '0'),
                             fg_color, bg_color);

    Tft_Driver_Show_Char(line, (uint8_t)(col + int_len), '.', fg_color, bg_color);

    for (i = 0; i < fract_len; i++)
        Tft_Driver_Show_Char(line, (uint8_t)(col + int_len + 1 + i),
                             (char)(fract_part / Tft_Int_Pow(10, fract_len - i - 1) % 10 + '0'),
                             fg_color, bg_color);
}
