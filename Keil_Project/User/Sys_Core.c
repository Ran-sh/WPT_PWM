/**
 ******************************************************************************
 * @file    User/Sys_Core.c
 * @brief   系统核心模块 — V5.0.1
 *
 *  System-wide pin overview (all peripherals, hardware-exclusive pins):
 *  +------------------------------------------------------------+
 *  |                      STM32F103C8T6  LQFP-48                 |
 *  |                                                             |
 *  |    -- Display --                                            |
 *  |    PA5=SCK  PA7=MOSI  PA4=TFT_CS  PA6=DC/MISO  PA0=TFT_RST  |
 *  |    PA12=TFT_BL (GPIO ON/OFF)                                 |
 *  |    -- Flash --                                              |
 *  |    PA5=SCK  PA7=MOSI  PA6=DC/MISO(dyn)  PB12=FLASH_CS       |
 *  |    -- PWM --                                                |
 *  |    PA8=CH1  PA9=CH2  PB13=CH1N  PB14=CH2N                   |
 *  |    -- ADC --                                                |
 *  |    PB0=CH8(I)  PB1=CH9(V)                                   |
 *  |    -- ESP8266 --                                            |
 *  |    PA2=TX  PA3=RX  PA1=RST  PB11=EN                         |
 *  |    -- Keys --                                               |
 *  |    PB9=KEY0(电源) PB8=KEY1(返回) PB7=KEY2(UP) PB6=KEY3(DOWN) PB5=KEY4(确定) |
 *  |    -- LEDs --                                               |
 *  |    PA15=STATUS(PWM指示) PB4=WIFI PB3=POWER(12V) PC13=HEARTBEAT |
 *  |    -- Power control --                                      |
 *  |    PB10=PowerCtrl (KEY0 manual toggle, HIGH=12V enable)     |
 *  |    -- Buzzer --                                             |
 *  |    PB15=Buzzer                                              |
 *  |                                                             |
 *  |    State machine: SYS_INIT -> SYS_IDLE -> SYS_SWEEP -> SYS  |
 *  |                       ^             |            |          |
 *  |                       +--- SYS_FAULT <-----------+          |
 *  |                                                             |
 *  |    Sys_Safety (independent of UI):                          |
 *  |      EMA filter a=0.25, only RUNNING checks overcurrent     |
 *  |      I > 5.0A -> FAULT + Buzzer + PWM off                   |
 *  +------------------------------------------------------------+
 *
 * @note    Init order: Sys_Timer_Init -> Sys_Hardware_Init ->
 *          W25Q_Driver_Init -> Tft_Driver_Font_Init ->
 *          App_Storage_Init -> Sys_Startup_Screen -> Sys_Post_Init
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

#define SYS_BLACKBOX_SAMPLE_PERIOD_MS  200U

static Sys_State s_sys_state = SYS_STATE_INIT;

/* ── 全局配置实例 ── */
static App_Storage_Config s_sys_config;
static Sys_Fault_Code s_fault_code = SYS_FAULT_NONE;
static uint32_t s_blackbox_sample_last = 0U;

typedef void (*Sys_Core_State_Task)(void);

static void Sys_Core_Set_State(Sys_State state)
{
    if (state == s_sys_state) return;
    if ((s_sys_state == SYS_STATE_SWEEP ||
         s_sys_state == SYS_STATE_RUNNING) &&
        state != SYS_STATE_SWEEP && state != SYS_STATE_RUNNING) {
        App_Storage_Request_Blackbox_Checkpoint();
    }
    s_sys_state = state;
    W25Q_Driver_Set_Erase_Allowed(
        (state == SYS_STATE_SWEEP || state == SYS_STATE_RUNNING) ? 0U : 1U);
    switch (state) {
        case SYS_STATE_SWEEP:
            Led_Driver_Set_Status(LED_DRIVER_STATE_SLOW);
            break;
        case SYS_STATE_RUNNING:
            Led_Driver_Set_Status(LED_DRIVER_STATE_ON);
            break;
        case SYS_STATE_FAULT:
            Inverter_Control_Freq_Ramp_Cancel();
            Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
            break;
        case SYS_STATE_INIT:
        case SYS_STATE_IDLE:
        default:
            Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
            break;
    }
}

