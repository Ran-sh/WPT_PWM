/**
 ******************************************************************************
 * @file    User/App_Storage.h
 * @brief   应用存储层 — 参数双副本 + 黑匣子日志 (V4.3.2)
 * @note    底层全部委托 W25Q_Driver, 本层负责分区逻辑、CRC32/CRC8 校验、
 *          页面跨页保护、故障锁存
 ******************************************************************************
 */

#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "stm32f10x.h"
#include "Sys_Core.h"       /* Sys_State */

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
    uint8_t  backlight;      /* 1B  背光亮度 */
    uint8_t  language;       /* 1B  语言 */
    uint8_t  reserved[3];    /* 3B  对齐 */
    /* 校验 */
    uint32_t crc32;          /* 4B  CRC32 (不含自身) */
} App_Storage_Config;         /* 总计 252+4=256B */

/* ══ 公开接口 ══ */

/** @brief 上电初始化: 恢复黑匣子写指针 */
void App_Storage_Init(void);

/* ── CRC32 代数量具 — 暴露给 W25Q_Driver Font_Header_Load 复用 ── */
/** @brief CRC32 (多项式 0x04C11DB7, 含 final XOR, 与 WinRAR/zlib 一致) */
uint32_t CRC32_Compute(const uint8_t *data, uint32_t len);

/* ── 参数配置 (P3) ── */
/** @brief 上电加载配置: A→B→出厂默认 三级回退, 返回 0=用了默认 */
uint8_t App_Storage_Load_Config(App_Storage_Config *cfg);

/** @brief 保存配置: 写A→验A CRC→写B (回路上电时加载A优先) */
void App_Storage_Save_Config(const App_Storage_Config *cfg);

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

#endif /* APP_STORAGE_H */
