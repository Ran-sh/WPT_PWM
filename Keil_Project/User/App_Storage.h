#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "stm32f10x.h"

/* 第二版黑匣子分区；参数双副本仍固定在0x300000和0x301000。 */
#define APP_STORAGE_META_A_ADDR       0x310000U
#define APP_STORAGE_META_B_ADDR       0x311000U
#define APP_STORAGE_LOG_START_ADDR    0x312000U
#define APP_STORAGE_FAULT_START_ADDR  0x6D0000U
#define APP_STORAGE_BLACK_END_ADDR    0x710000U
#define APP_STORAGE_FAULT_SLOT_SIZE   4096U
#define APP_STORAGE_FAULT_SLOT_COUNT  64U

#define APP_STORAGE_BLACKBOX_MAGIC    0x32424257UL
#define APP_STORAGE_FAULT_MAGIC       0x32544657UL
#define APP_STORAGE_BLACKBOX_VERSION  2U
#define BLACKBOX_ENTRY_SIZE           12U
#define APP_STORAGE_LOG_STATE_INVALID 0x80U

typedef struct {
    uint32_t timestamp;
    uint16_t voltage_x100;
    uint16_t current_x1000;
    uint16_t frequency_100hz;
    uint8_t  system_state;
    uint8_t  crc8;
} App_Storage_Log_Entry;

typedef char App_Storage_Log_Size_Check[
    (sizeof(App_Storage_Log_Entry) == 12U) ? 1 : -1];

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    uint32_t write_addr;
    uint32_t entry_count;
    uint32_t wrap_count;
    uint16_t next_fault_slot;
    uint16_t reserved16;
    uint32_t dropped_count;
    uint32_t reserved[2];
    uint32_t crc32;
} App_Storage_Blackbox_Metadata;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    uint32_t trigger_timestamp;
    uint16_t entry_count;
    uint16_t pre_trigger_count;
    uint16_t post_trigger_count;
    uint16_t fault_reason;
    uint32_t data_addr;
    uint32_t data_crc32;
    uint32_t reserved;
    uint32_t crc32;
} App_Storage_Fault_Header;

/* 参数配置结构，共208字节，不跨越256字节写入页。 */
#define CFG_MAGIC     0x57434647U  /* 参数区固定识别标记 */
#define CFG_VERSION   2U

#define APP_STORAGE_FREQ_BAND_LOW   0U
#define APP_STORAGE_FREQ_BAND_HIGH  1U

typedef struct {
    uint32_t magic;          /* 4字节固定识别标记 */
    uint32_t version;        /* 4字节结构版本 */
    /* 无线配网凭证。 */
    char     ssid[32];       /* 32字节无线网络名称，以\0结尾 */
    char     password[64];   /* 64字节无线网络密码 */
    char     mqtt_key[64];   /* 64字节消息平台访问密钥 */
    /* 硬件校准 */
    float    adc_i_offset;   /* 4字节电流零点 */
    float    adc_v_gain;     /* 4字节电压增益 */
    int32_t  freq_trim_hz;  /* 4字节频率微调值 */
    /* 系统偏好 */
    uint16_t default_freq;   /* 2字节默认频率，单位为kHz */
    uint8_t  backlight;      /* 1字节历史兼容字段，固定为100 */
    uint8_t  language;       /* 1字节语言选项，0为中文，1为英文 */
    uint8_t  font_size;      /* 1字节历史字体兼容字段 */
    uint8_t  letter_spacing; /* 1字节字符间距选项，范围0至3 */
    uint8_t  color_preset;   /* 1字节配色预设，0至5有效，255为自定义 */
    uint16_t color_fg;       /* 2字节RGB565前景色 */
    uint16_t color_bg;       /* 2字节RGB565背景色 */
    uint32_t startup_low_freq_hz;
    uint32_t startup_high_freq_hz;
    uint8_t  startup_freq_band;
    uint8_t  menu_cursor_icon;
    uint16_t reserved_v2;
    /* 校验 */
    uint32_t crc32;          /* 4字节校验值，计算时不包含自身 */
} App_Storage_Config;         /* 总计208字节，完整位于一个256字节写入页内 */

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

