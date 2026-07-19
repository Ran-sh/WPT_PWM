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
#define APP_STORAGE_METADATA_INTERVAL  60U
#define APP_STORAGE_METADATA_RETRY_MS  1000U
#define APP_STORAGE_LOG_MAINTENANCE_MS  1000U
#define APP_STORAGE_LOG_ENTRIES_PER_SECTOR  (W25Q_SECTOR_SIZE / BLACKBOX_ENTRY_SIZE)
#define APP_STORAGE_LOG_SECTOR_COUNT \
    ((APP_STORAGE_FAULT_START_ADDR - APP_STORAGE_LOG_START_ADDR) / W25Q_SECTOR_SIZE)
#define APP_STORAGE_LOG_CAPACITY \
    (APP_STORAGE_LOG_SECTOR_COUNT * APP_STORAGE_LOG_ENTRIES_PER_SECTOR)
#define APP_STORAGE_INVALID_ADDR  0xFFFFFFFFUL

/* ── 黑匣子运行时状态 ── */
static App_Storage_Blackbox_Metadata s_blackbox_metadata;
static uint8_t s_blackbox_v2_ready = 0U;
static uint32_t s_metadata_active_base = APP_STORAGE_META_A_ADDR;
static uint32_t s_metadata_next_addr = APP_STORAGE_META_A_ADDR;
static uint32_t s_metadata_saved_entry_count = 0U;
static uint8_t s_metadata_checkpoint_pending = 1U;
static uint8_t s_metadata_attempted = 0U;
static uint32_t s_metadata_last_attempt = 0U;
static uint32_t s_log_prepared_sectors[2] = {
    APP_STORAGE_INVALID_ADDR, APP_STORAGE_INVALID_ADDR
};
static uint32_t s_log_maintenance_last = 0U;
static App_Storage_Config s_pending_config;
static uint8_t s_config_save_pending = 0U;
static uint8_t s_config_save_attempted = 0U;
static uint32_t s_config_save_last_attempt = 0U;
static App_Storage_Result s_storage_last_result = APP_STORAGE_RESULT_OK;

static void App_Storage_Metadata_Task(void);
static void App_Storage_Log_Maintenance_Task(void);
static uint8_t App_Storage_Is_Erased(const uint8_t *data, uint32_t len);

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

    if (s_config_save_pending == 0U) {
        App_Storage_Metadata_Task();
        App_Storage_Log_Maintenance_Task();
        return;
    }
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

