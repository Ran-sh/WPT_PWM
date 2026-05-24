/**
 ******************************************************************************
 * @file    Hardware/KEY.h
 * @brief   物理按键驱动 —— 公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          硬件接口: PB12 (按键0), PB13 (按键1)
 *          按键特性: 内部上拉 (IPU), 按下为低电平 (GND), 释放为高电平 (3.3V)
 *          功能: 10ms 去抖 + 200ms 双击窗口 + 3000ms 长按检测
 ******************************************************************************
 */

#ifndef __KEY_H
#define __KEY_H

void    KEY_Init(void);
void    KEY_Scan_All(void);
uint8_t KEY_Get_Event(uint8_t key_id);

/*
 * KEY_Task: 非阻塞按键扫描任务 (时间戳差值法, 内部 10ms 周期)
 * 替代原先在 SysTick_Handler 中调用的 KEY_Scan_All()
 */
void    KEY_Task(void);

#endif
