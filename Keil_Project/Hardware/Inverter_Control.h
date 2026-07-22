#ifndef INVERTER_CONTROL_H
#define INVERTER_CONTROL_H

#include "stm32f10x.h"

#define SOFTSTART_STEP_MS         10

#define FREQ_RAMP_STEP_HZ         1000
#define FREQ_RAMP_STEP_MS         10

typedef enum {
    INVERTER_CONTROL_SS_STATE_IDLE   = 0,
    INVERTER_CONTROL_SS_STATE_SWEEP  = 1,
    INVERTER_CONTROL_SS_STATE_DONE   = 2,
    INVERTER_CONTROL_SS_STATE_FAULT  = 3
} Inverter_Control_Soft_Start_State;

typedef enum {
    INVERTER_CONTROL_STARTUP_LOW  = 0,
    INVERTER_CONTROL_STARTUP_HIGH = 1
} Inverter_Control_Startup_Band;

/** @brief 频率斜坡状态 */
typedef enum {
    INVERTER_CONTROL_RAMP_IDLE  = 0,
    INVERTER_CONTROL_RAMP_ACTIVE = 1
} Inverter_Control_Ramp_State;

/** @brief 在空闲状态下触发150kHz至100kHz的软启动扫频 */
/** @brief 保存下一次软启动使用的经校验档位配置；不改变正在执行的扫频快照
 *  @param band 启动档位
 *  @param low_freq_hz 低频档目标频率，20kHz至99.9kHz且步进100Hz
 *  @param high_freq_hz 高频档目标频率，100kHz至200kHz且步进1kHz
 */
void     Inverter_Control_Configure_Startup(Inverter_Control_Startup_Band band,
                                             uint32_t low_freq_hz,
                                             uint32_t high_freq_hz);
/** @brief 获取当前或下一次扫频锁定的起始频率，单位为Hz */
uint32_t Inverter_Control_Get_Sweep_Start_Freq(void);
/** @brief 获取当前或下一次扫频锁定的目标频率，单位为Hz */
uint32_t Inverter_Control_Get_Sweep_Target_Freq(void);

/** @brief 在空闲状态下按已配置档位触发非阻塞软启动扫频 */
void     Inverter_Control_Soft_Start_Trigger(void);
/** @brief 周期推进软启动状态机，每10ms降低200Hz */
/** @brief 每10ms按已锁定档位步进推进软启动状态机 */
void     Inverter_Control_Soft_Start_Task(void);
/** @brief 停止逆变器并返回空闲状态，同时关闭PWM主输出 */
void     Inverter_Control_Soft_Start_Stop(void);
/** @brief 关闭PWM主输出并锁存故障状态 */
void     Inverter_Control_Soft_Start_Fault(void);
/** @brief 清除故障锁存、取消频率斜坡并返回空闲状态 */
void     Inverter_Control_Soft_Start_Reset(void);
/** @brief 获取当前软启动状态；该32位对齐读操作无需关闭中断 */
Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void);
/** @brief 获取软启动当前频率，单位为Hz */
uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void);

/** @brief 触发运行频率渐变，仅在软启动完成后有效
 *  @param target_hz 目标频率，单位为Hz，超出95kHz至150kHz时自动钳位
 */
void     Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz);
/** @brief 周期推进频率斜坡状态机，每10ms变化1kHz */
void     Inverter_Control_Freq_Ramp_Task(void);
/** @brief 取消频率渐变，不改变当前PWM输出频率 */
void     Inverter_Control_Freq_Ramp_Cancel(void);
#endif /* 逆变器控制接口结束 */