/* 公开接口。 */

/** @brief 上电初始化并恢复黑匣子写入位置 */
void App_Storage_Init(void);

/* 参数配置。 */
/** @brief 上电加载配置，按副本甲、副本乙和出厂值三级回退
 *  @retval 1表示至少一个存储副本有效，0表示使用出厂默认值
 */
uint8_t App_Storage_Load_Config(App_Storage_Config *cfg);

/** @brief 把配置复制到内存，并请求后台执行带校验的持久化 */
void App_Storage_Request_Save_Config(const App_Storage_Config *cfg);

/** @brief 执行一次待处理的校验写入，只允许由空闲状态调度器调用 */
void App_Storage_Task(void);

/** @brief 获取最近一次配置持久化结果 */
App_Storage_Result App_Storage_Get_Last_Result(void);

/** @brief 判断配置保存是否仍在等待执行或允许重试 */
uint8_t App_Storage_Is_Save_Pending(void);

/** @brief 请求在下一次空闲任务中持久化黑匣子元数据检查点 */
void App_Storage_Request_Blackbox_Checkpoint(void);

/** @brief 挂起采样校准保存请求，不在当前调用栈中擦写外部存储器 */
void App_Storage_Request_Save_ADC_Calibration(float i_offset, float v_gain);
/** @brief 写入出厂安全默认值 (所有字段归零/安全值) */
void App_Storage_Write_Factory_Defaults(void);

/* 黑匣子日志。 */
/** @brief 每200ms写入一条12字节紧凑日志，并执行八位校验和跨页保护
 *  @note  系统处于空闲或故障状态时不写入。
 */
void Blackbox_Log_Tick(float v, float i, uint32_t freq_hz, uint8_t state);

/** @brief 向故障前后采样状态机送入一条200ms内存样本
 *  @param v 电压样本
 *  @param i 电流样本
 *  @param freq PWM频率，单位为Hz
 *  @param state 系统状态，第7位保留为无效标记
 *  @param sample_valid 1表示采样新鲜有效，0表示采样失效
 *  @param pretrigger_eligible 仅在扫频或运行状态传入1
 */
void Blackbox_Capture_Tick(float v, float i, uint32_t freq_hz, uint8_t state,
                           uint8_t sample_valid,
                           uint8_t pretrigger_eligible);

/** @brief 在新一轮扫频开始前清除过期的故障前样本 */
void Blackbox_Reset_Pretrigger(void);

/** @brief 冻结故障前环形缓冲，并开始采集故障后5秒数据
 *  @param fault_reason 首次锁存的系统故障码
 *  @note 本函数只更新内存状态，不擦除或写入外部存储器。
 */
void Blackbox_Lock_Fault_Snapshot(uint8_t fault_reason);

/** @brief 在确认功率输出关闭后持久化一份完整故障快照
 *  @param power_safe 只有TIM1 PWM和PB10均关闭时才传入1
 */
void Blackbox_Fault_Persist_Task(uint8_t power_safe);

/** @brief 获取当前有效黑匣子日志条数，供网络层上传 */
uint32_t Blackbox_Get_Entry_Count(void);

/** @brief 按顺序读取单条日志，序号0表示最旧记录
 *  @retval 1表示记录和校验值有效，0表示读取或校验失败
 */
uint8_t Blackbox_Read_Entry(uint32_t index, App_Storage_Log_Entry *out);

/* 界面设置便捷接口，包括字符间距字段。 */
/** @brief 加载供界面控制器使用的设置参数 */
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* spacing, uint8_t* preset,
                                uint16_t* fg, uint16_t* bg,
                                uint32_t* low_freq_hz,
                                uint32_t* high_freq_hz,
                                uint8_t* freq_band,
                                uint8_t* cursor_icon);
/** @brief 请求后台持久化界面设置 */
void App_Storage_Request_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                       uint8_t spacing, uint8_t preset,
                                       uint16_t fg, uint16_t bg,
                                       uint32_t low_freq_hz,
                                       uint32_t high_freq_hz,
                                       uint8_t freq_band,
                                       uint8_t cursor_icon);

#endif /* 应用存储层接口结束 */