void App_Storage_Request_Blackbox_Checkpoint(void)
{
    s_metadata_checkpoint_pending = 1U;
    s_metadata_attempted = 0U;
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

static uint32_t App_Storage_Log_Address_To_Slot(uint32_t addr)
{
    uint32_t offset;
    uint32_t sector;
    uint32_t in_sector;
    uint32_t slot;

    if (addr < APP_STORAGE_LOG_START_ADDR ||
        addr >= APP_STORAGE_FAULT_START_ADDR) return 0U;
    offset = addr - APP_STORAGE_LOG_START_ADDR;
    sector = offset / W25Q_SECTOR_SIZE;
    in_sector = offset & (W25Q_SECTOR_SIZE - 1U);
    slot = in_sector / BLACKBOX_ENTRY_SIZE;
    if (slot >= APP_STORAGE_LOG_ENTRIES_PER_SECTOR) {
        sector++;
        slot = 0U;
    }
    if (sector >= APP_STORAGE_LOG_SECTOR_COUNT) sector = 0U;
    return sector * APP_STORAGE_LOG_ENTRIES_PER_SECTOR + slot;
}

static uint32_t App_Storage_Log_Slot_To_Address(uint32_t slot)
{
    uint32_t sector;
    uint32_t in_sector_slot;

    slot %= APP_STORAGE_LOG_CAPACITY;
    sector = slot / APP_STORAGE_LOG_ENTRIES_PER_SECTOR;
    in_sector_slot = slot % APP_STORAGE_LOG_ENTRIES_PER_SECTOR;
    return APP_STORAGE_LOG_START_ADDR + sector * W25Q_SECTOR_SIZE +
           in_sector_slot * BLACKBOX_ENTRY_SIZE;
}

static uint32_t App_Storage_Log_Normalize_Address(uint32_t addr)
{
    return App_Storage_Log_Slot_To_Address(
        App_Storage_Log_Address_To_Slot(addr));
}

static uint32_t App_Storage_Log_Advance_Address(uint32_t addr)
{
    uint32_t slot;

    slot = App_Storage_Log_Address_To_Slot(addr);
    slot = (slot + 1U) % APP_STORAGE_LOG_CAPACITY;
    return App_Storage_Log_Slot_To_Address(slot);
}

static uint32_t App_Storage_Log_Sector_Base(uint32_t addr)
{
    uint32_t offset;

    addr = App_Storage_Log_Normalize_Address(addr);
    offset = addr - APP_STORAGE_LOG_START_ADDR;
    return APP_STORAGE_LOG_START_ADDR +
           (offset / W25Q_SECTOR_SIZE) * W25Q_SECTOR_SIZE;
}

static uint8_t App_Storage_Log_Is_Sector_Prepared(uint32_t sector_base)
{
    return (s_log_prepared_sectors[0] == sector_base ||
            s_log_prepared_sectors[1] == sector_base) ? 1U : 0U;
}

static void App_Storage_Log_Mark_Sector_Prepared(uint32_t sector_base,
                                                 uint32_t current_base)
{
    if (App_Storage_Log_Is_Sector_Prepared(sector_base) != 0U) return;
    if (s_log_prepared_sectors[0] == APP_STORAGE_INVALID_ADDR) {
        s_log_prepared_sectors[0] = sector_base;
    } else if (s_log_prepared_sectors[1] == APP_STORAGE_INVALID_ADDR) {
        s_log_prepared_sectors[1] = sector_base;
    } else if (s_log_prepared_sectors[0] != current_base) {
        s_log_prepared_sectors[0] = sector_base;
    } else {
        s_log_prepared_sectors[1] = sector_base;
    }
}

static void App_Storage_Log_Unmark_Sector(uint32_t sector_base)
{
    if (s_log_prepared_sectors[0] == sector_base) {
        s_log_prepared_sectors[0] = APP_STORAGE_INVALID_ADDR;
    }
    if (s_log_prepared_sectors[1] == sector_base) {
        s_log_prepared_sectors[1] = APP_STORAGE_INVALID_ADDR;
    }
}

static uint8_t App_Storage_Log_Is_Target_Erased(uint32_t addr)
{
    uint8_t bytes[BLACKBOX_ENTRY_SIZE];

    memset(bytes, 0, sizeof(bytes));
    if (W25Q_Driver_Read(addr, bytes, sizeof(bytes)) !=
        W25Q_DRIVER_RESULT_OK) return 0U;
    return App_Storage_Is_Erased(bytes, sizeof(bytes));
}

static uint8_t App_Storage_Verify_Log_Entry(
    uint32_t addr, const App_Storage_Log_Entry *expected)
{
    App_Storage_Log_Entry actual;

    memset(&actual, 0, sizeof(actual));
    if (W25Q_Driver_Read(addr, (uint8_t *)&actual, sizeof(actual)) !=
        W25Q_DRIVER_RESULT_OK) return 0U;
    if (actual.crc8 != Checksum_CRC8((const uint8_t *)&actual, 11U)) return 0U;
    return (memcmp(&actual, expected, sizeof(actual)) == 0) ? 1U : 0U;
}

/** @brief Pack physical values into one fixed 12-byte V2 record. */
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
    uint32_t next_addr;
    uint32_t sector_base;
    uint32_t next_sector_base;
    App_Storage_Log_Entry entry;
    W25Q_Driver_Result result;

    if (s_blackbox_v2_ready == 0U) return;

    Blackbox_Pack(v, i, freq, state, &entry);
    addr = App_Storage_Log_Normalize_Address(
        s_blackbox_metadata.write_addr);
    sector_base = App_Storage_Log_Sector_Base(addr);
    if (App_Storage_Log_Is_Sector_Prepared(sector_base) == 0U ||
        App_Storage_Log_Is_Target_Erased(addr) == 0U) {
        s_blackbox_metadata.dropped_count++;
        s_metadata_checkpoint_pending = 1U;
        return;
    }

    result = W25Q_Driver_Write(addr, (const uint8_t *)&entry, sizeof(entry));
    if (result == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Verify_Log_Entry(addr, &entry) != 0U) {
        next_addr = App_Storage_Log_Advance_Address(addr);
        next_sector_base = App_Storage_Log_Sector_Base(next_addr);
        if (next_addr < addr) s_blackbox_metadata.wrap_count++;
        s_blackbox_metadata.write_addr = next_addr;
        if (s_blackbox_metadata.entry_count < APP_STORAGE_LOG_CAPACITY) {
            s_blackbox_metadata.entry_count++;
        }
        if (next_sector_base != sector_base) {
            App_Storage_Log_Unmark_Sector(sector_base);
        }
        if ((uint32_t)(s_blackbox_metadata.entry_count -
                       s_metadata_saved_entry_count) >=
            APP_STORAGE_METADATA_INTERVAL) {
            s_metadata_checkpoint_pending = 1U;
        }
        return;
    }

    s_blackbox_metadata.dropped_count++;
    s_metadata_checkpoint_pending = 1U;
    return;
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
    uint32_t write_slot;
    uint32_t oldest_slot;
    uint32_t target_slot;

    if (out == 0 || s_blackbox_v2_ready == 0U ||
        index >= s_blackbox_metadata.entry_count) return 0U;
    write_slot = App_Storage_Log_Address_To_Slot(
        App_Storage_Log_Normalize_Address(s_blackbox_metadata.write_addr));
    oldest_slot = (write_slot + APP_STORAGE_LOG_CAPACITY -
                   s_blackbox_metadata.entry_count) % APP_STORAGE_LOG_CAPACITY;
    target_slot = (oldest_slot + index) % APP_STORAGE_LOG_CAPACITY;
    addr = App_Storage_Log_Slot_To_Address(target_slot);
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
        metadata->write_addr >= APP_STORAGE_FAULT_START_ADDR ||
        metadata->entry_count > APP_STORAGE_LOG_CAPACITY ||
        metadata->next_fault_slot >= APP_STORAGE_FAULT_SLOT_COUNT) return 0U;
    crc = Checksum_CRC32((const uint8_t *)metadata,
                         sizeof(*metadata) - 4U);
    return (metadata->crc32 == crc) ? 1U : 0U;
}

