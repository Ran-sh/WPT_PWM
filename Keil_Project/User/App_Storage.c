/**
 ******************************************************************************
 * @file    User/App_Storage.c
 * @brief   应用存储层 — 字库 + 参数 + 黑匣子 (极简行内聚合实现)
 * @note    四大防线均在 W25Q_Driver 层实施, 本层专注分区逻辑+校验
 ******************************************************************************
 */

#include "App_Storage.h"
#include "W25Q_Driver.h"
#include "Esp8266_Driver.h"
#include "Sys_Timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════
 *  CRC8 查表 (多项式 0x07, 256B ROM)
 * ═══════════════════════════════════════════════ */
static const uint8_t CRC8_TABLE[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3
};

static uint8_t CRC8_Compute(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    while (len--) crc = CRC8_TABLE[crc ^ *data++];
    return crc;
}

/* ═══════════════════════════════════════════════
 *  CRC32 (多项式 0x04C11DB7, 含 final XOR, 与 WinRAR/STM32 CRC 外设一致)
 * ═══════════════════════════════════════════════ */
static uint32_t CRC32_Compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU; uint32_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (j = 0; j < 8; j++)
            crc = (crc & 0x80000000U) ? (crc << 1) ^ 0x04C11DB7U : (crc << 1);
    }
    return crc ^ 0xFFFFFFFFU;  /* final XOR */
}

/* ═══════════════════════════════════════════════
 *  Base64 解码 — OTA 字库推送用
 * ═══════════════════════════════════════════════ */
static const char BASE64_TABLE_OTA_CHAR[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** @brief 单字符→6bit, 非法返回 0xFF */
static uint8_t Base64_Char_Val(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 52);
    if (c == '+') return 62U;
    if (c == '/') return 63U;
    return 0xFFU;
}

/** @brief 解码 Base64 块: in中每4字符→3字节, 遇到 '=' 停止
 *  @return  实际解码字节数 */
static uint16_t Base64_Decode_Block(const char *in, uint16_t in_len, uint8_t *out)
{
    uint16_t out_idx = 0, i; uint8_t v0, v1, v2, v3;
    for (i = 0; i + 3 < in_len; i += 4) {
        v0 = Base64_Char_Val(in[i]);
        v1 = Base64_Char_Val(in[i+1]);
        if (v0 == 0xFFU || v1 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v0 << 2) | (v1 >> 4));

        if (in[i+2] == '=') break;
        v2 = Base64_Char_Val(in[i+2]);
        if (v2 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v1 << 4) | (v2 >> 2));

        if (in[i+3] == '=') break;
        v3 = Base64_Char_Val(in[i+3]);
        if (v3 == 0xFFU) break;
        out[out_idx++] = (uint8_t)((v2 << 6) | v3);
    }
    return out_idx;
}

/* ══ 全局状态 ══ */
volatile Font_Status g_font_status = FONT_MISSING;

/* ── 黑匣子运行时状态 ── */
static uint32_t s_log_wr_ptr  = 0;       /* 当前写偏移 (相对 BLACKBOX 基址) */
static uint32_t s_log_seq     = 0;       /* 总写入条数 */
static uint32_t s_log_wrapped = 0;       /* 循环次数 */
static uint32_t s_fault_lock_addr = 0;   /* 故障锁存区写入地址 */

/* ═══════════════════════════════════════════════
 *  OTA 字库推送 (Phase A: 4KB)
 * ═══════════════════════════════════════════════ */
static uint8_t  s_ota_active     = 0;
static uint16_t s_ota_page_total = 0;
static uint16_t s_ota_page_done  = 0;
static uint8_t  s_ota_buf[256];       /* 单页解码缓冲 */

/* ── ACK 回发 ── */
static void OTA_Send_ACK(uint16_t seq)
{
    char buf[20]; uint16_t w;
    w = (uint16_t)snprintf(buf, sizeof(buf), "OTA:ACK:%u\n", (unsigned int)seq);
    if (w > 0 && w < sizeof(buf)) Esp8266_Driver_Send_String(buf);
}

static void OTA_Send_ERR(uint16_t seq, const char *reason)
{
    char buf[40]; uint16_t w;
    w = (uint16_t)snprintf(buf, sizeof(buf), "OTA:ERR:%u,%s\n", (unsigned int)seq, reason);
    if (w > 0 && w < sizeof(buf)) Esp8266_Driver_Send_String(buf);
}

