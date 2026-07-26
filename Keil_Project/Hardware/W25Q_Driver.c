/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.c
 * @brief   W25Q128有界读写驱动实现 — V5.1.3
 * @note    发生任何总线错误时都会释放两个片选，并把PA6恢复为显示屏控制模式。
 ******************************************************************************
 */

#include "W25Q_Driver.h"
#include "Spi1_Shared.h"
#include "Sys_Timer.h"

#define W25Q_CMD_WREN               0x06U
#define W25Q_CMD_RDSR1              0x05U
#define W25Q_CMD_READ               0x03U
#define W25Q_CMD_PAGE_PROGRAM       0x02U
#define W25Q_CMD_SECTOR_ERASE       0x20U
#define W25Q_CMD_JEDEC              0x9FU

#define W25Q_BUSY_BIT               0x01U
#define W25Q_SPI_TIMEOUT_MS         5U
#define W25Q_PROGRAM_TIMEOUT_MS     10U
#define W25Q_ERASE_TIMEOUT_MS       500U

static uint8_t s_w25q_chip_ok = 0U;
static uint8_t s_w25q_erase_allowed = 0U;
static W25Q_Driver_Result s_w25q_last_result = W25Q_DRIVER_RESULT_NO_DEVICE;

uint32_t g_w25q_jedec_id = 0U;

static W25Q_Driver_Result W25Q_Driver_Set_Result(W25Q_Driver_Result result)
{
    s_w25q_last_result = result;
    return result;
}

static W25Q_Driver_Result W25Q_Driver_Check_Range(uint32_t addr,
                                                  uint32_t len)
{
    if (len == 0U) {
        return W25Q_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    if (addr >= W25Q_CHIP_SIZE || len > (W25Q_CHIP_SIZE - addr)) {
        return W25Q_DRIVER_RESULT_OUT_OF_RANGE;
    }
    return W25Q_DRIVER_RESULT_OK;
}

static W25Q_Driver_Result W25Q_Driver_Begin_Transaction(void)
{
    if (Spi1_Shared_Acquire(SPI1_SHARED_MODE_FLASH_8,
                            W25Q_SPI_TIMEOUT_MS) != SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        return W25Q_DRIVER_RESULT_SPI_TIMEOUT;
    }
    if (Spi1_Shared_Select_Flash(1U) != SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        return W25Q_DRIVER_RESULT_SPI_TIMEOUT;
    }
    return W25Q_DRIVER_RESULT_OK;
}

static W25Q_Driver_Result W25Q_Driver_End_Transaction(void)
{
    if (Spi1_Shared_Release() != SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        return W25Q_DRIVER_RESULT_SPI_TIMEOUT;
    }
    return W25Q_DRIVER_RESULT_OK;
}

static W25Q_Driver_Result W25Q_Driver_Transfer(uint8_t tx, uint8_t *rx)
{
    if (Spi1_Shared_Transfer8(tx, rx, W25Q_SPI_TIMEOUT_MS) !=
        SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        return W25Q_DRIVER_RESULT_SPI_TIMEOUT;
    }
    return W25Q_DRIVER_RESULT_OK;
}

static W25Q_Driver_Result W25Q_Driver_Send_Address(uint32_t addr)
{
    W25Q_Driver_Result result;

    result = W25Q_Driver_Transfer((uint8_t)(addr >> 16), 0);
    if (result != W25Q_DRIVER_RESULT_OK) return result;
    result = W25Q_Driver_Transfer((uint8_t)(addr >> 8), 0);
    if (result != W25Q_DRIVER_RESULT_OK) return result;
    return W25Q_Driver_Transfer((uint8_t)addr, 0);
}

static W25Q_Driver_Result W25Q_Driver_Write_Enable(void)
{
    W25Q_Driver_Result result;

    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return result;
    result = W25Q_Driver_Transfer(W25Q_CMD_WREN, 0);
    if (result != W25Q_DRIVER_RESULT_OK) return result;
    return W25Q_Driver_End_Transaction();
}

static W25Q_Driver_Result W25Q_Driver_Wait_Busy(uint32_t timeout_ms)
{
    uint32_t start;
    uint8_t sr1;
    W25Q_Driver_Result result;

    start = Sys_Timer_Get_Tick();
    do {
        result = W25Q_Driver_Read_SR1(&sr1);
        if (result != W25Q_DRIVER_RESULT_OK) return result;
        if ((sr1 & W25Q_BUSY_BIT) == 0U) return W25Q_DRIVER_RESULT_OK;
    } while ((uint32_t)(Sys_Timer_Get_Tick() - start) < timeout_ms);

    Spi1_Shared_Force_Release();
    return W25Q_DRIVER_RESULT_BUSY_TIMEOUT;
}

W25Q_Driver_Result W25Q_Driver_Init(void)
{
    uint8_t retry;
    uint32_t id;
    W25Q_Driver_Result result;

    s_w25q_chip_ok = 0U;
    s_w25q_erase_allowed = 0U;
    g_w25q_jedec_id = 0U;

    for (retry = 0U; retry < 3U; retry++) {
        if (retry != 0U) Sys_Timer_Delay_Ms(1U);
        result = W25Q_Driver_Read_JEDEC_ID(&id);
        if (result == W25Q_DRIVER_RESULT_OK) {
            g_w25q_jedec_id = id;
            if (id == W25Q_JEDEC_ID) {
                s_w25q_chip_ok = 1U;
                return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_OK);
            }
        }
    }

    g_w25q_jedec_id = 0U;
    return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_NO_DEVICE);
}

