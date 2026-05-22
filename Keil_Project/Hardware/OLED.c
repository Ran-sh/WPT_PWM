/**
 ******************************************************************************
 * @file    Hardware/OLED.c
 * @brief   0.96 寸 OLED 显示屏驱动 (SSD1306 芯片, 模拟 I2C 通信)
 * @note    存放路径: 项目根目录\Hardware\
 *
 *          硬件接口:
 *            PA11 → OLED_SCL (I2C 时钟线, 开漏输出)
 *            PA12 → OLED_SDA (I2C 数据线, 开漏输出)
 *
 *          驱动参数:
 *            芯片型号  : SSD1306
 *            分辨率    : 128 x 64 像素
 *            行列映射  : 行(Y) 0~7 (8页, 每页8像素高), 列(X) 0~127
 *            从机地址  : 0x78 (SA0=0)
 *            字体      : 8x16 (宽8像素, 高16像素占2页)
 *            最大字符数: 每行16个字符, 共4行
 *
 *          模拟 I2C 时序:
 *            起始条件: SCL=1 时 SDA 下降沿
 *            停止条件: SCL=1 时 SDA 上升沿
 *            数据发送: MSB 先发, 每个 bit 在 SCL 高电平时采样
 *            应答处理: 第9个时钟忽略 ACK (因 OLED 复位后拉高 SDA, 不产生 ACK)
 *
 *          关键寄存器指令:
 *            0xAE/AF : 显示 关/开
 *            0xB0~B7 : 页地址 (Y 坐标, 0~7)
 *            0x00~0x0F | 0x10~0x1F : 列地址低4位/高4位 (X 坐标, 0~127)
 *            0x40    : 数据模式 (写入 GDDRAM)
 *            0x00    : 命令模式
 *
 *          初始化序列 (SSD1306 数据手册 §10.1):
 *            关闭显示 → 时钟设置 → 复用率 → 偏移 → 起始行 →
 *            方向设置 → COM引脚 → 对比度 → 预充电 → VCOMH →
 *            全屏开关 → 反色 → 电荷泵 → 开启显示 → 清屏
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "OLED_Font.h"

/*
 * 引脚操作宏
 *
 * 电路连接:
 *   PA11 (SCL) ──┐
 *                ├──→ OLED 模块 SCL 引脚
 *   PA12 (SDA) ──┘──→ OLED 模块 SDA 引脚
 *
 * 两者均配置为开漏输出 (Out_OD):
 *   开漏输出 + 外部上拉电阻 (4.7kΩ 至 3.3V) 实现线与功能,
 *   满足 I2C 电气特性 (双向数据线, 多主从)
 */
#define OLED_W_SCL(x)  GPIO_WriteBit(GPIOA, GPIO_Pin_11, (BitAction)(x))
#define OLED_W_SDA(x)  GPIO_WriteBit(GPIOA, GPIO_Pin_12, (BitAction)(x))

/**
 * @brief  I2C 引脚初始化 — PA11(SCL) / PA12(SDA) 均配置为开漏输出
 * @note   开漏输出 + 外部上拉 = I2C 标准电气接口
 *         初始化后强制拉高总线进入空闲态
 */
void OLED_I2C_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;      /* 开漏输出, 配合外部上拉实现 I2C */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;              /* SCL 时钟线 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;              /* SDA 数据线 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 总线空闲: SCL 和 SDA 均拉高 */
    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_W_SDA(0);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(!!(Byte & (0x80 >> i)));
		OLED_W_SCL(1);
		OLED_W_SCL(0);
	}
	OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command); 
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x40);		//写数据
	OLED_I2C_SendByte(Data);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{  
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{      	
	uint8_t i;
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);			//显示上半部分内容
	}
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);		//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)							
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	uint32_t i, j;
	
	for (i = 0; i < 1000; i++)			//上电延时
	{
		for (j = 0; j < 1000; j++);
	}
	
	OLED_I2C_Init();			//端口初始化
	
	OLED_WriteCommand(0xAE);	//关闭显示
	
	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);
	
	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);
	
	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);
	
	OLED_WriteCommand(0x40);	//设置显示开始行
	
	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置
	
	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);
	
	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/倒转显示

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示
		
	OLED_Clear();				//OLED清屏
}

/**
  * @brief  OLED显示浮点数（带符号和自动四舍五入）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的浮点数字，支持正负数
  * @param  IntLength 整数部分的长度，范围：1~10（不足补0）
  * @param  FractLength 小数部分的长度，范围：1~9
  * @retval 无
  */
void OLED_ShowFloatNum(uint8_t Line, uint8_t Column, double Number, uint8_t IntLength, uint8_t FractLength)
{
	uint32_t IntNum;
	uint32_t FractNum;
	
	// 1. 处理符号位：负数显示 '-'，正数显示空格防重影
	if (Number < 0)
	{
		OLED_ShowChar(Line, Column, '-');
		Number = -Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, ' '); 
	}
	
	// 2. 核心魔法：四舍五入补偿
	// 浮点数在单片机内存里往往是 3.1399999... 直接截断会变成 3.13。加上这个半值就能完美修正为 3.14
	Number += 0.5 / OLED_Pow(10, FractLength);
	
	// 3. 暴力拆分：整数部分与小数部分
	IntNum = (uint32_t)Number;
	FractNum = (uint32_t)((Number - IntNum) * OLED_Pow(10, FractLength));
	
	// 4. 依次打屏：整数 + 小数点 + 小数部分
	OLED_ShowNum(Line, Column + 1, IntNum, IntLength);
	OLED_ShowChar(Line, Column + 1 + IntLength, '.');
	OLED_ShowNum(Line, Column + 1 + IntLength + 1, FractNum, FractLength);
}