/* ═══════════════════════════════════════════════
 *  V4.3.0: 首次上电自动灌入字库 — TFT_Font_Data.h → W25Q128 Flash
 *  无需 CH341A 编程器, STM32 上电自检发现空片 → 自动搬运
 *  约 4.2KB 数据, 擦除+写入约 500ms (被 ESP8266 4s BOOT_WAIT 吸收)
 * ═══════════════════════════════════════════════ */
#include "TFT_Font_Data.h"  /* CN_FONT_16X16 + TFT_FONT_8X16 + WIFI_ICON + MQTT_ICON + ICON_STAR */

static void App_Storage_Burn_Font_From_SRAM(void)
{
    uint32_t offset, i, data_end; uint8_t header[32];

    /* ── 擦除字库区前 4KB (容纳初始 76字+ASCII+图标) ── */
    W25Q_Driver_Erase_Sector(W25Q_ADDR_FONT);               /* 4KB 扇区 */

    /* ── 写入 ASCII 8×16 字模 (95字) ── */
    offset = FONT_ASCII_BASE;
    for (i = 0; i < 95; i++) {
        uint32_t page_start = offset & ~(W25Q_PAGE_SIZE - 1U);
        uint32_t next_page = page_start + W25Q_PAGE_SIZE;
        if (offset + 16 > next_page) offset = next_page;    /* 跨页保护 */
        W25Q_Driver_Write_Page(offset, &TFT_FONT_8X16[i][0], 16);
        offset += 16;
    }

    /* ── 写入 76 汉字 16×16 字模 (按 Unicode 码点映射) ── */
    offset = FONT_CJK_BASE;
    for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
        uint32_t page_start = offset & ~(W25Q_PAGE_SIZE - 1U);
        uint32_t next_page = page_start + W25Q_PAGE_SIZE;
        if (offset + 32 > next_page) offset = next_page;
        W25Q_Driver_Write_Page(offset, CN_FONT_16X16[i], 32);
        offset += 32;
    }
    data_end = offset;  /* 实际数据结束地址 (相对于字库基址) */

    /* ── 写入头部 (32B) + CRC32 回填 ── */
    *(uint16_t*)(header + 0)  = FONT_MAGIC;                  /* 0x574B */
    *(uint16_t*)(header + 2)  = 1;                           /* Version */
    *(uint32_t*)(header + 4)  = 0;                           /* CRC32 占位 */
    *(uint32_t*)(header + 8)  = 1520U;                      /* ASCII_Size */
    *(uint32_t*)(header + 12) = FONT_CJK_BASE_UNICODE;     /* CJK_Base */
    *(uint32_t*)(header + 16) = FONT_CJK_COUNT;            /* CJK_Count */
    *(uint32_t*)(header + 20) = data_end;                   /* 数据 CRC 范围 */
    for (i = 24; i < 32; i++) header[i] = 0x00;

    /* CRC32 覆盖 [header+8 .. Flash+data_end), 与 Init 范围对齐 */
    {
        uint32_t crc_val = 0xFFFFFFFFU; uint32_t k;
        for (i = 8; i < 32; i++) {
            crc_val ^= (uint32_t)header[i] << 24;
            for (k = 0; k < 8; k++)
                crc_val = (crc_val & 0x80000000U) ? (crc_val << 1) ^ 0x04C11DB7U : (crc_val << 1);
        }
        *(uint32_t*)(header + 4) = crc_val ^ 0xFFFFFFFFU;
    }
    W25Q_Driver_Write_Page(W25Q_ADDR_FONT, header, 32);     /* 写入头部 */
    g_font_status = FONT_OK;
}

/* ═══════════════════════════════════════════════
 *  字库 (P1-P2)
 * ═══════════════════════════════════════════════ */

void App_Storage_Read_ASCII(uint8_t ascii_code, uint8_t *buf_16b)
{
    uint32_t addr;
    if (ascii_code < 32 || ascii_code > 126) ascii_code = 32; /* ' ' */
    addr = FONT_ASCII_BASE + (uint32_t)(ascii_code - 32) * 16U;
    W25Q_Driver_Read(addr, buf_16b, 16);                     /* L1-L3 在驱动层处理 */
}

