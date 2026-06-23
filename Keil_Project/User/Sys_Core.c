/**
 ******************************************************************************
 * @file    User/Sys_Core.c
 * @brief   系统核心模块 — 实现 (V4.3.0: W25Q128 配置+黑匣子集成)
 * @note    V4.3.0r3: 字库回退片内 ROM, W25Q128 仅配置+黑匣子
 *          Sys_Post_Init 中加载 Flash 参数配置 (WiFi/校准/偏好)
 *          V4.2.0: 合并 8 个 Sys_* 文件为 2 个
 ******************************************************************************
 */

#include "Sys_Core.h"
#include "Pwm_Driver.h"
#include "Tft_Driver.h"
#include "Led_Driver.h"
#include "Buzzer_Driver.h"
#include "Adc_Driver.h"
#include "Key_Driver.h"
#include "Inverter_Control.h"
#include "Ui_Controller.h"
#include "Sys_Timer.h"
#include "App_Network.h"
#include "W25Q_Driver.h"
#include "App_Storage.h"

volatile Sys_State g_sys_state = SYS_STATE_INIT;

/* ── 全局配置实例 ── */
static App_Storage_Config s_sys_config;

/* ═══════════════════════════════════════════════════════════════
 *  1. 初始化 (原 Sys_Init.c)
 * ═══════════════════════════════════════════════════════════════ */

void Sys_Clamp_ESP(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);          /* RST=0 */

    gpio.GPIO_Pin   = GPIO_Pin_11;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_11);         /* CH_PD=0 */
}

void Sys_Hardware_Init(void)
{
    GPIO_InitTypeDef gpio;

    Pwm_Driver_Init();

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_10);         /* 初始关断 12V */

    Tft_Driver_Init();
    Led_Driver_Init();
    Buzzer_Driver_Init();
    Adc_Driver_Init();
    Key_Driver_Init();
}

void Sys_Startup_Screen(void)
{
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...",
                              TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);
}

void Sys_Post_Init(void)
{
    uint8_t cfg_valid;
    Sys_Timer_Init();
    Led_Driver_Set_System(1);

    /* V4.3.0: Flash 加载参数配置, 双副本 CRC32 闭锁回退 */
    cfg_valid = App_Storage_Load_Config(&s_sys_config);

    /* ADC 校准: Flash 优先 → 强制解锁冷启动自测算降级 (设计文档 §9.3) */
    if (cfg_valid && s_sys_config.adc_i_offset != 0.0f) {
        Adc_Driver_Set_Calibration(s_sys_config.adc_i_offset,
                                    s_sys_config.adc_v_gain,
                                    s_sys_config.freq_trim_hz);   /* Flash 固化直达 */
    } else {
        Adc_Driver_Force_Recalibrate();                           /* 强制解锁状态机 */
        Adc_Driver_Calibrate_Offset();                            /* 冷启动自测算 */
        s_sys_config.adc_i_offset = Adc_Driver_Get_Current_Offset();
    }

    /* 背光: 从配置加载 */
    if (s_sys_config.backlight > 0)
        Tft_Driver_Set_Backlight(s_sys_config.backlight);

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1000);
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

    App_Network_Start_Connect();
}

/* ═══════════════════════════════════════════════════════════════
 *  2. 安全监测 (原 Sys_Safety.c)
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_SAFETY_OVERCURRENT_A  5.0f
#define SYS_SAFETY_POWER_V        12.0f

static float  s_safety_ema_v = 0.0f, s_safety_ema_i = 0.0f;
static uint8_t s_safety_ema_ok = 0;

static void Sys_Safety_Update_EMA(void)
{
    float v = Adc_Driver_Get_Voltage();
    float c = Adc_Driver_Get_Current();
    if (s_safety_ema_ok) {
        s_safety_ema_v = s_safety_ema_v * 0.75f + v * 0.25f;
        s_safety_ema_i = s_safety_ema_i * 0.75f + c * 0.25f;
    } else {
        s_safety_ema_v = v;
        s_safety_ema_i = c;
        s_safety_ema_ok = 1;
    }
}

float Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }
float Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }

/**
 * @brief  重置过流 EMA 滤波缓存, 防止 FAULT 复位后 EMA 残留值立即重新触发过流
 * @note   FAULT 状态下 KEY0 单击复位时, Sys_Safety 持有的 EMA 电流值可能仍 > 5.0A,
 *         若不重置, Sys_Safety_Task 下一圈又会将 g_sys_state 拉回 FAULT, 造成"消除无效"。
 *         调用后将 EMA 重置为当前 ADC 原始值, 同时锁定新状态让 EMA 重新收敛。
 */
