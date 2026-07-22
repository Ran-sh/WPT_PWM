#ifndef SPI1_SHARED_H
#define SPI1_SHARED_H

#include "stm32f10x.h"

typedef enum {
    SPI1_SHARED_RESULT_OK = 0,
    SPI1_SHARED_RESULT_BUSY,
    SPI1_SHARED_RESULT_TIMEOUT,
    SPI1_SHARED_RESULT_INVALID
} Spi1_Shared_Result;

typedef enum {
    SPI1_SHARED_MODE_TFT_8 = 0,
    SPI1_SHARED_MODE_TFT_16,
    SPI1_SHARED_MODE_FLASH_8
} Spi1_Shared_Mode;

/** @brief 初始化SPI1，并保持显示屏和外部存储器均未选中 */
void Spi1_Shared_Init(void);

/**
 * @brief 按指定模式独占SPI1共享总线
 * @param mode 显示屏8位、显示屏16位或外部存储器8位模式
 * @param timeout_ms 等待前一次硬件传输结束的最长时间，单位为ms
 * @retval SPI1_SHARED_RESULT_OK 表示获取成功，其余值表示具体失败原因
 */
Spi1_Shared_Result Spi1_Shared_Acquire(Spi1_Shared_Mode mode,
                                       uint32_t timeout_ms);

/** @brief 取消两个设备的片选，恢复SPI1标准状态并释放总线 */
Spi1_Shared_Result Spi1_Shared_Release(void);

/** @brief 立即将共享总线恢复到未占用的安全状态 */
void Spi1_Shared_Force_Release(void);

/** @brief 等待SPI1发送缓冲区为空且外设不再忙碌 */
Spi1_Shared_Result Spi1_Shared_Wait_Idle(uint32_t timeout_ms);

/**
 * @brief 在8位总线模式下交换一个字节
 * @param tx 待发送字节
 * @param rx 接收字节保存地址；不需要接收值时可传入空指针
 * @param timeout_ms 等待发送与接收标志的最长时间，单位为ms
 */
Spi1_Shared_Result Spi1_Shared_Transfer8(uint8_t tx, uint8_t *rx,
                                        uint32_t timeout_ms);

/** @brief 设置PA6电平；低电平表示命令，高电平表示显示数据 */
Spi1_Shared_Result Spi1_Shared_Set_Tft_DC(uint8_t data_mode);

/** @brief 在显示屏占用总线时控制其片选信号 */
Spi1_Shared_Result Spi1_Shared_Select_Tft(uint8_t selected);

/** @brief 在外部存储器占用总线时控制其片选信号 */
Spi1_Shared_Result Spi1_Shared_Select_Flash(uint8_t selected);

/** @brief 获取最近一次共享总线操作结果 */
Spi1_Shared_Result Spi1_Shared_Get_Last_Result(void);

#endif /* SPI1共享总线接口结束 */
