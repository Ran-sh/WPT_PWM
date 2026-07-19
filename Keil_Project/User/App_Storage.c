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

#define APP_STORAGE_RETRY_MS  1000U

/* ── 黑匣子运行时状态 ── */
static App_Storage_Blackbox_Metadata s_blackbox_metadata;
static uint8_t s_blackbox_v2_ready = 0U;
static App_Storage_Config s_pending_config;
static uint8_t s_config_save_pending = 0U;
static uint8_t s_config_save_attempted = 0U;
static uint32_t s_config_save_last_attempt = 0U;
static App_Storage_Result s_storage_last_result = APP_STORAGE_RESULT_OK;

/* ═══════════════════════════════════════════════
 *  参数配置 (P3) — 双副本 CRC32 闭锁
 * ═══════════════════════════════════════════════ */

/** @brief 安全默认出厂值 — V4.5.2: BL 1-100%, font 小号, letter_spacing 0 */
static void App_Storage_Defaults(App_Storage_Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic     = CFG_MAGIC;
    cfg->version   = CFG_VERSION;
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

static uint8_t App_Storage_Is_Config_Valid(const App_Storage_Config *cfg)
{
    uint32_t computed_crc;

    if (cfg->magic != CFG_MAGIC || cfg->version != CFG_VERSION) return 0U;
    computed_crc = Checksum_CRC32((const uint8_t *)cfg,
                                  sizeof(App_Storage_Config) - 4U);
    return (cfg->crc32 == computed_crc) ? 1U : 0U;
}

uint8_t App_Storage_Load_Config(App_Storage_Config *cfg)
{
    if (cfg == 0) return 0U;

    memset(cfg, 0, sizeof(*cfg));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_A, (uint8_t *)cfg,
                         sizeof(*cfg)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_Valid(cfg) != 0U) return 1U;

    memset(cfg, 0, sizeof(*cfg));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_B, (uint8_t *)cfg,
                         sizeof(*cfg)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_Valid(cfg) != 0U) return 1U;

    App_Storage_Defaults(cfg);
    return 0U;
}

void App_Storage_Request_Save_Config(const App_Storage_Config *cfg)
{
    if (cfg == 0) {
        s_storage_last_result = APP_STORAGE_RESULT_INVALID_ARGUMENT;
        return;
    }

    s_pending_config = *cfg;
    s_pending_config.magic = CFG_MAGIC;
    s_pending_config.version = CFG_VERSION;
    s_pending_config.crc32 = Checksum_CRC32(
        (const uint8_t *)&s_pending_config,
        sizeof(App_Storage_Config) - 4U);
    s_config_save_pending = 1U;
    s_config_save_attempted = 0U;
    s_storage_last_result = APP_STORAGE_RESULT_PENDING;
}

static App_Storage_Result App_Storage_Verify_Config_Copy(
    uint32_t addr, const App_Storage_Config *expected)
{
    App_Storage_Config actual;

    memset(&actual, 0, sizeof(actual));
    if (W25Q_Driver_Read(addr, (uint8_t *)&actual, sizeof(actual)) !=
        W25Q_DRIVER_RESULT_OK) return APP_STORAGE_RESULT_READ_FAILED;
    if (App_Storage_Is_Config_Valid(&actual) == 0U ||
        memcmp(&actual, expected, sizeof(actual)) != 0) {
        return APP_STORAGE_RESULT_VERIFY_FAILED;
    }
    return APP_STORAGE_RESULT_OK;
}

static App_Storage_Result App_Storage_Write_Config_Copy(
    uint32_t addr, const App_Storage_Config *cfg)
{
    if (W25Q_Driver_Erase_Sector(addr) != W25Q_DRIVER_RESULT_OK) {
        return APP_STORAGE_RESULT_ERASE_FAILED;
    }
    if (W25Q_Driver_Write(addr, (const uint8_t *)cfg, sizeof(*cfg)) !=
        W25Q_DRIVER_RESULT_OK) return APP_STORAGE_RESULT_WRITE_FAILED;
    return APP_STORAGE_RESULT_OK;
}

void App_Storage_Task(void)
{
    App_Storage_Result result;
    uint32_t now;

    if (s_config_save_pending == 0U) return;
    now = Sys_Timer_Get_Tick();
    if (s_config_save_attempted != 0U &&
        (uint32_t)(now - s_config_save_last_attempt) < APP_STORAGE_RETRY_MS) return;
    s_config_save_attempted = 1U;
    s_config_save_last_attempt = now;
    if (W25Q_Driver_Is_Available() == 0U) {
        s_storage_last_result = APP_STORAGE_RESULT_NO_DEVICE;
        return;
    }

    result = App_Storage_Write_Config_Copy(W25Q_ADDR_CFG_A,
                                           &s_pending_config);
    if (result == APP_STORAGE_RESULT_OK) {
        result = App_Storage_Verify_Config_Copy(W25Q_ADDR_CFG_A,
                                                &s_pending_config);
    }
    if (result == APP_STORAGE_RESULT_OK) {
        result = App_Storage_Write_Config_Copy(W25Q_ADDR_CFG_B,
                                               &s_pending_config);
    }
    if (result == APP_STORAGE_RESULT_OK) {
        result = App_Storage_Verify_Config_Copy(W25Q_ADDR_CFG_B,
                                                &s_pending_config);
    }

    s_storage_last_result = result;
    if (result == APP_STORAGE_RESULT_OK) {
        s_config_save_pending = 0U;
        s_config_save_attempted = 0U;
    }
}

