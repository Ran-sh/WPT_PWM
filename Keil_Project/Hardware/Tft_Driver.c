/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.c
 * @brief   ST7735彩屏显示与字库驱动 — V5.1.2
 *
 *  硬件连接（显示屏与W25Q128分时使用SPI1）:
 *  +------------------------------------------------------------+
 *  |    STM32F103C8T6                   ST7735彩屏                |
 *  |                                                             |
 *  |    PA5  --- SPI1_SCK -----------------> SCL，18MHz           |
 *  |    PA7  --- SPI1_MOSI ----------------> SDA                  |
 *  |    PA4  --- 推挽输出 -----------------> CS，低电平有效       |
 *  |    PA6  --- 推挽输出 -----------------> DC，区分命令与数据   |
 *  |    PA0  --- 推挽输出 -----------------> RES，低电平复位      |
 *  |    PA12 --- 推挽输出 -----------------> BL，背光开关         |
 *  |                                                             |
 *  |    PA6  --- SPI1_MISO <--------------- W25Q128 DO           |
 *  |    PB12 --- 推挽输出 -----------------> W25Q128片选          |
 *  |                                                             |
 *  |    SPI采用模式3和全双工配置                                 |
 *  |    横屏分辨率160x128，窗口坐标横向加1、纵向加2              |
 *  |    命令使用8位轮询，像素使用DMA1通道3批量发送               |
 *  |                                                             |
 *  |    PA6在显示屏控制输出与存储器数据输入之间动态切换         |
 *  |    SPI1在8位命令与16位像素模式之间受控切换                  |
 *  +------------------------------------------------------------+
 *
 * @note    外部字库校验成功时使用完整字库，失败时回退到片内四字启动字库。
 *          开机动画完全由代码绘制，不依赖W25Q128中的图片资源。
 ******************************************************************************
 */

#include "Tft_Driver.h"
#include "TFT_Font_Data.h"
#include "W25Q_Driver.h"
#include "Sys_Timer.h"
#include "Checksum.h"
#include "Spi1_Shared.h"
#include <stddef.h>

#define TFT_DRIVER_RST_PIN  GPIO_Pin_0
#define TFT_DRIVER_BL_PIN   GPIO_Pin_12
#define TFT_DRIVER_BL_PORT  GPIOA

#define TFT_DMA_MAX_PIXELS  65535
#define TFT_DMA_TIMEOUT_MS  200U
#define TFT_FONT_MAGIC      0x574BU
#define TFT_FONT_MAX_SIZE   0x200000U
#define TFT_FONT_VERSION_LEGACY       1U
#define TFT_FONT_VERSION_PAYLOAD_CRC  2U
#define TFT_FONT_ICON_TABLE_ADDR      0x00000700UL
#define TFT_FONT_ICON_HEADER_SIZE     16U
#define TFT_FONT_ICON_ENTRY_SIZE      8U
#define TFT_FONT_ICON_GLYPH_SIZE      32U
#define TFT_FONT_CRC_CHUNK_SIZE       256U

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  reserved;
    uint32_t total_size;
    uint32_t crc32;
    uint16_t ascii_offset;
    uint16_t ascii_count;
    uint16_t ascii_bytes;
    uint16_t reserved2;
    uint32_t cjk_index_offset;
    uint16_t cjk_index_count;
    uint16_t cjk_data_bytes;
    uint32_t cjk_data_offset;
} Font_Header;

/* DMA传输状态。 */
static uint8_t s_dma_configured = 0;
static uint8_t  s_font_flash_valid = 0;     /* 1表示外部字库头及校验值有效 */
static Font_Header g_font_header;           /* 缓存在内存中的32字节字库头 */
static uint16_t s_icon_count = 0U;
static uint32_t s_icon_data_addr = 0U;
static uint32_t s_icon_data_size = 0U;
static Tft_Driver_Result s_tft_last_result = TFT_DRIVER_RESULT_OK;
static uint8_t s_tft_draw_blocked = 0U;

/* 像素缓冲区。
 * 两倍中文字符最大为32列乘32行，共需2048字节。
 * 缓冲区占用约2KB内存，一倍字符只使用其中128个半字。 */
static uint16_t s_dma_buf[1024];

/* 字体缩放倍数，当前通过像素复制实现两倍放大。 */
static const uint8_t s_font_scale = 1U;

/* 字符间距，允许在每个字符后增加0至6个像素。 */
static uint8_t s_letter_spacing = 0;


static Tft_Driver_Result Tft_Driver_Map_Spi_Result(
    Spi1_Shared_Result result)
{
    if (result == SPI1_SHARED_RESULT_BUSY) {
        return TFT_DRIVER_RESULT_BUSY;
    }
    if (result == SPI1_SHARED_RESULT_TIMEOUT) {
        return TFT_DRIVER_RESULT_SPI_TIMEOUT;
    }
    if (result == SPI1_SHARED_RESULT_OK) {
        return TFT_DRIVER_RESULT_OK;
    }
    return TFT_DRIVER_RESULT_INVALID;
}

static void Tft_Driver_Record_Error(Tft_Driver_Result result)
{
    if (result == TFT_DRIVER_RESULT_OK) return;
    s_tft_last_result = result;
    s_tft_draw_blocked = 1U;
}

void Tft_Driver_Begin_Draw_Cycle(void)
{
    s_tft_last_result = TFT_DRIVER_RESULT_OK;
    s_tft_draw_blocked = 0U;
}

Tft_Driver_Result Tft_Driver_Get_Last_Result(void)
{
    return s_tft_last_result;
}

uint8_t Tft_Driver_Is_Draw_Blocked(void)
{
    return s_tft_draw_blocked;
}

/* ==============================================================
 *  8位基础通信
 * ============================================================== */

static void Tft_Driver_WrCmd(uint8_t c)
{
    Spi1_Shared_Result result;

    if (s_tft_draw_blocked) return;
    result = Spi1_Shared_Acquire(SPI1_SHARED_MODE_TFT_8,
                                 TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        return;
    }
    if (Spi1_Shared_Set_Tft_DC(0U) != SPI1_SHARED_RESULT_OK ||
        Spi1_Shared_Select_Tft(1U) != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(
            Spi1_Shared_Get_Last_Result()));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Transfer8(c, 0, TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Release();
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
    }
}

