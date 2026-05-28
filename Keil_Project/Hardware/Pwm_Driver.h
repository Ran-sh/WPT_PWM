/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.h
 * @brief   全桥 PWM 驱动 — 公开接口 (纯硬件抽象)
 * @note    TIM1 全桥: CH1+CH1N / CH2+CH2N, 50% 固定占空比
 *          频率范围 95kHz~150kHz, 死区 1000ns
 *
 *          安全设计:
 *          - 上电 MOE 关断 (Pwm_Driver_Init 后无输出)
 *          - Pwm_Driver_Enable/Disable 控制 MOE
 *          - ARR+CCR 预载使能, Set_Frequency 原子更新 (UDIS 批量加载)
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
uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz);   /* 返回实际设置值 */
uint32_t Pwm_Driver_Get_Frequency(void);

#endif /* PWM_DRIVER_H */
