/**
 ******************************************************************************
 * @file    Hardware/W25Q_Driver.h
 * @brief   W25Q128 16MB SPI NOR Flash 底层驱动 — V5.0.1
 * @note    SPI1 分时复用: PA5=SCK PA7=MOSI PA6=动态(MISO/DC) PA12=CS
 *          四大硬件防线: 写使能锁存 / Busy 死等 / DFF 原子闪切 / 发波禁擦
 *          接线详见 W25Q_Driver.c 头部注释
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
#define SPLASH_MAGIC          0x5350U   /* "SP" 魔数 LE */
#define W25Q_ADDR_CFG_A       0x300000U  /* 参数配置 A 4KB */
#define W25Q_ADDR_CFG_B       0x301000U  /* 参数配置 B 4KB */
#define W25Q_ADDR_BLACKBOX    0x310000U  /* 黑匣子 4MB */
#define W25Q_ADDR_BLACKBOX_END 0x710000U /* 黑匣子尾 */
#define W25Q_CHIP_SIZE        0x1000000U /* 16MB */

#define W25Q_SECTOR_SIZE      4096U
#define W25Q_PAGE_SIZE        256U

/* ── 字库头部偏移 ── */
#define FONT_MAGIC            0x574BU   /* "WK" */
#define FONT_ADDR             0x000000U /* 字库分区基址 */
#define FONT_ASCII_BASE       0x000020U /* ASCII 起始 */
#define FONT_CJK_BASE         0x000700U /* CJK U+4E00 起始 */
#define FONT_CJK_BASE_UNICODE 0x4E00U   /* Unicode 起始码点 */
#define FONT_CJK_COUNT        20902U    /* U+4E00~U+9FFF */
#define FONT_CHAR_BYTES       32U       /* 16×16 LSB-first, 经 bit_reverse */

/* ── Font_Header — 硬件对齐 32B (设计文档 §3 位对位一致) ── */
typedef struct {
    uint16_t magic;             /* 0x00 魔数 0x574B */
    uint8_t  version;           /* 0x02 */
    uint8_t  reserved;          /* 0x03 */
    uint32_t total_size;        /* 0x04 字库分区总字节数 */
    uint32_t crc32;             /* 0x08 CRC32 (校验 0x0C→0x1F 的 20B) */
    uint16_t ascii_offset;      /* 0x0C */
    uint16_t ascii_count;       /* 0x0E */
    uint16_t ascii_bytes;       /* 0x10 */
    uint16_t reserved2;         /* 0x12 */
    uint32_t cjk_index_offset;  /* 0x14 */
    uint16_t cjk_index_count;   /* 0x18 */
    uint16_t cjk_data_bytes;    /* 0x1A */
    uint32_t cjk_data_offset;   /* 0x1C */
} Font_Header;                  /* 0x20 = 32B */

/* ══ 公开接口 ══ */

/** @brief 上电 JEDEC ID 原始值 (0xEF4018=W25Q128, 0=无响应, 0xFFFFFF=浮空) */
extern uint32_t g_w25q_jedec_id;

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

/* ── 低级总线控制 — 暴露给 Tft_Driver Font Index 二分检索 ── */
/** @brief PA6→Input Floating + CS=Low, 独占 SPI 总线 */
void W25Q_Enter_Mode(void);
/** @brief CS=High + PA6→GPIO Out PP, 释放总线归还 TFT */
void W25Q_Leave_Mode(void);
/** @brief SPI1→8位帧 (DISABLE→清DFF→ENABLE 原子序列) */
void W25Q_SPI_8bit(void);
/** @brief 死等 W25Q128 Busy 位清零, 超时护底 */
void W25Q_Wait_Busy_Timeout(void);
/** @brief 单字节 SPI 收发 (CS 已 Low, 8bit 已切) */
uint8_t W25Q_SPI_Transfer(uint8_t tx);

/** @brief 总线独占二分搜索 CJK Index (6763条 Unicode 升序)
 *  @note  全程持锁 W25Q_Enter_Mode, return 前统一释放,
 *          禁止中途调用带 Leave_Mode 的通用读, 根除频繁闪切对灌短路毛刺
 *  @param unicode UTF-16 码点 (0x4E00~0x9FA0)
 *  @param hdr     Font_Header 指针 (需已加载验证通过)
 *  @retval data_offset  相对 CJK_Data_BASE 的字模偏移(32位), 0xFFFFFFFF=未找到 */
uint32_t W25Q_Font_Index_Binary_Search(uint16_t unicode, const Font_Header *hdr);

/** @brief 加载并校验 Font Header, 返回 1=有效 0=无效
 *  @note  内部调用 CRC32_Compute (需 extern 声明, 见 App_Storage.h) */
uint8_t Font_Header_Load(Font_Header *hdr);

#endif /* W25Q_DRIVER_H */