static void Tft_Driver_WrDat(uint8_t d)
{
    Spi1_Shared_Result result;

    if (s_tft_draw_blocked) return;
    result = Spi1_Shared_Acquire(SPI1_SHARED_MODE_TFT_8,
                                 TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        return;
    }
    if (Spi1_Shared_Set_Tft_DC(1U) != SPI1_SHARED_RESULT_OK ||
        Spi1_Shared_Select_Tft(1U) != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(
            Spi1_Shared_Get_Last_Result()));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Transfer8(d, 0, TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Release();
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
    }
}

/* ==============================================================
 *  SPI模式切换：8位命令与16位DMA像素数据
 * ============================================================== */

/* ==============================================================
 *  DMA一次性初始化
 * ============================================================== */

static void Tft_Driver_DMA_Init(void)
{
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel3);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    dma.DMA_MemoryBaseAddr     = 0;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 0;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    s_dma_configured = 1;
}

/* ==============================================================
 *  DMA核心传输，使用超时保护避免总线永久锁死
 * ============================================================== */

static void Tft_Driver_DMA_Transfer(const uint16_t* buf, uint32_t count, uint8_t inc_mem)
{
    uint32_t start;
    Spi1_Shared_Result result;

    if (s_tft_draw_blocked) return;
    result = Spi1_Shared_Acquire(SPI1_SHARED_MODE_TFT_16,
                                 TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        return;
    }
    if (Spi1_Shared_Set_Tft_DC(1U) != SPI1_SHARED_RESULT_OK ||
        Spi1_Shared_Select_Tft(1U) != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(
            Spi1_Shared_Get_Last_Result()));
        Spi1_Shared_Force_Release();
        return;
    }

    DMA_ClearFlag(DMA1_FLAG_TC3);  /* 清除上次超时后仍可能保留的传输完成标志。 */
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->CMAR  = (uint32_t)buf;
    DMA1_Channel3->CNDTR = (uint16_t)count;
    if (inc_mem) DMA1_Channel3->CCR |= DMA_MemoryInc_Enable;
    else         DMA1_Channel3->CCR &= ~DMA_MemoryInc_Enable;
    DMA_Cmd(DMA1_Channel3, ENABLE);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    start = Sys_Timer_Get_Tick();
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET) {
        if ((uint32_t)(Sys_Timer_Get_Tick() - start) <
            TFT_DMA_TIMEOUT_MS) continue;
        /* 超时后立即停止DMA并释放SPI总线，避免主循环永久卡死。 */
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        Tft_Driver_Record_Error(TFT_DRIVER_RESULT_DMA_TIMEOUT);
        Spi1_Shared_Force_Release();
        return;
    }
    DMA_ClearFlag(DMA1_FLAG_TC3);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    result = Spi1_Shared_Wait_Idle(TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Select_Tft(0U);
    if (result == SPI1_SHARED_RESULT_OK) {
        result = Spi1_Shared_Release();
    }
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
    }
}

/* ==============================================================
 *  像素批量发送，在切换帧宽前确认DMA和SPI均已空闲
 * ============================================================== */

static void Tft_Driver_DMA_Fill(uint32_t pixel_count, uint16_t color)
{
    if (pixel_count == 0) return;
    if (pixel_count > TFT_DMA_MAX_PIXELS) pixel_count = TFT_DMA_MAX_PIXELS;
    if (!s_dma_configured) Tft_Driver_DMA_Init();

    Tft_Driver_DMA_Transfer(&color, pixel_count, 0);
}

static void Tft_Driver_DMA_Send(const uint16_t* buf, uint32_t pixel_count)
{
    if (pixel_count == 0) return;
    if (!s_dma_configured) Tft_Driver_DMA_Init();

    Tft_Driver_DMA_Transfer(buf, pixel_count, 1);
}

/* ==============================================================
 *  设置显示窗口
 * ============================================================== */

static uint8_t Tft_Driver_Write_Selected_Byte(uint8_t data_mode,
                                              uint8_t value)
{
    if (Spi1_Shared_Set_Tft_DC(data_mode) != SPI1_SHARED_RESULT_OK) return 0U;
    if (Spi1_Shared_Transfer8(value, 0, TFT_DMA_TIMEOUT_MS) !=
        SPI1_SHARED_RESULT_OK) return 0U;
    return 1U;
}

static void Tft_Driver_Set_Window(uint16_t xs, uint16_t ys, uint16_t xe,
                                  uint16_t ye)
{
    Spi1_Shared_Result result;

    if (s_tft_draw_blocked != 0U) return;
    xs += 1U;
    xe += 1U;
    ys += 2U;
    ye += 2U;

    /* 地址窗口的十一字节必须保持在同一次片选周期内，减少细线绘制时的总线切换。 */
    result = Spi1_Shared_Acquire(SPI1_SHARED_MODE_TFT_8,
                                 TFT_DMA_TIMEOUT_MS);
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        return;
    }
    if (Spi1_Shared_Select_Tft(1U) != SPI1_SHARED_RESULT_OK ||
        Tft_Driver_Write_Selected_Byte(0U, 0x2AU) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)(xs >> 8)) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)xs) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)(xe >> 8)) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)xe) == 0U ||
        Tft_Driver_Write_Selected_Byte(0U, 0x2BU) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)(ys >> 8)) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)ys) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)(ye >> 8)) == 0U ||
        Tft_Driver_Write_Selected_Byte(1U, (uint8_t)ye) == 0U ||
        Tft_Driver_Write_Selected_Byte(0U, 0x2CU) == 0U) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(
            Spi1_Shared_Get_Last_Result()));
        Spi1_Shared_Force_Release();
        return;
    }
    result = Spi1_Shared_Release();
    if (result != SPI1_SHARED_RESULT_OK) {
        Tft_Driver_Record_Error(Tft_Driver_Map_Spi_Result(result));
        Spi1_Shared_Force_Release();
    }
}

/* ==============================================================
 *  显示屏初始化的三级上电保护
 *
 *  第一级：先接管复用功能并关闭JTAG，释放显示相关引脚。
 *  第二级：在SPI初始化前把PB12存储器片选强制拉高。
 *  第三级：明确设置PA6的初始输出电平，避免共享引脚漂移。
 * ============================================================== */

