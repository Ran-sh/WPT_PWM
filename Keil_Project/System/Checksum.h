#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "stm32f10x.h"

/**
 * @brief  使用多项式0x07和初始值0x00计算八位循环冗余校验
 * @param  data 输入字节缓冲区
 * @param  len 输入字节数
 * @retval 校验结果；数据指针为空且长度非零时返回0
 */
uint8_t Checksum_CRC8(const uint8_t *data, uint16_t len);

/**
 * @brief  计算W25Q128数据格式使用的非反射三十二位循环冗余校验
 * @param  data 输入字节缓冲区
 * @param  len 输入字节数
 * @retval 校验结果；数据指针为空且长度非零时返回0
 * @note    多项式为0x04C11DB7，初始值和最终异或值均为0xFFFFFFFF。
 */
uint32_t Checksum_CRC32(const uint8_t *data, uint32_t len);

/**
 * @brief  使用固定字符串“123456789”验证两种校验算法
 * @retval 两种算法均通过时返回1，否则返回0
 */
uint8_t Checksum_Self_Test(void);

#endif /* 校验算法接口结束 */
