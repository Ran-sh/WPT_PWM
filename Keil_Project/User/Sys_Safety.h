/**
 ******************************************************************************
 * @file    User/Sys_Safety.h
 * @brief   系统安全监测 — 过流/过压/PB10 电源控制
 * @note    V14: 从 Ui_Controller Phase 7/8 剥离, 在共性任务层统一处理
 *          每个主循环周期都调用, 不依赖 UI 刷新频率
 ******************************************************************************
 */

#ifndef SYS_SAFETY_H
#define SYS_SAFETY_H

#include "stm32f10x.h"

void Sys_Safety_Task(void);

float Sys_Safety_Get_EMA_Voltage(void);
float Sys_Safety_Get_EMA_Current(void);

#endif /* SYS_SAFETY_H */
