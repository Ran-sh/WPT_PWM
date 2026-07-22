/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.c
 * @brief   模数转换与模拟量采集驱动 — V5.1.0
 *
 *  硬件连接:
 *  +--------------------------------------------------------+
 *  |                     STM32F103C8T6                       |
 *  |                                                         |
 *  |    PB0 --- ADC_CH8 ------- CC6920BSO 电流传感器输出     |
 *  |                           标称灵敏度 132mV/A            |
 *  |    PB1 --- ADC_CH9 ------- 电压采样分压网络输出         |
 *  |                           分压比 20:1                   |
 *  |                                                         |
 *  |    TIM3 以 500Hz 触发 ADC1 双通道扫描                   |
 *  |    DMA1 通道1每 2ms 保存一组电流和电压原始值           |
 *  |    显示使用64点窗口，安全保护使用8点快速窗口            |
 *  +--------------------------------------------------------+
 *
 * @note    中断只保存原始快照，滤波、换算和校准均在主循环完成。
 ******************************************************************************
 */

#include "Adc_Driver.h"
#include "Sys_Timer.h"

#define ADC_DRIVER_VREF_MCU            3.30f
#define ADC_DRIVER_VOLTAGE_DIVIDER     20.0f
#define ADC_DRIVER_CURRENT_SENSITIVITY 0.132f   /* CC6920BSO 标称灵敏度 132mV/A */
#define ADC_DRIVER_CURRENT_CAL_FACTOR  0.602f   /* 灵敏度校准系数: 显示值=原始值*系数 */
#define ADC_DRIVER_DISPLAY_WINDOW      64U
#define ADC_DRIVER_SAFETY_WINDOW       8U

#define ADC_DRIVER_TIM3_COUNTER_HZ      1000000UL
#define ADC_DRIVER_TIM3_PERIOD          1999U

#define ADC_DRIVER_CAL_SAMPLES      50
#define ADC_DRIVER_CAL_INTERVAL_MS  10
#define ADC_DRIVER_STALE_TIMEOUT_MS 20U
#define ADC_DRIVER_HW_CAL_TIMEOUT_MS 10U
#define ADC_DRIVER_SNAPSHOT_RETRY_LIMIT 3U

/* ── 显示与安全窗口使用不同长度，禁止共享累加器。 ── */
typedef struct {
    uint16_t buf[ADC_DRIVER_DISPLAY_WINDOW];
    uint8_t  idx;
    uint8_t  filled;
    uint32_t accum;
} Adc_Driver_Display_Window;

typedef struct {
    uint16_t buf[ADC_DRIVER_SAFETY_WINDOW];
    uint8_t  idx;
    uint8_t  filled;
    uint32_t accum;
} Adc_Driver_Safety_Window;

static void Adc_Driver_Display_Push(Adc_Driver_Display_Window* fw,
                                    uint16_t new_val)
{
    uint16_t old = fw->buf[fw->idx];
    fw->buf[fw->idx] = new_val;
    fw->accum += new_val;
    if (fw->filled >= ADC_DRIVER_DISPLAY_WINDOW)
        fw->accum -= old;
    fw->idx = (fw->idx + 1U) % ADC_DRIVER_DISPLAY_WINDOW;
    if (fw->filled < ADC_DRIVER_DISPLAY_WINDOW) fw->filled++;
}

static void Adc_Driver_Safety_Push(Adc_Driver_Safety_Window* fw,
                                   uint16_t new_val)
{
    uint16_t old = fw->buf[fw->idx];
    fw->buf[fw->idx] = new_val;
    fw->accum += new_val;
    if (fw->filled >= ADC_DRIVER_SAFETY_WINDOW)
        fw->accum -= old;
    fw->idx = (fw->idx + 1U) % ADC_DRIVER_SAFETY_WINDOW;
    if (fw->filled < ADC_DRIVER_SAFETY_WINDOW) fw->filled++;
}

static float Adc_Driver_Accum_To_Pin_Voltage(uint32_t accum, uint8_t count)
{
    if (count == 0U) return 0.0f;
    return ((float)accum / (float)count / 4095.0f) * ADC_DRIVER_VREF_MCU;
}

/* ── 模块状态 ── */
static volatile uint16_t s_adc_dma_raw[2];  /* DMA目标: 第0项为电流，第1项为电压 */
static volatile uint16_t s_adc_snapshot[2];
static volatile uint32_t s_adc_sample_sequence = 0U;
static volatile uint32_t s_adc_last_sample_tick = 0U;
static uint32_t s_adc_processed_sequence = 0U;

static Adc_Driver_Display_Window s_v_display_filter;
static Adc_Driver_Display_Window s_c_display_filter;
static Adc_Driver_Safety_Window s_c_safety_filter;

static float s_display_voltage = 0.0f;
static float s_display_current = 0.0f;
static float s_safety_current = 0.0f;
static float s_raw_pin_v     = 1.65f;

