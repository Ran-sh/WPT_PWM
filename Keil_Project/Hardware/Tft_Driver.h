#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#include "stm32f10x.h"

/* RGB565颜色常量。 */
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

/* 外部字库图标编号常量，对应字库头中的图标索引表。 */
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

/* 显示参数。 */
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

/** @brief 设置每个字符后附加的像素间距，允许范围为0至6 */
void Tft_Driver_Set_Letter_Spacing(uint8_t sp);
/** @brief 获取当前字符间距 */
uint8_t Tft_Driver_Get_Letter_Spacing(void);

/** @brief 初始化ST7735显示屏，包括硬件复位、寄存器配置和背光引脚
 *  @note  此阶段只初始化显示硬件，不访问尚未就绪的W25Q128。
 */
void Tft_Driver_Init(void);
/** @brief 开始一次可恢复的界面绘制周期，并允许重新尝试总线访问 */
void Tft_Driver_Begin_Draw_Cycle(void);
/** @brief 获取当前绘制周期保留的总线操作结果 */
Tft_Driver_Result Tft_Driver_Get_Last_Result(void);
/** @brief 判断当前绘制周期是否因传输错误而停止后续绘制 */
uint8_t Tft_Driver_Is_Draw_Blocked(void);
/** @brief 初始化外部字库，必须在W25Q128驱动初始化后调用
 *  @note  字库头和校验值有效时启用完整字库，否则回退到片内四字启动字库。
 */
void Tft_Driver_Font_Init(void);
/** @brief 全屏填充单色 */
void Tft_Driver_Clear(uint16_t color);
/** @brief 设置背光引脚，0表示关闭，非0表示开启 */
void Tft_Driver_Set_Backlight(uint8_t brightness);

/** @brief 在指定行列绘制一个8乘16单字节字符 */
void Tft_Driver_Show_Char(uint8_t line, uint8_t column, char ch,
                          uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制单字节字符串，超出边界时自动换行或截断 */
void Tft_Driver_Show_String(uint8_t line, uint8_t column, const char* str,
                            uint16_t fg_color, uint16_t bg_color);
/** @brief 绘制中英文混合字符串，自动识别UTF-8中文编码和单字节字符 */
void Tft_Driver_Show_CN_String(uint8_t line, uint8_t column, const char* str,
                              uint16_t fg_color, uint16_t bg_color);
/** @brief 像素级填充矩形 (坐标+宽高, 含边界裁剪) */
void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
/** @brief 用黑底擦除指定像素区域 (等价 Fill_Rect 黑色, 语义明确) */
void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/** @brief 按像素坐标绘制5乘10微型数字字符串，字符步进为7像素 */
void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,
                                       const char* s,
                                       uint16_t fg, uint16_t bg);
/** @brief 按像素坐标绘制整数倍放大的5乘10数字字符串，scale为1或2 */
void Tft_Driver_Show_5x10_String_Scaled_Pixel(uint16_t x, uint16_t y,
                                               const char* s, uint8_t scale,
                                               uint16_t fg, uint16_t bg);

/** @brief 按编号和帧号绘制16乘16图标
 *  @param x 图标左上角横坐标，从0开始
 *  @param y 图标左上角纵坐标，从0开始
 *  @param icon_id 图标编号，允许范围为0至34
 *  @param frame 动画帧编号，超出范围时在内部钳位
 *  @param fg RGB565前景色
 *  @param bg RGB565背景色
 *  @retval 1表示绘制成功，0表示字库无效或图标编号越界
 *  @note   编号0至8支持片内回退，其余图标需要有效的外部字库。
 */
uint8_t Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y,
    uint8_t icon_id, uint8_t frame, uint16_t fg, uint16_t bg);

/** @brief 显示由代码绘制的开机动画，包括背光开启和逐字渐亮
 *  @note  动画使用启动阶段延时，不依赖W25Q128中的图片数据。
 */
void Tft_Driver_Show_Splash(void);

/** @brief 查询外部字库是否就绪
 *  @retval 1表示完整字库有效，0表示正在使用片内四字回退
 */
uint8_t Tft_Driver_Is_Font_Flash_Valid(void);

#endif /* 彩屏显示驱动接口结束 */