static void Sys_Core_Set_Power_Output(uint8_t enabled)
{
    if (enabled != 0U) {
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
        Led_Driver_Set_Power(1U);
    }
    else {
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        Led_Driver_Set_Power(0U);
    }
}

uint8_t Sys_Core_Is_Power_Enabled(void)
{
    return (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_10) != Bit_RESET) ? 1U : 0U;
}

Sys_State Sys_Core_Get_State(void)
{
    return s_sys_state;
}

Sys_Fault_Code Sys_Core_Get_Fault(void)
{
    return s_fault_code;
}

void Sys_Core_Trigger_Fault(Sys_Fault_Code fault_code)
{
    uint8_t first_fault;

    if (fault_code == SYS_FAULT_NONE) {
        fault_code = SYS_FAULT_CONTROL_INVARIANT;
    }
    first_fault = (s_fault_code == SYS_FAULT_NONE) ? 1U : 0U;
    if (first_fault != 0U) {
        s_fault_code = fault_code;
        Blackbox_Lock_Fault_Snapshot((uint8_t)fault_code);
    }

    /* 功率安全顺序不可交换：先关闭TIM1/MOE，再切断12V。 */
    Inverter_Control_Soft_Start_Fault();
    GPIO_ResetBits(GPIOB, GPIO_Pin_10);
    Led_Driver_Set_Power(0U);
    Sys_Core_Set_State(SYS_STATE_FAULT);
    Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
}

static uint8_t Sys_Core_Check_Control_Invariant(void)
{
    uint8_t power_enabled;
    uint8_t pwm_enabled;
    uint8_t invalid;

    power_enabled = Sys_Core_Is_Power_Enabled();
    pwm_enabled = Pwm_Driver_Is_Enabled();
    invalid = 0U;

    if (power_enabled == 0U && pwm_enabled != 0U) {
        invalid = 1U;
    }
    if (s_sys_state == SYS_STATE_IDLE && pwm_enabled != 0U) {
        invalid = 1U;
    }
    if ((s_sys_state == SYS_STATE_SWEEP || s_sys_state == SYS_STATE_RUNNING) &&
        (power_enabled == 0U || pwm_enabled == 0U)) {
        invalid = 1U;
    }
    if (s_sys_state == SYS_STATE_FAULT &&
        (power_enabled != 0U || pwm_enabled != 0U)) {
        invalid = 1U;
    }

    if (invalid != 0U) {
        Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT);
        return 0U;
    }
    return 1U;
}

Sys_Control_Result Sys_Core_Request_Start(void)
{
    if (s_sys_state == SYS_STATE_FAULT || s_fault_code != SYS_FAULT_NONE) {
        return SYS_CONTROL_RESULT_FAULT_LATCHED;
    }
    if (s_sys_state == SYS_STATE_SWEEP || s_sys_state == SYS_STATE_RUNNING) {
        return (Sys_Core_Check_Control_Invariant() != 0U) ?
               SYS_CONTROL_RESULT_OK : SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (s_sys_state != SYS_STATE_IDLE) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (Sys_Core_Is_Power_Enabled() == 0U) {
        return SYS_CONTROL_RESULT_POWER_OFF;
    }
    if (Adc_Driver_Get_Calibration_State() != ADC_DRIVER_CAL_READY ||
        Adc_Driver_Is_Data_Fresh() == 0U) {
        return SYS_CONTROL_RESULT_ADC_NOT_READY;
    }

    Blackbox_Reset_Pretrigger();
    Sys_Core_Set_State(SYS_STATE_SWEEP);
    Inverter_Control_Soft_Start_Trigger();
    if (Pwm_Driver_Is_Enabled() == 0U ||
        Sys_Core_Check_Control_Invariant() == 0U) {
        Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT);
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    return SYS_CONTROL_RESULT_OK;
}

Sys_Control_Result Sys_Core_Request_Stop(void)
{
    if (s_sys_state == SYS_STATE_INIT) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (s_sys_state == SYS_STATE_FAULT) {
        Inverter_Control_Soft_Start_Fault();
        Sys_Core_Set_Power_Output(0U);
        return SYS_CONTROL_RESULT_FAULT_LATCHED;
    }

    Inverter_Control_Soft_Start_Stop();
    if (s_sys_state == SYS_STATE_SWEEP || s_sys_state == SYS_STATE_RUNNING) {
        Sys_Core_Set_State(SYS_STATE_IDLE);
    }
    if (Sys_Core_Check_Control_Invariant() == 0U) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    return SYS_CONTROL_RESULT_OK;
}

Sys_Control_Result Sys_Core_Reset_Fault(void)
{
    if (s_sys_state != SYS_STATE_FAULT) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }

    Inverter_Control_Soft_Start_Reset();
    Sys_Core_Set_Power_Output(0U);
    Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_OFF);
    Sys_Safety_Reset_EMA();
    s_fault_code = SYS_FAULT_NONE;
    Sys_Core_Set_State(SYS_STATE_IDLE);

    if (Sys_Core_Check_Control_Invariant() == 0U) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    return SYS_CONTROL_RESULT_OK;
}

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
    Key_Driver_Configure(KEY_DRIVER_ID_POWER,   0U);
    Key_Driver_Configure(KEY_DRIVER_ID_BACK,    KEY_DRIVER_CFG_DOUBLE_ENABLE);
    Key_Driver_Configure(KEY_DRIVER_ID_UP,      0U);
    Key_Driver_Configure(KEY_DRIVER_ID_DOWN,    0U);
    Key_Driver_Configure(KEY_DRIVER_ID_CONFIRM, KEY_DRIVER_CFG_LONG_ENABLE);
}