static float   s_i_offset    = 1.65f;
static float   s_v_gain      = 1.0f;
static uint8_t s_cal_count   = 0;
static float   s_cal_accum   = 0.0f;
static uint32_t s_cal_last_tick = 0U;
static Adc_Driver_Calibration_State s_cal_state = ADC_DRIVER_CAL_UNINITIALIZED;
static uint8_t s_cal_completed_event = 0U;
static uint8_t s_adc_hw_ready = 0U;

static void Adc_Driver_Abort_Hardware_Init(void)
{
    TIM_Cmd(TIM3, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
    ADC_Cmd(ADC1, DISABLE);
    s_adc_hw_ready = 0U;
    s_cal_state = ADC_DRIVER_CAL_ERROR;
}

void Adc_Driver_Init(void)
{
    ADC_InitTypeDef  adc;
    GPIO_InitTypeDef gpio;
    DMA_InitTypeDef  dma;
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef nvic;
    uint32_t cal_start;

    s_adc_hw_ready = 0U;
    s_cal_state = ADC_DRIVER_CAL_UNINITIALIZED;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    gpio.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOB, &gpio);

    DMA_DeInit(DMA1_Channel1);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&(ADC1->DR);
    dma.DMA_MemoryBaseAddr     = (uint32_t)s_adc_dma_raw;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = 2;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_ClearITPendingBit(DMA1_IT_GL1);
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);

    nvic.NVIC_IRQChannel = DMA1_Channel1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic.NVIC_IRQChannelSubPriority = 1U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler = (uint16_t)(SystemCoreClock /
                                   ADC_DRIVER_TIM3_COUNTER_HZ - 1UL);
    tim.TIM_Period = ADC_DRIVER_TIM3_PERIOD;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM3, &tim);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, DISABLE);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = ENABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 2;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 2, ADC_SampleTime_239Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    { volatile uint16_t i; for (i = 0; i < 100; i++) __NOP(); }

    ADC_ResetCalibration(ADC1);
    cal_start = Sys_Timer_Get_Tick();
    while (ADC_GetResetCalibrationStatus(ADC1)) {
        if ((uint32_t)(Sys_Timer_Get_Tick() - cal_start) >=
            ADC_DRIVER_HW_CAL_TIMEOUT_MS) {
            Adc_Driver_Abort_Hardware_Init();
            return;
        }
    }
    ADC_StartCalibration(ADC1);
    cal_start = Sys_Timer_Get_Tick();
    while (ADC_GetCalibrationStatus(ADC1)) {
        if ((uint32_t)(Sys_Timer_Get_Tick() - cal_start) >=
            ADC_DRIVER_HW_CAL_TIMEOUT_MS) {
            Adc_Driver_Abort_Hardware_Init();
            return;
        }
    }

    s_adc_sample_sequence = 0U;
    s_adc_processed_sequence = 0U;
    s_adc_last_sample_tick = Sys_Timer_Get_Tick();
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    s_adc_hw_ready = 1U;
}

void Adc_Driver_Filter_Task(void)
{
    uint16_t current_raw;
    uint16_t voltage_raw;
    uint32_t sequence_before;
    uint32_t sequence_after;
    uint8_t snapshot_attempts;
    float safety_pin_v;

    snapshot_attempts = 0U;
    do {
        sequence_before = s_adc_sample_sequence;
        if (sequence_before == s_adc_processed_sequence) return;
        current_raw = s_adc_snapshot[0];
        voltage_raw = s_adc_snapshot[1];
        sequence_after = s_adc_sample_sequence;
        snapshot_attempts++;
        if (sequence_before != sequence_after &&
            snapshot_attempts >= ADC_DRIVER_SNAPSHOT_RETRY_LIMIT) return;
    } while (sequence_before != sequence_after);

    Adc_Driver_Display_Push(&s_v_display_filter, voltage_raw);
    s_display_voltage = Adc_Driver_Accum_To_Pin_Voltage(
        s_v_display_filter.accum, s_v_display_filter.filled) *
        ADC_DRIVER_VOLTAGE_DIVIDER * s_v_gain;

    Adc_Driver_Display_Push(&s_c_display_filter, current_raw);
    s_raw_pin_v = Adc_Driver_Accum_To_Pin_Voltage(
        s_c_display_filter.accum, s_c_display_filter.filled);
    s_display_current = (s_raw_pin_v - s_i_offset) /
        ADC_DRIVER_CURRENT_SENSITIVITY * ADC_DRIVER_CURRENT_CAL_FACTOR;
    if (s_display_current < 0.0f) s_display_current = 0.0f;

    Adc_Driver_Safety_Push(&s_c_safety_filter, current_raw);
    safety_pin_v = Adc_Driver_Accum_To_Pin_Voltage(
        s_c_safety_filter.accum, s_c_safety_filter.filled);
    s_safety_current = (safety_pin_v - s_i_offset) /
        ADC_DRIVER_CURRENT_SENSITIVITY * ADC_DRIVER_CURRENT_CAL_FACTOR;
    if (s_safety_current < 0.0f) s_safety_current = 0.0f;

    /* 最后发布处理序号，读者看到新序号时三项结果已经全部更新。 */
    s_adc_processed_sequence = sequence_after;
}