void Tft_Driver_Init(void)
{
    GPIO_InitTypeDef  gpio;
    /* 第一级保护必须最先执行：开启复用功能时钟并关闭JTAG。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); /* 释放PB3、PB4和PA15供普通引脚使用。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 第二级保护：SPI初始化前必须把PB12片选拉高。
       如果PB12上电悬空并误选中W25Q128，PA6会发生总线冲突并导致白屏。 */
    /* PA0用于显示屏复位，其余SPI相关引脚由共享总线模块统一管理。 */
    gpio.GPIO_Pin  = TFT_DRIVER_RST_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA12作为背光开关，只支持亮灭控制。 */
    gpio.GPIO_Pin   = TFT_DRIVER_BL_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TFT_DRIVER_BL_PORT, &gpio);
    GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);  /* 初始化期间保持背光关闭。 */

    Spi1_Shared_Init();

    /* 执行显示控制器硬件复位。 */
    GPIO_ResetBits(GPIOA, TFT_DRIVER_RST_PIN);
    Sys_Timer_Delay_Ms(20U);
    GPIO_SetBits(GPIOA, TFT_DRIVER_RST_PIN);
    Sys_Timer_Delay_Ms(120U);

    /* ----------------------------------------------------------
     *  已通过实机验证的ST7735初始化寄存器序列
     * ---------------------------------------------------------- */

    Tft_Driver_WrCmd(0x11); Sys_Timer_Delay_Ms(120U);      /* 退出休眠模式 */

    Tft_Driver_WrCmd(0x3A); Tft_Driver_WrDat(0x05);       /* 设置RGB565像素格式 */

    Tft_Driver_WrCmd(0xB1);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrCmd(0xB2);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrCmd(0xB3);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);
    Tft_Driver_WrDat(0x05); Tft_Driver_WrDat(0x3C); Tft_Driver_WrDat(0x3C);

    Tft_Driver_WrCmd(0xB4); Tft_Driver_WrDat(0x03);       /* 设置显示反转控制 */

    Tft_Driver_WrCmd(0xC0);
    Tft_Driver_WrDat(0x28); Tft_Driver_WrDat(0x08); Tft_Driver_WrDat(0x04);
    Tft_Driver_WrCmd(0xC1); Tft_Driver_WrDat(0xC0);
    Tft_Driver_WrCmd(0xC2); Tft_Driver_WrDat(0x0D); Tft_Driver_WrDat(0x00);
    Tft_Driver_WrCmd(0xC3); Tft_Driver_WrDat(0x8D); Tft_Driver_WrDat(0x2A);
    Tft_Driver_WrCmd(0xC4); Tft_Driver_WrDat(0x8D); Tft_Driver_WrDat(0xEE);
    Tft_Driver_WrCmd(0xC5); Tft_Driver_WrDat(0x1A);

    Tft_Driver_WrCmd(0x36); Tft_Driver_WrDat(0xA0);        /* 设置横屏扫描方向 */  /* 正极性伽马参数 */
    Tft_Driver_WrCmd(0xE0);
    Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x22);Tft_Driver_WrDat(0x07);Tft_Driver_WrDat(0x0A);
    Tft_Driver_WrDat(0x2E);Tft_Driver_WrDat(0x30);Tft_Driver_WrDat(0x25);Tft_Driver_WrDat(0x2A);
    Tft_Driver_WrDat(0x28);Tft_Driver_WrDat(0x26);Tft_Driver_WrDat(0x2E);Tft_Driver_WrDat(0x3A);
    Tft_Driver_WrDat(0x00);Tft_Driver_WrDat(0x01);Tft_Driver_WrDat(0x03);Tft_Driver_WrDat(0x13);   /* 负极性伽马参数 */
    Tft_Driver_WrCmd(0xE1);
    Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x16);Tft_Driver_WrDat(0x06);Tft_Driver_WrDat(0x0D);
    Tft_Driver_WrDat(0x2D);Tft_Driver_WrDat(0x26);Tft_Driver_WrDat(0x23);Tft_Driver_WrDat(0x27);
    Tft_Driver_WrDat(0x27);Tft_Driver_WrDat(0x25);Tft_Driver_WrDat(0x2D);Tft_Driver_WrDat(0x3B);
    Tft_Driver_WrDat(0x00);Tft_Driver_WrDat(0x01);Tft_Driver_WrDat(0x04);Tft_Driver_WrDat(0x13);

    Tft_Driver_WrCmd(0x13);                                 /* 开启正常显示模式 */

    Tft_Driver_WrCmd(0x3A); Tft_Driver_WrDat(0x05);        /* 再次确认RGB565像素格式 */

    Tft_Driver_WrCmd(0x29); Sys_Timer_Delay_Ms(50U);        /* 开启显示输出 */

    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255U);
}

/* ===============================================================
 *  字库初始化，必须在W25Q128驱动初始化之后调用
 *  读取外部字库头并完成CRC32校验；通过后启用20897字完整字库，
 *  校验失败时保持外部字库无效，并回退到片内四字启动字库。
 * ============================================================= */

static uint8_t Tft_Driver_Is_Font_Header_Valid(const Font_Header *header)
{
    uint32_t ascii_size;
    uint32_t cjk_index_size;
    uint32_t cjk_data_size;

    if (header == 0 || header->magic != TFT_FONT_MAGIC ||
        (header->version != TFT_FONT_VERSION_LEGACY &&
         header->version != TFT_FONT_VERSION_PAYLOAD_CRC) ||
        header->total_size < sizeof(Font_Header) ||
        header->total_size > TFT_FONT_MAX_SIZE ||
        header->ascii_offset != sizeof(Font_Header) ||
        header->ascii_count != 95U || header->ascii_bytes != 16U ||
        header->cjk_index_count == 0U || header->cjk_data_bytes != 32U) {
        return 0U;
    }

    ascii_size = (uint32_t)header->ascii_count * header->ascii_bytes;
    if (header->ascii_offset > header->total_size ||
        ascii_size > header->total_size - header->ascii_offset ||
        ascii_size > TFT_FONT_ICON_TABLE_ADDR - header->ascii_offset) {
        return 0U;
    }

    cjk_index_size = (uint32_t)header->cjk_index_count * 6U;
    if (header->cjk_index_offset > header->total_size ||
        header->cjk_index_offset <= TFT_FONT_ICON_TABLE_ADDR ||
        cjk_index_size > header->total_size - header->cjk_index_offset) {
        return 0U;
    }

    cjk_data_size = (uint32_t)header->cjk_index_count *
                    header->cjk_data_bytes;
    if (header->cjk_data_offset <
            header->cjk_index_offset + cjk_index_size ||
        header->cjk_data_offset > header->total_size ||
        cjk_data_size > header->total_size - header->cjk_data_offset) {
        return 0U;
    }
    return 1U;
}

