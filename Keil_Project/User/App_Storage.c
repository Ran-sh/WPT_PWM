/**
 ******************************************************************************
 * @file    User/App_Storage.c
 * @brief   参数双副本与黑匣子日志应用存储层 — V5.0.2
 *
 *  W25Q128分区布局（总容量16MB）:
 *  +------------------------------------------------------------+
 *  |    W25Q128通过SPI1访问，PB12控制片选                       |
 *  |                                                             |
 *  |    [0x300000~0x300FFF] 参数副本甲，4KB                      |
 *  |      保存无线凭证、采样校准值和界面偏好                    |
 *  |      使用三十二位校验和双副本轮换抵抗掉电                  |
 *  |                                                             |
 *  |    [0x301000~0x301FFF] 参数副本乙，4KB                      |
 *  |      与副本甲交替写入，任一副本有效即可恢复                |
 *  |                                                             |
 *  |    [0x310000~0x311FFF] 双扇区元数据日志                     |
 *  |    [0x312000~0x6CFFFF] 12字节循环日志，每200ms采样          |
 *  |      最多恢复326678条记录，可连续记录约18.1小时            |
 *  |    [0x6D0000~0x70FFFF] 64个故障槽，每槽4KB                 |
 *  |      每次故障保存触发前25点和触发后25点，共10秒数据        |
 *  |                                                             |
 *  |    底层驱动负责总线和擦写保护，本层负责分区、校验和恢复    |
 *  +------------------------------------------------------------+
 *
 * @note    所有擦写均在输出安全关闭后由空闲调度器分步执行。
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
#define APP_STORAGE_FAULT_PRE_SAMPLES   25U
#define APP_STORAGE_FAULT_POST_SAMPLES  25U
#define APP_STORAGE_FAULT_TOTAL_SAMPLES \
    (APP_STORAGE_FAULT_PRE_SAMPLES + APP_STORAGE_FAULT_POST_SAMPLES)
#define APP_STORAGE_FAULT_RETRY_MS 1000U

typedef enum {
    APP_STORAGE_FAULT_CAPTURE_ARMED = 0,
    APP_STORAGE_FAULT_CAPTURE_POST,
    APP_STORAGE_FAULT_CAPTURE_PERSIST_PENDING
} App_Storage_Fault_Capture_State;

/* 黑匣子运行时状态。 */
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
static App_Storage_Log_Entry
    s_fault_pre_ring[APP_STORAGE_FAULT_PRE_SAMPLES];
static App_Storage_Log_Entry
    s_fault_snapshot[APP_STORAGE_FAULT_TOTAL_SAMPLES];
static App_Storage_Log_Entry
    s_fault_verify_buffer[APP_STORAGE_FAULT_TOTAL_SAMPLES];
static App_Storage_Fault_Capture_State s_fault_capture_state =
    APP_STORAGE_FAULT_CAPTURE_ARMED;
static uint8_t s_fault_pre_write = 0U;
static uint8_t s_fault_pre_count = 0U;
static uint8_t s_fault_post_count = 0U;
static uint8_t s_fault_reason = 0U;
static uint32_t s_fault_trigger_timestamp = 0U;
static uint32_t s_fault_generation = 0U;
static uint8_t s_fault_persist_attempted = 0U;
static uint32_t s_fault_persist_last_attempt = 0U;
static App_Storage_Config s_pending_config;
static uint8_t s_config_save_pending = 0U;
static uint8_t s_config_save_attempted = 0U;
static uint32_t s_config_save_last_attempt = 0U;
static App_Storage_Result s_storage_last_result = APP_STORAGE_RESULT_OK;

static void App_Storage_Metadata_Task(void);
static void App_Storage_Log_Maintenance_Task(void);
static void App_Storage_Recover_Fault_Slots(void);
static uint8_t App_Storage_Is_Erased(const uint8_t *data, uint32_t len);

/* ===============================================
 *  参数配置：双副本与三十二位校验闭锁
 * =============================================== */

