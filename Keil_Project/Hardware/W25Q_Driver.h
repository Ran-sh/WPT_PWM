#ifndef W25Q_DRIVER_H
#define W25Q_DRIVER_H

#include "stm32f10x.h"

#define W25Q_JEDEC_ID             0xEF4018U

#define W25Q_ADDR_FONT            0x000000U
#define W25Q_ADDR_SPLASH          0x200000U
#define SPLASH_MAGIC              0x5350U
#define W25Q_ADDR_CFG_A           0x300000U
#define W25Q_ADDR_CFG_B           0x301000U
#define W25Q_ADDR_BLACKBOX_META_A 0x310000U
#define W25Q_ADDR_BLACKBOX_META_B 0x311000U
#define W25Q_ADDR_BLACKBOX_LOG    0x312000U
#define W25Q_ADDR_BLACKBOX_FAULT  0x6D0000U
#define W25Q_ADDR_BLACKBOX_END    0x710000U
#define W25Q_CHIP_SIZE            0x1000000U

#define W25Q_SECTOR_SIZE          4096U
#define W25Q_PAGE_SIZE            256U

typedef enum {
    W25Q_DRIVER_RESULT_OK = 0,
    W25Q_DRIVER_RESULT_NO_DEVICE,
    W25Q_DRIVER_RESULT_INVALID_ARGUMENT,
    W25Q_DRIVER_RESULT_OUT_OF_RANGE,
    W25Q_DRIVER_RESULT_PAGE_CROSS,
    W25Q_DRIVER_RESULT_ERASE_BLOCKED,
    W25Q_DRIVER_RESULT_SPI_TIMEOUT,
    W25Q_DRIVER_RESULT_BUSY_TIMEOUT,
    W25Q_DRIVER_RESULT_VERIFY_FAILED
} W25Q_Driver_Result;

/** @brief 上电探测得到的芯片识别码；未发现有效器件时为0 */
extern uint32_t g_w25q_jedec_id;

/** @brief 最多尝试三次探测W25Q128 */
W25Q_Driver_Result W25Q_Driver_Init(void);

/** @brief 在芯片容量范围内读取连续字节 */
W25Q_Driver_Result W25Q_Driver_Read(uint32_t addr, uint8_t *buf,
                                    uint32_t len);

/** @brief 在不跨越256字节页边界的前提下写入一页数据 */
W25Q_Driver_Result W25Q_Driver_Write_Page(uint32_t addr,
                                          const uint8_t *buf,
                                          uint16_t len);

/** @brief 将任意长度的有效地址范围拆分为多页写入 */
W25Q_Driver_Result W25Q_Driver_Write(uint32_t addr, const uint8_t *buf,
                                     uint32_t len);

/** @brief 在允许擦除时，擦除指定地址所在的4KB扇区 */
W25Q_Driver_Result W25Q_Driver_Erase_Sector(uint32_t addr);

/** @brief 读取状态寄存器一 */
W25Q_Driver_Result W25Q_Driver_Read_SR1(uint8_t *sr1);

/** @brief 读取原始24位芯片识别码 */
W25Q_Driver_Result W25Q_Driver_Read_JEDEC_ID(uint32_t *jedec_id);

/** @brief 设置擦除许可；仅允许上层在安全状态下开启 */
void W25Q_Driver_Set_Erase_Allowed(uint8_t allowed);

/** @brief 判断预期型号的W25Q128是否可用 */
uint8_t W25Q_Driver_Is_Available(void);

/** @brief 获取最近一次驱动操作结果 */
W25Q_Driver_Result W25Q_Driver_Get_Last_Result(void);

#endif /* W25Q128驱动接口结束 */