static uint8_t Tft_Driver_Is_Glyph_Offset_Valid(
    uint32_t glyph_offset, const Font_Header *header)
{
    uint32_t data_size;

    if (header == 0 || header->cjk_data_bytes == 0U) return 0U;
    data_size = (uint32_t)header->cjk_index_count *
                header->cjk_data_bytes;
    if (data_size < header->cjk_data_bytes) return 0U;
    return (glyph_offset <= data_size - header->cjk_data_bytes) ? 1U : 0U;
}

static uint8_t Tft_Driver_Verify_Font_Payload(const Font_Header *header)
{
    uint8_t *buffer;
    uint32_t address;
    uint32_t remaining;
    uint32_t chunk;
    uint32_t state;

    if (header == 0) return 0U;
    buffer = (uint8_t *)s_dma_buf;
    address = W25Q_ADDR_FONT + 0x0CU;
    remaining = header->total_size - 0x0CU;
    state = Checksum_CRC32_Begin();
    while (remaining != 0U) {
        chunk = (remaining > TFT_FONT_CRC_CHUNK_SIZE) ?
                TFT_FONT_CRC_CHUNK_SIZE : remaining;
        if (W25Q_Driver_Read(address, buffer, chunk) !=
            W25Q_DRIVER_RESULT_OK) return 0U;
        state = Checksum_CRC32_Update(state, buffer, chunk);
        address += chunk;
        remaining -= chunk;
    }
    return (Checksum_CRC32_Finish(state) == header->crc32) ? 1U : 0U;
}

static uint8_t Tft_Driver_Init_Icon_Table(const Font_Header *header)
{
    uint8_t icon_header[TFT_FONT_ICON_HEADER_SIZE];
    uint32_t table_size;
    uint32_t data_size;
    uint16_t icon_count;

    s_icon_count = 0U;
    s_icon_data_addr = 0U;
    s_icon_data_size = 0U;
    if (header == 0 || TFT_FONT_ICON_TABLE_ADDR >= header->total_size ||
        header->cjk_index_offset <= TFT_FONT_ICON_TABLE_ADDR) return 0U;
    if (W25Q_Driver_Read(TFT_FONT_ICON_TABLE_ADDR, icon_header,
                         sizeof(icon_header)) != W25Q_DRIVER_RESULT_OK) {
        return 0U;
    }

    icon_count = (uint16_t)icon_header[0] |
                 ((uint16_t)icon_header[1] << 8);
    data_size = (uint32_t)icon_header[8] |
                ((uint32_t)icon_header[9] << 8) |
                ((uint32_t)icon_header[10] << 16) |
                ((uint32_t)icon_header[11] << 24);
    if (icon_count == 0U || icon_count > 255U) return 0U;
    table_size = TFT_FONT_ICON_HEADER_SIZE +
                 (uint32_t)icon_count * TFT_FONT_ICON_ENTRY_SIZE;
    if (table_size > header->cjk_index_offset - TFT_FONT_ICON_TABLE_ADDR) {
        return 0U;
    }
    s_icon_data_addr = TFT_FONT_ICON_TABLE_ADDR + table_size;
    if (data_size > header->cjk_index_offset - s_icon_data_addr) {
        s_icon_data_addr = 0U;
        return 0U;
    }
    s_icon_count = icon_count;
    s_icon_data_size = data_size;
    return 1U;
}

void Tft_Driver_Font_Init(void)
{
    uint32_t crc_computed;

    s_font_flash_valid = 0U;
    s_icon_count = 0U;
    if (W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)&g_font_header,
                         sizeof(Font_Header)) != W25Q_DRIVER_RESULT_OK) return;
    if (Tft_Driver_Is_Font_Header_Valid(&g_font_header) == 0U) return;

    if (g_font_header.version == TFT_FONT_VERSION_LEGACY) {
        crc_computed = Checksum_CRC32((uint8_t*)&g_font_header + 0x0CU,
                                      20U);
        if (g_font_header.crc32 != crc_computed) return;
    } else if (Tft_Driver_Verify_Font_Payload(&g_font_header) == 0U) {
        return;
    }
    s_font_flash_valid = 1U;
    (void)Tft_Driver_Init_Icon_Table(&g_font_header);
}

static uint32_t Tft_Driver_Font_Index_Binary_Search(uint16_t unicode,
                                                    const Font_Header *hdr)
{
    uint32_t lo;
    uint32_t hi;
    uint32_t mid;
    uint32_t addr;
    uint8_t entry[6];
    uint16_t mid_unicode;

    if (hdr == 0 || hdr->cjk_index_count == 0U) return 0xFFFFFFFFUL;
    lo = 0U;
    hi = hdr->cjk_index_count;
    while (lo < hi) {
        mid = lo + ((hi - lo) >> 1);
        addr = hdr->cjk_index_offset + mid * 6U;
        if (W25Q_Driver_Read(addr, entry, sizeof(entry)) !=
            W25Q_DRIVER_RESULT_OK) return 0xFFFFFFFFUL;
        mid_unicode = (uint16_t)entry[0] | ((uint16_t)entry[1] << 8);
        if (mid_unicode < unicode) lo = mid + 1U;
        else hi = mid;
    }

    if (lo >= hdr->cjk_index_count) return 0xFFFFFFFFUL;
    addr = hdr->cjk_index_offset + lo * 6U;
    if (W25Q_Driver_Read(addr, entry, sizeof(entry)) !=
        W25Q_DRIVER_RESULT_OK) return 0xFFFFFFFFUL;
    mid_unicode = (uint16_t)entry[0] | ((uint16_t)entry[1] << 8);
    if (mid_unicode != unicode) return 0xFFFFFFFFUL;

    return (uint32_t)entry[2] |
           ((uint32_t)entry[3] << 8) |
           ((uint32_t)entry[4] << 16) |
           ((uint32_t)entry[5] << 24);
}

/* 公开字库状态查询，供启动页面显示加载结果。 */
uint8_t Tft_Driver_Is_Font_Flash_Valid(void)
{
    return s_font_flash_valid;
}

