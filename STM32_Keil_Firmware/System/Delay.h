/* ⚠️ 已废弃: 此模块直接重编程 SysTick 寄存器, 与 System/SysTimer 的 1ms 中断冲突。
   项目中所有延时已迁移至 SysTimer_DelayMs(), 请勿再使用此模块。 */
#ifndef __DELAY_H
#define __DELAY_H

void Delay_us(uint32_t us);
void Delay_ms(uint32_t ms);
void Delay_s(uint32_t s);

#endif