/* 第一版固定196字节布局，仅用于读取并迁移历史配置。 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    char     ssid[32];
    char     password[64];
    char     mqtt_key[64];
    float    adc_i_offset;
    float    adc_v_gain;
    int32_t  freq_trim_hz;
    uint16_t default_freq;
    uint8_t  backlight;
    uint8_t  language;
    uint8_t  font_size;
    uint8_t  letter_spacing;
    uint8_t  color_preset;
    uint16_t color_fg;
    uint16_t color_bg;
    uint32_t crc32;
} App_Storage_Config_V1;

typedef char App_Storage_Config_V1_Size_Check[
    (sizeof(App_Storage_Config_V1) == 196U) ? 1 : -1];
typedef char App_Storage_Config_Size_Check[
    (sizeof(App_Storage_Config) <= 256U) ? 1 : -1];

static void App_Storage_Set_Frequency_Defaults(App_Storage_Config *cfg)
{
    cfg->startup_low_freq_hz = 20000U;
    cfg->startup_high_freq_hz = 100000U;
    cfg->startup_freq_band = APP_STORAGE_FREQ_BAND_HIGH;
    cfg->menu_cursor_icon = 0U;
    cfg->reserved_v2 = 0U;
}

static uint8_t App_Storage_Is_Frequency_Config_Valid(
    const App_Storage_Config *cfg)
{
    if (cfg->startup_low_freq_hz < 20000U ||
        cfg->startup_low_freq_hz > 99900U ||
        (cfg->startup_low_freq_hz % 100U) != 0U) return 0U;
    if (cfg->startup_high_freq_hz < 100000U ||
        cfg->startup_high_freq_hz > 200000U ||
        (cfg->startup_high_freq_hz % 1000U) != 0U) return 0U;
    if (cfg->startup_freq_band > APP_STORAGE_FREQ_BAND_HIGH) return 0U;
    if (cfg->menu_cursor_icon >= 8U) return 0U;
    return 1U;
}

static uint8_t App_Storage_Is_Config_V1_Valid(
    const App_Storage_Config_V1 *cfg)
{
    uint32_t computed_crc;

    if (cfg->magic != CFG_MAGIC || cfg->version != 1U) return 0U;
    computed_crc = Checksum_CRC32((const uint8_t *)cfg,
                                  sizeof(App_Storage_Config_V1) - 4U);
    return (cfg->crc32 == computed_crc) ? 1U : 0U;
}

static void App_Storage_Migrate_V1(App_Storage_Config *dst,
                                   const App_Storage_Config_V1 *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->magic = CFG_MAGIC;
    dst->version = CFG_VERSION;
    memcpy(dst->ssid, src->ssid, sizeof(dst->ssid));
    memcpy(dst->password, src->password, sizeof(dst->password));
    memcpy(dst->mqtt_key, src->mqtt_key, sizeof(dst->mqtt_key));
    dst->adc_i_offset = src->adc_i_offset;
    dst->adc_v_gain = src->adc_v_gain;
    dst->freq_trim_hz = src->freq_trim_hz;
    dst->default_freq = src->default_freq;
    dst->backlight = src->backlight;
    dst->language = src->language;
    dst->font_size = src->font_size;
    dst->letter_spacing = src->letter_spacing;
    dst->color_preset = src->color_preset;
    dst->color_fg = src->color_fg;
    dst->color_bg = src->color_bg;
    App_Storage_Set_Frequency_Defaults(dst);
    dst->crc32 = Checksum_CRC32((const uint8_t *)dst,
                                sizeof(App_Storage_Config) - 4U);
}

/** @brief 生成安全出厂默认值，背光兼容字段为开启，字符间距为0 */
static void App_Storage_Defaults(App_Storage_Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic     = CFG_MAGIC;
    cfg->version   = CFG_VERSION;
    cfg->adc_i_offset  = 0.0f;
    cfg->adc_v_gain    = 1.0f;
    cfg->freq_trim_hz = 0;
    cfg->default_freq  = 100;      /* 100kHz安全中间频率。 */
    cfg->backlight     = 100;      /* 历史兼容值；PA12现在只支持亮灭。 */
    cfg->language      = 1;        /* 默认英文，切换中文后才依赖外部字库。 */
    cfg->font_size     = 0;        /* 历史字体字段，仅用于配置兼容。 */
    cfg->letter_spacing = 0;       /* 默认不增加字符间距。 */
    cfg->color_preset  = 0;        /* 默认使用经典配色。 */
    cfg->color_fg      = 0xFFFF;
    cfg->color_bg      = 0x0000;
    App_Storage_Set_Frequency_Defaults(cfg);
}

static uint8_t App_Storage_Is_Config_Valid(const App_Storage_Config *cfg)
{
    uint32_t computed_crc;

    if (cfg->magic != CFG_MAGIC || cfg->version != CFG_VERSION ||
        App_Storage_Is_Frequency_Config_Valid(cfg) == 0U) return 0U;
    computed_crc = Checksum_CRC32((const uint8_t *)cfg,
                                  sizeof(App_Storage_Config) - 4U);
    return (cfg->crc32 == computed_crc) ? 1U : 0U;
}

