/**
 ******************************************************************************
 * @file    System/Checksum.h
 * @brief   CRC8 and non-reflected CRC32 checksums for V5.0.2
 * @note    CRC formats are fixed for compatibility with existing Flash data.
 ******************************************************************************
 */

#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "stm32f10x.h"

/**
 * @brief  Compute CRC8 with polynomial 0x07 and initial value 0x00.
 * @param  data Input byte buffer.
 * @param  len  Number of bytes.
 * @retval CRC8 value, or 0 when data is null and len is non-zero.
 */
uint8_t Checksum_CRC8(const uint8_t *data, uint16_t len);

/**
 * @brief  Compute non-reflected CRC32 used by the W25Q128 data format.
 * @param  data Input byte buffer.
 * @param  len  Number of bytes.
 * @retval CRC32 value, or 0 when data is null and len is non-zero.
 * @note    Polynomial 0x04C11DB7, init 0xFFFFFFFF, xorout 0xFFFFFFFF.
 */
uint32_t Checksum_CRC32(const uint8_t *data, uint32_t len);

/**
 * @brief  Verify the CRC implementation against the "123456789" vector.
 * @retval 1 when both CRC8 and CRC32 vectors pass, otherwise 0.
 */
uint8_t Checksum_Self_Test(void);

#endif /* CHECKSUM_H */
