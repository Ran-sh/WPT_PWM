/**
 ******************************************************************************
 * @file    System/Checksum.c
 * @brief   八位与三十二位循环冗余校验实现 — V5.1.2
 * @note    八位校验采用逐位计算，省去片内存储器中的256字节查找表。
 ******************************************************************************
 */

#include "Checksum.h"

#define CHECKSUM_CRC8_POLYNOMIAL   0x07U
#define CHECKSUM_CRC32_POLYNOMIAL  0x04C11DB7UL
#define CHECKSUM_CRC32_INITIAL     0xFFFFFFFFUL
#define CHECKSUM_CRC32_XOR_OUT     0xFFFFFFFFUL

uint8_t Checksum_CRC8(const uint8_t *data, uint16_t len)
{
    uint8_t crc;
    uint8_t bit_index;
    uint16_t byte_index;

    if (data == 0 && len != 0U) {
        return 0U;
    }

    crc = 0U;
    for (byte_index = 0U; byte_index < len; byte_index++) {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1) ^ CHECKSUM_CRC8_POLYNOMIAL);
            }
            else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t Checksum_CRC32(const uint8_t *data, uint32_t len)
{
    uint32_t crc;

    if (data == 0 && len != 0U) {
        return 0U;
    }

    crc = Checksum_CRC32_Begin();
    crc = Checksum_CRC32_Update(crc, data, len);
    return Checksum_CRC32_Finish(crc);
}

uint32_t Checksum_CRC32_Begin(void)
{
    return CHECKSUM_CRC32_INITIAL;
}

uint32_t Checksum_CRC32_Update(uint32_t state, const uint8_t *data,
                               uint32_t len)
{
    uint32_t crc;
    uint32_t byte_index;
    uint8_t bit_index;

    if (data == 0) return state;

    crc = state;
    for (byte_index = 0U; byte_index < len; byte_index++) {
        crc ^= (uint32_t)data[byte_index] << 24;
        for (bit_index = 0U; bit_index < 8U; bit_index++) {
            if ((crc & 0x80000000UL) != 0UL) {
                crc = (crc << 1) ^ CHECKSUM_CRC32_POLYNOMIAL;
            }
            else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint32_t Checksum_CRC32_Finish(uint32_t state)
{
    return state ^ CHECKSUM_CRC32_XOR_OUT;
}

uint8_t Checksum_Self_Test(void)
{
    static const uint8_t test_data[9] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };

    if (Checksum_CRC8(test_data, 9U) != 0xF4U) {
        return 0U;
    }
    if (Checksum_CRC32(test_data, 9U) != 0xFC891918UL) {
        return 0U;
    }
    return 1U;
}
