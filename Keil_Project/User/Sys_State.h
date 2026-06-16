/**
 ******************************************************************************
 * @file    User/Sys_State.h
 * @brief   系统全局状态机 V1.0 — 公开接口
 * @note    定义系统级运行模式, 替代 while(1) 无条件顺序轮询
 *          状态驱动 Task 子集, 安全生产
 ******************************************************************************
 */

#ifndef SYS_STATE_H
#define SYS_STATE_H

#include "stm32f10x.h"

/** @brief 系统全局运行模式 */
typedef enum {
    SYS_STATE_INIT    = 0,  /* 上电初始化中, 不进入主循环业务 */
    SYS_STATE_IDLE    = 1,  /* 待机 – 主菜单/配网/故障页, PWM 关断 */
    SYS_STATE_SWEEP   = 2,  /* 软启动扫频中 150k→100kHz */
    SYS_STATE_RUNNING = 3,  /* 正常运行 – 仪表盘页面, PWM 稳态输出 */
    SYS_STATE_FAULT   = 4   /* 过流锁存 – 故障页面, 等待按键复位 */
} Sys_State;

/** @brief 全局状态变量 (ISR 可写, 主循环可读) */
extern volatile Sys_State g_sys_state;

#endif /* SYS_STATE_H */
