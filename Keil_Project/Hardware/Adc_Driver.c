/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.c
 * @brief   模拟量采集驱动 — 实现 (V9)
 * @note    PB0=ADC_CH8 电流, PB1=ADC_CH9 电压
 ******************************************************************************
 */

#include "Adc_Driver.h"
#include "Sys_Timer.h"

#define ADC_DRIVER_VREF_MCU            3.30f
#define ADC_DRIVER_VOLTAGE_DIVIDER     20.0f
#define ADC_DRIVER_CURRENT_SENSITIVITY 0.132f   /* CC6920BSO 标称灵敏度 132mV/A */
#define ADC_DRIVER_CURRENT_CAL_FACTOR  0.602f   /* 灵敏度校准系数: 显示值=原始值*系数, 匹配实际电流 */
#define ADC_DRIVER_FILTER_WINDOW       64

/*
 * 采样周期: 144241 CPU 周期, 与 100kHz PWM 互质 → 覆盖 720 个相位
 * gcd(144241, 720) = 1  ✓
 */
typedef char Adc_Driver_Assert_HSE_72MHz[(HSE_VALUE == 8000000) ? 1 : -1];  /* 编译期断言: 必须是 8MHz HSE → PLL → 72MHz, 否则互质采样假设失效 */

#define ADC_DRIVER_FILTER_PERIOD_CYCLES 144241

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
static volatile uint16_t s_adc_raw[2];   /* DMA 循环刷新: [0]=电流, [1]=电压 */

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

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

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

    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 2, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

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

    {
        uint32_t now = Sys_Timer_Get_Cycles();
        if (now - last_cyc < ADC_DRIVER_FILTER_PERIOD_CYCLES) return;
        last_cyc = now;
    }

    Adc_Driver_Filter_Push(&s_v_filter, s_adc_raw[1]);
    s_voltage = Adc_Driver_Filter_To_Voltage(&s_v_filter) * ADC_DRIVER_VOLTAGE_DIVIDER;

    Adc_Driver_Filter_Push(&s_c_filter, s_adc_raw[0]);
    s_raw_pin_v = Adc_Driver_Filter_To_Voltage(&s_c_filter);
    s_current   = (s_raw_pin_v - s_i_offset) / ADC_DRIVER_CURRENT_SENSITIVITY * ADC_DRIVER_CURRENT_CAL_FACTOR;
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