void Sys_Startup_Screen(void)
{
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_Splash();               /* 纯代码 SPLASH: 逐字渐亮 ~4.8s */
}

void Sys_Post_Init(void)
{
    uint8_t cfg_valid;
    /* Sys_Timer_Init 已提前到 main.c 中 (SPLASH 需要 SysTick) */
    Led_Driver_Set_Heartbeat(1);     /* PC13 heartbeat: MCU alive indicator */

    /* V4.3.0: Flash 加载参数配置, 双副本 CRC32 闭锁回退 */
    cfg_valid = App_Storage_Load_Config(&s_sys_config);

    /* ADC 校准: Flash 优先 → 强制解锁冷启动自测算降级 (设计文档 §9.3) */
    if (cfg_valid && s_sys_config.adc_i_offset != 0.0f) {
        Adc_Driver_Set_Calibration(s_sys_config.adc_i_offset,
                                    s_sys_config.adc_v_gain,
                                    s_sys_config.freq_trim_hz);   /* Flash 固化直达 */
    } else {
        Adc_Driver_Force_Recalibrate();                           /* IDLE中非阻塞自测算 */
    }

    /* V4.5.2: 背光已由 Ui_Controller_Apply_Settings 映射 1-100 → 0-255 PWM 控制,
     *   此处仅做额外保底: 如果配置值 >0 且在 Ui_Controller_Apply_Settings 之前,
     *   按新标度映射后设置 (1-100% → 0-255)。 */
    if (s_sys_config.backlight > 0 && s_sys_config.backlight <= 100)
        Tft_Driver_Set_Backlight((s_sys_config.backlight * 255 + 50) / 100);

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(4000);   /* 4000 × ~1.6ms = ~6.4s (LSI ~40kHz typ); worst-case LSI 60kHz: 4000×1.07ms=4.3s */
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

    /* V4.5.2: Load persistent settings into UI (includes letter_spacing) */
    {
        uint8_t lang, font, bl, spacing, preset;
        uint16_t fg, bg;
        App_Storage_Load_Settings(&lang, &font, &bl, &spacing, &preset, &fg, &bg);
        Ui_Controller_Apply_Settings(lang, font, bl, spacing, preset, fg, bg);
    }

    App_Network_Start_Connect();
    Sys_Core_Set_State(SYS_STATE_IDLE);
}

