/**
 ******************************************************************************
 * @file    Hardware/ADC.c
 * @brief   模拟量采集驱动 (ADC1 + DMA1 双通道 + 独立滤波任务)
 * @note    ADC_Filter_Task 每 2ms 推入样本并更新平均值,
 *          Get_Real_Voltage/Current 直接返回预计算结果, O(1) 零开销
 *
 *          DMA 后台循环刷新 ADC_ConvertedValue[2]
 *          [0]=电流(PA0), [1]=电压(PA1)
 *
 *          滤波延迟: 16×2ms=32ms (vs 旧方案 16×200ms=3.2s)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"

volatile uint16_t ADC_ConvertedValue[2];   /* DMA 循环刷新 */

#define VREF_MCU        3.30f
#define I_SENSITIVITY   0.132f
#define I_OFFSET        1.65f
#define ADC_FILTER_WINDOW  16              /* 滑动窗口: 16 样本, 32ms 响应 */

/* ── 滤波私有变量 ── */
static float    s_voltage  = 0.0f;
static float    s_current  = 0.0f;

static uint16_t s_vbuf[ADC_FILTER_WINDOW] = {0};
static uint8_t  s_vidx = 0, s_vfilled = 0;
static uint32_t s_vaccum = 0;                          /* 电压运行累加器 */

static uint16_t s_cbuf[ADC_FILTER_WINDOW] = {0};
static uint8_t  s_cidx = 0, s_cfilled = 0;
static uint32_t s_caccum = 0;                          /* 电流运行累加器 */

void ADC_DMA_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(ADC1->DR);
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)ADC_ConvertedValue;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = 2;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 2;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

    /*
     * STM32 手册: ADC 使能后须等待 t_STAB (≥2 ADC 周期) 才能校准,
     * 否则校准值包含上电噪声, 导致基准漂移。
     * ADC 时钟 = 72MHz/6 = 12MHz → 2 周期 = 167ns, 这里等 ~2μs 足够
     */
    {
        volatile uint16_t adc_stab;
        for (adc_stab = 0; adc_stab < 100; adc_stab++) { __NOP(); }
    }

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

/**
 * @brief  独立滤波任务 — 每 2ms 推入样本并更新平均值
 * @note   由 main.c 主循环调用, 时间戳差值法保证 2ms 节拍
 *         与 UI 调用频率完全解耦
 */
void ADC_Filter_Task(void)
{
    static uint32_t last = 0;
    float pin_v;

    if (SysTimer_GetTick() - last < 2) return;
    last = SysTimer_GetTick();

    /* ── 电压通道 (运行累加器 O(1)) ── */
    {
        uint16_t old_v = s_vbuf[s_vidx];              /* 先保存最旧值 */
        s_vbuf[s_vidx] = ADC_ConvertedValue[1];       /* 覆盖 */
        s_vaccum += s_vbuf[s_vidx];                   /* 加新值 */
        if (s_vfilled >= ADC_FILTER_WINDOW)
            s_vaccum -= old_v;                        /* 减真正的旧值 */
        s_vidx = (s_vidx + 1) & (ADC_FILTER_WINDOW - 1);
        if (s_vfilled < ADC_FILTER_WINDOW) s_vfilled++;
    }
    s_voltage = ((float)s_vaccum / s_vfilled / 4095.0f) * VREF_MCU * 20.0f;

    /* ── 电流通道 (运行累加器 O(1)) ── */
    {
        uint16_t old_c = s_cbuf[s_cidx];              /* 先保存最旧值 */
        s_cbuf[s_cidx] = ADC_ConvertedValue[0];       /* 覆盖 */
        s_caccum += s_cbuf[s_cidx];                   /* 加新值 */
        if (s_cfilled >= ADC_FILTER_WINDOW)
            s_caccum -= old_c;                        /* 减真正的旧值 */
        s_cidx = (s_cidx + 1) & (ADC_FILTER_WINDOW - 1);
        if (s_cfilled < ADC_FILTER_WINDOW) s_cfilled++;
    }

    pin_v     = ((float)s_caccum / s_cfilled / 4095.0f) * VREF_MCU;
    s_current = (pin_v - I_OFFSET) / I_SENSITIVITY;
}

float Get_Real_Voltage(void) { return s_voltage; }
float Get_Real_Current(void) { return s_current; }
