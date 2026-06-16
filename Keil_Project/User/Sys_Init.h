/**
 ******************************************************************************
 * @file    User/Sys_Init.h
 * @brief   系统上电初始化 — 阶段0-4, 拆分自 main()
 * @note    V14: 所有硬件 init 从 main() 提取到此, main() 仅调用 + 运行状态机
 ******************************************************************************
 */

#ifndef SYS_INIT_H
#define SYS_INIT_H

void Sys_Clamp_ESP(void);
void Sys_Hardware_Init(void);
void Sys_Startup_Screen(void);
void Sys_Post_Init(void);

#endif /* SYS_INIT_H */