/* ═══════════════════════════════════════════════════════════════
 *  2. 安全监测 (原 Sys_Safety.c)
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_SAFETY_OVERCURRENT_A  5.0f
#define SYS_SAFETY_CONFIRM_SAMPLES 3U

static float  s_safety_ema_v = 0.0f, s_safety_ema_i = 0.0f;
static uint32_t s_safety_last_sequence = 0U;
static uint8_t s_safety_over_count = 0U;

static void Sys_Safety_Update_EMA(void)
{
    s_safety_ema_v = Adc_Driver_Get_Display_Voltage();
    s_safety_ema_i = Adc_Driver_Get_Display_Current();
}

float Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }
float Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }

/**
 * @brief  重置过流 EMA 滤波缓存, 防止 FAULT 复位后 EMA 残留值立即重新触发过流
 * @note   FAULT 状态下 PAGE 单击复位时, Sys_Safety 持有的 EMA 电流值可能仍 > 5.0A,
 *         若不重置, Sys_Safety_Task 下一圈又会将系统状态拉回FAULT, 造成"消除无效"。
 *         调用后将 EMA 重置为当前 ADC 原始值, 同时锁定新状态让 EMA 重新收敛。
 */
void Sys_Safety_Reset_EMA(void)
{
    s_safety_ema_v = Adc_Driver_Get_Display_Voltage();
    s_safety_ema_i = Adc_Driver_Get_Display_Current();
    s_safety_last_sequence = Adc_Driver_Get_Processed_Sequence();
    s_safety_over_count = 0U;
}

void Sys_Safety_Task(void)
{
    uint32_t sequence;
    float safety_current;

    if (s_sys_state == SYS_STATE_IDLE) {
        Adc_Driver_Calibration_Task(Sys_Core_Is_Power_Enabled());
        if (Adc_Driver_Take_Calibration_Completed() != 0U) {
            s_sys_config.adc_i_offset = Adc_Driver_Get_Current_Offset();
            s_sys_config.adc_v_gain = Adc_Driver_Get_Voltage_Gain();
            App_Storage_Request_Save_ADC_Calibration(
                s_sys_config.adc_i_offset, s_sys_config.adc_v_gain);
        }
    }

    if ((s_sys_state == SYS_STATE_SWEEP ||
         s_sys_state == SYS_STATE_RUNNING) &&
        Adc_Driver_Is_Data_Fresh() == 0U) {
        Sys_Core_Trigger_Fault(SYS_FAULT_ADC_STALE);
        return;
    }

    sequence = Adc_Driver_Get_Processed_Sequence();
    if (sequence == s_safety_last_sequence) return;
    s_safety_last_sequence = sequence;

    /* 只在新ADC结果发布后更新显示量，消除主循环速度对滤波的影响。 */
    Sys_Safety_Update_EMA();

    /* 扫频阶段已经发波，必须与稳定运行使用同一过流保护。 */
    if (s_sys_state != SYS_STATE_SWEEP &&
        s_sys_state != SYS_STATE_RUNNING) {
        s_safety_over_count = 0U;
        return;
    }

    safety_current = Adc_Driver_Get_Safety_Current();
    if (safety_current > SYS_SAFETY_OVERCURRENT_A) {
        if (s_safety_over_count < SYS_SAFETY_CONFIRM_SAMPLES) {
            s_safety_over_count++;
        }
        if (s_safety_over_count >= SYS_SAFETY_CONFIRM_SAMPLES) {
            Sys_Core_Trigger_Fault(SYS_FAULT_OVERCURRENT);
        }
    }
    else {
        s_safety_over_count = 0U;
    }
}

/**
 * @brief  Handle KEY0 power toggle — hardware power switch
 * @note   KEY0 click: toggle PB10(12V) + POWER LED(PB3)
 *         Power ON  -> PB10 HIGH + POWER LED ON
 *         Power OFF -> force PWM stop + PB10 LOW + POWER LED OFF + STATUS LED OFF
 *         PWM restart requires KEY4 explicit action after power-on
 */
void Sys_Power_Control_Handle(Key_Driver_Event ke[5])
{
    if (ke[KEY_DRIVER_ID_POWER] != KEY_DRIVER_EVENT_CLICK) return;

    if (s_sys_state == SYS_STATE_FAULT || s_fault_code != SYS_FAULT_NONE) {
        /* FAULT锁存期间KEY0不得重新接通12V。 */
        Inverter_Control_Soft_Start_Fault();
        Sys_Core_Set_Power_Output(0U);
    }
    else if (Sys_Core_Is_Power_Enabled() == 0U) {
        if (s_sys_state == SYS_STATE_IDLE && Pwm_Driver_Is_Enabled() == 0U) {
            Sys_Core_Set_Power_Output(1U);
        }
        else {
            Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT);
        }
    }
    else {
        /* KEY0关电必须先停止PWM，再拉低PB10。 */
        (void)Sys_Core_Request_Stop();
        Sys_Core_Set_Power_Output(0U);
    }

    (void)Sys_Core_Check_Control_Invariant();

    /* Consume KEY0 event — do not propagate to UI */
    ke[KEY_DRIVER_ID_POWER] = KEY_DRIVER_EVENT_NONE;
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

