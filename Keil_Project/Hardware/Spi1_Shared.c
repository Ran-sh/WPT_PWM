/**
 ******************************************************************************
 * @file    Hardware/Spi1_Shared.c
 * @brief   SPI1 shared-bus ownership and recovery implementation - V5.0.2
 * @note    TFT and W25Q128 may never have their chip selects low together.
 ******************************************************************************
 */

#include "Spi1_Shared.h"
#include "Sys_Timer.h"

#define SPI1_SHARED_RX_DRAIN_LIMIT 4U

#define SPI1_SHARED_TFT_CS_PORT       GPIOA
#define SPI1_SHARED_TFT_CS_PIN        GPIO_Pin_4
#define SPI1_SHARED_FLASH_CS_PORT     GPIOB
#define SPI1_SHARED_FLASH_CS_PIN      GPIO_Pin_12
#define SPI1_SHARED_DC_MISO_PORT      GPIOA
#define SPI1_SHARED_DC_MISO_PIN       GPIO_Pin_6

static uint8_t s_spi1_shared_owned = 0U;
static Spi1_Shared_Mode s_spi1_shared_mode = SPI1_SHARED_MODE_TFT_8;
static Spi1_Shared_Result s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;

static void Spi1_Shared_Deselect_All(void)
{
    GPIO_SetBits(SPI1_SHARED_TFT_CS_PORT, SPI1_SHARED_TFT_CS_PIN);
    GPIO_SetBits(SPI1_SHARED_FLASH_CS_PORT, SPI1_SHARED_FLASH_CS_PIN);
}

static void Spi1_Shared_Clear_Rx_And_Ovr(void)
{
    volatile uint16_t dummy;
    uint8_t drain_count;

    drain_count = 0U;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == SET &&
           drain_count < SPI1_SHARED_RX_DRAIN_LIMIT) {
        dummy = SPI_I2S_ReceiveData(SPI1);
        drain_count++;
    }

    if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_OVR) == SET) {
        dummy = SPI1->DR;
        dummy = SPI1->SR;
    }

    (void)dummy;
}

static void Spi1_Shared_Configure_PA6(Spi1_Shared_Mode mode)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Pin = SPI1_SHARED_DC_MISO_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    if (mode == SPI1_SHARED_MODE_FLASH_8) {
        gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(SPI1_SHARED_DC_MISO_PORT, &gpio);
    } else {
        GPIO_SetBits(SPI1_SHARED_DC_MISO_PORT, SPI1_SHARED_DC_MISO_PIN);
        gpio.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(SPI1_SHARED_DC_MISO_PORT, &gpio);
    }
}

static void Spi1_Shared_Set_Frame_Mode(Spi1_Shared_Mode mode)
{
    SPI_Cmd(SPI1, DISABLE);
    if (mode == SPI1_SHARED_MODE_TFT_16) {
        SPI1->CR1 |= SPI_CR1_DFF;
    } else {
        SPI1->CR1 &= (uint16_t)~SPI_CR1_DFF;
    }
    Spi1_Shared_Configure_PA6(mode);
    SPI_Cmd(SPI1, ENABLE);
}

static uint8_t Spi1_Shared_Timed_Out(uint32_t start, uint32_t timeout_ms)
{
    return ((uint32_t)(Sys_Timer_Get_Tick() - start) >= timeout_ms) ? 1U : 0U;
}