static uint8_t App_Storage_Is_Erased(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        if (data[i] != 0xFFU) return 0U;
    }
    return 1U;
}

static void App_Storage_Scan_Metadata_Sector(
    uint32_t base, App_Storage_Blackbox_Metadata *best,
    uint8_t *valid, uint32_t *next_addr)
{
    uint32_t addr;
    App_Storage_Blackbox_Metadata record;

    *valid = 0U;
    *next_addr = base;
    memset(best, 0, sizeof(*best));
    for (addr = base;
         addr + sizeof(record) <= base + W25Q_SECTOR_SIZE;
         addr += sizeof(record)) {
        memset(&record, 0, sizeof(record));
        if (W25Q_Driver_Read(addr, (uint8_t *)&record, sizeof(record)) !=
            W25Q_DRIVER_RESULT_OK) return;
        if (App_Storage_Is_Erased((const uint8_t *)&record,
                                  sizeof(record)) != 0U) continue;
        *next_addr = addr + sizeof(record);
        if (App_Storage_Is_Metadata_Valid(&record) != 0U &&
            (*valid == 0U || record.generation > best->generation)) {
            *best = record;
            *valid = 1U;
        }
    }
}

static void App_Storage_Prepare_Metadata_Record(
    App_Storage_Blackbox_Metadata *candidate)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->magic = APP_STORAGE_BLACKBOX_MAGIC;
    candidate->version = APP_STORAGE_BLACKBOX_VERSION;
    candidate->size = sizeof(*candidate);
    candidate->generation = s_blackbox_metadata.generation + 1U;
    candidate->write_addr = s_blackbox_metadata.write_addr;
    candidate->entry_count = s_blackbox_metadata.entry_count;
    candidate->wrap_count = s_blackbox_metadata.wrap_count;
    candidate->next_fault_slot = s_blackbox_metadata.next_fault_slot;
    candidate->dropped_count = s_blackbox_metadata.dropped_count;
    candidate->crc32 = Checksum_CRC32((const uint8_t *)candidate,
                                      sizeof(*candidate) - 4U);
}

