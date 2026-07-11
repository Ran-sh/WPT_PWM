/**
 ******************************************************************************
 * @file    Hardware/Pwm_Driver.h
 * @brief   全桥 PWM 驱动 — V5.0.1
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

/** @brief 初始化 TIM1 全桥 PWM (计数器+MOE 全关, 零输出) */
void     Pwm_Driver_Init(void);
/** @brief 开启 PWM 输出 (同时启动计数器 + MOE, 从 Set_Frequency 设定的频率开始) */
void     Pwm_Driver_Enable(void);
/** @brief 关闭 PWM 输出 (关 MOE + 关计数器, 全停) */
void     Pwm_Driver_Disable(void);
/** @brief 设置 PWM 频率并原子更新寄存器 (自动钳位 95k~150kHz, 强制偶数 ticks 防偏磁)
 *  @param  freq_hz 目标频率 (Hz)
 *  @return 实际设定频率 (Hz, 可能因整数分频偏离传入值)
 */
uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz);
/** @brief 获取当前 PWM 频率 (Hz) */
uint32_t Pwm_Driver_Get_Frequency(void);

#endif /* PWM_DRIVER_H */