App_Storage_Result App_Storage_Get_Last_Result(void)
{
    return s_storage_last_result;
}

uint8_t App_Storage_Is_Save_Pending(void)
{
    return s_config_save_pending;
}

void App_Storage_Request_Save_ADC_Calibration(float i_offset, float v_gain)
{
    App_Storage_Config cfg;

    if (i_offset <= 0.5f || i_offset >= 2.8f) return;
    if (v_gain <= 0.0f || v_gain > 10.0f) v_gain = 1.0f;
    if (s_config_save_pending != 0U) {
        cfg = s_pending_config;
    } else if (App_Storage_Load_Config(&cfg) == 0U) {
        App_Storage_Defaults(&cfg);
    }
    cfg.adc_i_offset = i_offset;
    cfg.adc_v_gain = v_gain;
    App_Storage_Request_Save_Config(&cfg);
}

void App_Storage_Write_Factory_Defaults(void)
{
    App_Storage_Config defs;
    App_Storage_Defaults(&defs);
    App_Storage_Request_Save_Config(&defs);
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

void App_Storage_Request_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                       uint8_t spacing, uint8_t preset,
                                       uint16_t fg, uint16_t bg)
{
    App_Storage_Config cfg;
    if (s_config_save_pending != 0U) {
        cfg = s_pending_config;
    } else if (App_Storage_Load_Config(&cfg) == 0U) {
        App_Storage_Defaults(&cfg);
    }
    cfg.language       = lang;
    cfg.font_size      = font;
    cfg.backlight      = bl;
    cfg.letter_spacing = spacing;
    cfg.color_preset   = preset;
    cfg.color_fg       = fg;
    cfg.color_bg       = bg;
    App_Storage_Request_Save_Config(&cfg);
}

/* ═══════════════════════════════════════════════
 *  Blackbox Log — 14B packed binary + CRC8 + page-safe + fault latch
 * ═══════════════════════════════════════════════ */

/** @brief Pack: float params → 14B compact binary */
static void Blackbox_Pack(float v, float i, uint16_t freq, uint8_t state,
                          App_Storage_Log_Entry *out)
{
    memset(out, 0, sizeof(*out));
    if (v < 0.0f) v = 0.0f;
    if (i < 0.0f) i = 0.0f;
    if (v > 655.35f) v = 655.35f;
    if (i > 65.535f) i = 65.535f;
    out->timestamp = Sys_Timer_Get_Tick();
    out->voltage_x100 = (uint16_t)(v * 100.0f + 0.5f);
    out->current_x1000 = (uint16_t)(i * 1000.0f + 0.5f);
    out->frequency_hz = freq;
    out->system_state = state;
    out->crc8 = Checksum_CRC8((const uint8_t *)out, 11U);
}

void Blackbox_Log_Tick(float v, float i, uint16_t freq, uint8_t state)
{
    uint32_t addr;
    uint32_t pos_in_page;
    App_Storage_Log_Entry entry;

    if (s_blackbox_v2_ready == 0U) return;

    Blackbox_Pack(v, i, freq, state, &entry);

    addr = s_blackbox_metadata.write_addr;
    pos_in_page = addr & (W25Q_PAGE_SIZE - 1U);
    if (pos_in_page + BLACKBOX_ENTRY_SIZE > W25Q_PAGE_SIZE) {
        addr = (addr & ~(W25Q_PAGE_SIZE - 1U)) + W25Q_PAGE_SIZE;
    }
    if (addr + BLACKBOX_ENTRY_SIZE > APP_STORAGE_FAULT_START_ADDR) {
        addr = APP_STORAGE_LOG_START_ADDR;
        s_blackbox_metadata.wrap_count++;
    }

    if (W25Q_Driver_Write(addr, (const uint8_t *)&entry,
                          sizeof(entry)) == W25Q_DRIVER_RESULT_OK) {
        s_blackbox_metadata.write_addr = addr + sizeof(entry);
        s_blackbox_metadata.entry_count++;
    } else {
        s_blackbox_metadata.dropped_count++;
    }
}