static App_Storage_Result App_Storage_Verify_Metadata_Record(
    uint32_t addr, const App_Storage_Blackbox_Metadata *expected)
{
    App_Storage_Blackbox_Metadata actual;

    memset(&actual, 0, sizeof(actual));
    if (W25Q_Driver_Read(addr, (uint8_t *)&actual, sizeof(actual)) !=
        W25Q_DRIVER_RESULT_OK) return APP_STORAGE_RESULT_READ_FAILED;
    if (App_Storage_Is_Metadata_Valid(&actual) == 0U ||
        memcmp(&actual, expected, sizeof(actual)) != 0) {
        return APP_STORAGE_RESULT_VERIFY_FAILED;
    }
    return APP_STORAGE_RESULT_OK;
}

static void App_Storage_Metadata_Task(void)
{
    App_Storage_Blackbox_Metadata candidate;
    App_Storage_Result result;
    uint32_t target_base;
    uint32_t target_addr;
    uint32_t now;
    uint8_t switch_sector;

    if (s_metadata_checkpoint_pending == 0U) return;
    now = Sys_Timer_Get_Tick();
    if (s_metadata_attempted != 0U &&
        (uint32_t)(now - s_metadata_last_attempt) <
        APP_STORAGE_METADATA_RETRY_MS) return;
    s_metadata_attempted = 1U;
    s_metadata_last_attempt = now;
    if (W25Q_Driver_Is_Available() == 0U) return;

    App_Storage_Prepare_Metadata_Record(&candidate);
    switch_sector = (s_blackbox_v2_ready == 0U ||
                     s_metadata_next_addr + sizeof(candidate) >
                     s_metadata_active_base + W25Q_SECTOR_SIZE) ? 1U : 0U;
    target_base = s_metadata_active_base;
    target_addr = s_metadata_next_addr;

    if (switch_sector != 0U) {
        if (s_blackbox_v2_ready == 0U) {
            target_base = APP_STORAGE_META_A_ADDR;
        } else {
            target_base = (s_metadata_active_base == APP_STORAGE_META_A_ADDR) ?
                          APP_STORAGE_META_B_ADDR : APP_STORAGE_META_A_ADDR;
        }
        target_addr = target_base;
        if (W25Q_Driver_Erase_Sector(target_base) !=
            W25Q_DRIVER_RESULT_OK) {
            s_storage_last_result = APP_STORAGE_RESULT_ERASE_FAILED;
            return;
        }
    }

    if (W25Q_Driver_Write(target_addr, (const uint8_t *)&candidate,
                          sizeof(candidate)) != W25Q_DRIVER_RESULT_OK) {
        s_storage_last_result = APP_STORAGE_RESULT_WRITE_FAILED;
        return;
    }
    result = App_Storage_Verify_Metadata_Record(target_addr, &candidate);
    s_storage_last_result = result;
    if (result == APP_STORAGE_RESULT_OK) {
        s_blackbox_metadata = candidate;
        s_blackbox_v2_ready = 1U;
        s_metadata_active_base = target_base;
        s_metadata_next_addr = target_addr + sizeof(candidate);
        s_metadata_saved_entry_count = candidate.entry_count;
        s_metadata_checkpoint_pending = 0U;
        s_metadata_attempted = 0U;
    }
}

