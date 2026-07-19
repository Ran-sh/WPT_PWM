/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.h
 * @brief   W25Q128 bounded SPI NOR Flash interface - V5.0.2
 * @note    Uses Spi1_Shared; contains no application or font-format knowledge.
 ******************************************************************************
 */

#ifndef W25Q_DRIVER_H
#define W25Q_DRIVER_H

#include "stm32f10x.h"

#define W25Q_JEDEC_ID             0xEF4018U

#define W25Q_ADDR_FONT            0x000000U
#define W25Q_ADDR_SPLASH          0x200000U
#define SPLASH_MAGIC              0x5350U
#define W25Q_ADDR_CFG_A           0x300000U
#define W25Q_ADDR_CFG_B           0x301000U
#define W25Q_ADDR_BLACKBOX        0x310000U
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

/** @brief Raw power-on JEDEC ID, or zero when no valid device was found. */
extern uint32_t g_w25q_jedec_id;

/** @brief Probe W25Q128 up to three times. */
W25Q_Driver_Result W25Q_Driver_Init(void);

/** @brief Read a bounded byte range. */
W25Q_Driver_Result W25Q_Driver_Read(uint32_t addr, uint8_t *buf,
                                    uint32_t len);

/** @brief Program one page without crossing its 256-byte boundary. */
W25Q_Driver_Result W25Q_Driver_Write_Page(uint32_t addr,
                                          const uint8_t *buf,
                                          uint16_t len);

/** @brief Program an arbitrary bounded range by splitting it into pages. */
W25Q_Driver_Result W25Q_Driver_Write(uint32_t addr, const uint8_t *buf,
                                     uint32_t len);

/** @brief Erase the 4KB sector containing addr when erasing is permitted. */
W25Q_Driver_Result W25Q_Driver_Erase_Sector(uint32_t addr);

/** @brief Read status register 1. */
W25Q_Driver_Result W25Q_Driver_Read_SR1(uint8_t *sr1);

/** @brief Read the raw 24-bit JEDEC ID. */
W25Q_Driver_Result W25Q_Driver_Read_JEDEC_ID(uint32_t *jedec_id);

/** @brief Permit erases only when the application is in a safe state. */
void W25Q_Driver_Set_Erase_Allowed(uint8_t allowed);

/** @brief Return one when the expected W25Q128 device is available. */
uint8_t W25Q_Driver_Is_Available(void);

/** @brief Return the most recent driver result. */
W25Q_Driver_Result W25Q_Driver_Get_Last_Result(void);

#endif /* W25Q_DRIVER_H */