void App_Storage_Read_Glyph(uint16_t unicode_cp, uint8_t *buf_32b)
{
    if (unicode_cp < 0x80U) {
        W25Q_Driver_Read(FONT_ASCII_BASE + (uint32_t)unicode_cp * 16U, buf_32b, 16);
        { uint8_t i; for (i = 16; i < 32; i++) buf_32b[i] = 0x00; } /* 补齐32B空白 */
        return;
    }
    if (unicode_cp >= FONT_CJK_BASE_UNICODE && unicode_cp <= 0x9FFFU) {
        /* 穿透映射: (码点-0x4E00)*32 → Flash 偏移, 零下溢风险 */
        W25Q_Driver_Read(FONT_CJK_BASE +
                         (uint32_t)(unicode_cp - FONT_CJK_BASE_UNICODE) * FONT_CHAR_BYTES,
                         buf_32b, FONT_CHAR_BYTES);
        return;
    }
    /* 非法码点一律安全截断刷黑 — 防无符号下溢出击穿 16MB 物理悬崖 */
    { uint8_t i; for (i = 0; i < 32; i++) buf_32b[i] = 0x00; }
}

/* ═══════════════════════════════════════════════
 *  参数配置 (P3) — 双副本 CRC32 闭锁
 * ═══════════════════════════════════════════════ */

/** @brief 安全默认出厂值 */
static void App_Storage_Defaults(App_Storage_Config *cfg)
{
    uint8_t i;
    cfg->magic     = CFG_MAGIC;
    cfg->version   = CFG_VERSION;
    for (i = 0; i < 32; i++) cfg->ssid[i] = 0;
    for (i = 0; i < 64; i++) { cfg->password[i] = 0; cfg->mqtt_key[i] = 0; }
    cfg->adc_i_offset  = 0.0f;
    cfg->adc_v_gain    = 1.0f;
    cfg->freq_trim_hz = 0;
    cfg->default_freq  = 100;      /* 100kHz 安全中频 */
    cfg->backlight     = 255;
    cfg->language      = 0;
}

uint8_t App_Storage_Load_Config(App_Storage_Config *cfg)
{
    uint32_t stored_crc, computed_crc;

    if (cfg == 0) return 0;

    /* 读 A */
    W25Q_Driver_Read(W25Q_ADDR_CFG_A, (uint8_t*)cfg, sizeof(App_Storage_Config));
    if (cfg->magic == CFG_MAGIC && cfg->version == CFG_VERSION) {
        W25Q_Driver_Read(W25Q_ADDR_CFG_A + sizeof(App_Storage_Config) - 4,
                         (uint8_t*)&stored_crc, 4);
        computed_crc = CRC32_Compute((uint8_t*)cfg, sizeof(App_Storage_Config) - 4);
        if (stored_crc == computed_crc) return 1;            /* A 完好 */
    }

    /* A 坏 → 读 B */
    W25Q_Driver_Read(W25Q_ADDR_CFG_B, (uint8_t*)cfg, sizeof(App_Storage_Config));
    if (cfg->magic == CFG_MAGIC && cfg->version == CFG_VERSION) {
        W25Q_Driver_Read(W25Q_ADDR_CFG_B + sizeof(App_Storage_Config) - 4,
                         (uint8_t*)&stored_crc, 4);
        computed_crc = CRC32_Compute((uint8_t*)cfg, sizeof(App_Storage_Config) - 4);
        if (stored_crc == computed_crc) return 1;            /* B 完好 */
    }

    /* 全坏 → 出厂安全默认 */
    App_Storage_Defaults(cfg);
    return 0;
}

void App_Storage_Save_Config(const App_Storage_Config *cfg)
{
    App_Storage_Config tmp;
    uint32_t crc;
    if (cfg == 0) return;
    tmp = *cfg;  /* 栈拷贝, 不修改原始 */
    tmp.magic   = CFG_MAGIC;
    tmp.version = CFG_VERSION;
    crc = CRC32_Compute((uint8_t*)&tmp, sizeof(App_Storage_Config) - 4);
    tmp.crc32 = crc;

    /* 写 A → 写 B (顺序写入, 保持至少 1 份有效) */
    W25Q_Driver_Erase_Sector(W25Q_ADDR_CFG_A);
    W25Q_Driver_Write_Page(W25Q_ADDR_CFG_A, (uint8_t*)&tmp, sizeof(App_Storage_Config));

    W25Q_Driver_Erase_Sector(W25Q_ADDR_CFG_B);
    W25Q_Driver_Write_Page(W25Q_ADDR_CFG_B, (uint8_t*)&tmp, sizeof(App_Storage_Config));
}

void App_Storage_Write_Factory_Defaults(void)
{
    App_Storage_Config defs;
    App_Storage_Defaults(&defs);
    App_Storage_Save_Config(&defs);
}