static uint32_t App_Storage_Log_Count_Valid_In_Sector(uint32_t sector_base)
{
    uint32_t write_slot;
    uint32_t oldest_slot;
    uint32_t sector_slot;
    uint32_t distance;
    uint32_t i;
    uint32_t count;

    if (s_blackbox_metadata.entry_count == 0U) return 0U;
    write_slot = App_Storage_Log_Address_To_Slot(
        App_Storage_Log_Normalize_Address(s_blackbox_metadata.write_addr));
    oldest_slot = (write_slot + APP_STORAGE_LOG_CAPACITY -
                   s_blackbox_metadata.entry_count) % APP_STORAGE_LOG_CAPACITY;
    sector_slot = App_Storage_Log_Address_To_Slot(sector_base);
    count = 0U;
    for (i = 0U; i < APP_STORAGE_LOG_ENTRIES_PER_SECTOR; i++) {
        distance = ((sector_slot + i) + APP_STORAGE_LOG_CAPACITY -
                    oldest_slot) % APP_STORAGE_LOG_CAPACITY;
        if (distance < s_blackbox_metadata.entry_count) count++;
    }
    return count;
}

static uint8_t App_Storage_Log_Prepare_Sector(uint32_t sector_base,
                                              uint32_t current_base)
{
    uint32_t valid_count;
    uint32_t write_slot;
    uint32_t oldest_slot;
    uint32_t sector_slot;

    valid_count = App_Storage_Log_Count_Valid_In_Sector(sector_base);
    if (valid_count != 0U) {
        write_slot = App_Storage_Log_Address_To_Slot(
            App_Storage_Log_Normalize_Address(s_blackbox_metadata.write_addr));
        oldest_slot = (write_slot + APP_STORAGE_LOG_CAPACITY -
                       s_blackbox_metadata.entry_count) % APP_STORAGE_LOG_CAPACITY;
        sector_slot = App_Storage_Log_Address_To_Slot(sector_base);
        if (sector_slot != oldest_slot) return 0U;
    }

    if (W25Q_Driver_Erase_Sector(sector_base) != W25Q_DRIVER_RESULT_OK) {
        return 0U;
    }
    if (valid_count > s_blackbox_metadata.entry_count) {
        s_blackbox_metadata.entry_count = 0U;
    } else {
        s_blackbox_metadata.entry_count -= valid_count;
    }
    s_metadata_saved_entry_count = s_blackbox_metadata.entry_count;
    s_metadata_checkpoint_pending = 1U;
    App_Storage_Log_Mark_Sector_Prepared(sector_base, current_base);
    return 1U;
}

static void App_Storage_Log_Maintenance_Task(void)
{
    uint32_t now;
    uint32_t addr;
    uint32_t current_base;
    uint32_t next_base;
    uint32_t offset;

    if (s_blackbox_v2_ready == 0U ||
        W25Q_Driver_Is_Available() == 0U) return;
    now = Sys_Timer_Get_Tick();
    if ((uint32_t)(now - s_log_maintenance_last) <
        APP_STORAGE_LOG_MAINTENANCE_MS) return;
    s_log_maintenance_last = now;

    addr = App_Storage_Log_Normalize_Address(s_blackbox_metadata.write_addr);
    s_blackbox_metadata.write_addr = addr;
    current_base = App_Storage_Log_Sector_Base(addr);
    offset = addr - current_base;

    if (App_Storage_Log_Is_Sector_Prepared(current_base) == 0U) {
        if (App_Storage_Log_Is_Target_Erased(addr) != 0U && offset != 0U) {
            App_Storage_Log_Mark_Sector_Prepared(current_base, current_base);
        } else if (offset != 0U) {
            next_base = current_base + W25Q_SECTOR_SIZE;
            if (next_base >= APP_STORAGE_FAULT_START_ADDR) {
                next_base = APP_STORAGE_LOG_START_ADDR;
            }
            s_blackbox_metadata.write_addr = next_base;
            s_blackbox_metadata.dropped_count++;
            s_metadata_checkpoint_pending = 1U;
            return;
        } else {
            (void)App_Storage_Log_Prepare_Sector(current_base, current_base);
            return;
        }
    }

    next_base = current_base + W25Q_SECTOR_SIZE;
    if (next_base >= APP_STORAGE_FAULT_START_ADDR) {
        next_base = APP_STORAGE_LOG_START_ADDR;
    }
    if (App_Storage_Log_Is_Sector_Prepared(next_base) == 0U) {
        (void)App_Storage_Log_Prepare_Sector(next_base, current_base);
    }
}

