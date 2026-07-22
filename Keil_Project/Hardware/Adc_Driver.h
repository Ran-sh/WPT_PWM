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
/** @brief DMA1通道1传输完成中断入口，仅复制双通道原始快照 */
void  Adc_Driver_DMA_Transfer_Complete_ISR(void);
/** @brief 获取最近一次完成显示/安全滤波的采样序号 */
uint32_t Adc_Driver_Get_Processed_Sequence(void);
/** @brief 推进非阻塞校准状态机
 *  @param power_enabled 12V电源使能状态；为0时才允许推进校准
 */
void  Adc_Driver_Calibration_Task(uint8_t power_enabled);
/** @brief 获取当前校准状态 */
Adc_Driver_Calibration_State Adc_Driver_Get_Calibration_State(void);
/** @brief 消费一次校准完成事件，用于请求后台持久化 */
uint8_t Adc_Driver_Take_Calibration_Completed(void);
/** @brief 判断最近20ms内是否收到有效的双通道采样
 *  @retval 1表示采样有效，0表示采样超时
 */
uint8_t Adc_Driver_Is_Data_Fresh(void);
/** @brief 写入从外部存储器读取的校准参数 */
void  Adc_Driver_Set_Calibration(float i_offset, float v_gain, int32_t freq_trim);
/** @brief 强制解锁校准状态机，供配置副本全部失效时重新校准 */
void  Adc_Driver_Force_Recalibrate(void);
/** @brief 获取当前电流零点值，供参数持久化使用 */
float Adc_Driver_Get_Current_Offset(void);
/** @brief 获取当前电压校准增益 */
float Adc_Driver_Get_Voltage_Gain(void);
/** @brief 获取64点显示窗口电压 */
float Adc_Driver_Get_Display_Voltage(void);
/** @brief 获取64点显示窗口电流 */
float Adc_Driver_Get_Display_Current(void);
/** @brief 获取8点快速安全窗口电流 */
float Adc_Driver_Get_Safety_Current(void);
#endif /* 模数转换驱动接口结束 */