void Blackbox_Lock_Fault_Snapshot(void)
{
    App_Storage_Fault_Header header;

    memset(&header, 0, sizeof(header));
    header.magic = APP_STORAGE_FAULT_MAGIC;
    header.version = APP_STORAGE_BLACKBOX_VERSION;
    header.size = sizeof(header);
    header.trigger_timestamp = Sys_Timer_Get_Tick();
    header.data_addr = APP_STORAGE_FAULT_START_ADDR + sizeof(header);
    header.crc32 = Checksum_CRC32((const uint8_t *)&header,
                                  sizeof(header) - 4U);
    (void)header;
}

uint32_t Blackbox_Get_Entry_Count(void)
{
    return (s_blackbox_v2_ready != 0U) ?
           s_blackbox_metadata.entry_count : 0U;
}

uint8_t Blackbox_Read_Entry(uint32_t index, App_Storage_Log_Entry *out)
{
    uint32_t addr;

    if (out == 0 || s_blackbox_v2_ready == 0U ||
        index >= s_blackbox_metadata.entry_count) return 0U;
    addr = APP_STORAGE_LOG_START_ADDR + index * BLACKBOX_ENTRY_SIZE;
    if (addr + sizeof(*out) > APP_STORAGE_FAULT_START_ADDR) return 0U;
    if (W25Q_Driver_Read(addr, (uint8_t *)out, sizeof(*out)) !=
        W25Q_DRIVER_RESULT_OK) return 0U;
    return (out->crc8 == Checksum_CRC8((const uint8_t *)out, 11U)) ? 1U : 0U;
}

/* ═══════════════════════════════════════════════
 *  上电自检 (仅 SYS_STATE_INIT 调用一次, ~200ms)
 * ═══════════════════════════════════════════════ */

static uint8_t App_Storage_Is_Metadata_Valid(
    const App_Storage_Blackbox_Metadata *metadata)
{
    uint32_t crc;

    if (metadata->magic != APP_STORAGE_BLACKBOX_MAGIC ||
        metadata->version != APP_STORAGE_BLACKBOX_VERSION ||
        metadata->size != sizeof(*metadata) ||
        metadata->write_addr < APP_STORAGE_LOG_START_ADDR ||
        metadata->write_addr > APP_STORAGE_FAULT_START_ADDR ||
        metadata->next_fault_slot >= APP_STORAGE_FAULT_SLOT_COUNT) return 0U;
    crc = Checksum_CRC32((const uint8_t *)metadata,
                         sizeof(*metadata) - 4U);
    return (metadata->crc32 == crc) ? 1U : 0U;
}

static void App_Storage_Reset_Blackbox_V2(void)
{
    memset(&s_blackbox_metadata, 0, sizeof(s_blackbox_metadata));
    s_blackbox_metadata.magic = APP_STORAGE_BLACKBOX_MAGIC;
    s_blackbox_metadata.version = APP_STORAGE_BLACKBOX_VERSION;
    s_blackbox_metadata.size = sizeof(s_blackbox_metadata);
    s_blackbox_metadata.write_addr = APP_STORAGE_LOG_START_ADDR;
    s_blackbox_metadata.crc32 = Checksum_CRC32(
        (const uint8_t *)&s_blackbox_metadata,
        sizeof(s_blackbox_metadata) - 4U);
    s_blackbox_v2_ready = 0U;
}

void App_Storage_Init(void)
{
    App_Storage_Blackbox_Metadata metadata_a;
    App_Storage_Blackbox_Metadata metadata_b;
    uint8_t valid_a;
    uint8_t valid_b;

    App_Storage_Reset_Blackbox_V2();
    if (Checksum_Self_Test() == 0U ||
        W25Q_Driver_Is_Available() == 0U) return;

    memset(&metadata_a, 0, sizeof(metadata_a));
    memset(&metadata_b, 0, sizeof(metadata_b));
    valid_a = (W25Q_Driver_Read(APP_STORAGE_META_A_ADDR,
                                (uint8_t *)&metadata_a,
                                sizeof(metadata_a)) == W25Q_DRIVER_RESULT_OK &&
               App_Storage_Is_Metadata_Valid(&metadata_a) != 0U) ? 1U : 0U;
    valid_b = (W25Q_Driver_Read(APP_STORAGE_META_B_ADDR,
                                (uint8_t *)&metadata_b,
                                sizeof(metadata_b)) == W25Q_DRIVER_RESULT_OK &&
               App_Storage_Is_Metadata_Valid(&metadata_b) != 0U) ? 1U : 0U;

    if (valid_a != 0U &&
        (valid_b == 0U || metadata_a.generation >= metadata_b.generation)) {
        s_blackbox_metadata = metadata_a;
        s_blackbox_v2_ready = 1U;
    } else if (valid_b != 0U) {
        s_blackbox_metadata = metadata_b;
        s_blackbox_v2_ready = 1U;
    }
}
