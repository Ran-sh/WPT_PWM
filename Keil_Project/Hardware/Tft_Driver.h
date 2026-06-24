/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.h
 * @brief   ST7735 128×160 TFT 彩色显示驱动 — 公开接口
 * @note    V4.3.0r3: SPI1 分时复用 (PA5=SCK, PA7=MOSI, PA6=DC/MISO动态切换)
 *          PA4=TFT_CS, PA12=W25Q128_CS (双 CS 门控), PA0=RST, PB6=BL
 *          SPI Mode 3, 全双工 (TFT 只写不读, Flash 读写)
 *          字模来源: 片内 ROM TFT_Font_Data.h (ASCI ICJK 字库)
 *          横屏 160×128, RGB565 色彩, MADCTL=0xA0
 ******************************************************************************
 */

#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#include "stm32f10x.h"

/* ── RGB565 颜色宏 ── */
#define TFT_COLOR_BLACK       0x0000
#define TFT_COLOR_WHITE       0xFFFF
#define TFT_COLOR_RED         0xF800
#define TFT_COLOR_GREEN       0x07E0
#define TFT_COLOR_BLUE        0x001F
#define TFT_COLOR_YELLOW      0xFFE0
#define TFT_COLOR_CYAN        0x07FF
#define TFT_COLOR_MAGENTA     0xF81F
#define TFT_COLOR_GRAY        0x8410
#define TFT_COLOR_ORANGE      0xFD20
#define TFT_COLOR_DARK_GREEN  0x03E0
#define TFT_COLOR_DARK_BLUE   0x0018

/* ── 显示参数 ── */
#define TFT_WIDTH             160   /* 横屏宽 (物理160) */
#define TFT_HEIGHT            128   /* 横屏高 (物理128) */
#define TFT_CHAR_PER_LINE     20    /* 160/8 = 20 */
#define TFT_LINE_COUNT        8     /* 128/16 = 8 */
#define TFT_FONT_WIDTH        8
#define TFT_FONT_HEIGHT       16

/** @brief 初始化 ST7735 TFT (硬件复位+寄存器序列+背光 PWM) */
void Tft_Driver_Init(void);
/** @brief 全屏填充单色 */
void Tft_Driver_Clear(uint16_t color);
/** @brief 设置背光亮度 (0=灭, 255=最亮, TIM4_CH1 PWM) */
void Tft_Driver_Set_Backlight(uint8_t brightness);

/** @brief 在指定行列绘制一个 ASCII 字符 (8x16) */
void Tft_Driver_Show_Char(uint8_t line, uint8_t column, char ch,
                          uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制 ASCII 字符串 (自动换行截断) */
void Tft_Driver_Show_String(uint8_t line, uint8_t column, const char* str,
                            uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制无符号整数 (右对齐, 前导空格) */
void Tft_Driver_Show_Num(uint8_t line, uint8_t column, uint32_t num,
                         uint8_t len, uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制浮点数 (右对齐, 指定整数和小数位数) */
void Tft_Driver_Show_Float(uint8_t line, uint8_t column, float num,
                           uint8_t int_len, uint8_t fract_len,
                           uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制中英文混合字符串 (自动识别 UTF-8 + ASCII) */
void Tft_Driver_Show_CN_String(uint8_t line, uint8_t column, const char* str,
                              uint16_t fg_color, uint16_t bg_color);
/** @brief 像素级填充矩形 (坐标+宽高, 含边界裁剪) */
void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
/** @brief 用黑底擦除指定像素区域 (等价 Fill_Rect 黑色, 语义明确) */
void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
/** @brief 绘制 16x16 WIFI 信号动画图标 (frame:0-3, 逐帧扩散) */
void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg);
/** @brief 绘制 16x16 单帧图标 (32字节 LSB-first 位图) */
void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],
                                  uint16_t fg, uint16_t bg);
/** @brief 在 TFT 像素坐标绘制 5×10 微型数字字符串 (7px步进, DMA发送) */
void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,
                                       const char* s,
                                       uint16_t fg, uint16_t bg);

/** @brief 按 icon_id 绘制 16x16 图标 (现有 ROM 数组, Flash Table 模式待扩展)
 *  @param icon_id  11=BATTERY 12=WARNING 13=CHECK 14=CROSS 15=POWER 16=LIGHTNING
 *                   17=TEMP 18=FAN 19=LOCK 20=HOME 21=GEAR 22=REFRESH
 *                   23=ARROW_UP 24=ARROW_DN 25=ARROW_LT 26=ARROW_RT
 *                   27=SIGNAL 28=GLOBE 29=CHART 30=CLOCK */
void Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y, uint8_t icon_id,
                                 uint16_t fg, uint16_t bg);

/** @brief 显示 SPLASH 开机动画 (从 W25Q128 SPLASH 分区读取 5帧 fade-in, DMA 泵送)
 *  @note  若 SPLASH 分区无效 (魔数不匹配) 则静默跳过, 约 250ms */
void Tft_Driver_Show_Splash(void);

#endif /* TFT_DRIVER_H */
