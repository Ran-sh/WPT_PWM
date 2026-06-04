/**
 ******************************************************************************
 * @file    Hardware/Oled_Driver.c
 * @brief   SSD1315 128x64 OLED 驱动 — 实现
 * @note    模拟 I2C (PA11-SCL, PA12-SDA), 开漏 + 外部上拉
 ******************************************************************************
 */

#include "Oled_Driver.h"

/* ── 字体数据 (8x16, ASCII 空格 ~ '~') ── */
#include "OLED_Font.h"

/* ── 硬件常量 ── */
#define OLED_I2C_ADDR       0x78
#define OLED_CMD_PREFIX     0x00
#define OLED_DATA_PREFIX    0x40
#define OLED_LINE_COUNT     4
#define OLED_COLUMN_COUNT   16
#define OLED_PAGE_COUNT     8
#define OLED_PIXEL_PER_COL  128
#define OLED_FONT_WIDTH     8
#define OLED_FONT_HEIGHT    16
#define OLED_CHAR_PER_LINE  16

/* ── 引脚宏 ── */
#define OLED_PIN_SCL(x)  GPIO_WriteBit(GPIOA, GPIO_Pin_11, (BitAction)(x))
#define OLED_PIN_SDA(x)  GPIO_WriteBit(GPIOA, GPIO_Pin_12, (BitAction)(x))

/* ── 内部函数声明 ── */
static void Oled_I2C_Init(void);
static void Oled_I2C_Start(void);
static void Oled_I2C_Stop(void);
static void Oled_I2C_Send_Byte(uint8_t byte);
static void Oled_Write_Command(uint8_t cmd);
static void Oled_Write_Data(uint8_t data);
static void Oled_Set_Cursor(uint8_t page, uint8_t column);
static uint32_t Oled_Int_Pow(uint32_t base, uint32_t exp);

/* ═══════════════════════════════════════════════════════════════
 *  I2C 底层
 * ═══════════════════════════════════════════════════════════════ */

static void Oled_I2C_Init(void)
{
    GPIO_InitTypeDef cfg;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    cfg.GPIO_Mode  = GPIO_Mode_Out_OD;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;

    cfg.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOA, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOA, &cfg);

    OLED_PIN_SCL(1);
    OLED_PIN_SDA(1);
}

static void Oled_I2C_Start(void)
{
    OLED_PIN_SDA(1);
    OLED_PIN_SCL(1);
    OLED_PIN_SDA(0);
    OLED_PIN_SCL(0);
}

static void Oled_I2C_Stop(void)
{
    OLED_PIN_SDA(0);
    OLED_PIN_SCL(1);
    OLED_PIN_SDA(1);
}

static void Oled_I2C_Send_Byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        OLED_PIN_SDA(!!(byte & (0x80 >> i)));
        OLED_PIN_SCL(1);
        OLED_PIN_SCL(0);
    }
    OLED_PIN_SCL(1);
    OLED_PIN_SCL(0);  /* 第 9 个时钟, 忽略 ACK */
}

/* Oled_Write_Byte — I2C 写单字节 (合并 Cmd/Data 公共逻辑, 消除重复) */
static void Oled_Write_Byte(uint8_t prefix, uint8_t byte)
{
    Oled_I2C_Start();
    Oled_I2C_Send_Byte(OLED_I2C_ADDR);
    Oled_I2C_Send_Byte(prefix);
    Oled_I2C_Send_Byte(byte);
    Oled_I2C_Stop();
}

static void Oled_Write_Command(uint8_t cmd) { Oled_Write_Byte(OLED_CMD_PREFIX, cmd); }
static void Oled_Write_Data(uint8_t data)   { Oled_Write_Byte(OLED_DATA_PREFIX, data); }

static void Oled_Set_Cursor(uint8_t page, uint8_t column)
{
    Oled_Write_Command(0xB0 | page);
    Oled_Write_Command(0x10 | ((column & 0xF0) >> 4));
    Oled_Write_Command(0x00 | (column & 0x0F));
}


/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ═══════════════════════════════════════════════════════════════ */

void Oled_Driver_Init(void)
{
    volatile uint32_t i, j;
    for (i = 0; i < 1000; i++)
        for (j = 0; j < 1000; j++);

    Oled_I2C_Init();

    Oled_Write_Command(0xAE);  /* 关闭显示 */
    Oled_Write_Command(0xD5);  Oled_Write_Command(0x80);   /* 时钟分频 */
    Oled_Write_Command(0xA8);  Oled_Write_Command(0x3F);   /* 复用率 64 */
    Oled_Write_Command(0xD3);  Oled_Write_Command(0x00);   /* 偏移 0 */
    Oled_Write_Command(0x40);                          /* 起始行 */
    Oled_Write_Command(0xA1);                          /* 左右正常 */
    Oled_Write_Command(0xC8);                          /* 上下正常 */
    Oled_Write_Command(0xDA);  Oled_Write_Command(0x12);   /* COM 引脚 */
    Oled_Write_Command(0x81);  Oled_Write_Command(0xCF);   /* 对比度 */
    Oled_Write_Command(0xD9);  Oled_Write_Command(0xF1);   /* 预充电 */
    Oled_Write_Command(0xDB);  Oled_Write_Command(0x30);   /* VCOMH */
    Oled_Write_Command(0xA4);                          /* 全屏关 */
    Oled_Write_Command(0xA6);                          /* 正常显示 */
    Oled_Write_Command(0x8D);  Oled_Write_Command(0x14);   /* 电荷泵 */
    Oled_Write_Command(0xAF);                          /* 开启显示 */

    Oled_Driver_Clear();
}

