/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.h
 * @brief   ST7735 128×160 TFT 彩色显示驱动 — 公开接口
 * @note    V6.2: SPI1 硬件驱动 (PA5=SCK, PA7=MOSI)
 *          PA4=CS (软件NSS), PA6=DC, PA0=RST, PB6=BL (TIM4_CH1 PWM 背光)
 *          SPI Mode 3 (CPOL=High, CPHA=2Edge), 只写不读
 *          横屏显示 160×128, RGB565 色彩, MADCTL=0xA0 (方向3)
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

void Tft_Driver_Init(void);
void Tft_Driver_Clear(uint16_t color);
void Tft_Driver_Set_Backlight(uint8_t brightness);   /* 0~255 */

void Tft_Driver_Show_Char(uint8_t line, uint8_t column, char ch,
                          uint16_t fg_color, uint16_t bg_color);
void Tft_Driver_Show_String(uint8_t line, uint8_t column, const char* str,
                            uint16_t fg_color, uint16_t bg_color);
void Tft_Driver_Show_Num(uint8_t line, uint8_t column, uint32_t num,
                         uint8_t len, uint16_t fg_color, uint16_t bg_color);
void Tft_Driver_Show_Float(uint8_t line, uint8_t column, float num,
                           uint8_t int_len, uint8_t fract_len,
                           uint16_t fg_color, uint16_t bg_color);
void Tft_Driver_Show_CN_String(uint8_t line, uint8_t column, const char* str,
                              uint16_t fg_color, uint16_t bg_color);
void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#endif /* TFT_DRIVER_H */