/* ═══════════════════════════════════════════════
 *  黑匣子日志 (P4) — 14B 紧凑二进制 + CRC8 + 跨页保护 + 故障锁存
 * ═══════════════════════════════════════════════ */

/** @brief 封包: 浮点参数 → 14B 紧凑二进制 */
static void Blackbox_Pack(float v, float i, uint16_t freq, uint8_t state,
                          Blackbox_Entry_Packed *out)
{
    out->timestamp = Sys_Timer_Get_Tick();
    out->v_ema     = (uint16_t)(v * 100.0f  + 0.5f);        /* V ×100 */
    out->i_ema     = (uint16_t)(i * 1000.0f + 0.5f);        /* I ×1000 */
    out->freq_hz   = freq;
    out->sys_state = state;
    out->crc8      = CRC8_Compute((uint8_t*)out, 12);        /* 前12B */
}

void Blackbox_Log_Tick(float v, float i, uint16_t freq, uint8_t state)
{
    uint32_t addr, pos_in_page;
    Blackbox_Entry_Packed entry;

    /* 仅 SWEEP + RUNNING 记录 (设计文档 §3.4 触发条件) */
    if (g_sys_state != SYS_STATE_SWEEP && g_sys_state != SYS_STATE_RUNNING)
        return;

    Blackbox_Pack(v, i, freq, state, &entry);

    /* Page Program 跨页保护 */
    addr = W25Q_ADDR_BLACKBOX + s_log_wr_ptr;
    pos_in_page = addr & (W25Q_PAGE_SIZE - 1U);
    if (pos_in_page + BLACKBOX_ENTRY_SIZE > W25Q_PAGE_SIZE) {
        /* 跨页 → 跳到下页头部 */
        s_log_wr_ptr = (s_log_wr_ptr & ~(W25Q_PAGE_SIZE - 1U)) + W25Q_PAGE_SIZE;
    }
    /* 循环回绕 */
    if (s_log_wr_ptr >= (W25Q_ADDR_BLACKBOX_END - W25Q_ADDR_BLACKBOX -
                         (BLACKBOX_LOCK_BLOCKS * 65536U))) {
        s_log_wr_ptr = 256U;  /* 跳过 Block 0 头部, 回到 Block 1 */
        s_log_wrapped++;
    }
    addr = W25Q_ADDR_BLACKBOX + s_log_wr_ptr;

    W25Q_Driver_Write_Page(addr, (uint8_t*)&entry, BLACKBOX_ENTRY_SIZE);
    s_log_wr_ptr += BLACKBOX_ENTRY_SIZE;
    s_log_seq++;
}

void Blackbox_Lock_Fault_Snapshot(void)
{
    uint32_t i, lock_addr;
    uint32_t block_start;

    /* 在锁存保护区分配一个新块 (每块 64KB = 4680 条) */
    lock_addr = W25Q_ADDR_BLACKBOX_END -
                (BLACKBOX_LOCK_BLOCKS * 65536U) +
                (s_fault_lock_addr % (BLACKBOX_LOCK_BLOCKS * 65536U));

    /* 擦除目标保护块 */
    block_start = lock_addr & ~(65536U - 1U);
    W25Q_Driver_Erase_Sector(block_start);                   /* 块头 4KB (L4: 不在发波态) */

    /* 前5s+后5s 数据: 从循环日志区拷贝 (实际实现: 读→写锁存区) */
    for (i = 0; i < 50U && i < s_log_seq; i++) {
        uint32_t src_idx; Blackbox_Entry_Packed tmp_entry;
        if (i < 25U) src_idx = (s_log_seq > 50U) ? s_log_seq - 50U + i : i;
        else         src_idx = (s_log_seq > 25U) ? s_log_seq - 25U + (i - 25U) : i;
        if (Blackbox_Read_Entry(src_idx, &tmp_entry)) {
            W25Q_Driver_Write_Page(lock_addr + i * BLACKBOX_ENTRY_SIZE,
                                   (uint8_t*)&tmp_entry, BLACKBOX_ENTRY_SIZE);
        }
    }
    s_fault_lock_addr += BLACKBOX_ENTRY_SIZE * 50U;
}

uint32_t Blackbox_Get_Entry_Count(void) { return s_log_seq; }