/* ==============================================================
 *  字符间距配置
 * ============================================================== */

void Tft_Driver_Set_Letter_Spacing(uint8_t sp)
{
    if (sp > 6) sp = 6;
    s_letter_spacing = sp;
}

uint8_t Tft_Driver_Get_Letter_Spacing(void)
{
    return s_letter_spacing;
}

/* ==============================================================
 *  基础绘图
 * ============================================================== */

void Tft_Driver_Clear(uint16_t color)
{
    Tft_Driver_Set_Window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    Tft_Driver_DMA_Fill((uint32_t)TFT_WIDTH * TFT_HEIGHT, color);
}

void Tft_Driver_Set_Backlight(uint8_t v)
{
    if (v > 0)
        GPIO_SetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
    else
        GPIO_ResetBits(TFT_DRIVER_BL_PORT, TFT_DRIVER_BL_PIN);
}

void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t total;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH  - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    total = (uint32_t)w * h;
    if (total == 0) return;
    Tft_Driver_Set_Window(x, y, x + w - 1, y + h - 1);
    Tft_Driver_DMA_Fill(total, color);
}

void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    Tft_Driver_Fill_Rect(x, y, w, h, TFT_COLOR_BLACK);
}

/* ==============================================================
 *  位图解码器，字节内最低位对应最先绘制的像素
 * ============================================================== */

static void Tft_Driver_Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg,
                           uint16_t* out, uint8_t scale)
{
    uint8_t b;
    if (scale == 1) {
        for (b = 0; b < 8; b++)
            out[b] = (byte_val & (0x01 << b)) ? fg : bg;
    } else {
        /* 两倍横向放大时，每个源像素复制为两个相邻输出像素。 */
        for (b = 0; b < 8; b++) {
            uint16_t c = (byte_val & (0x01 << b)) ? fg : bg;
            out[b * 2]     = c;
            out[b * 2 + 1] = c;
        }
    }
}

static void Tft_Driver_Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg,
                          uint16_t* out, uint8_t scale)
{
    uint8_t b;
    if (scale == 1) {
        for (b = 0; b < 8; b++) {
            out[b]     = (lo & (0x01 << b)) ? fg : bg;
            out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
        }
    } else {
        /* 两倍横向放大时，16像素源行扩展为32像素输出行。 */
        for (b = 0; b < 8; b++) {
            uint16_t cl = (lo & (0x01 << b)) ? fg : bg;
            uint16_t ch = (hi & (0x01 << b)) ? fg : bg;
            out[b * 2]     = cl;
            out[b * 2 + 1] = cl;
            out[b * 2 + 16] = ch;
            out[b * 2 + 17] = ch;
        }
    }
}

/* ==============================================================
 *  单字节字符渲染：外部字库流式读取或片内字模回退
 * ============================================================== */

void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,
                          uint16_t fg, uint16_t bg)
{
    uint8_t idx; uint16_t* p;
    uint8_t char_w = TFT_FONT_WIDTH  * s_font_scale;  /* 字符宽度为8或16像素 */
    uint8_t char_h = TFT_FONT_HEIGHT * s_font_scale;  /* 字符高度为16或32像素 */
    uint8_t row;
    uint16_t line_h_scaled = (uint16_t)TFT_FONT_HEIGHT * s_font_scale;

    if (line * TFT_FONT_HEIGHT + char_h > TFT_HEIGHT) return;
    if (col * TFT_FONT_WIDTH  + char_w > TFT_WIDTH) return;
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) ch = ' ';

    idx = (uint8_t)(ch - 32);

    if (s_font_flash_valid) {
        uint32_t base;
        uint8_t ascii_rows[16];
        Tft_Driver_Set_Window(col * TFT_FONT_WIDTH, line * line_h_scaled,
               col * TFT_FONT_WIDTH + char_w - 1,
               line * line_h_scaled + char_h - 1);
        base = g_font_header.ascii_offset + (uint32_t)idx * 16;
        if (W25Q_Driver_Read(base, ascii_rows, 16U) ==
            W25Q_DRIVER_RESULT_OK) {
            p = s_dma_buf;
            if (s_font_scale == 1) {
                for (row = 0; row < 16; row++) {
                    Tft_Driver_Decode_Char_Row(ascii_rows[row], fg, bg, p + row * 8, 1);
                }
                Tft_Driver_DMA_Send(s_dma_buf, 128);
            } else {
                /* 两倍放大时复制当前像素行。 */
                for (row = 0; row < 16; row++) {
                    uint8_t r;
                    Tft_Driver_Decode_Char_Row(ascii_rows[row], fg, bg, p, 2);
                    /* 将放大后的像素行复制到下一行。 */
                    for (r = 0; r < 16; r++) p[32 + r] = p[r];
                    p += 32;
                }
                Tft_Driver_DMA_Send(s_dma_buf, 16 * 32);
            }
            return;
        }
        s_font_flash_valid = 0U;
    }

    {
        uint16_t x0, y0;
        x0 = col * TFT_FONT_WIDTH;
        y0 = line * line_h_scaled;
        Tft_Driver_Set_Window(x0, y0, x0 + char_w - 1, y0 + char_h - 1);
        p = s_dma_buf;
        if (s_font_scale == 1) {
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_Char_Row(TFT_FONT_8X16[idx][row], fg, bg, p + row * 8, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 128);
        } else {
            for (row = 0; row < 16; row++) {
                uint8_t r;
                Tft_Driver_Decode_Char_Row(TFT_FONT_8X16[idx][row], fg, bg, p, 2);
                for (r = 0; r < 16; r++) p[32 + r] = p[r];
                p += 32;
            }
            Tft_Driver_DMA_Send(s_dma_buf, 16 * 32);
        }
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

/* ==============================================================
 *  中文渲染：外部字库二分查找或片内四字线性回退
 * ============================================================== */

static uint8_t Tft_Driver_Is_UTF8_CN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

static void Tft_Driver_CN_Draw(uint8_t ln, uint8_t col, const uint8_t *utf8,
                               uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;
    uint8_t char_w = (uint16_t)TFT_FONT_WIDTH * 2 * s_font_scale;  /* 字符宽度为16或32像素 */
    uint8_t char_h = (uint16_t)TFT_FONT_HEIGHT * s_font_scale;     /* 字符高度为16或32像素 */
    uint16_t buf_x_start = col * TFT_FONT_WIDTH;
    uint16_t buf_y_start = ln  * ((uint16_t)TFT_FONT_HEIGHT * s_font_scale);

    if (buf_y_start + char_h > TFT_HEIGHT) return;
    if (buf_x_start + char_w > TFT_WIDTH) return;

    Tft_Driver_Set_Window(buf_x_start, buf_y_start, buf_x_start + char_w - 1, buf_y_start + char_h - 1);
    p = s_dma_buf;

    /* 优先检查片内“无、线、充、电”四字启动字库。 */
    {
        uint8_t g_idx; uint8_t i;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx != 0xFF) {
            if (s_font_scale == 1) {
                for (row = 0; row < 16; row++)
                    Tft_Driver_Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                                   fg, bg, p + row * 16, 1);
                Tft_Driver_DMA_Send(s_dma_buf, 256);
            } else {
                for (row = 0; row < 16; row++) {
                    uint8_t r;
                    Tft_Driver_Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                                   fg, bg, p, 2);
                    for (r = 0; r < 32; r++) p[64 + r] = p[r];
                    p += 64;
                }
                Tft_Driver_DMA_Send(s_dma_buf, 16 * 64);
            }
            return;
        }
    }

    /* 片内字库未命中时，再查询W25Q128中的20897字完整字库。 */
    if (s_font_flash_valid) {
        uint32_t unicode; uint32_t data_offset; uint32_t base;
        uint8_t cn_glyph[32];
        unicode  = ((uint32_t)(utf8[0] & 0x0F) << 12);
        unicode |= ((uint32_t)(utf8[1] & 0x3F) << 6);
        unicode |= ((uint32_t)(utf8[2] & 0x3F));
        data_offset = Tft_Driver_Font_Index_Binary_Search((uint16_t)unicode,
                                                          &g_font_header);
        if (data_offset == 0xFFFFFFFFUL ||
            Tft_Driver_Is_Glyph_Offset_Valid(data_offset,
                                             &g_font_header) == 0U) {
            Tft_Driver_DMA_Fill((uint32_t)char_w * char_h, bg);
            return;
        }
        base = g_font_header.cjk_data_offset + (uint32_t)data_offset;
        if (W25Q_Driver_Read(base, cn_glyph, 32U) !=
            W25Q_DRIVER_RESULT_OK) {
            s_font_flash_valid = 0U;
            Tft_Driver_DMA_Fill((uint32_t)char_w * char_h, bg);
            return;
        }
        if (s_font_scale == 1) {
            for (row = 0; row < 16; row++) {
                Tft_Driver_Decode_CN_Row(cn_glyph[row * 2], cn_glyph[row * 2 + 1], fg, bg, p + row * 16, 1);
            }
            Tft_Driver_DMA_Send(s_dma_buf, 256);
        } else {
            for (row = 0; row < 16; row++) {
                uint8_t r;
                Tft_Driver_Decode_CN_Row(cn_glyph[row * 2], cn_glyph[row * 2 + 1], fg, bg, p, 2);
                for (r = 0; r < 32; r++) p[64 + r] = p[r];
                p += 64;
            }
            Tft_Driver_DMA_Send(s_dma_buf, 16 * 64);
        }
        return;
    }

    /* 两条字库路径均未命中时绘制空白字符。 */
    Tft_Driver_DMA_Fill((uint32_t)char_w * char_h, bg);
}