static void App_Storage_Recover_Log_Tail(void)
{
    App_Storage_Log_Entry entry;
    uint32_t addr;
    uint32_t next_addr;
    uint32_t recovered;

    recovered = 0U;
    while (recovered < (APP_STORAGE_METADATA_INTERVAL - 1U)) {
        addr = App_Storage_Log_Normalize_Address(
            s_blackbox_metadata.write_addr);
        memset(&entry, 0, sizeof(entry));
        if (W25Q_Driver_Read(addr, (uint8_t *)&entry, sizeof(entry)) !=
            W25Q_DRIVER_RESULT_OK) break;
        if (App_Storage_Is_Erased((const uint8_t *)&entry,
                                  sizeof(entry)) != 0U) break;
        if (entry.crc8 != Checksum_CRC8((const uint8_t *)&entry, 11U)) break;
        next_addr = App_Storage_Log_Advance_Address(addr);
        if (next_addr < addr) s_blackbox_metadata.wrap_count++;
        s_blackbox_metadata.write_addr = next_addr;
        if (s_blackbox_metadata.entry_count < APP_STORAGE_LOG_CAPACITY) {
            s_blackbox_metadata.entry_count++;
        }
        recovered++;
    }
    if (recovered != 0U) s_metadata_checkpoint_pending = 1U;
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
    s_metadata_active_base = APP_STORAGE_META_A_ADDR;
    s_metadata_next_addr = APP_STORAGE_META_A_ADDR;
    s_metadata_saved_entry_count = 0U;
    s_metadata_checkpoint_pending = 1U;
    s_metadata_attempted = 0U;
    s_log_prepared_sectors[0] = APP_STORAGE_INVALID_ADDR;
    s_log_prepared_sectors[1] = APP_STORAGE_INVALID_ADDR;
    s_log_maintenance_last = 0U;
}

void App_Storage_Init(void)
{
    App_Storage_Blackbox_Metadata metadata_a;
    App_Storage_Blackbox_Metadata metadata_b;
    uint32_t next_a;
    uint32_t next_b;
    uint8_t valid_a;
    uint8_t valid_b;

    App_Storage_Reset_Blackbox_V2();
    if (Checksum_Self_Test() == 0U ||
        W25Q_Driver_Is_Available() == 0U) return;

    App_Storage_Scan_Metadata_Sector(APP_STORAGE_META_A_ADDR,
                                     &metadata_a, &valid_a, &next_a);
    App_Storage_Scan_Metadata_Sector(APP_STORAGE_META_B_ADDR,
                                     &metadata_b, &valid_b, &next_b);

    if (valid_a != 0U &&
        (valid_b == 0U || metadata_a.generation >= metadata_b.generation)) {
        s_blackbox_metadata = metadata_a;
        s_blackbox_v2_ready = 1U;
        s_metadata_active_base = APP_STORAGE_META_A_ADDR;
        s_metadata_next_addr = next_a;
    } else if (valid_b != 0U) {
        s_blackbox_metadata = metadata_b;
        s_blackbox_v2_ready = 1U;
        s_metadata_active_base = APP_STORAGE_META_B_ADDR;
        s_metadata_next_addr = next_b;
    }
    if (s_blackbox_v2_ready != 0U) {
        s_metadata_saved_entry_count = s_blackbox_metadata.entry_count;
        s_metadata_checkpoint_pending = 0U;
        App_Storage_Recover_Log_Tail();
    }
}
