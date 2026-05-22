/**
 ******************************************************************************
 * @file    Hardware/OLED.h
 * @brief   0.96 寸 OLED 显示屏驱动 —— 公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          硬件接口: 模拟 I2C (PA11=SCL, PA12=SDA), 开漏输出
 *          驱动芯片: SSD1306, 分辨率 128x64, 从机地址 0x78
 *          字体: 8x16 ASCII (OLED_Font.h)
 ******************************************************************************
 */

#ifndef __OLED_H
#define __OLED_H

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowFloatNum(uint8_t Line, uint8_t Column, double Number, uint8_t IntLength, uint8_t FractLength);

#endif