void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,
                                uint16_t fg, uint16_t bg)
{
    uint8_t as_char_w = TFT_FONT_WIDTH  * s_font_scale;  /* 单字节字符宽度为8或16像素 */
    uint8_t cn_char_w = TFT_FONT_WIDTH  * 2 * s_font_scale; /* 中文字符宽度为16或32像素 */
    uint8_t max_x = TFT_WIDTH - as_char_w;

    /* 字符间距按像素计算；每绘制一个字符后，横坐标增加字符宽度与设定间距，
     * 并用背景色填满间隔区域，防止保留旧画面。 */
    {
        uint16_t cur_x = (uint16_t)col * TFT_FONT_WIDTH;
        while (*s && cur_x <= max_x) {
            if (Tft_Driver_Is_UTF8_CN((uint8_t)*s) && *(s+1) && *(s+2)) {
                if (cur_x + cn_char_w > TFT_WIDTH) break;
                /* 先用背景色清除间隔区域，再绘制当前字形。 */
                if (s_letter_spacing > 0)
                    Tft_Driver_Fill_Rect(cur_x, (uint16_t)ln * (TFT_FONT_HEIGHT * s_font_scale),
                        s_letter_spacing, (uint16_t)TFT_FONT_HEIGHT * s_font_scale, bg);
                Tft_Driver_CN_Draw(ln, (uint8_t)(cur_x / TFT_FONT_WIDTH), (const uint8_t*)s, fg, bg);
                cur_x += cn_char_w + s_letter_spacing;
                s += 3;
            } else {
                if (cur_x + as_char_w > TFT_WIDTH) break;
                if (s_letter_spacing > 0)
                    Tft_Driver_Fill_Rect(cur_x, (uint16_t)ln * (TFT_FONT_HEIGHT * s_font_scale),
                        s_letter_spacing, (uint16_t)TFT_FONT_HEIGHT * s_font_scale, bg);
                Tft_Driver_Show_Char(ln, (uint8_t)(cur_x / TFT_FONT_WIDTH), *s, fg, bg);
                cur_x += as_char_w + s_letter_spacing;
                s++;
            }
        }
    }
}

/* ==============================================================
 *  无线状态图标，从片内只读存储区读取
 * ============================================================== */

void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;
    if (frame > 3) frame = 3;

    Tft_Driver_Set_Window(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Tft_Driver_Decode_CN_Row(WIFI_ICON[frame][row * 2], WIFI_ICON[frame][row * 2 + 1],
                       fg, bg, p + row * 16, 1);

    Tft_Driver_DMA_Send(s_dma_buf, 256);
}

void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],
                                  uint16_t fg, uint16_t bg)
{
    uint8_t row; uint16_t* p;

    Tft_Driver_Set_Window(x, y, x + 15, y + 15);

    p = s_dma_buf;
    for (row = 0; row < 16; row++)
        Tft_Driver_Decode_CN_Row(data[row * 2], data[row * 2 + 1], fg, bg, p + row * 16, 1);

    Tft_Driver_DMA_Send(s_dma_buf, 256);
}

