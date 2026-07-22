#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include "stm32f10x.h"

/** @brief 初始化1ms全局系统时基 */
void     Sys_Timer_Init(void);
/** @brief 在系统滴答中断内递增毫秒计数，禁止业务代码调用 */
void     Sys_Timer_Inc_Tick(void);
/** @brief 获取32位无符号毫秒时间戳，约49.7天自然回绕 */
uint32_t Sys_Timer_Get_Tick(void);
/** @brief 执行低功耗毫秒延时，仅允许在启动阶段和看门狗启用前使用
 *  @param ms 延时毫秒数，等待期间由系统滴答中断唤醒
 */
void     Sys_Timer_Delay_Ms(uint32_t ms);

#endif /* 系统时基接口结束 */
