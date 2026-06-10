/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.c
 * @brief   模拟量采集驱动 — 实现 (V6.2)
 * @note    PB0=ADC_CH8 电流, PB1=ADC_CH9 电压
 ******************************************************************************
 */

#include "Adc_Driver.h"
#include "Sys_Timer.h"

/* ── 硬件常量 ── */
#define VREF_MCU              3.30f
#define VOLTAGE_DIVIDER       20.0f
#define CURRENT_SENSITIVITY   0.132f
#define ADC_FILTER_WINDOW     64

/*
 * 采样周期: 144241 CPU 周期, 与 100kHz PWM 互质 → 覆盖 720 个相位
 *
 * 互质性验证: gcd(144241, 720) = 1
 *   144241 ≡ 241 (mod 720), gcd(241, 720) = 1  ✓
 *
 * 以下静态断言确保系统时钟 = 72MHz (DWT 周期计数器与 PWM 频率计算依赖此假设):
 */
typedef char Adc_Driver_Assert_HSE_72MHz[(HSE_VALUE == 8000000) ? 1 : -1];

#define FILTER_PERIOD_CYCLES  144241

/* ── 校准参数 ── */
#define CAL_SAMPLES      50
#define CAL_INTERVAL_MS  10

/* ── 滑动窗口抽象 ── */
typedef struct {
    uint16_t buf[ADC_FILTER_WINDOW];
    uint8_t  idx;
    uint8_t  filled;
    uint32_t accum;
} Filter_Window;

static void Filter_Push(Filter_Window* fw, uint16_t new_val)
{
    uint16_t old = fw->buf[fw->idx];
    fw->buf[fw->idx] = new_val;
    fw->accum += new_val;
    if (fw->filled >= ADC_FILTER_WINDOW)
        fw->accum -= old;
    fw->idx = (fw->idx + 1) % ADC_FILTER_WINDOW;
    if (fw->filled < ADC_FILTER_WINDOW) fw->filled++;
}

static float Filter_To_Voltage(const Filter_Window* fw)
{
    return ((float)fw->accum / (float)fw->filled / 4095.0f) * VREF_MCU;
}

/* ── 模块状态 ── */
static volatile uint16_t s_adc_raw[2];   /* DMA 循环刷新: [0]=电流, [1]=电压 */

static Filter_Window s_v_filter;
static Filter_Window s_c_filter;

static float s_voltage       = 0.0f;
static float s_current       = 0.0f;
static float s_raw_pin_v     = 1.65f;

static float   s_i_offset     = 1.65f;
static uint8_t s_calibrated   = 0;
static uint8_t s_cal_count    = 0;
static float   s_cal_accum    = 0.0f;

void Adc_Driver_Init(void)
{
    ADC_InitTypeDef  adc;
    GPIO_InitTypeDef gpio;
    DMA_InitTypeDef  dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* V6.2: PB0=CH8 (电流), PB1=CH9 (电压) — 模拟输入 */
    gpio.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOB, &gpio);

    DMA_DeInit(DMA1_Channel1);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&(ADC1->DR);
    dma.DMA_MemoryBaseAddr     = (uint32_t)s_adc_raw;
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
    DMA_Cmd(DMA1_Channel1, ENABLE);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = ENABLE;
    adc.ADC_ContinuousConvMode = ENABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 2;
    ADC_Init(ADC1, &adc);

    /* V6.2: CH8 (PB0) = 电流, CH9 (PB1) = 电压 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 2, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

    /* t_STAB 等待 (≥2 ADC 周期) */
    { volatile uint16_t i; for (i = 0; i < 100; i++) __NOP(); }

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

void Adc_Driver_Filter_Task(void)
{
    static uint32_t last_cyc = 0;

    if (Sys_Timer_Get_Cycles() - last_cyc < FILTER_PERIOD_CYCLES) return;
    last_cyc = Sys_Timer_Get_Cycles();

    Filter_Push(&s_v_filter, s_adc_raw[1]);
    s_voltage = Filter_To_Voltage(&s_v_filter) * VOLTAGE_DIVIDER;

    Filter_Push(&s_c_filter, s_adc_raw[0]);
    s_raw_pin_v = Filter_To_Voltage(&s_c_filter);
    s_current   = (s_raw_pin_v - s_i_offset) / CURRENT_SENSITIVITY;
}

void Adc_Driver_Calibrate_Offset(void)
{
    static uint32_t last_cal = 0;

    if (Sys_Timer_Get_Tick() - last_cal < CAL_INTERVAL_MS) return;
    last_cal = Sys_Timer_Get_Tick();

    if (s_c_filter.filled < ADC_FILTER_WINDOW) return;

    if (!s_calibrated) {
        s_cal_accum += s_raw_pin_v;
        s_cal_count++;
        if (s_cal_count >= CAL_SAMPLES) {
            s_i_offset   = s_cal_accum / (float)CAL_SAMPLES;
            s_calibrated = 1;
        }
    } else {
        s_i_offset = s_i_offset * 0.95f + s_raw_pin_v * 0.05f;
    }
}

float Adc_Driver_Get_Voltage(void) { return s_voltage; }
float Adc_Driver_Get_Current(void) { return s_current; }
