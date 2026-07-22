#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include "stm32f10x.h"

#define PWM_DRIVER_FREQ_MIN_HZ   20000U
#define PWM_DRIVER_FREQ_MAX_HZ  200000U
#define PWM_DRIVER_DEADTIME_NS   1000

/** @brief 初始化TIM1全桥PWM，保持计数器和主输出关闭 */
void     Pwm_Driver_Init(void);
/** @brief 按当前设定频率启动计数器并开启主输出 */
void     Pwm_Driver_Enable(void);
/** @brief 先关闭主输出，再停止计数器 */
void     Pwm_Driver_Disable(void);
/** @brief 设置PWM频率并在同一更新事件中装载相关寄存器
 *  @param freq_hz 目标频率，单位为Hz，超出95kHz至150kHz时自动钳位
 *  @retval 实际设定频率，可能因整数分频与目标值存在微小偏差
 */
/** @brief 设置20kHz至200kHz范围内的PWM频率并保持50%占空比 */
uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz);
/** @brief 获取当前PWM频率，单位为Hz */
uint32_t Pwm_Driver_Get_Frequency(void);
/** @brief 读取TIM1计数器和主输出的实际使能状态
 *  @retval 1=计数器与MOE均开启, 0=至少一项关闭
 */
uint8_t  Pwm_Driver_Is_Enabled(void);

#endif /* 全桥PWM驱动接口结束 */
