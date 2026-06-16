/**
 ******************************************************************************
 * @file    User/Sys_Init.h
 * @brief   系统上电初始化 — 阶段0-4, 拆分自 main()
 * @note    V14: 所有硬件 init 从 main() 提取到此, main() 仅调用 + 运行状态机
 ******************************************************************************
 */

#ifndef SYS_INIT_H
#define SYS_INIT_H

/** @brief 阶段0: 最早钳位 ESP8266 (RST=0, CH_PD=0) */
void Sys_Clamp_ESP(void);
/** @brief 阶段1: 硬件层初始化 (PWM+TFT+LED+Buzzer+ADC+Key+PB10拉低) */
void Sys_Hardware_Init(void);
/** @brief 显示启动欢迎画面 "WPT-PWM 启动中..." */
void Sys_Startup_Screen(void);
/** @brief 阶段2-4: 系统时基 + IWDG看门狗 + 开机自动联网 */
void Sys_Post_Init(void);

#endif /* SYS_INIT_H */
