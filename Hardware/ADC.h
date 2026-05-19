/**
 ******************************************************************************
 * @file    Hardware/ADC.h
 * @brief   模拟量采集驱动 —— 公开接口
 * @note    ADC1 + DMA1_Channel1 双通道循环扫描
 *          PA0=电流(CC6920-10A), PA1=电压(20:1分压)
 *          ADC_Filter_Task: 2ms 独立滤波任务, 与调用方解耦
 ******************************************************************************
 */

#ifndef __ADC_H
#define __ADC_H

void  ADC_DMA_Init(void);
void  ADC_Filter_Task(void);    /* 2ms 周期, 主循环调用, 更新滑动平均 */
float Get_Real_Voltage(void);   /* 直接返回预计算值, 无采集开销 */
float Get_Real_Current(void);

#endif
