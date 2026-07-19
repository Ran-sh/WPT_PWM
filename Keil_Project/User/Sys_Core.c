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
 * @note    Init order: Sys_Hardware_Init -> Sys_Timer_Init ->
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

volatile Sys_State g_sys_state = SYS_STATE_INIT;

/* ── 全局配置实例 ── */
static App_Storage_Config s_sys_config;
static Sys_Fault_Code s_fault_code = SYS_FAULT_NONE;

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
    return g_sys_state;
}

Sys_Fault_Code Sys_Core_Get_Fault(void)
{
    return s_fault_code;
}

void Sys_Core_Trigger_Fault(Sys_Fault_Code fault_code)
{
    if (fault_code == SYS_FAULT_NONE) {
        fault_code = SYS_FAULT_CONTROL_INVARIANT;
    }
    if (s_fault_code == SYS_FAULT_NONE) {
        s_fault_code = fault_code;
    }

    /* 功率安全顺序不可交换：先关闭TIM1/MOE，再切断12V。 */
    Inverter_Control_Soft_Start_Fault();
    GPIO_ResetBits(GPIOB, GPIO_Pin_10);
    Led_Driver_Set_Power(0U);
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
    g_sys_state = SYS_STATE_FAULT;
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
    if (g_sys_state == SYS_STATE_IDLE && pwm_enabled != 0U) {
        invalid = 1U;
    }
    if ((g_sys_state == SYS_STATE_SWEEP || g_sys_state == SYS_STATE_RUNNING) &&
        (power_enabled == 0U || pwm_enabled == 0U)) {
        invalid = 1U;
    }
    if (g_sys_state == SYS_STATE_FAULT &&
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
    if (g_sys_state == SYS_STATE_FAULT || s_fault_code != SYS_FAULT_NONE) {
        return SYS_CONTROL_RESULT_FAULT_LATCHED;
    }
    if (g_sys_state == SYS_STATE_SWEEP || g_sys_state == SYS_STATE_RUNNING) {
        return (Sys_Core_Check_Control_Invariant() != 0U) ?
               SYS_CONTROL_RESULT_OK : SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (g_sys_state != SYS_STATE_IDLE) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (Sys_Core_Is_Power_Enabled() == 0U) {
        return SYS_CONTROL_RESULT_POWER_OFF;
    }

    g_sys_state = SYS_STATE_SWEEP;
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
    if (g_sys_state == SYS_STATE_INIT) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    if (g_sys_state == SYS_STATE_FAULT) {
        Inverter_Control_Soft_Start_Fault();
        Sys_Core_Set_Power_Output(0U);
        return SYS_CONTROL_RESULT_FAULT_LATCHED;
    }

    Inverter_Control_Soft_Start_Stop();
    if (g_sys_state == SYS_STATE_SWEEP || g_sys_state == SYS_STATE_RUNNING) {
        g_sys_state = SYS_STATE_IDLE;
    }
    if (Sys_Core_Check_Control_Invariant() == 0U) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }
    return SYS_CONTROL_RESULT_OK;
}

Sys_Control_Result Sys_Core_Reset_Fault(void)
{
    if (g_sys_state != SYS_STATE_FAULT) {
        return SYS_CONTROL_RESULT_INVALID_STATE;
    }

    Inverter_Control_Soft_Start_Reset();
    Sys_Core_Set_Power_Output(0U);
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
    Sys_Safety_Reset_EMA();
    s_fault_code = SYS_FAULT_NONE;
    g_sys_state = SYS_STATE_IDLE;

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
    Key_Driver_Configure(KEY_DRIVER_ID_POWER,   KEY_DRIVER_CFG_CLICK_ONLY);
    Key_Driver_Configure(KEY_DRIVER_ID_BACK,    KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_UP,      KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_DOWN,    KEY_DRIVER_CFG_WITH_DOUBLE);
    Key_Driver_Configure(KEY_DRIVER_ID_CONFIRM, KEY_DRIVER_CFG_CLICK_ONLY);
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
        Adc_Driver_Force_Recalibrate();                           /* 强制解锁状态机 */
        Adc_Driver_Calibrate_Offset();                            /* 冷启动自测算 */
        s_sys_config.adc_i_offset = Adc_Driver_Get_Current_Offset();
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
}

/* ═══════════════════════════════════════════════════════════════
 *  2. 安全监测 (原 Sys_Safety.c)
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_SAFETY_OVERCURRENT_A  5.0f

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
 * @note   FAULT 状态下 PAGE 单击复位时, Sys_Safety 持有的 EMA 电流值可能仍 > 5.0A,
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
    /* EMA 滤波始终更新: IDLE 下也需显示实时 V/I */
    Sys_Safety_Update_EMA();

    /* 仅 RUNNING 状态执行安全监测: 非运行状态 PWM 已关, 无过流可能 */
    if (g_sys_state != SYS_STATE_RUNNING) return;

    /* 过流检测 → FAULT (先切状态再锁存: L4 禁擦需要 FAULT 而非 RUNNING) */
    if (s_safety_ema_i > SYS_SAFETY_OVERCURRENT_A) {
        Inverter_Control_Soft_Start_Fault();
        Buzzer_Driver_Set_State(BUZZER_DRIVER_STATE_BEEP);
        g_sys_state = SYS_STATE_FAULT;               /* 先切状态, Inverter已停波 */
        Blackbox_Lock_Fault_Snapshot();              /* L4 放行: 非 SWEEP/RUNNING */
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

    if (g_sys_state == SYS_STATE_FAULT || s_fault_code != SYS_FAULT_NONE) {
        /* FAULT锁存期间KEY0不得重新接通12V。 */
        Inverter_Control_Soft_Start_Fault();
        Sys_Core_Set_Power_Output(0U);
    }
    else if (Sys_Core_Is_Power_Enabled() == 0U) {
        if (g_sys_state == SYS_STATE_IDLE && Pwm_Driver_Is_Enabled() == 0U) {
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
        Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
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

void Sys_Run_Idle(void)
{
    if (Sys_Core_Check_Control_Invariant() == 0U) return;
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);

    Ui_Controller_Task();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
    Key_Driver_Task();
    Adc_Driver_Filter_Task();
    App_Network_Task();
    Sys_Safety_Task();
    IWDG_ReloadCounter();
    __WFI();
}

void Sys_Run_Sweep(void)
{
    if (Sys_Core_Check_Control_Invariant() == 0U) return;
    Led_Driver_Set_Status(LED_DRIVER_STATE_SLOW);
    static uint32_t last_bb_s = 0; uint32_t now_s = Sys_Timer_Get_Tick();
    Ui_Controller_Task();
    Inverter_Control_Soft_Start_Task();
    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE)
        g_sys_state = SYS_STATE_RUNNING;

    if (now_s - last_bb_s >= 200) { last_bb_s = now_s;
        Blackbox_Log_Tick(Sys_Safety_Get_EMA_Voltage(), Sys_Safety_Get_EMA_Current(),
                          Pwm_Driver_Get_Frequency(), (uint8_t)g_sys_state);
    }
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
    Key_Driver_Task();
    Adc_Driver_Filter_Task();
    App_Network_Task();
    Sys_Safety_Task();
    IWDG_ReloadCounter();
    __WFI();
}

void Sys_Run_Running(void)
{
    if (Sys_Core_Check_Control_Invariant() == 0U) return;
    Led_Driver_Set_Status(LED_DRIVER_STATE_ON);
    static uint32_t last_bb = 0; uint32_t now = Sys_Timer_Get_Tick();
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Task();

    if (now - last_bb >= 200) { last_bb = now;
        Blackbox_Log_Tick(Sys_Safety_Get_EMA_Voltage(), Sys_Safety_Get_EMA_Current(),
                          Pwm_Driver_Get_Frequency(), (uint8_t)g_sys_state);
    }
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
    Key_Driver_Task();
    Adc_Driver_Filter_Task();
    App_Network_Task();
    Sys_Safety_Task();
    IWDG_ReloadCounter();
    __WFI();
}

void Sys_Run_Fault(void)
{
    if (Sys_Core_Check_Control_Invariant() == 0U) return;
    Led_Driver_Set_Status(LED_DRIVER_STATE_OFF);
    Ui_Controller_Task();
    Inverter_Control_Freq_Ramp_Cancel();
    Sys_Run_Led_Tick();
    Sys_Run_Buzzer_Tick();
    Key_Driver_Task();
    Adc_Driver_Filter_Task();
    App_Network_Task();
    Sys_Safety_Task();
    IWDG_ReloadCounter();
    __WFI();
}