uint8_t App_Storage_Load_Config(App_Storage_Config *cfg)
{
    App_Storage_Config_V1 cfg_v1;

    if (cfg == 0) return 0U;

    memset(cfg, 0, sizeof(*cfg));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_A, (uint8_t *)cfg,
                         sizeof(*cfg)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_Valid(cfg) != 0U) return 1U;

    memset(cfg, 0, sizeof(*cfg));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_B, (uint8_t *)cfg,
                         sizeof(*cfg)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_Valid(cfg) != 0U) return 1U;

    memset(&cfg_v1, 0, sizeof(cfg_v1));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_A, (uint8_t *)&cfg_v1,
                         sizeof(cfg_v1)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_V1_Valid(&cfg_v1) != 0U) {
        App_Storage_Migrate_V1(cfg, &cfg_v1);
        return 1U;
    }

    memset(&cfg_v1, 0, sizeof(cfg_v1));
    if (W25Q_Driver_Read(W25Q_ADDR_CFG_B, (uint8_t *)&cfg_v1,
                         sizeof(cfg_v1)) == W25Q_DRIVER_RESULT_OK &&
        App_Storage_Is_Config_V1_Valid(&cfg_v1) != 0U) {
        App_Storage_Migrate_V1(cfg, &cfg_v1);
        return 1U;
    }

    App_Storage_Defaults(cfg);
    return 0U;
}

void App_Storage_Request_Save_Config(const App_Storage_Config *cfg)
{
    if (cfg == 0 || App_Storage_Is_Frequency_Config_Valid(cfg) == 0U) {
        s_storage_last_result = APP_STORAGE_RESULT_INVALID_ARGUMENT;
        return;
    }

    s_pending_config = *cfg;
    s_pending_config.magic = CFG_MAGIC;
    s_pending_config.version = CFG_VERSION;
    s_pending_config.reserved_v2 = 0U;
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

/* ===============================================
 *  界面设置便捷接口
 * =============================================== */
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* spacing, uint8_t* preset,
                                uint16_t* fg, uint16_t* bg,
                                uint32_t* low_freq_hz,
                                uint32_t* high_freq_hz,
                                uint8_t* freq_band,
                                uint8_t* cursor_icon)
{
    App_Storage_Config cfg;

    (void)App_Storage_Load_Config(&cfg);
    *lang         = cfg.language;
    *font         = cfg.font_size;
    *bl           = 100U;
    *spacing      = cfg.letter_spacing;
    *preset       = cfg.color_preset;
    *fg           = cfg.color_fg;
    *bg           = cfg.color_bg;
    *low_freq_hz  = cfg.startup_low_freq_hz;
    *high_freq_hz = cfg.startup_high_freq_hz;
    *freq_band    = cfg.startup_freq_band;
    *cursor_icon  = cfg.menu_cursor_icon;
}

void App_Storage_Request_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                       uint8_t spacing, uint8_t preset,
                                       uint16_t fg, uint16_t bg,
                                       uint32_t low_freq_hz,
                                       uint32_t high_freq_hz,
                                       uint8_t freq_band,
                                       uint8_t cursor_icon)
{
    App_Storage_Config cfg;

    (void)bl;
    if (s_config_save_pending != 0U) {
        cfg = s_pending_config;
    } else if (App_Storage_Load_Config(&cfg) == 0U) {
        App_Storage_Defaults(&cfg);
    }
    cfg.startup_low_freq_hz = low_freq_hz;
    cfg.startup_high_freq_hz = high_freq_hz;
    cfg.startup_freq_band = freq_band;
    cfg.menu_cursor_icon = cursor_icon;
    cfg.reserved_v2 = 0U;
    if (App_Storage_Is_Frequency_Config_Valid(&cfg) == 0U) return;
    cfg.language       = lang;
    cfg.font_size      = font;
    cfg.backlight      = 100U;
    cfg.letter_spacing = spacing;
    cfg.color_preset   = preset;
    cfg.color_fg       = fg;
    cfg.color_bg       = bg;
    App_Storage_Request_Save_Config(&cfg);
}

/* ===============================================
 *  黑匣子日志：12字节紧凑记录、八位校验、跨页保护和故障锁存
 * =============================================== */

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

