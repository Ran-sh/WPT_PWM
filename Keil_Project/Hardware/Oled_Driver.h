/**
 ******************************************************************************
 * @file    Hardware/Oled_Driver.h
 * @brief   SSD1315 128x64 OLED 驱动 — 公开接口
 * @note    4 线 I2C (PA11-SCL, PA12-SDA), 8x16 英文字体
 *          显示坐标系: Line=1~4, Column=1~16
 ******************************************************************************
 */

#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include "stm32f10x.h"

void Oled_Driver_Init(void);
void Oled_Driver_Clear(void);

void Oled_Driver_Show_Char(uint8_t line, uint8_t column, char ch);
void Oled_Driver_Show_String(uint8_t line, uint8_t column, const char* str);
void Oled_Driver_Show_Num(uint8_t line, uint8_t column, uint32_t num, uint8_t len);
void Oled_Driver_Show_Signed_Num(uint8_t line, uint8_t column, int32_t num, uint8_t len);
void Oled_Driver_Show_Float(uint8_t line, uint8_t column, double num, uint8_t int_len, uint8_t fract_len);
void Oled_Driver_Show_Hex(uint8_t line, uint8_t column, uint32_t num, uint8_t len);
void Oled_Driver_Show_Bin(uint8_t line, uint8_t column, uint32_t num, uint8_t len);

#endif /* OLED_DRIVER_H */
