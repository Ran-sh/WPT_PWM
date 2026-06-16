/**
 ******************************************************************************
 * @file    User/Sys_Run.h
 * @brief   系统运行状态机 — 按状态分发 Task 子集
 * @note    V14: 4 个模式 (IDLE/SWEEP/RUNNING/FAULT) 各含不同 Task 组合
 ******************************************************************************
 */

#ifndef SYS_RUN_H
#define SYS_RUN_H

/** @brief IDLE 模式: 待机, PWM 关断, 仅 UI+LED+Buzzer */
void Sys_Run_Idle(void);
/** @brief SWEEP 模式: 软启动扫频 150k→100kHz, 完成自动转 RUNNING */
void Sys_Run_Sweep(void);
/** @brief RUNNING 模式: PWM 稳态输出 + 频率斜坡活跃 */
void Sys_Run_Running(void);
/** @brief FAULT 模式: 过流锁存, PWM 关断, 取消所有斜坡, 等待按键复位 */
void Sys_Run_Fault(void);

#endif /* SYS_RUN_H */
