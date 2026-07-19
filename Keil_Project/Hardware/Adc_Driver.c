/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.c
 * @brief   ADC 模拟量采集驱动 — V5.0.1
 *
 *  Pinout:
 *  +--------------------------------------------------------+
 *  |                     STM32F103C8T6                       |
 *  |                                                         |
 *  |    PB0 --- ADC_CH8 ---+--- Current sensor CC6920BSO     |
 *  |                        |   (132mV/A, 0~50A range)       |
 *  |    PB1 --- ADC_CH9 ---+--- Voltage divider 20:1         |
 *  |                            (0~60V -> 0~3.0V)            |
 *  |                                                         |
 *  |    Sampling: TIM3 TRGO 500Hz -> ADC1 scan -> DMA1 CH1   |
 *  |    One stable current/voltage pair every 2ms            |
 *  |    64-sample sliding window, EMA a=0.25 (tau~800ms)     |
 *  +--------------------------------------------------------+
 *
 * @note    PB0=ADC_CH8(I), PB1=ADC_CH9(V)
 ******************************************************************************
 */

#include "Adc_Driver.h"
#include "Sys_Timer.h"

#define ADC_DRIVER_VREF_MCU            3.30f
#define ADC_DRIVER_VOLTAGE_DIVIDER     20.0f
#define ADC_DRIVER_CURRENT_SENSITIVITY 0.132f   /* CC6920BSO 标称灵敏度 132mV/A */
#define ADC_DRIVER_CURRENT_CAL_FACTOR  0.602f   /* 灵敏度校准系数: 显示值=原始值*系数, 匹配实际电流 */
#define ADC_DRIVER_FILTER_WINDOW       64

#define ADC_DRIVER_TIM3_COUNTER_HZ      1000000UL
#define ADC_DRIVER_TIM3_PERIOD          1999U

#define ADC_DRIVER_CAL_SAMPLES      50
#define ADC_DRIVER_CAL_INTERVAL_MS  10

/* ── 滑动窗口抽象 ── */
typedef struct {
    uint16_t buf[ADC_DRIVER_FILTER_WINDOW];
    uint8_t  idx;
    uint8_t  filled;
    uint32_t accum;
} Adc_Driver_Filter_Window;

/* 推入新 ADC 样本到滑动窗口: 窗口未满时仅累加, 满后减去最老值 → 维持 64 样本滚动平均 */
static void Adc_Driver_Filter_Push(Adc_Driver_Filter_Window* fw, uint16_t new_val)
{
    uint16_t old = fw->buf[fw->idx];
    fw->buf[fw->idx] = new_val;
    fw->accum += new_val;
    if (fw->filled >= ADC_DRIVER_FILTER_WINDOW)
        fw->accum -= old;
    fw->idx = (fw->idx + 1) % ADC_DRIVER_FILTER_WINDOW;
    if (fw->filled < ADC_DRIVER_FILTER_WINDOW) fw->filled++;
}

static float Adc_Driver_Filter_To_Voltage(const Adc_Driver_Filter_Window* fw)
{
    if (fw->filled == 0) return 0.0f;
    return ((float)fw->accum / (float)fw->filled / 4095.0f) * ADC_DRIVER_VREF_MCU;
}

/* ── 模块状态 ── */
static volatile uint16_t s_adc_dma_raw[2];  /* DMA目标: [0]=电流, [1]=电压 */
static volatile uint16_t s_adc_snapshot[2];
static volatile uint32_t s_adc_sample_sequence = 0U;
static volatile uint32_t s_adc_last_sample_tick = 0U;
static uint32_t s_adc_processed_sequence = 0U;

static Adc_Driver_Filter_Window s_v_filter;
static Adc_Driver_Filter_Window s_c_filter;

static float s_voltage       = 0.0f;
static float s_current       = 0.0f;
static float s_raw_pin_v     = 1.65f;

static float   s_i_offset    = 1.65f;
static uint8_t s_calibrated  = 0;
static uint8_t s_cal_count   = 0;
static float   s_cal_accum   = 0.0f;

