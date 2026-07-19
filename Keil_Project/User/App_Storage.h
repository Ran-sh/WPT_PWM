/**
 ******************************************************************************
 * @file    User/App_Storage.h
 * @brief   应用存储层 — 参数双副本 + 黑匣子日志 (V5.0.1)
 * @note    底层全部委托 W25Q_Driver, 本层负责分区逻辑、CRC32/CRC8 校验、
 *          页面跨页保护、故障锁存
 ******************************************************************************
 */

#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "stm32f10x.h"

/* ══ 黑匣子日志结构 (14字节 紧凑二进制, 对齐设计文档 §3.4) ══ */
#define BLACKBOX_ENTRY_SIZE  14U
#define BLACKBOX_ENTRIES_PER_BLOCK  4680U  /* 64KB/14B */
#define BLACKBOX_LOCK_BLOCKS 4U            /* 故障锁存保护区块数 */

typedef struct {
    uint32_t timestamp;      /* Sys_Timer_Get_Tick() 4B */
    uint16_t v_ema;          /* 电压 EMA ×100      2B */
    uint16_t i_ema;          /* 电流 EMA ×1000     2B */
    uint16_t freq_hz;        /* 实际频率 Hz         2B */
    uint8_t  sys_state;      /* Sys_State 枚举      1B */
    uint8_t  crc8;           /* 前12B CRC8 校验     1B */
} Blackbox_Entry_Packed;     /* 共 12+1+1=14 */

/* ══ 参数配置结构 (252B 有效载荷, 对齐设计文档 §3.3) ══ */
#define CFG_MAGIC     0x57434647U  /* "WCFG" */
#define CFG_VERSION   1U

typedef struct {
    uint32_t magic;          /* 4B  Magic */
    uint32_t version;        /* 4B  版本 */
    /* WiFi 配网凭证 */
    char     ssid[32];       /* 32B SSID \0结尾 */
    char     password[64];   /* 64B WiFi 密码 */
    char     mqtt_key[64];   /* 64B MQTT API Key */
    /* 硬件校准 */
    float    adc_i_offset;   /* 4B  电流零点 */
    float    adc_v_gain;     /* 4B  电压增益 */
    int32_t  freq_trim_hz;  /* 4B  频率微调 */
    /* 系统偏好 */
    uint16_t default_freq;   /* 2B  默认频率 kHz */
    uint8_t  backlight;      /* 1B  [V4.5.2] 背光 1-100% */
    uint8_t  language;       /* 1B  语言 0=CN 1=EN */
    uint8_t  font_size;      /* 1B  [V4.5.2] 0=小 1=中 */
    uint8_t  letter_spacing; /* 1B  [V4.5.2] 0-3 px extra gap */
    uint8_t  color_preset;   /* 1B  [V4.5.2] 0-5 preset, 255=custom */
    uint16_t color_fg;       /* 2B  [V4.5.2] RGB565 foreground */
    uint16_t color_bg;       /* 2B  [V4.5.2] RGB565 background */
    /* 校验 */
    uint32_t crc32;          /* 4B  CRC32 (不含自身) */
} App_Storage_Config;         /* 总计 196B (V4.5.2实测); 远在 4KB 扇区/256B 页内 */

typedef enum {
    APP_STORAGE_RESULT_OK = 0,
    APP_STORAGE_RESULT_PENDING,
    APP_STORAGE_RESULT_INVALID_ARGUMENT,
    APP_STORAGE_RESULT_NO_DEVICE,
    APP_STORAGE_RESULT_READ_FAILED,
    APP_STORAGE_RESULT_ERASE_FAILED,
    APP_STORAGE_RESULT_WRITE_FAILED,
    APP_STORAGE_RESULT_VERIFY_FAILED
} App_Storage_Result;

/* ══ 公开接口 ══ */

/** @brief 上电初始化: 恢复黑匣子写指针 */
void App_Storage_Init(void);

/* ── 参数配置 (P3) ── */
/** @brief 上电加载配置: A→B→出厂默认 三级回退, 返回 0=用了默认 */
uint8_t App_Storage_Load_Config(App_Storage_Config *cfg);

/** @brief Copy configuration to RAM and request deferred verified persistence. */
void App_Storage_Request_Save_Config(const App_Storage_Config *cfg);

/** @brief Execute one pending verified save; call only from the IDLE scheduler. */
void App_Storage_Task(void);

/** @brief Return the latest configuration persistence result. */
App_Storage_Result App_Storage_Get_Last_Result(void);

/** @brief Return one while a configuration save is pending or retryable. */
uint8_t App_Storage_Is_Save_Pending(void);

/** @brief 挂起ADC校准保存请求，不在调用栈中擦写Flash */
void App_Storage_Request_Save_ADC_Calibration(float i_offset, float v_gain);
/** @brief 写入出厂安全默认值 (所有字段归零/安全值) */
void App_Storage_Write_Factory_Defaults(void);

/* ── 黑匣子日志 (P4) ── */
/** @brief 每 200ms 调用一次: 写一条 14B 紧凑日志 (带 CRC8 + 跨页保护)
 *  @note  SYS_STATE_IDLE/FAULT 时不写, 静默跳过 */
void Blackbox_Log_Tick(float v, float i, uint16_t freq, uint8_t state);

/** @brief 过流触发时调用: 锁存触发点前后各 5s (50条) 到保护区 */
void Blackbox_Lock_Fault_Snapshot(void);

/** @brief 读取黑匣子条数 (供网络层上传用) */
uint32_t Blackbox_Get_Entry_Count(void);

/** @brief 按序号读单条日志 (0=最旧), 返回值 1=有效 0=CRC坏 */
uint8_t Blackbox_Read_Entry(uint32_t index, Blackbox_Entry_Packed *out);

/* ── V4.5.2 Settings Convenience — includes letter_spacing ── */
/** @brief 加载设置参数到 Ui_Controller */
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* spacing, uint8_t* preset,
                                uint16_t* fg, uint16_t* bg);
/** @brief Request deferred persistence of UI settings. */
void App_Storage_Request_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                       uint8_t spacing, uint8_t preset,
                                       uint16_t fg, uint16_t bg);

#endif /* APP_STORAGE_H */
