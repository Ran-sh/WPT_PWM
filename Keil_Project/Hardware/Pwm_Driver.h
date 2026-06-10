/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.h
 * @brief   全桥 PWM 驱动 — 公开接口
 * @note    TIM1 默认映射: PA8=CH1, PA9=CH2, PB13=CH1N, PB14=CH2N
 *          50% 固定占空比, 频率 95~150kHz, 死区 1000ns
 ******************************************************************************
 */

#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include "stm32f10x.h"

#define PWM_DRIVER_FREQ_MIN_HZ   95000
#define PWM_DRIVER_FREQ_MAX_HZ  150000
#define PWM_DRIVER_DEADTIME_NS   1000

void     Pwm_Driver_Init(void);
void     Pwm_Driver_Enable(void);
void     Pwm_Driver_Disable(void);
uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz);
uint32_t Pwm_Driver_Get_Frequency(void);

#endif /* PWM_DRIVER_H */
