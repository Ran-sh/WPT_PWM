/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.h
 * @brief   模拟量采集驱动 — 公开接口
 * @note    ADC1 + DMA1 双通道扫描: PA0=电流(CC6920), PA1=电压(分压 20:1)
 *          Adc_Driver_Filter_Task 每 ~2ms 推入样本更新滑动平均
 *          Get_Voltage/Get_Current 直接返回预计算值, O(1)
 *
 *          自动零点校准: Adc_Driver_Calibrate_Offset 在逆变器未工作时调用
 *          首次采集 50 样本 (0.5s) 取平均, 后续 EMA 慢速追踪温漂
 ******************************************************************************
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

void  Adc_Driver_Init(void);
void  Adc_Driver_Filter_Task(void);             /* ~2ms 周期, 主循环调用 */
void  Adc_Driver_Calibrate_Offset(void);        /* PWM 关断时调用, 自动追踪电流零点 */
float Adc_Driver_Get_Voltage(void);
float Adc_Driver_Get_Current(void);

#endif /* ADC_DRIVER_H */
