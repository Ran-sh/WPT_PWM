/**
 ******************************************************************************
 * @file    Hardware/PWM.h
 * @brief   全桥 PWM 驱动 —— 公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          硬件接口: TIM1 高级定时器 (部分重映射)
 *            CH1 : PA8 (上管左)   CH1N: PA7 (下管左)
 *            CH2 : PA9 (上管右)   CH2N: PB0 (下管右)
 *          特性: 互补输出 + 死区插入 (DEADTIME_NS 宏可调) + 50% 固定占空比
 *          控制方式: PFM (脉冲频率调制), 通过改变频率调节谐振功率
 *          安全红线: 频率 >= 95kHz 绝对硬限幅, 低于此值钳位拒绝
 *          驱动芯片: IR2103S (3.3V 逻辑兼容)
 ******************************************************************************
 */

#ifndef __PWM_H
#define __PWM_H

#include "stm32f10x.h"

/* ── 死区时间宏定义 (ns), 修改此值即可调整硬件死区 ── */
#define DEADTIME_NS  1000

/* ── 软启动扫频参数 (与 PWM.c 内部实现一致, 供 UI 等外部模块使用) ── */
#define SOFTSTART_START_FREQ_HZ   150000UL
#define SOFTSTART_TARGET_FREQ_HZ  100000UL

/* ── 软启动状态 ── */
typedef enum {
    SS_IDLE  = 0,   /* 待机: MOE 关, 等待触发 */
    SS_SWEEP = 1,   /* 扫频中: 150kHz → 100kHz */
    SS_DONE  = 2,   /* 完成: 100kHz 谐振稳态运行 */
    SS_FAULT = 3    /* 故障: 过流保护触发, KEY1 复位退出 */
} SoftStart_State_t;

void     PWM_Init(void);
uint32_t PWM_SetFrequency(uint32_t freq_Hz);
uint32_t PWM_GetFrequency(void);
void     PWM_Enable(void);
void     PWM_Disable(void);

/*
 * 非阻塞软启动扫频 (150kHz → 100kHz, 200Hz/步, 20ms/步, 共 250 步 ≈ 5s)
 *   Trigger: KEY0 或 PC "ON" 触发, 仅 SS_IDLE 时有效
 *   Task:    主循环每轮调用, 内部 2ms 时间戳节拍
 *   Stop:    KEY1 或 PC "OFF" 关断, 回复 SS_IDLE
 *   每次 Trigger 必定从 150kHz 重新开始, 不复用上次状态
 */
void               Inverter_SoftStart_Trigger(void);
void               Inverter_SoftStart_Task(void);
void               Inverter_SoftStart_Stop(void);
void               Inverter_SoftStart_Fault(void);   /* 过流保护: 紧急关断 + 故障锁存 */
SoftStart_State_t  Inverter_SoftStart_GetState(void);
uint32_t           Inverter_SoftStart_GetCurrentFreq(void);

#endif