void Adc_Driver_DMA_Transfer_Complete_ISR(void)
{
    uint16_t current_raw;
    uint16_t voltage_raw;

    current_raw = s_adc_dma_raw[0];
    voltage_raw = s_adc_dma_raw[1];
    s_adc_snapshot[0] = current_raw;
    s_adc_snapshot[1] = voltage_raw;
    s_adc_last_sample_tick = Sys_Timer_Get_Tick();
    s_adc_sample_sequence++;
}

uint32_t Adc_Driver_Get_Processed_Sequence(void)
{
    return s_adc_processed_sequence;
}

void Adc_Driver_Calibration_Task(uint8_t power_enabled)
{
    float new_offset;
    uint32_t now;

    if (s_cal_state == ADC_DRIVER_CAL_UNINITIALIZED ||
        s_cal_state == ADC_DRIVER_CAL_READY ||
        s_cal_state == ADC_DRIVER_CAL_ERROR) return;
    if (power_enabled != 0U) return;

    if (s_cal_state == ADC_DRIVER_CAL_FILLING) {
        if (s_c_display_filter.filled < ADC_DRIVER_DISPLAY_WINDOW) return;
        s_cal_accum = 0.0f;
        s_cal_count = 0U;
        s_cal_last_tick = Sys_Timer_Get_Tick();
        s_cal_state = ADC_DRIVER_CAL_CALIBRATING;
        return;
    }

    now = Sys_Timer_Get_Tick();
    if (now - s_cal_last_tick < ADC_DRIVER_CAL_INTERVAL_MS) return;
    s_cal_last_tick = now;

    s_cal_accum += s_raw_pin_v;
    s_cal_count++;
    if (s_cal_count >= ADC_DRIVER_CAL_SAMPLES) {
        new_offset = s_cal_accum / (float)ADC_DRIVER_CAL_SAMPLES;
        if (new_offset > 0.5f && new_offset < 2.8f) {
            s_i_offset = new_offset;
            s_cal_state = ADC_DRIVER_CAL_READY;
            s_cal_completed_event = 1U;
        }
        else {
            s_cal_state = ADC_DRIVER_CAL_ERROR;
        }
    }
}

Adc_Driver_Calibration_State Adc_Driver_Get_Calibration_State(void)
{
    return s_cal_state;
}

uint8_t Adc_Driver_Take_Calibration_Completed(void)
{
    uint8_t completed;

    completed = s_cal_completed_event;
    s_cal_completed_event = 0U;
    return completed;
}

uint8_t Adc_Driver_Is_Data_Fresh(void)
{
    if (s_adc_sample_sequence == 0U) return 0U;
    return ((Sys_Timer_Get_Tick() - s_adc_last_sample_tick) <=
            ADC_DRIVER_STALE_TIMEOUT_MS) ? 1U : 0U;
}

float Adc_Driver_Get_Display_Voltage(void) { return s_display_voltage; }
float Adc_Driver_Get_Display_Current(void) { return s_display_current; }
float Adc_Driver_Get_Safety_Current(void) { return s_safety_current; }

/** @brief 写入从外部存储器参数区读取的校准值 */
void Adc_Driver_Set_Calibration(float i_offset, float v_gain, int32_t freq_trim)
{
    if (s_adc_hw_ready == 0U) {
        s_cal_state = ADC_DRIVER_CAL_ERROR;
        return;
    }
    if (i_offset > 0.5f && i_offset < 2.8f) {            /* 合理范围覆盖1.65V附近的零点 */
        s_i_offset = i_offset;
        s_cal_state = ADC_DRIVER_CAL_READY;
    }
    else s_cal_state = ADC_DRIVER_CAL_ERROR;
    if (v_gain > 0.0f && v_gain <= 10.0f) s_v_gain = v_gain;
    else s_v_gain = 1.0f;
    s_cal_completed_event = 0U;
    (void)freq_trim;
}

/** @brief 获取当前电流零点值，供参数持久化使用 */
float Adc_Driver_Get_Current_Offset(void) { return s_i_offset; }
float Adc_Driver_Get_Voltage_Gain(void) { return s_v_gain; }

/** @brief 强制解锁校准状态机，供配置副本全部失效时重新校准 */
void Adc_Driver_Force_Recalibrate(void)
{
    if (s_adc_hw_ready == 0U) {
        s_cal_state = ADC_DRIVER_CAL_ERROR;
        return;
    }
    s_cal_count = 0U;
    s_cal_accum = 0.0f;
    s_cal_completed_event = 0U;
    s_cal_state = ADC_DRIVER_CAL_FILLING;
}