void Sys_Safety_Reset_EMA(void)
{
    s_safety_ema_i  = 0.0f;
    s_safety_ema_ok = 0;  /* 下一轮 Update_EMA 将从原始 ADC 值重新开始 */
    /* 电压 EMA 保持不变 — 电压值不受过流复位影响 */
}

void Sys_Safety_Task(void)
{
    /* 仅 RUNNING 状态执行安全监测: 非运行状态 PWM 已关, 无过流可能 */
    if (g_sys_state != SYS_STATE_RUNNING) return;
    Sys_Safety_Update_EMA();

    /* PB10 电源控制 */
    {
        static uint8_t s_last_pwr = 0xFF;
        uint8_t pwr_on = (Adc_Driver_Get_Voltage() > SYS_SAFETY_POWER_V);
        if (pwr_on != s_last_pwr) {
            s_last_pwr = pwr_on;
            if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_10);
            else        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        }
    }

    /* 过流检测 → FAULT (先切状态再锁存: L4 禁擦需要 FAULT 而非 RUNNING) */
    if (s_safety_ema_i > SYS_SAFETY_OVERCURRENT_A) {
        Inverter_Control_Soft_Start_Fault();
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
        g_sys_state = SYS_STATE_FAULT;               /* 先切状态, Inverter已停波 */
        Blackbox_Lock_Fault_Snapshot();              /* L4 放行: 非 SWEEP/RUNNING */
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  3. 运行调度 (原 Sys_Run.c)
 * ═══════════════════════════════════════════════════════════════ */

static void Sys_Run_Led_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 200) {
        last = Sys_Timer_Get_Tick();
        Led_Driver_Task();
    }
}

static void Sys_Run_Buzzer_Tick(void)
{
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= 50) {
        last = Sys_Timer_Get_Tick();
        Buzzer_Driver_Task();
    }
}

void Sys_Run_Idle(void)
{
    Ui_Controller_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Sweep(void)
{
    static uint32_t last_bb_s = 0; uint32_t now_s = Sys_Timer_Get_Tick();
    Ui_Controller_Task();
    Inverter_Control_Soft_Start_Task();
    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE)
        g_sys_state = SYS_STATE_RUNNING;

    /* V4.3.0: 黑匣子 200ms 日志 (SWEEP 也记录) */
    if (now_s - last_bb_s >= 200) { last_bb_s = now_s;
        Blackbox_Log_Tick(Sys_Safety_Get_EMA_Voltage(), Sys_Safety_Get_EMA_Current(),
                          Pwm_Driver_Get_Frequency(), (uint8_t)g_sys_state);
    }
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Running(void)
{
    static uint32_t last_bb = 0; uint32_t now = Sys_Timer_Get_Tick();
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Task();

    /* V4.3.0: 黑匣子 200ms 日志 (仅 RUNNING) */
    if (now - last_bb >= 200) { last_bb = now;
        Blackbox_Log_Tick(Sys_Safety_Get_EMA_Voltage(), Sys_Safety_Get_EMA_Current(),
                          Pwm_Driver_Get_Frequency(), (uint8_t)g_sys_state);
    }
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}

void Sys_Run_Fault(void)
{
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Cancel();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
}
