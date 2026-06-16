/**
 ******************************************************************************
 * @file    User/Sys_Run.h
 * @brief   系统运行状态机 — 按状态分发 Task 子集
 * @note    V14: 4 个模式 (IDLE/SWEEP/RUNNING/FAULT) 各含不同 Task 组合
 ******************************************************************************
 */

#ifndef SYS_RUN_H
#define SYS_RUN_H

void Sys_Run_Idle(void);
void Sys_Run_Sweep(void);
void Sys_Run_Running(void);
void Sys_Run_Fault(void);

#endif /* SYS_RUN_H */
