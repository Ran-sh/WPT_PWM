/**
 ******************************************************************************
 * @file    Hardware/Adc_Driver.h
 * @brief   ADC 模拟量采集驱动 — V5.0.1
 * @note    ADC1 + DMA1 双通道扫描: PB0=CH8 电流(CC6920BSO), PB1=CH9 电压(20:1分压)
 *          64 样本滑动窗口, 144241 周期互质采样
 ******************************************************************************
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    ADC_DRIVER_CAL_UNINITIALIZED = 0,
    ADC_DRIVER_CAL_FILLING,
    ADC_DRIVER_CAL_CALIBRATING,
    ADC_DRIVER_CAL_READY,
    ADC_DRIVER_CAL_ERROR
} Adc_Driver_Calibration_State;

void  Adc_Driver_Init(void);
void  Adc_Driver_Filter_Task(void);
/** @brief DMA1 Channel1传输完成中断入口，仅复制原始双通道快照 */
void  Adc_Driver_DMA_Transfer_Complete_ISR(void);
/** @brief 获取已完成的双通道采样序号 */
uint32_t Adc_Driver_Get_Sample_Sequence(void);
/** @brief 获取最近一次双通道采样完成的毫秒时刻 */
uint32_t Adc_Driver_Get_Last_Sample_Tick(void);
/** @brief 获取最近一次完成显示/安全滤波的采样序号 */
uint32_t Adc_Driver_Get_Processed_Sequence(void);
/* 电流零点校准 (偏移默认 1.65V = CC6920BSO 零电流中点) */
void  Adc_Driver_Calibrate_Offset(void);
/** @brief 非阻塞校准状态机；仅power_enabled=0时推进 */
void  Adc_Driver_Calibration_Task(uint8_t power_enabled);
/** @brief 获取当前校准状态 */
Adc_Driver_Calibration_State Adc_Driver_Get_Calibration_State(void);
/** @brief 消费一次校准完成事件，用于请求后台持久化 */
uint8_t Adc_Driver_Take_Calibration_Completed(void);
/** @brief 最近20ms内是否收到有效DMA双通道样本 */
uint8_t Adc_Driver_Is_Data_Fresh(void);
/** @brief V4.3.0: 从 Flash 固化值写入校准参数 (替代每次上电自测算) */
void  Adc_Driver_Set_Calibration(float i_offset, float v_gain, int32_t freq_trim);
/** @brief V4.3.0: 强制解锁校准状态机 (双副本全损→冷启动自测算前调用) */
void  Adc_Driver_Force_Recalibrate(void);
/** @brief V4.3.0: 获取当前 ADC 电流零点值 (用于回写 Flash) */
float Adc_Driver_Get_Current_Offset(void);
/** @brief 获取当前电压校准增益 */
float Adc_Driver_Get_Voltage_Gain(void);
/** @brief 获取64点显示窗口电压 */
float Adc_Driver_Get_Display_Voltage(void);
/** @brief 获取64点显示窗口电流 */
float Adc_Driver_Get_Display_Current(void);
/** @brief 获取8点快速安全窗口电流 */
float Adc_Driver_Get_Safety_Current(void);
/** @brief 兼容接口，等价于Adc_Driver_Get_Display_Voltage */
float Adc_Driver_Get_Voltage(void);
/** @brief 兼容接口，等价于Adc_Driver_Get_Display_Current */
float Adc_Driver_Get_Current(void);

#endif /* ADC_DRIVER_H */
