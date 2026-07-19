/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.h
 * @brief   ST7735 128x160 TFT 彩屏显示驱动 — V5.0.2
 * @note    SPI1由Spi1_Shared仲裁 (PA5=SCK, PA7=MOSI, PA6=DC/MISO, PA4=TFT_CS,
 *          PB12=W25Q128_CS, PA0=RST, PA12=BL)
 *          SPI Mode3, 全双工 (TFT 只写, Flash 读写)
 *          字库: Flash V2 20897 字 + 31图标 (CRC32), ROM 仅 SPLASH 4汉字
 *          横屏 160x128, RGB565, MADCTL=0xA0
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

/* ── Flash Icon ID 常量 (V2 Font_Header icon_table 索引) ── */
#define ICON_ID_WIFI_SIGNAL       0
#define ICON_ID_WIFI_CONNECT_ANIM 1
#define ICON_ID_WIFI_OFF          2
#define ICON_ID_WIFI_REMOVE       3
#define ICON_ID_MQTT_BASE         4
#define ICON_ID_MQTT_YES          5
#define ICON_ID_MQTT_NO           6
#define ICON_ID_MQTT_ANIM         7
#define ICON_ID_STAR              8
#define ICON_ID_STAR_CURSOR_ANIM  9
#define ICON_ID_ROCKET_ANIM       10
#define ICON_ID_BATTERY           11
#define ICON_ID_WARNING           12
#define ICON_ID_CHECK             13
#define ICON_ID_CROSS             14
#define ICON_ID_POWER             15
#define ICON_ID_LIGHTNING         16
#define ICON_ID_TEMP              17
#define ICON_ID_FAN               18
#define ICON_ID_LOCK              19
#define ICON_ID_HOME              20
#define ICON_ID_GEAR              21
#define ICON_ID_REFRESH           22
#define ICON_ID_ARROW_UP          23
#define ICON_ID_ARROW_DN          24
#define ICON_ID_ARROW_LT          25
#define ICON_ID_ARROW_RT          26
#define ICON_ID_SIGNAL            27
#define ICON_ID_GLOBE             28
#define ICON_ID_CHART             29
#define ICON_ID_CLOCK             30
#define ICON_ID_EXTRA1            31
#define ICON_ID_EXTRA2            32
#define ICON_ID_EXTRA3            33
#define ICON_ID_EXTRA4            34

/* ── 显示参数 ── */
#define TFT_WIDTH             160   /* 横屏宽 (物理160) */
#define TFT_HEIGHT            128   /* 横屏高 (物理128) */
#define TFT_CHAR_PER_LINE     20    /* 160/8 = 20 */
#define TFT_LINE_COUNT        8     /* 128/16 = 8 */
#define TFT_FONT_WIDTH        8
#define TFT_FONT_HEIGHT       16

typedef enum {
    TFT_DRIVER_RESULT_OK = 0,
    TFT_DRIVER_RESULT_BUSY,
    TFT_DRIVER_RESULT_SPI_TIMEOUT,
    TFT_DRIVER_RESULT_DMA_TIMEOUT,
    TFT_DRIVER_RESULT_INVALID
} Tft_Driver_Result;

/** @brief [V4.5.0] Set letter spacing extra pixels (0-6) applied after each char */
void Tft_Driver_Set_Letter_Spacing(uint8_t sp);
/** @brief [V4.5.0] Get current letter spacing */
uint8_t Tft_Driver_Get_Letter_Spacing(void);

/** @brief 初始化 ST7735 TFT (硬件复位+寄存器序列+GPIO背光)
 *  @note  仅初始化 TFT 硬件, 不访问 W25Q128 (Flash 驱动尚未就绪) */
void Tft_Driver_Init(void);
/** @brief Start a recoverable UI draw cycle and allow one new bus attempt. */
void Tft_Driver_Begin_Draw_Cycle(void);
/** @brief Return the result retained from the current draw cycle. */
Tft_Driver_Result Tft_Driver_Get_Last_Result(void);
/** @brief Return non-zero after a transfer error blocks the current draw cycle. */
uint8_t Tft_Driver_Is_Draw_Blocked(void);
/** @brief 初始化 Flash 字库 (W25Q_Driver_Init 之后调用)
 *  @note  读取并校验 W25Q128 中的 Font Header, 有效则启用 Flash 全字库路径,
 *         无效则自动回退 ROM 4 字启动页字库 */
void Tft_Driver_Font_Init(void);
/** @brief 全屏填充单色 */
void Tft_Driver_Clear(uint16_t color);
/** @brief 设置 GPIO 背光 (0=灭, 非0=亮) */
void Tft_Driver_Set_Backlight(uint8_t brightness);

/** @brief 在指定行列绘制一个 ASCII 字符 (8x16) */
void Tft_Driver_Show_Char(uint8_t line, uint8_t column, char ch,
                          uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制 ASCII 字符串 (自动换行截断) */
void Tft_Driver_Show_String(uint8_t line, uint8_t column, const char* str,
                            uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制中英文混合字符串 (自动识别 UTF-8 + ASCII) */
void Tft_Driver_Show_CN_String(uint8_t line, uint8_t column, const char* str,
                              uint16_t fg_color, uint16_t bg_color);
/** @brief 像素级填充矩形 (坐标+宽高, 含边界裁剪) */
void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
/** @brief 用黑底擦除指定像素区域 (等价 Fill_Rect 黑色, 语义明确) */
void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/** @brief 在 TFT 像素坐标绘制 5x10 微型数字字符串 (7px步进, DMA发送) */
void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,
                                       const char* s,
                                       uint16_t fg, uint16_t bg);

/** @brief 统一 Flash 图标绘制: (icon_id, frame) -> 16x16 pixel render
 *  @param x,y     top-left TFT pixel coordinates (0-based pixel)
 *  @param icon_id 0-34, see ICON_ID_* defines above
 *  @param frame   0..n_frames-1, clamped internally
 *  @param fg,bg   foreground/background RGB565 colors
 *  @retval 1=success, 0=Flash not valid or icon_id out of range
 *  @note   图标0-8支持ROM回退，其余图标需要有效Flash V2 Font_Header */
uint8_t Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y,
    uint8_t icon_id, uint8_t frame, uint16_t fg, uint16_t bg);

/** @brief 显示 SPLASH 开机动画 (纯代码: 背光渐亮 + 逐字点亮, ~4.8s, ROM 4 字)
 *  @note  Delay_Ms 步进, 不依赖 W25Q Flash */
void Tft_Driver_Show_Splash(void);

/** @brief 查询 Flash 字库是否就绪 (1=Flash 20897字+31图标, 0=ROM 4字回退)
 *  @note  Sys_Startup_Screen 用此在启动末行显示加载状态 */
uint8_t Tft_Driver_Is_Font_Flash_Valid(void);

#endif /* TFT_DRIVER_H */