/** @brief 把物理量压缩为一条固定12字节的第二版日志记录 */
static void App_Storage_Blackbox_Pack(float v, float i, uint32_t freq_hz, uint8_t state,
                          uint8_t sample_valid,
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
    if (freq_hz > 6553500UL) freq_hz = 6553500UL;
    out->frequency_100hz = (uint16_t)((freq_hz + 50U) / 100U);
    out->system_state = (uint8_t)(state &
        (uint8_t)(~APP_STORAGE_LOG_STATE_INVALID));
    if (sample_valid == 0U) {
        out->system_state |= APP_STORAGE_LOG_STATE_INVALID;
    }
    out->crc8 = Checksum_CRC8((const uint8_t *)out, 11U);
}

void Blackbox_Log_Tick(float v, float i, uint32_t freq_hz, uint8_t state)
{
    uint32_t addr;
    uint32_t next_addr;
    uint32_t sector_base;
    uint32_t next_sector_base;
    App_Storage_Log_Entry entry;
    W25Q_Driver_Result result;

    if (s_blackbox_v2_ready == 0U) return;

    App_Storage_Blackbox_Pack(v, i, freq_hz, state, 1U, &entry);
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

void Blackbox_Capture_Tick(float v, float i, uint32_t freq_hz,
                           uint8_t state, uint8_t sample_valid,
                           uint8_t pretrigger_eligible)
{
    App_Storage_Log_Entry entry;
    uint8_t target;

    App_Storage_Blackbox_Pack(v, i, freq_hz, state, sample_valid, &entry);
    if (s_fault_capture_state == APP_STORAGE_FAULT_CAPTURE_ARMED) {
        if (sample_valid == 0U || pretrigger_eligible == 0U) return;
        s_fault_pre_ring[s_fault_pre_write] = entry;
        s_fault_pre_write = (uint8_t)((s_fault_pre_write + 1U) %
                                      APP_STORAGE_FAULT_PRE_SAMPLES);
        if (s_fault_pre_count < APP_STORAGE_FAULT_PRE_SAMPLES) {
            s_fault_pre_count++;
        }
        return;
    }

    if (s_fault_capture_state != APP_STORAGE_FAULT_CAPTURE_POST) return;
    target = (uint8_t)(APP_STORAGE_FAULT_PRE_SAMPLES + s_fault_post_count);
    s_fault_snapshot[target] = entry;
    s_fault_post_count++;
    if (s_fault_post_count >= APP_STORAGE_FAULT_POST_SAMPLES) {
        s_fault_capture_state = APP_STORAGE_FAULT_CAPTURE_PERSIST_PENDING;
        s_fault_persist_attempted = 0U;
    }
}

void Blackbox_Reset_Pretrigger(void)
{
    if (s_fault_capture_state != APP_STORAGE_FAULT_CAPTURE_ARMED) return;
    memset(s_fault_pre_ring, 0, sizeof(s_fault_pre_ring));
    s_fault_pre_write = 0U;
    s_fault_pre_count = 0U;
}

void Blackbox_Lock_Fault_Snapshot(uint8_t fault_reason)
{
    App_Storage_Log_Entry invalid_entry;
    uint8_t oldest;
    uint8_t padding;
    uint8_t i;

    if (s_fault_capture_state != APP_STORAGE_FAULT_CAPTURE_ARMED) return;

    App_Storage_Blackbox_Pack(0.0f, 0.0f, 0U, 0U, 0U, &invalid_entry);
    invalid_entry.timestamp = 0U;
    invalid_entry.crc8 = Checksum_CRC8((const uint8_t *)&invalid_entry, 11U);
    for (i = 0U; i < APP_STORAGE_FAULT_PRE_SAMPLES; i++) {
        s_fault_snapshot[i] = invalid_entry;
    }

    oldest = (uint8_t)((s_fault_pre_write + APP_STORAGE_FAULT_PRE_SAMPLES -
                        s_fault_pre_count) % APP_STORAGE_FAULT_PRE_SAMPLES);
    padding = (uint8_t)(APP_STORAGE_FAULT_PRE_SAMPLES - s_fault_pre_count);
    for (i = 0U; i < s_fault_pre_count; i++) {
        s_fault_snapshot[padding + i] =
            s_fault_pre_ring[(oldest + i) % APP_STORAGE_FAULT_PRE_SAMPLES];
    }

    s_fault_reason = fault_reason;
    s_fault_trigger_timestamp = Sys_Timer_Get_Tick();
    s_fault_post_count = 0U;
    s_fault_capture_state = APP_STORAGE_FAULT_CAPTURE_POST;
}

static uint8_t App_Storage_Is_Fault_Header_Valid(
    const App_Storage_Fault_Header *header, uint32_t slot_base)
{
    uint32_t crc;

    if (header->magic != APP_STORAGE_FAULT_MAGIC ||
        header->version != APP_STORAGE_BLACKBOX_VERSION ||
        header->size != sizeof(*header) ||
        header->entry_count != APP_STORAGE_FAULT_TOTAL_SAMPLES ||
        header->pre_trigger_count != APP_STORAGE_FAULT_PRE_SAMPLES ||
        header->post_trigger_count != APP_STORAGE_FAULT_POST_SAMPLES ||
        header->data_addr != slot_base + sizeof(*header)) return 0U;
    crc = Checksum_CRC32((const uint8_t *)header, sizeof(*header) - 4U);
    return (crc == header->crc32) ? 1U : 0U;
}

static App_Storage_Result App_Storage_Verify_Fault_Snapshot(
    uint32_t slot_base, const App_Storage_Fault_Header *expected)
{
    App_Storage_Fault_Header actual;
    uint32_t data_size;

    data_size = sizeof(s_fault_snapshot);
    memset(&actual, 0, sizeof(actual));
    if (W25Q_Driver_Read(slot_base, (uint8_t *)&actual, sizeof(actual)) !=
        W25Q_DRIVER_RESULT_OK) return APP_STORAGE_RESULT_READ_FAILED;
    if (App_Storage_Is_Fault_Header_Valid(&actual, slot_base) == 0U ||
        memcmp(&actual, expected, sizeof(actual)) != 0) {
        return APP_STORAGE_RESULT_VERIFY_FAILED;
    }
    memset(s_fault_verify_buffer, 0, sizeof(s_fault_verify_buffer));
    if (W25Q_Driver_Read(actual.data_addr,
                         (uint8_t *)s_fault_verify_buffer, data_size) !=
        W25Q_DRIVER_RESULT_OK) return APP_STORAGE_RESULT_READ_FAILED;
    if (Checksum_CRC32((const uint8_t *)s_fault_verify_buffer, data_size) !=
            actual.data_crc32 ||
        memcmp(s_fault_verify_buffer, s_fault_snapshot, data_size) != 0) {
        return APP_STORAGE_RESULT_VERIFY_FAILED;
    }
    return APP_STORAGE_RESULT_OK;
}

void Blackbox_Fault_Persist_Task(uint8_t power_safe)
{
    App_Storage_Fault_Header header;
    App_Storage_Result result;
    uint32_t slot_base;
    uint32_t now;
    uint16_t slot;

    if (s_fault_capture_state !=
        APP_STORAGE_FAULT_CAPTURE_PERSIST_PENDING || power_safe == 0U) return;
    now = Sys_Timer_Get_Tick();
    if (s_fault_persist_attempted != 0U &&
        (uint32_t)(now - s_fault_persist_last_attempt) <
        APP_STORAGE_FAULT_RETRY_MS) return;
    s_fault_persist_attempted = 1U;
    s_fault_persist_last_attempt = now;
    if (s_blackbox_v2_ready == 0U ||
        W25Q_Driver_Is_Available() == 0U) {
        s_storage_last_result = APP_STORAGE_RESULT_NO_DEVICE;
        return;
    }

    slot = s_blackbox_metadata.next_fault_slot;
    if (slot >= APP_STORAGE_FAULT_SLOT_COUNT) slot = 0U;
    slot_base = APP_STORAGE_FAULT_START_ADDR +
                (uint32_t)slot * APP_STORAGE_FAULT_SLOT_SIZE;

    memset(&header, 0, sizeof(header));
    header.magic = APP_STORAGE_FAULT_MAGIC;
    header.version = APP_STORAGE_BLACKBOX_VERSION;
    header.size = sizeof(header);
    header.generation = s_fault_generation + 1U;
    header.trigger_timestamp = s_fault_trigger_timestamp;
    header.entry_count = APP_STORAGE_FAULT_TOTAL_SAMPLES;
    header.pre_trigger_count = APP_STORAGE_FAULT_PRE_SAMPLES;
    header.post_trigger_count = APP_STORAGE_FAULT_POST_SAMPLES;
    header.fault_reason = s_fault_reason;
    header.data_addr = slot_base + sizeof(header);
    header.data_crc32 = Checksum_CRC32(
        (const uint8_t *)s_fault_snapshot, sizeof(s_fault_snapshot));
    header.crc32 = Checksum_CRC32((const uint8_t *)&header,
                                  sizeof(header) - 4U);

    result = APP_STORAGE_RESULT_ERASE_FAILED;
    if (W25Q_Driver_Erase_Sector(slot_base) == W25Q_DRIVER_RESULT_OK) {
        result = APP_STORAGE_RESULT_WRITE_FAILED;
        if (W25Q_Driver_Write(header.data_addr,
                              (const uint8_t *)s_fault_snapshot,
                              sizeof(s_fault_snapshot)) ==
            W25Q_DRIVER_RESULT_OK &&
            W25Q_Driver_Write(slot_base, (const uint8_t *)&header,
                              sizeof(header)) == W25Q_DRIVER_RESULT_OK) {
            result = App_Storage_Verify_Fault_Snapshot(slot_base, &header);
        }
    }
    s_storage_last_result = result;
    if (result == APP_STORAGE_RESULT_OK) {
        s_fault_generation = header.generation;
        s_blackbox_metadata.next_fault_slot =
            (uint16_t)((slot + 1U) % APP_STORAGE_FAULT_SLOT_COUNT);
        s_metadata_checkpoint_pending = 1U;
        s_metadata_attempted = 0U;
        s_fault_capture_state = APP_STORAGE_FAULT_CAPTURE_ARMED;
        s_fault_pre_write = 0U;
        s_fault_pre_count = 0U;
        s_fault_post_count = 0U;
        s_fault_persist_attempted = 0U;
    }
}

static uint8_t App_Storage_Is_Fault_Slot_Committed(
    uint32_t slot_base, App_Storage_Fault_Header *header)
{
    uint32_t data_size;

    memset(header, 0, sizeof(*header));
    if (W25Q_Driver_Read(slot_base, (uint8_t *)header, sizeof(*header)) !=
        W25Q_DRIVER_RESULT_OK) return 0U;
    if (App_Storage_Is_Fault_Header_Valid(header, slot_base) == 0U) return 0U;
    data_size = (uint32_t)header->entry_count * BLACKBOX_ENTRY_SIZE;
    if (data_size != sizeof(s_fault_verify_buffer)) return 0U;
    if (W25Q_Driver_Read(header->data_addr,
                         (uint8_t *)s_fault_verify_buffer, data_size) !=
        W25Q_DRIVER_RESULT_OK) return 0U;
    return (Checksum_CRC32((const uint8_t *)s_fault_verify_buffer, data_size) ==
            header->data_crc32) ? 1U : 0U;
}

static void App_Storage_Recover_Fault_Slots(void)
{
    App_Storage_Fault_Header header;
    uint32_t slot_base;
    uint16_t latest_slot;
    uint16_t slot;
    uint8_t found;

    found = 0U;
    latest_slot = 0U;
    s_fault_generation = 0U;
    for (slot = 0U; slot < APP_STORAGE_FAULT_SLOT_COUNT; slot++) {
        slot_base = APP_STORAGE_FAULT_START_ADDR +
                    (uint32_t)slot * APP_STORAGE_FAULT_SLOT_SIZE;
        if (App_Storage_Is_Fault_Slot_Committed(slot_base, &header) != 0U &&
            (found == 0U || header.generation > s_fault_generation)) {
            found = 1U;
            latest_slot = slot;
            s_fault_generation = header.generation;
        }
    }

    if (found != 0U) {
        slot = (uint16_t)((latest_slot + 1U) %
                          APP_STORAGE_FAULT_SLOT_COUNT);
        if (s_blackbox_metadata.next_fault_slot != slot) {
            s_blackbox_metadata.next_fault_slot = slot;
            s_metadata_checkpoint_pending = 1U;
            s_metadata_attempted = 0U;
        }
    }
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

/* ===============================================
 *  上电自检，仅在系统初始化状态调用一次，约耗时200ms
 * =============================================== */

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
    memset(s_fault_pre_ring, 0, sizeof(s_fault_pre_ring));
    memset(s_fault_snapshot, 0, sizeof(s_fault_snapshot));
    memset(s_fault_verify_buffer, 0, sizeof(s_fault_verify_buffer));
    s_fault_capture_state = APP_STORAGE_FAULT_CAPTURE_ARMED;
    s_fault_pre_write = 0U;
    s_fault_pre_count = 0U;
    s_fault_post_count = 0U;
    s_fault_reason = 0U;
    s_fault_trigger_timestamp = 0U;
    s_fault_generation = 0U;
    s_fault_persist_attempted = 0U;
    s_fault_persist_last_attempt = 0U;
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
    App_Storage_Recover_Fault_Slots();
}