/* ==============================================================
 *  5乘10微型数字，保留在片内以避免外部存储器等待
 * ============================================================== */

static const uint8_t FONT_5X10[12][10] = {
    {0x00,0x00,0x06,0x09,0x09,0x09,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x04,0x06,0x04,0x04,0x04,0x04,0x0E,0x00},
    {0x00,0x00,0x0E,0x0A,0x08,0x04,0x04,0x02,0x0E,0x00},
    {0x00,0x00,0x0E,0x0A,0x08,0x04,0x08,0x0A,0x0E,0x00},
    {0x00,0x00,0x08,0x0C,0x0A,0x09,0x1F,0x08,0x1C,0x00},
    {0x00,0x00,0x0E,0x02,0x0E,0x08,0x08,0x0A,0x06,0x00},
    {0x00,0x00,0x0E,0x01,0x07,0x09,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x0E,0x08,0x08,0x04,0x04,0x04,0x04,0x00},
    {0x00,0x00,0x06,0x09,0x09,0x06,0x09,0x09,0x06,0x00},
    {0x00,0x00,0x06,0x09,0x09,0x09,0x0F,0x0C,0x07,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static uint8_t Tft_Driver_Map_5x10_Index(char ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch == '.') return 10;
    return 11;
}

void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,
                                        const char* s,
                                        uint16_t fg, uint16_t bg)
{
    uint8_t row, b, idx; uint16_t* p;

    while (*s && x + 5 <= TFT_WIDTH) {
        idx = Tft_Driver_Map_5x10_Index(*s);
        Tft_Driver_Set_Window(x, y, x + 4, y + 9);

        p = s_dma_buf;
        for (row = 0; row < 10; row++) {
            uint8_t byte_val = FONT_5X10[idx][row];
            for (b = 0; b < 5; b++)
                *p++ = (byte_val & (0x01 << b)) ? fg : bg;
        }

        Tft_Driver_DMA_Send(s_dma_buf, 50);
        x += 7; s++;
    }
}

/* 2倍字符保持独立坐标与独立缩放，不改变页面文字使用的全局字号状态。 */
void Tft_Driver_Show_Char_2X(uint16_t x, uint16_t y, char ch,
                              uint16_t fg, uint16_t bg)
{
    uint8_t idx;
    uint8_t row;
    uint16_t* p;
    uint8_t ascii_rows[16];
    const uint8_t* glyph;

    if (Tft_Driver_Is_Draw_Blocked()) return;
    if (x > (uint16_t)(TFT_WIDTH - 16U) || y > (uint16_t)(TFT_HEIGHT - 32U)) return;
    if ((uint8_t)ch < 32U || (uint8_t)ch > 126U) ch = ' ';
    idx = (uint8_t)(ch - 32);
    glyph = TFT_FONT_8X16[idx];

    if (s_font_flash_valid) {
        uint32_t base = g_font_header.ascii_offset + (uint32_t)idx * 16U;
        if (W25Q_Driver_Read(base, ascii_rows, 16U) == W25Q_DRIVER_RESULT_OK)
            glyph = ascii_rows;
        else
            s_font_flash_valid = 0U;
    }

    Tft_Driver_Set_Window(x, y, (uint16_t)(x + 15U), (uint16_t)(y + 31U));
    p = s_dma_buf;
    for (row = 0U; row < 16U; row++) {
        uint8_t column;
        Tft_Driver_Decode_Char_Row(glyph[row], fg, bg, p, 2U);
        for (column = 0U; column < 16U; column++) p[16U + column] = p[column];
        p += 32U;
    }
    Tft_Driver_DMA_Send(s_dma_buf, 512U);
}

void Tft_Driver_Show_String_2X(uint16_t x, uint16_t y, const char* str,
                                uint16_t fg, uint16_t bg)
{
    if (str == NULL) return;
    if (Tft_Driver_Is_Draw_Blocked()) return;
    while (*str) {
        if (x > (uint16_t)(TFT_WIDTH - 16U) || y > (uint16_t)(TFT_HEIGHT - 32U)) return;
        Tft_Driver_Show_Char_2X(x, y, *str, fg, bg);
        if (Tft_Driver_Is_Draw_Blocked()) return;
        x = (uint16_t)(x + 16U);
        str++;
    }
}

/* ==============================================================
 *  按编号读取并绘制16乘16图标
 * ============================================================== */