W25Q_Driver_Result W25Q_Driver_Read_JEDEC_ID(uint32_t *jedec_id)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    W25Q_Driver_Result result;

    if (jedec_id == 0) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_INVALID_ARGUMENT);
    }
    *jedec_id = 0U;
    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_Transfer(W25Q_CMD_JEDEC, 0);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Transfer(0xFFU, &b0);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Transfer(0xFFU, &b1);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Transfer(0xFFU, &b2);
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_End_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);

    *jedec_id = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
    return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_OK);
}

W25Q_Driver_Result W25Q_Driver_Read_SR1(uint8_t *sr1)
{
    W25Q_Driver_Result result;

    if (sr1 == 0) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_INVALID_ARGUMENT);
    }
    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_Transfer(W25Q_CMD_RDSR1, 0);
    if (result == W25Q_DRIVER_RESULT_OK) {
        result = W25Q_Driver_Transfer(0xFFU, sr1);
    }
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_End_Transaction();
    return W25Q_Driver_Set_Result(result);
}

W25Q_Driver_Result W25Q_Driver_Read(uint32_t addr, uint8_t *buf,
                                    uint32_t len)
{
    uint32_t i;
    W25Q_Driver_Result result;

    if (s_w25q_chip_ok == 0U) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_NO_DEVICE);
    }
    if (buf == 0) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_INVALID_ARGUMENT);
    }
    result = W25Q_Driver_Check_Range(addr, len);
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);

    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_Transfer(W25Q_CMD_READ, 0);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Send_Address(addr);
    for (i = 0U; i < len && result == W25Q_DRIVER_RESULT_OK; i++) {
        result = W25Q_Driver_Transfer(0xFFU, &buf[i]);
    }
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_End_Transaction();
    return W25Q_Driver_Set_Result(result);
}

W25Q_Driver_Result W25Q_Driver_Write_Page(uint32_t addr,
                                          const uint8_t *buf,
                                          uint16_t len)
{
    uint16_t i;
    uint32_t page_offset;
    W25Q_Driver_Result result;

    if (s_w25q_chip_ok == 0U) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_NO_DEVICE);
    }
    if (buf == 0 || len == 0U || len > W25Q_PAGE_SIZE) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_INVALID_ARGUMENT);
    }
    result = W25Q_Driver_Check_Range(addr, len);
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    page_offset = addr & (W25Q_PAGE_SIZE - 1U);
    if ((uint32_t)len > (W25Q_PAGE_SIZE - page_offset)) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_PAGE_CROSS);
    }

    result = W25Q_Driver_Wait_Busy(W25Q_PROGRAM_TIMEOUT_MS);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Write_Enable();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);

    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_Transfer(W25Q_CMD_PAGE_PROGRAM, 0);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Send_Address(addr);
    for (i = 0U; i < len && result == W25Q_DRIVER_RESULT_OK; i++) {
        result = W25Q_Driver_Transfer(buf[i], 0);
    }
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_End_Transaction();
    if (result == W25Q_DRIVER_RESULT_OK) {
        result = W25Q_Driver_Wait_Busy(W25Q_PROGRAM_TIMEOUT_MS);
    }
    return W25Q_Driver_Set_Result(result);
}

W25Q_Driver_Result W25Q_Driver_Write(uint32_t addr, const uint8_t *buf,
                                     uint32_t len)
{
    uint32_t page_offset;
    uint32_t chunk;
    W25Q_Driver_Result result;

    if (s_w25q_chip_ok == 0U) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_NO_DEVICE);
    }
    if (buf == 0) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_INVALID_ARGUMENT);
    }
    result = W25Q_Driver_Check_Range(addr, len);
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);

    while (len != 0U) {
        page_offset = addr & (W25Q_PAGE_SIZE - 1U);
        chunk = W25Q_PAGE_SIZE - page_offset;
        if (chunk > len) chunk = len;
        result = W25Q_Driver_Write_Page(addr, buf, (uint16_t)chunk);
        if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_OK);
}

W25Q_Driver_Result W25Q_Driver_Erase_Sector(uint32_t addr)
{
    W25Q_Driver_Result result;

    if (s_w25q_chip_ok == 0U) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_NO_DEVICE);
    }
    if (addr >= W25Q_CHIP_SIZE) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_OUT_OF_RANGE);
    }
    if (s_w25q_erase_allowed == 0U) {
        return W25Q_Driver_Set_Result(W25Q_DRIVER_RESULT_ERASE_BLOCKED);
    }
    addr &= ~(W25Q_SECTOR_SIZE - 1U);

    result = W25Q_Driver_Wait_Busy(W25Q_ERASE_TIMEOUT_MS);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Write_Enable();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);

    result = W25Q_Driver_Begin_Transaction();
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_Transfer(W25Q_CMD_SECTOR_ERASE, 0);
    if (result == W25Q_DRIVER_RESULT_OK) result = W25Q_Driver_Send_Address(addr);
    if (result != W25Q_DRIVER_RESULT_OK) return W25Q_Driver_Set_Result(result);
    result = W25Q_Driver_End_Transaction();
    if (result == W25Q_DRIVER_RESULT_OK) {
        result = W25Q_Driver_Wait_Busy(W25Q_ERASE_TIMEOUT_MS);
    }
    return W25Q_Driver_Set_Result(result);
}

void W25Q_Driver_Set_Erase_Allowed(uint8_t allowed)
{
    s_w25q_erase_allowed = (allowed != 0U) ? 1U : 0U;
}

uint8_t W25Q_Driver_Is_Available(void)
{
    return s_w25q_chip_ok;
}

W25Q_Driver_Result W25Q_Driver_Get_Last_Result(void)
{
    return s_w25q_last_result;
}