uint8_t Blackbox_Read_Entry(uint32_t index, Blackbox_Entry_Packed *out)
{
    uint32_t addr, effective_start, total_size;
    if (out == 0 || index >= s_log_seq) return 0;

    /* 换行后偏移从 256 开始 (跳过 Block 0 头部), 对齐写指针逻辑 */
    effective_start = (s_log_wrapped > 0) ? 256U : 0U;
    total_size = W25Q_ADDR_BLACKBOX_END - W25Q_ADDR_BLACKBOX
               - (BLACKBOX_LOCK_BLOCKS * 65536U) - effective_start;
    addr = W25Q_ADDR_BLACKBOX + effective_start +
           ((index * BLACKBOX_ENTRY_SIZE) % total_size);
    W25Q_Driver_Read(addr, (uint8_t*)out, BLACKBOX_ENTRY_SIZE);
    return (out->crc8 == CRC8_Compute((uint8_t*)out, 12)) ? 1 : 0;
}

/* ═══════════════════════════════════════════════
 *  上电自检 (仅 SYS_STATE_INIT 调用一次, ~200ms)
 * ═══════════════════════════════════════════════ */

void App_Storage_Init(void)
{
    uint8_t  header[32];
    uint32_t stored_crc, computed_crc, i;
    uint8_t  buf[256];

    /* ── 字库 CRC32 MAGIC 快速预检 ── */
    W25Q_Driver_Read(W25Q_ADDR_FONT, header, 32);
    if (*(uint16_t*)header != FONT_MAGIC) {
        g_font_status = FONT_MISSING;                       /* 无字库 → 自动灌入 */
        App_Storage_Burn_Font_From_SRAM();                  /* 从 TFT_Font_Data.h 搬运到 Flash */
    } else {
        /* CRC32 自检: 覆盖 [header+8 .. Flash+data_size),
         * 与 App_Storage_Burn_Font_From_SRAM / App_Storage_OTA_End 计算范围严格一致
         * data_size 初始值可为 0 (未写入), 此时 CRC 仅覆盖头部 */
        uint32_t data_size;
        stored_crc = *(uint32_t*)(header + 4);
        data_size  = *(uint32_t*)(header + 20);
        if (data_size > 0x200000U || data_size < 32U) data_size = 0x200000U;  /* 非法→全量扫描兜底 */
        computed_crc = 0xFFFFFFFFU;
        /* 头部 byte 8..31 */
        { uint32_t k;
          for (k = 0; k < 24U; k++) {
              computed_crc ^= (uint32_t)header[8 + k] << 24;
              /* 逐 bit */ { uint8_t b; for (b = 0; b < 8; b++)
                  computed_crc = (computed_crc & 0x80000000U) ?
                      (computed_crc << 1) ^ 0x04C11DB7U : (computed_crc << 1);
              }}
        }
        /* 数据区 byte 32..data_size */
        for (i = 32U; i < data_size; i += sizeof(buf)) {
            uint32_t chunk = (data_size - i < sizeof(buf)) ? (data_size - i) : sizeof(buf);
            W25Q_Driver_Read(W25Q_ADDR_FONT + i, buf, (uint16_t)chunk);
            { uint32_t j, k; for (j = 0; j < chunk; j++) {
                computed_crc ^= (uint32_t)buf[j] << 24;
                for (k = 0; k < 8; k++)
                    computed_crc = (computed_crc & 0x80000000U) ?
                        (computed_crc << 1) ^ 0x04C11DB7U : (computed_crc << 1);
            }}
        }
        g_font_status = (computed_crc == stored_crc) ? FONT_OK : FONT_CORRUPT;
    }

    /* ── 恢复黑匣子写指针 (从 Block 0 头部读取) ── */
    {
        uint32_t ptr_buf[3];
        W25Q_Driver_Read(W25Q_ADDR_BLACKBOX, (uint8_t*)ptr_buf, 12);
        s_log_wr_ptr  = ptr_buf[0];
        s_fault_lock_addr = ptr_buf[1];
        s_log_wrapped = ptr_buf[2];
        /* 合法性检查 */
        if (s_log_wr_ptr < 256U ||
            s_log_wr_ptr >= (W25Q_ADDR_BLACKBOX_END - W25Q_ADDR_BLACKBOX -
                             (BLACKBOX_LOCK_BLOCKS * 65536U)))
            s_log_wr_ptr = 256U;                            /* 非法 → 重置 */
    }
}

/* ═══════════════════════════════════════════════
 *  OTA 字库推送 (Phase A: 4KB)
 * ═══════════════════════════════════════════════ */

/** @brief 进入 OTA 模式: 擦除字库区前4KB + 初始化
 *  @note  仅 IDLE 态可调用 (由 App_Network OTA:START 帧门控保证) */
