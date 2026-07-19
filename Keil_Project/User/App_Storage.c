/**
 ******************************************************************************
 * @file    User/App_Storage.c
 * @brief   应用存储层 — 参数双副本 + 黑匣子日志 (V5.0.1)
 *
 *  W25Q128 Flash partition map (16MB):
 *  +------------------------------------------------------------+
 *  |    W25Q128 16MB SPI NOR Flash (SPI1: PA5/PA7/PA6(MISO)/PA1  |
 *  |                                                             |
 *  |    [0x300000~0x300FFF] Param copy A (4KB)                   |
 *  |      sys_config: WiFi creds + ADC cal + preferences         |
 *  |      CRC32 verified, dual-copy rotation (power-loss safe)   |
 *  |                                                             |
 *  |    [0x301000~0x301FFF] Param copy B (4KB)                   |
 *  |      Alternates with copy A, either valid = recoverable     |
 *  |                                                             |
 *  |    [0x400000~0x7FFFFF] Blackbox circular log (4MB)          |
 *  |      Circular: pointer wraps 0x800000 -> 0x400256           |
 *  |      Granularity: 1s/entry, ~174762 entries (~48 hours)     |
 *  |      Fault latch: SYS_FAULT pre-5s + post-5s window preser  |
 *  |                                                             |
 *  |    Safety: Four guards in W25Q_Driver (L1~L4),              |
 *  |      this layer handles partition logic + CRC + recovery    |
 *  +------------------------------------------------------------+
 *
 * @note    Four hardware guards in W25Q_Driver layer
 ******************************************************************************
 */

#include "App_Storage.h"
#include "W25Q_Driver.h"
#include "Sys_Timer.h"
#include "Checksum.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── 黑匣子运行时状态 ── */
static uint32_t s_log_wr_ptr  = 0;       /* 当前写偏移 (相对 BLACKBOX 基址) */
static uint32_t s_log_seq     = 0;       /* 总写入条数 */
static uint32_t s_log_wrapped = 0;       /* 循环次数 */
static uint32_t s_fault_lock_addr = 0;   /* 故障锁存区写入地址 */
static uint32_t s_log_last_save = 0;     /* 上次写回 Block 0 时的 s_log_seq */

/* V4.5.2: Blackbox_Save_Header — 每 60 条日志写回一次指针到 Block 0,
 *   防止重启后丢失全部历史数据. 不每帧写 (减少 Flash 磨损). */
static void Blackbox_Save_Header(void)
{
    uint32_t ptr_buf[3];
    if (s_log_seq - s_log_last_save < 60) return;  /* 未到 60 条, 跳过 */
    s_log_last_save = s_log_seq;
    ptr_buf[0] = s_log_wr_ptr;
    ptr_buf[1] = s_fault_lock_addr;
    ptr_buf[2] = s_log_wrapped;
    W25Q_Driver_Erase_Sector(W25Q_ADDR_BLACKBOX);              /* Block 0 头 4KB */
    W25Q_Driver_Write_Page(W25Q_ADDR_BLACKBOX, (uint8_t*)ptr_buf, 12);
}

/* ═══════════════════════════════════════════════
 *  参数配置 (P3) — 双副本 CRC32 闭锁
 * ═══════════════════════════════════════════════ */

/** @brief 安全默认出厂值 — V4.5.2: BL 1-100%, font 小号, letter_spacing 0 */
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
    cfg->backlight     = 100;      /* V4.5.2: 100% */
    cfg->language      = 1;        /* EN (默认英文, 设置内手动切中文后才调用 W25Q) */
    cfg->font_size     = 0;        /* V4.5.2: 小号(1x) */
    cfg->letter_spacing = 0;       /* V4.5.2: 0px gap */
    cfg->color_preset  = 0;        /* Classic */
    cfg->color_fg      = 0xFFFF;
    cfg->color_bg      = 0x0000;
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
        computed_crc = Checksum_CRC32((uint8_t*)cfg, sizeof(App_Storage_Config) - 4);
        if (stored_crc == computed_crc) return 1;            /* A 完好 */
    }

    /* A 坏 → 读 B */
    W25Q_Driver_Read(W25Q_ADDR_CFG_B, (uint8_t*)cfg, sizeof(App_Storage_Config));
    if (cfg->magic == CFG_MAGIC && cfg->version == CFG_VERSION) {
        W25Q_Driver_Read(W25Q_ADDR_CFG_B + sizeof(App_Storage_Config) - 4,
                         (uint8_t*)&stored_crc, 4);
        computed_crc = Checksum_CRC32((uint8_t*)cfg, sizeof(App_Storage_Config) - 4);
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
    crc = Checksum_CRC32((uint8_t*)&tmp, sizeof(App_Storage_Config) - 4);
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
 *  V4.5.2 Settings Convenience
 * ═══════════════════════════════════════════════ */
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* spacing, uint8_t* preset,
                                uint16_t* fg, uint16_t* bg)
{
    App_Storage_Config cfg;
    if (App_Storage_Load_Config(&cfg)) {
        *lang    = cfg.language;
        *font    = cfg.font_size;
        *bl      = cfg.backlight;
        *spacing = cfg.letter_spacing;
        *preset  = cfg.color_preset;
        *fg      = cfg.color_fg;
        *bg      = cfg.color_bg;
    } else {
        *lang    = 1;  /* EN fallback */
        *font    = 0;
        *bl      = 100;
        *spacing = 0;
        *preset  = 0;
        *fg      = 0xFFFF;
        *bg      = 0x0000;
    }
}