void Adc_Driver_Init(void)
{
    ADC_InitTypeDef  adc;
    GPIO_InitTypeDef gpio;
    DMA_InitTypeDef  dma;
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef nvic;

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
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    s_adc_sample_sequence = 0U;
    s_adc_processed_sequence = 0U;
    s_adc_last_sample_tick = Sys_Timer_Get_Tick();
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

void Adc_Driver_Filter_Task(void)
{
    uint16_t current_raw;
    uint16_t voltage_raw;
    uint32_t sequence_before;
    uint32_t sequence_after;

    do {
        sequence_before = s_adc_sample_sequence;
        if (sequence_before == s_adc_processed_sequence) return;
        current_raw = s_adc_snapshot[0];
        voltage_raw = s_adc_snapshot[1];
        sequence_after = s_adc_sample_sequence;
    } while (sequence_before != sequence_after);

    s_adc_processed_sequence = sequence_after;
    Adc_Driver_Filter_Push(&s_v_filter, voltage_raw);
    s_voltage = Adc_Driver_Filter_To_Voltage(&s_v_filter) * ADC_DRIVER_VOLTAGE_DIVIDER;

    Adc_Driver_Filter_Push(&s_c_filter, current_raw);
    s_raw_pin_v = Adc_Driver_Filter_To_Voltage(&s_c_filter);
    s_current   = (s_raw_pin_v - s_i_offset) / ADC_DRIVER_CURRENT_SENSITIVITY * ADC_DRIVER_CURRENT_CAL_FACTOR;
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

uint32_t Adc_Driver_Get_Sample_Sequence(void)
{
    return s_adc_sample_sequence;
}

uint32_t Adc_Driver_Get_Last_Sample_Tick(void)
{
    return s_adc_last_sample_tick;
}

void Adc_Driver_Calibrate_Offset(void)
{
    static uint32_t last_cal = 0;

    /* 一次校准完成后锁定, 避免运行时有电流导致零点漂移 */
    if (s_calibrated) return;

    if (Sys_Timer_Get_Tick() - last_cal < ADC_DRIVER_CAL_INTERVAL_MS) return;
    last_cal = Sys_Timer_Get_Tick();

    if (s_c_filter.filled < ADC_DRIVER_FILTER_WINDOW) return;

    s_cal_accum += s_raw_pin_v;
    s_cal_count++;
    if (s_cal_count >= ADC_DRIVER_CAL_SAMPLES) {
        s_i_offset   = s_cal_accum / (float)ADC_DRIVER_CAL_SAMPLES;
        s_calibrated = 1;
    }
}

float Adc_Driver_Get_Voltage(void) { return s_voltage; }
float Adc_Driver_Get_Current(void) { return s_current; }

/** @brief V4.3.0: 从 Flash 固化值写入校准参数 (W25Q128 参数区加载后调用) */
void Adc_Driver_Set_Calibration(float i_offset, float v_gain, int32_t freq_trim)
{
    if (i_offset > 0.5f && i_offset < 2.8f) {            /* 合理性守卫: 1.65V 附近 */
        s_i_offset   = i_offset;
        s_calibrated = 1;                                /* 锁定, 禁止自测算覆盖 */
    }
    if (v_gain > 0.0f) {
        /** @note s_v_gain 当前在 filter 中使用硬编码 20:1 分压比,
         *        后续可扩展为可变增益 */
    }
    /** @note freq_trim_hz 保留给 Inverter_Control 适配 */
}

/** @brief V4.3.0: 获取当前 ADC 电流零点值 (用于回写 Flash 配置) */
float Adc_Driver_Get_Current_Offset(void) { return s_i_offset; }

/** @brief V4.3.0: 强制解锁校准状态机 (双副本全损→冷启动自测算) */
void Adc_Driver_Force_Recalibrate(void)
{
    s_calibrated = 0; s_cal_count = 0; s_cal_accum = 0.0f; /* 原子解锁 */
}