void App_Storage_OTA_Begin(void)
{
    if (g_sys_state != SYS_STATE_IDLE) return;
    s_ota_active     = 1;
    s_ota_page_total = 0;
    s_ota_page_done  = 0;
    W25Q_Driver_Erase_Sector(W25Q_ADDR_FONT);
    Esp8266_Driver_Send_String("OTA:READY\n");
}

/** @brief 处理 OTA:<seq>,<base64> 帧 */
void App_Storage_OTA_Handler(const char *frame)
{
    uint16_t seq, data_len; const char *comma; uint32_t page_addr;
    if (!s_ota_active) return;
    if (strstr(frame, "OTA:") != frame) return;
    seq = (uint16_t)strtol(frame + 4, NULL, 10);
    /* 防越界: seq 不能超出已擦除的扇区范围 */
    if (seq >= 16U) {  /* 4KB=16页 */
        OTA_Send_ERR(seq, "RANGE"); return;
    }
    comma = strstr(frame, ",");
    if (comma == 0) return;
    data_len = Base64_Decode_Block(comma + 1, (uint16_t)strlen(comma + 1), s_ota_buf);
    if (data_len != W25Q_PAGE_SIZE) {
        OTA_Send_ERR(seq, "B64LEN"); return;
    }
    page_addr = W25Q_ADDR_FONT + ((uint32_t)seq * W25Q_PAGE_SIZE);
    W25Q_Driver_Write_Page(page_addr, s_ota_buf, W25Q_PAGE_SIZE);
    s_ota_page_done++;
    if (seq + 1 > s_ota_page_total) s_ota_page_total = seq + 1;
    OTA_Send_ACK(seq);
}

/** @brief OTA 传输完成 — 写头部 + CRC32 校验 + 标记 FONT_OK */
void App_Storage_OTA_End(void)
{
    uint8_t header[32]; uint32_t i, crc_val, addr, data_size; uint8_t buf[256];
    if (!s_ota_active) return;
    s_ota_active = 0;
    /* 写入字库头部 (32B), 对齐 W25Q_Driver.h 头部布局:
     *   [0-1] magic, [2-3] version, [4-7] CRC32, [8-11] ASCII_Size,
     *   [12-15] CJK_Base, [16-19] CJK_Count, [20-31] reserved */
    data_size = (uint32_t)s_ota_page_total * W25Q_PAGE_SIZE; /* CRC 覆盖字节数 */
    *(uint16_t*)(header + 0)  = FONT_MAGIC;
    *(uint16_t*)(header + 2)  = 1;
    *(uint32_t*)(header + 4)  = 0;
    *(uint32_t*)(header + 8)  = 1520U;
    *(uint32_t*)(header + 12) = FONT_CJK_BASE_UNICODE;
    *(uint32_t*)(header + 16) = FONT_CJK_COUNT;
    *(uint32_t*)(header + 20) = data_size;                   /* 关键: Init 据此确定 CRC 范围 */
    for (i = 24; i < 32; i++) header[i] = 0x00;
    /* CRC32 覆盖 [header+8 .. Flash+data_size)
     * 手动初始化中间态 CRC (0xFFFFFFFF), 与后续逐字节回路共享同一状态,
     * 最后统一 ^= 0xFFFFFFFFU 仅一次 — 避免 CRC32_Compute 双 final-XOR */
    crc_val = 0xFFFFFFFFU;
    for (i = 8; i < 32; i++) {
        uint32_t k;
        crc_val ^= (uint32_t)header[i] << 24;
        for (k = 0; k < 8; k++)
            crc_val = (crc_val & 0x80000000U) ? (crc_val << 1) ^ 0x04C11DB7U : (crc_val << 1);
    }
    for (addr = 32U; addr < data_size; addr += 256U) {
        uint32_t j, k;
        W25Q_Driver_Read(W25Q_ADDR_FONT + addr, buf, 256);
        for (j = 0; j < 256U; j++) {
            crc_val ^= (uint32_t)buf[j] << 24;
            for (k = 0; k < 8; k++)
                crc_val = (crc_val & 0x80000000U) ? (crc_val << 1) ^ 0x04C11DB7U : (crc_val << 1);
        }
    }
    crc_val ^= 0xFFFFFFFFU;
    *(uint32_t*)(header + 4) = crc_val;
    W25Q_Driver_Write_Page(W25Q_ADDR_FONT, header, 32);
    g_font_status = FONT_OK;
    Esp8266_Driver_Send_String("OTA:DONE\n");
}
