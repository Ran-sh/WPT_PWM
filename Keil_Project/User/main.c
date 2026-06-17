/**
 ******************************************************************************
 * @file    User/Main.c
 * @brief   无线充电 PWM 系统 — 程序入口
 * @note    V3.0.0: 全面重构 — 面向对象模块化架构
 *
 *          上电流程: 硬件 Init → 系统时基 → 联网 → 主循环
 *
 *          主循环调度 (全非阻塞):
 *            Key_Driver_Task       10ms 按键轮询
 *            Adc_Driver_Filter_Task    ~2ms 模拟量采集
 *            Ui_Controller_Task    200ms 界面刷新
 *            App_Network_Task      指令接收 + 遥测发送
 *            Inverter_Control_*    软启动扫频 + 频率斜坡
 *            Led_Driver_Task       心跳 + 闪烁
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Timer.h"
#include "Led_Driver.h"
#include "Oled_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Key_Driver.h"
#include "Esp8266_Driver.h"
#include "Ui_Controller.h"
#include "App_Network.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 阶段 1: 硬件层初始化 (MOE 关断, 全桥无输出) */
    Pwm_Driver_Init();
    Oled_Driver_Init();
    Led_Driver_Init();
    Adc_Driver_Init();
    Key_Driver_Init();

    Oled_Driver_Clear();
    Oled_Driver_Show_String(1, 1, "Wireless Charge");
    Oled_Driver_Show_String(2, 1, "Booting ESP... ");

    /* 阶段 2: 系统时基 */
    Sys_Timer_Init();

    /* 阶段 2.5: 独立看门狗 (LSI 40kHz, 分频 64 → 1.6s 超时) */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1000);  /* 1000 / (40k/64) = 1.6s */
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;  /* 调试/下载时冻结看门狗, 避免 Flash Download failed */

    /* 阶段 3: 自动启动联网 (ESP 非阻塞初始化) */
    App_Network_Start_Connect();

    /* 阶段 4: 主循环 (全非阻塞, 空闲休眠) */
    while (1) {
        Key_Driver_Task();
        Adc_Driver_Filter_Task();
        Ui_Controller_Task();
        App_Network_Task();
        Inverter_Control_Soft_Start_Task();
        Inverter_Control_Freq_Ramp_Task();
        Led_Driver_Task();

        IWDG_ReloadCounter();  /* 喂狗 */
        __WFI();  /* 休眠等 SysTick 中断, 空闲电流 30mA→5mA */
    }
}
