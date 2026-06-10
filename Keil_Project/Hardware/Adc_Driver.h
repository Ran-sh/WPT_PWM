/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.h
 * @brief   模拟量采集驱动 — 公开接口 (V6.2)
 * @note    ADC1 + DMA1 双通道扫描: PB0=电流 CH8 (CC6920BSO), PB1=电压 CH9 (20:1分压)
 *          64 样本滑动窗口, 144241 周期互质采样
 ******************************************************************************
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

void  Adc_Driver_Init(void);
void  Adc_Driver_Filter_Task(void);
void  Adc_Driver_Calibrate_Offset(void);
float Adc_Driver_Get_Voltage(void);
float Adc_Driver_Get_Current(void);

#endif /* ADC_DRIVER_H */