uint8_t Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y, uint8_t icon_id,
                                   uint8_t frame, uint16_t fg, uint16_t bg)
{
    /* 图标数据主要存放在W25Q128字库图标表中。
     * 外部字库不可用时，编号0至8的无线、消息和星形图标可使用片内回退；
     * 其余图标必须从有效的外部字库读取。 */
    uint8_t row; uint16_t* p;

    if (icon_id > 8U &&
        (s_font_flash_valid == 0U || icon_id >= s_icon_count)) return 0U;

    Tft_Driver_Set_Window(x, y, x + 15, y + 15);
    p = s_dma_buf;

    /* 先检查片内保留的无线、消息和星形图标。 */
    switch (icon_id) {
        case 0:  /* 根据信号强度帧编号选择无线图标。 */
            if (frame > 3) frame = 3;
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(WIFI_ICON[frame][row*2], WIFI_ICON[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256);
            return 1;
        case 1:  /* 根据帧编号选择无线连接动画。 */
            if (frame > 5) frame = 5;
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(WIFI_CONNECT_ANIM[frame][row*2], WIFI_CONNECT_ANIM[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256);
            return 1;
        case 2:  /* 带叉号的无线关闭静态图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(WIFI_OFF_ICON[row*2], WIFI_OFF_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256);
            return 1;
        case 3:  /* 带减号的无线移除静态图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(WIFI_REMOVE_ICON[row*2], WIFI_REMOVE_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256);
            return 1;
        case 4:  /* 消息协议静态图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(MQTT_ICON[row*2], MQTT_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256); return 1;
        case 5:  /* 消息协议已连接静态图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(MQTT_YES_ICON[row*2], MQTT_YES_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256); return 1;
        case 6:  /* 消息协议未连接静态图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(MQTT_NO_ICON[row*2], MQTT_NO_ICON[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256); return 1;
        case 7:  /* 六帧消息协议连接动画。 */
            if (frame > 5) frame = 5;
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(MQTT_ANIM[frame][row*2], MQTT_ANIM[frame][row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256); return 1;
        case 8:  /* 星形图标。 */
            for (row = 0; row < 16; row++)
                Tft_Driver_Decode_CN_Row(ICON_STAR[row*2], ICON_STAR[row*2+1], fg, bg, p + row*16, 1);
            Tft_Driver_DMA_Send(s_dma_buf, 256);
            return 1;
        default: break;  /* 编号9及以上的图标仅存在于外部字库。 */
    }

    /* 读取仅存在于外部字库中的图标。 */
    if (s_font_flash_valid != 0U && icon_id < s_icon_count) {
        uint32_t data_offset;
        uint32_t frame_offset;
        uint32_t frame_bytes;
        uint16_t entry_id;
        uint16_t n_frames;
        uint8_t icon_entry[TFT_FONT_ICON_ENTRY_SIZE];
        uint8_t icon_glyph[TFT_FONT_ICON_GLYPH_SIZE];

        /* 读取包含编号、帧数、数据偏移和保留字段的8字节索引项。 */
        if (W25Q_Driver_Read(TFT_FONT_ICON_TABLE_ADDR +
                             TFT_FONT_ICON_HEADER_SIZE +
                             (uint32_t)icon_id * TFT_FONT_ICON_ENTRY_SIZE,
                             icon_entry, sizeof(icon_entry)) !=
            W25Q_DRIVER_RESULT_OK) {
            Tft_Driver_DMA_Fill(256U, bg);
            return 0U;
        }
        entry_id = (uint16_t)icon_entry[0] |
                   ((uint16_t)icon_entry[1] << 8);
        n_frames = (uint16_t)icon_entry[2] |
                   ((uint16_t)icon_entry[3] << 8);
        data_offset = (uint32_t)icon_entry[4] |
                      ((uint32_t)icon_entry[5] << 8);
        frame_bytes = (uint32_t)n_frames * TFT_FONT_ICON_GLYPH_SIZE;
        if (entry_id != icon_id || n_frames == 0U ||
            data_offset > s_icon_data_size ||
            frame_bytes > s_icon_data_size - data_offset) {
            Tft_Driver_DMA_Fill(256U, bg);
            return 0U;
        }
        if (frame >= n_frames) frame = (uint8_t)(n_frames - 1U);
        frame_offset = s_icon_data_addr + data_offset +
                       (uint32_t)frame * TFT_FONT_ICON_GLYPH_SIZE;
        if (W25Q_Driver_Read(frame_offset, icon_glyph, sizeof(icon_glyph)) !=
            W25Q_DRIVER_RESULT_OK) {
            s_font_flash_valid = 0U;
            Tft_Driver_DMA_Fill(256U, bg);
            return 0U;
        }
        for (row = 0; row < 16; row++) {
            Tft_Driver_Decode_CN_Row(icon_glyph[row * 2], icon_glyph[row * 2 + 1], fg, bg, p + row * 16, 1);
        }
        Tft_Driver_DMA_Send(s_dma_buf, 256);
        return 1;
    }

    /* 片内和外部字库均无对应图标时绘制空白。 */
    { uint8_t i; for (i = 0; i < 256; i++) p[i] = bg; }
    Tft_Driver_DMA_Send(s_dma_buf, 256);
    return 0;
}

/* ===============================================================
 *  开机动画，由代码直接绘制
 *
 *  布局 (160×128):
 *  +----------------------------------+
 *  |                           V5.1.2|  第7行右侧显示暗灰版本号
 *  |                                  |
 *  |     无  线  充  电               |  中文逐字渐亮
 *  |                                  |
 *  |          W  P  T                 |  项目缩写逐字渐亮
 *  +----------------------------------+
 *
 *  动画总长约2秒：
 *    渐亮阶段为1600ms，每字使用8帧、每帧50ms；
 *    全亮后再定格400ms。
 *
 *  使用片内“无、线、充、电”四字字库，不依赖外部存储器。
 * ============================================================= */

void Tft_Driver_Show_Splash(void)
{
    uint8_t col;
    /* 四个中文字符按UTF-8编码保存。 */
    const char s_cn[4][4] = {
        "\xe6\x97\xa0", "\xe7\xba\xbf", "\xe5\x85\x85", "\xe7\x94\xb5"
    };

    Tft_Driver_Clear(TFT_COLOR_BLACK);

    /* 版本号: 右下角暗灰 */
    Tft_Driver_Show_String(7, 14, "V5.1.2", 0x3186U, TFT_COLOR_BLACK);

    /* 第一阶段让中文与项目缩写同步逐字渐亮，总计1600ms。 */
    for (col = 0; col < 4; col++) {
        uint16_t cn_fg, wpt_fg;
        uint8_t  sub;

        for (sub = 0; sub < 8; sub++) {
            /* 当前字符颜色由暗逐步过渡到目标亮色。 */
            cn_fg  = 0x31BEU + (uint16_t)((0xFFE0U - 0x31BEU) * (sub + 1) / 8);
            wpt_fg = 0x031FU + (uint16_t)((0x07FFU - 0x031FU) * (sub + 1) / 8);

            /* 之前已点亮的字保持全亮 */
            if (col > 0) {
                uint8_t prev;
                for (prev = 0; prev < col; prev++)
                    Tft_Driver_Show_CN_String(2, (uint8_t)(5 + prev * 2),
                        s_cn[prev], 0xFFE0U, TFT_COLOR_BLACK);
                for (prev = 0; prev < col && prev < 3; prev++)
                    Tft_Driver_Show_String(5, (uint8_t)(8 + prev),
                        "WPT" + prev, 0x07FFU, TFT_COLOR_BLACK);
            }

            /* 当前字: 渐亮 */
            Tft_Driver_Show_CN_String(2, (uint8_t)(5 + col * 2),
                s_cn[col], cn_fg, TFT_COLOR_BLACK);

            if (col < 3)
                Tft_Driver_Show_String(5, (uint8_t)(8 + col),
                    "WPT" + col, wpt_fg, TFT_COLOR_BLACK);

            Sys_Timer_Delay_Ms(50);
        }
    }

    /* 第二阶段保持全亮400ms。 */
    Sys_Timer_Delay_Ms(400);
}
