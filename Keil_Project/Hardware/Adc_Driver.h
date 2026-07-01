/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.h
 * @brief   ADC 模拟量采集驱动 — V4.3.2
 *
 *  连接: PB0=ADC_CH8 电流(CC6920BSO 132mV/A), PB1=ADC_CH9 电压(20:1分压)
 *        ADC1 + DMA1 双通道扫描, 144241 时钟周期触发(与 100kHz PWM 互质)
 *        64 样本滑动窗口, 编译期 HSE=8MHz 断言
 ******************************************************************************
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include "stm32f10x.h"

/** @brief 初始化 ADC1 + DMA1 双通道连续扫描模式 */
void  Adc_Driver_Init(void);
/** @brief 周期调用: 推进 ADC 校准状态机 (冷启动自测算 / Flash 固化直达) */
void  Adc_Driver_Calibrate_Offset(void);
/** @brief 50Hz 周期 DMA 中断回调: 推入滑动窗口并更新 EMA */
void  Adc_Driver_Filter_Task(void);
/** @brief 从 Flash 固化值写入校准参数 (替代冷启动自测算)
 *  @param i_offset   电流零点偏移 (A)
 *  @param v_gain     电压增益系数
 *  @param freq_trim  频率微调 (Hz) */
void  Adc_Driver_Set_Calibration(float i_offset, float v_gain, int32_t freq_trim);
/** @brief 强制解锁校准状态机 (双副本全损时冷启动自测算前调用) */
void  Adc_Driver_Force_Recalibrate(void);
/** @brief 获取校准后的电流零点偏移值 (用于回写 Flash 固化) */
float Adc_Driver_Get_Current_Offset(void);
/** @brief 获取 EMA 滤波后的实时电压值 (V) */
float Adc_Driver_Get_Voltage(void);
/** @brief 获取 EMA 滤波后的实时电流值 (A) */
float Adc_Driver_Get_Current(void);

#endif /* ADC_DRIVER_H */