void App_Storage_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                uint8_t spacing, uint8_t preset,
                                uint16_t fg, uint16_t bg)
{
    App_Storage_Config cfg;
    if (App_Storage_Load_Config(&cfg) == 0) {
        App_Storage_Defaults(&cfg);
    }
    cfg.language       = lang;
    cfg.font_size      = font;
    cfg.backlight      = bl;
    cfg.letter_spacing = spacing;
    cfg.color_preset   = preset;
    cfg.color_fg       = fg;
    cfg.color_bg       = bg;
    App_Storage_Save_Config(&cfg);
}

/* ═══════════════════════════════════════════════
 *  Blackbox Log — 14B packed binary + CRC8 + page-safe + fault latch
 * ═══════════════════════════════════════════════ */

/** @brief Pack: float params → 14B compact binary */
static void Blackbox_Pack(float v, float i, uint16_t freq, uint8_t state,
                          Blackbox_Entry_Packed *out)
{
    out->timestamp = Sys_Timer_Get_Tick();
    out->v_ema     = (uint16_t)(v * 100.0f  + 0.5f);        /* V ×100 */
    out->i_ema     = (uint16_t)(i * 1000.0f + 0.5f);        /* I ×1000 */
    out->freq_hz   = freq;
    out->sys_state = state;
    out->crc8      = Checksum_CRC8((uint8_t*)out, 12);       /* 前12B */
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

    /* V4.5.2: 每 60 条写回指针到 Block 0, 重启后可恢复 */
    Blackbox_Save_Header();
}

void Blackbox_Lock_Fault_Snapshot(void)
{
    uint32_t i, lock_addr;

    /* 在锁存保护区分配一个新块 (每块 64KB = 4680 条) */
    lock_addr = W25Q_ADDR_BLACKBOX_END -
                (BLACKBOX_LOCK_BLOCKS * 65536U) +
                (s_fault_lock_addr % (BLACKBOX_LOCK_BLOCKS * 65536U));

    /* V4.5.2: 擦除目标扇区 (4KB) + 跨页保护扇区.
     *   50条×14B=700B, 锁存地址若靠近扇区末尾可能跨页, 多擦一个扇区保安全. */
    {
        uint32_t sec_start = lock_addr & ~(4096U - 1U);
        W25Q_Driver_Erase_Sector(sec_start);                     /* 主扇区 (L4: FAULT 放行) */
        if ((lock_addr & 4095U) + 50U * BLACKBOX_ENTRY_SIZE > 4096U) {
            W25Q_Driver_Erase_Sector(sec_start + 4096U);         /* 跨页: 擦下一扇区 */
        }
    }

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
    return (out->crc8 == Checksum_CRC8((uint8_t*)out, 12)) ? 1 : 0;
}

/* ═══════════════════════════════════════════════
 *  上电自检 (仅 SYS_STATE_INIT 调用一次, ~200ms)
 * ═══════════════════════════════════════════════ */

void App_Storage_Init(void)
{
    /* 校验算法异常时禁止解释Flash内容，避免错误CRC放行损坏数据。 */
    if (Checksum_Self_Test() == 0U) {
        s_log_wr_ptr = 256U;
        s_fault_lock_addr = 0U;
        s_log_wrapped = 0U;
        return;
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
