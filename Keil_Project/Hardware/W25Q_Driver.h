/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.h
 * @brief   W25Q128 16MB SPI NOR Flash 底层驱动 — 公开接口
 * @note    SPI1 分时复用: PA5=SCK PA7=MOSI PA6=动态(MISO/DC) PA12=CS
 *          四大硬件防线: 写使能锁存 / Busy死等 / DFF原子闪切 / 发波禁擦
 *          ARMCC V5 SPL, 禁止 // 注释, 禁止 HAL
 ******************************************************************************
 */

#ifndef W25Q_DRIVER_H
#define W25Q_DRIVER_H

#include "stm32f10x.h"

/* ── W25Q128 JEDEC ID ── */
#define W25Q_JEDEC_ID   0xEF4018U

/* ── 分区基址 ── */
#define W25Q_ADDR_FONT        0x000000U  /* 全字库 2MB */
#define W25Q_ADDR_SPLASH      0x200000U  /* 开机画面 1MB */
#define W25Q_ADDR_CFG_A       0x300000U  /* 参数配置 A 4KB */
#define W25Q_ADDR_CFG_B       0x301000U  /* 参数配置 B 4KB */
#define W25Q_ADDR_BLACKBOX    0x310000U  /* 黑匣子 4MB */
#define W25Q_ADDR_BLACKBOX_END 0x710000U /* 黑匣子尾 */
#define W25Q_CHIP_SIZE        0x1000000U /* 16MB */

#define W25Q_SECTOR_SIZE      4096U
#define W25Q_PAGE_SIZE        256U

/* ── 字库头部偏移 ── */
#define FONT_MAGIC            0x574BU   /* "WK" */
#define FONT_ASCII_BASE       0x000020U /* ASCII 起始 */
#define FONT_CJK_BASE         0x000700U /* CJK U+4E00 起始 */
#define FONT_CJK_BASE_UNICODE 0x4E00U   /* Unicode 起始码点 */
#define FONT_CJK_COUNT        20902U    /* U+4E00~U+9FFF */
#define FONT_CHAR_BYTES       32U       /* 16×16 LSB-first, 经 bit_reverse */

/* ══ 公开接口 ══ */

/** @brief 初始化: GPIO + SPI1 参数 + JEDEC 校验 (失败→LED红灯) */
void W25Q_Driver_Init(void);

/** @brief 通用读: 任意地址, 任意长度 (0x03, 轮询, MISO 8位)
 *  @note  调用期间 USART2 ISR 可打断, CS 门控保数据安全 */
void W25Q_Driver_Read(uint32_t addr, uint8_t *buf, uint16_t len);

/** @brief 页写: ≤256B, 调用方已保证不跨页 (0x02 Page Program)
 *  @note  内部自动发送写使能 (0x06) + 等待 Busy 解除 */
void W25Q_Driver_Write_Page(uint32_t addr, const uint8_t *buf, uint16_t len);

/** @brief 扇区擦除: 4KB (0x20), 阻塞 ~45ms
 *  @note  SYS_STATE_SWEEP/RUNNING 时硬件拦截, 直接 return */
void W25Q_Driver_Erase_Sector(uint32_t addr);

/** @brief 读状态寄存器 1 (用于外部 Busy 检查) */
uint8_t W25Q_Driver_Read_SR1(void);

/** @brief 读 JEDEC ID (24位: 0xEF4018=W25Q128) */
uint32_t W25Q_Driver_Read_JEDEC_ID(void);

#endif /* W25Q_DRIVER_H */
