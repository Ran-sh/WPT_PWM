/**
 ******************************************************************************
 * @file    Hardware/Spi1_Shared.h
 * @brief   SPI1 shared-bus ownership and recovery interface - V5.0.2
 * @note    Owns PA4/PB12 chip selects, PA6 role and SPI1 8/16-bit frame mode.
 ******************************************************************************
 */

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

/** @brief Initialize SPI1 and leave both devices deselected. */
void Spi1_Shared_Init(void);

/**
 * @brief Acquire exclusive ownership of SPI1 in the requested mode.
 * @param mode TFT 8-bit, TFT 16-bit or Flash 8-bit mode.
 * @param timeout_ms Maximum wait for the previous hardware transfer to finish.
 * @retval SPI1_SHARED_RESULT_OK on success, otherwise an explicit error.
 */
Spi1_Shared_Result Spi1_Shared_Acquire(Spi1_Shared_Mode mode,
                                       uint32_t timeout_ms);

/** @brief Deselect both devices, normalize SPI1 and release ownership. */
Spi1_Shared_Result Spi1_Shared_Release(void);

/** @brief Immediately recover the bus to the safe unowned state. */
void Spi1_Shared_Force_Release(void);

/** @brief Wait until SPI1 TX is empty and the peripheral is no longer busy. */
Spi1_Shared_Result Spi1_Shared_Wait_Idle(uint32_t timeout_ms);

/**
 * @brief Exchange one byte while the bus is owned in an 8-bit mode.
 * @param tx Byte to transmit.
 * @param rx Optional received-byte destination; may be NULL.
 * @param timeout_ms Maximum wait for TXE and RXNE.
 */
Spi1_Shared_Result Spi1_Shared_Transfer8(uint8_t tx, uint8_t *rx,
                                        uint32_t timeout_ms);

/** @brief Set PA6 low for command or high for TFT data. */
Spi1_Shared_Result Spi1_Shared_Set_Tft_DC(uint8_t data_mode);

/** @brief Assert or deassert TFT chip select while TFT owns the bus. */
Spi1_Shared_Result Spi1_Shared_Select_Tft(uint8_t selected);

/** @brief Assert or deassert Flash chip select while Flash owns the bus. */
Spi1_Shared_Result Spi1_Shared_Select_Flash(uint8_t selected);

/** @brief Return the most recent shared-bus operation result. */
Spi1_Shared_Result Spi1_Shared_Get_Last_Result(void);

#endif /* SPI1_SHARED_H */