static void Sys_Run_Blackbox_Tick(void)
{
    uint32_t now;
    uint32_t frequency_hz;
    uint8_t sample_valid;
    uint8_t pretrigger_eligible;

    now = Sys_Timer_Get_Tick();
    if ((uint32_t)(now - s_blackbox_sample_last) <
        SYS_BLACKBOX_SAMPLE_PERIOD_MS) return;
    s_blackbox_sample_last = now;

    sample_valid = Adc_Driver_Is_Data_Fresh();
    pretrigger_eligible =
        (s_sys_state == SYS_STATE_SWEEP ||
         s_sys_state == SYS_STATE_RUNNING) ? 1U : 0U;
    frequency_hz = (pretrigger_eligible != 0U) ?
                   Pwm_Driver_Get_Frequency() : 0U;
    Blackbox_Capture_Tick(Sys_Safety_Get_EMA_Voltage(),
                          Sys_Safety_Get_EMA_Current(),
                          frequency_hz, (uint8_t)s_sys_state,
                          sample_valid, pretrigger_eligible);
    if (pretrigger_eligible != 0U && sample_valid != 0U) {
        Blackbox_Log_Tick(Sys_Safety_Get_EMA_Voltage(),
                          Sys_Safety_Get_EMA_Current(),
                          frequency_hz, (uint8_t)s_sys_state);
    }
}

static void Sys_Run_Fault_Persist_Task(void)
{
    uint8_t power_safe;

    power_safe = (Pwm_Driver_Is_Enabled() == 0U &&
                  Sys_Core_Is_Power_Enabled() == 0U) ? 1U : 0U;
    Blackbox_Fault_Persist_Task(power_safe);
}

static void Sys_Run_Key_And_Ui_Task(void)
{
    Key_Driver_Event events[KEY_DRIVER_COUNT];

    Key_Driver_Task();
    Key_Driver_Get_All_Events(events);
    Sys_Power_Control_Handle(events);
    Ui_Controller_Task(events);
}

static void Sys_Core_Run_Sweep_State_Task(void)
{
    Inverter_Control_Soft_Start_Task();
    if (Inverter_Control_Soft_Start_Get_State() ==
        INVERTER_CONTROL_SS_STATE_DONE) {
        Sys_Core_Set_State(SYS_STATE_RUNNING);
    }
}

static void Sys_Core_Run_Running_State_Task(void)
{
    Inverter_Control_Freq_Ramp_Task();
}

static void Sys_Core_Run_Common(Sys_State expected_state,
                                Sys_Core_State_Task state_task)
{
    (void)Sys_Core_Check_Control_Invariant();
    Adc_Driver_Filter_Task();
    Sys_Safety_Task();
    Sys_Run_Key_And_Ui_Task();
    if ((s_sys_state == expected_state) && (state_task != 0)) {
        state_task();
    }
    App_Network_Task();
    Sys_Run_Blackbox_Tick();
    Sys_Run_Fault_Persist_Task();
    if (s_sys_state == SYS_STATE_IDLE) {
        App_Storage_Task();
    }
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
    IWDG_ReloadCounter();
    __WFI();
}

void Sys_Run_Idle(void)
{
    Sys_Core_Run_Common(SYS_STATE_IDLE, 0);
}

void Sys_Run_Sweep(void)
{
    Sys_Core_Run_Common(SYS_STATE_SWEEP,
                        Sys_Core_Run_Sweep_State_Task);
}

void Sys_Run_Running(void)
{
    Sys_Core_Run_Common(SYS_STATE_RUNNING,
                        Sys_Core_Run_Running_State_Task);
}

void Sys_Run_Fault(void)
{
    Sys_Core_Run_Common(SYS_STATE_FAULT, 0);
}