void Spi1_Shared_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_SPI1, ENABLE);

    gpio.GPIO_Pin = SPI1_SHARED_TFT_CS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SPI1_SHARED_TFT_CS_PORT, &gpio);

    gpio.GPIO_Pin = SPI1_SHARED_FLASH_CS_PIN;
    GPIO_Init(SPI1_SHARED_FLASH_CS_PORT, &gpio);
    Spi1_Shared_Deselect_All();

    gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    Spi1_Shared_Configure_PA6(SPI1_SHARED_MODE_TFT_8);

    SPI_I2S_DeInit(SPI1);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    Spi1_Shared_Clear_Rx_And_Ovr();
    s_spi1_shared_owned = 0U;
    s_spi1_shared_mode = SPI1_SHARED_MODE_TFT_8;
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Wait_Idle(uint32_t timeout_ms)
{
    uint32_t start;

    start = Sys_Timer_Get_Tick();
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
        if (Spi1_Shared_Timed_Out(start, timeout_ms) != 0U) {
            s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
            return SPI1_SHARED_RESULT_TIMEOUT;
        }
    }
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
        if (Spi1_Shared_Timed_Out(start, timeout_ms) != 0U) {
            s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
            return SPI1_SHARED_RESULT_TIMEOUT;
        }
    }

    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Acquire(Spi1_Shared_Mode mode,
                                       uint32_t timeout_ms)
{
    if (mode > SPI1_SHARED_MODE_FLASH_8) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_INVALID;
        return SPI1_SHARED_RESULT_INVALID;
    }
    if (s_spi1_shared_owned != 0U) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_BUSY;
        return SPI1_SHARED_RESULT_BUSY;
    }

    Spi1_Shared_Deselect_All();
    if (Spi1_Shared_Wait_Idle(timeout_ms) != SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
        return SPI1_SHARED_RESULT_TIMEOUT;
    }

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    Spi1_Shared_Clear_Rx_And_Ovr();
    Spi1_Shared_Set_Frame_Mode(mode);
    Spi1_Shared_Clear_Rx_And_Ovr();

    s_spi1_shared_mode = mode;
    s_spi1_shared_owned = 1U;
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Release(void)
{
    if (Spi1_Shared_Wait_Idle(200U) != SPI1_SHARED_RESULT_OK) {
        Spi1_Shared_Force_Release();
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
        return SPI1_SHARED_RESULT_TIMEOUT;
    }

    Spi1_Shared_Deselect_All();
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    Spi1_Shared_Clear_Rx_And_Ovr();
    Spi1_Shared_Set_Frame_Mode(SPI1_SHARED_MODE_TFT_8);
    Spi1_Shared_Clear_Rx_And_Ovr();
    s_spi1_shared_mode = SPI1_SHARED_MODE_TFT_8;
    s_spi1_shared_owned = 0U;
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

void Spi1_Shared_Force_Release(void)
{
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    Spi1_Shared_Deselect_All();
    SPI_Cmd(SPI1, DISABLE);
    SPI1->CR1 &= (uint16_t)~SPI_CR1_DFF;
    Spi1_Shared_Configure_PA6(SPI1_SHARED_MODE_TFT_8);
    Spi1_Shared_Clear_Rx_And_Ovr();
    SPI_Cmd(SPI1, ENABLE);
    Spi1_Shared_Clear_Rx_And_Ovr();
    s_spi1_shared_mode = SPI1_SHARED_MODE_TFT_8;
    s_spi1_shared_owned = 0U;
}

Spi1_Shared_Result Spi1_Shared_Transfer8(uint8_t tx, uint8_t *rx,
                                        uint32_t timeout_ms)
{
    uint32_t start;
    uint8_t received;

    if ((s_spi1_shared_owned == 0U) ||
        (s_spi1_shared_mode == SPI1_SHARED_MODE_TFT_16)) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_INVALID;
        return SPI1_SHARED_RESULT_INVALID;
    }

    start = Sys_Timer_Get_Tick();
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
        if (Spi1_Shared_Timed_Out(start, timeout_ms) != 0U) {
            Spi1_Shared_Force_Release();
            s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
            return SPI1_SHARED_RESULT_TIMEOUT;
        }
    }
    SPI_I2S_SendData(SPI1, tx);

    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) {
        if (Spi1_Shared_Timed_Out(start, timeout_ms) != 0U) {
            Spi1_Shared_Force_Release();
            s_spi1_shared_last_result = SPI1_SHARED_RESULT_TIMEOUT;
            return SPI1_SHARED_RESULT_TIMEOUT;
        }
    }
    received = (uint8_t)SPI_I2S_ReceiveData(SPI1);
    if (rx != 0) {
        *rx = received;
    }

    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Set_Tft_DC(uint8_t data_mode)
{
    if ((s_spi1_shared_owned == 0U) ||
        (s_spi1_shared_mode == SPI1_SHARED_MODE_FLASH_8)) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_INVALID;
        return SPI1_SHARED_RESULT_INVALID;
    }

    if (data_mode != 0U) {
        GPIO_SetBits(SPI1_SHARED_DC_MISO_PORT, SPI1_SHARED_DC_MISO_PIN);
    } else {
        GPIO_ResetBits(SPI1_SHARED_DC_MISO_PORT, SPI1_SHARED_DC_MISO_PIN);
    }
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Select_Tft(uint8_t selected)
{
    if ((s_spi1_shared_owned == 0U) ||
        (s_spi1_shared_mode == SPI1_SHARED_MODE_FLASH_8)) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_INVALID;
        return SPI1_SHARED_RESULT_INVALID;
    }

    GPIO_SetBits(SPI1_SHARED_FLASH_CS_PORT, SPI1_SHARED_FLASH_CS_PIN);
    if (selected != 0U) {
        GPIO_ResetBits(SPI1_SHARED_TFT_CS_PORT, SPI1_SHARED_TFT_CS_PIN);
    } else {
        GPIO_SetBits(SPI1_SHARED_TFT_CS_PORT, SPI1_SHARED_TFT_CS_PIN);
    }
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Select_Flash(uint8_t selected)
{
    if ((s_spi1_shared_owned == 0U) ||
        (s_spi1_shared_mode != SPI1_SHARED_MODE_FLASH_8)) {
        s_spi1_shared_last_result = SPI1_SHARED_RESULT_INVALID;
        return SPI1_SHARED_RESULT_INVALID;
    }

    GPIO_SetBits(SPI1_SHARED_TFT_CS_PORT, SPI1_SHARED_TFT_CS_PIN);
    if (selected != 0U) {
        GPIO_ResetBits(SPI1_SHARED_FLASH_CS_PORT, SPI1_SHARED_FLASH_CS_PIN);
    } else {
        GPIO_SetBits(SPI1_SHARED_FLASH_CS_PORT, SPI1_SHARED_FLASH_CS_PIN);
    }
    s_spi1_shared_last_result = SPI1_SHARED_RESULT_OK;
    return SPI1_SHARED_RESULT_OK;
}

Spi1_Shared_Result Spi1_Shared_Get_Last_Result(void)
{
    return s_spi1_shared_last_result;
}