void Oled_Driver_Clear(void)
{
    uint8_t page, col;
    for (page = 0; page < OLED_PAGE_COUNT; page++) {
        Oled_Set_Cursor(page, 0);
        for (col = 0; col < OLED_PIXEL_PER_COL; col++) {
            Oled_Write_Data(0x00);
        }
    }
}

void Oled_Driver_Show_Char(uint8_t line, uint8_t column, char ch)
{
    uint8_t i;
    if (line < 1 || line > OLED_LINE_COUNT) return;
    if ((uint8_t)ch < ' ' || (uint8_t)ch > '~')  return;

    Oled_Set_Cursor((line - 1) * 2, (column - 1) * OLED_FONT_WIDTH);
    for (i = 0; i < 8; i++)
        Oled_Write_Data(OLED_F8X16[ch - ' '][i]);

    Oled_Set_Cursor((line - 1) * 2 + 1, (column - 1) * OLED_FONT_WIDTH);
    for (i = 0; i < 8; i++)
        Oled_Write_Data(OLED_F8X16[ch - ' '][i + 8]);
}

void Oled_Driver_Show_String(uint8_t line, uint8_t column, const char* str)
{
    uint8_t i;
    for (i = 0; str[i] != '\0'; i++)
        Oled_Driver_Show_Char(line, column + i, str[i]);
}

/* Oled_Pow10_Lut — 10^n 查找表 (n≤9), 替代 Oled_Int_Pow 循环乘法 */
static const uint32_t s_pow10_lut[] = {1, 10, 100, 1000, 10000, 100000,
                                       1000000, 10000000, 100000000, 1000000000};

void Oled_Driver_Show_Num(uint8_t line, uint8_t column, uint32_t num, uint8_t len)
{
    uint8_t i;
    uint32_t div = s_pow10_lut[len - 1];  /* 预取最高位除数, O(n) 整数除法 */
    for (i = 0; i < len; i++) {
        Oled_Driver_Show_Char(line, column + i, (num / div) % 10 + '0');
        div /= 10;
    }
}

void Oled_Driver_Show_Signed_Num(uint8_t line, uint8_t column, int32_t num, uint8_t len)
{
    uint32_t abs_val;
    uint8_t i;
    uint32_t div;
    if (num >= 0) {
        Oled_Driver_Show_Char(line, column, '+');
        abs_val = (uint32_t)num;
    } else {
        Oled_Driver_Show_Char(line, column, '-');
        abs_val = (uint32_t)(-num);
    }
    div = s_pow10_lut[len - 1];
    for (i = 0; i < len; i++) {
        Oled_Driver_Show_Char(line, column + i + 1, (abs_val / div) % 10 + '0');
        div /= 10;
    }
}

void Oled_Driver_Show_Float(uint8_t line, uint8_t column, float num,
                             uint8_t int_len, uint8_t fract_len)
{
    uint32_t int_part, fract_part;
    uint32_t pow10 = s_pow10_lut[fract_len];  /* LUT 替代 Oled_Int_Pow */
    uint8_t  offset = 0;

    if (num < 0.0f) {
        Oled_Driver_Show_Char(line, column, '-');
        num = -num;
        offset = 1;
    }

    num += 0.5f / (float)pow10;
    int_part   = (uint32_t)num;
    fract_part = (uint32_t)((num - (float)int_part) * (float)pow10);

    Oled_Driver_Show_Num(line, column + offset, int_part, int_len);
    Oled_Driver_Show_Char(line, column + offset + int_len, ':');
    Oled_Driver_Show_Num(line, column + offset + int_len + 1, fract_part, fract_len);
}

void Oled_Driver_Show_Hex(uint8_t line, uint8_t column, uint32_t num, uint8_t len)
{
    uint8_t i, val, shift;
    for (i = 0; i < len; i++) {
        shift = 4 * (len - i - 1);   /* 位位移替代 Oled_Int_Pow(16,*) */
        val = (num >> shift) & 0xF;
        Oled_Driver_Show_Char(line, column + i, val < 10 ? val + '0' : val - 10 + 'A');
    }
}

void Oled_Driver_Show_Bin(uint8_t line, uint8_t column, uint32_t num, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        Oled_Driver_Show_Char(line, column + i, ((num >> (len - i - 1)) & 1) + '0');  /* 位移替代 Oled_Int_Pow */
}
