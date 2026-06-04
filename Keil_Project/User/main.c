/**
 ******************************************************************************
 * @file    User/Main.c
 * @brief   无线充电 PWM 系统 — 程序入口 (V6.2 TFT 版)
 * @note    V6.2: TFT 彩屏版, ST7735S 128×160 SPI 彩屏
 *
 *          上电流程: 硬件 Init → 系统时基 → 联网 → 主循环
 *
 *          主循环调度 (全非阻塞):
 *            Key_Driver_Task       10ms 按键轮询
 *            Adc_Driver_Filter_Task    ~2ms 模拟量采集
 *            Ui_Controller_Task    200ms 界面刷新
 *            App_Network_Task      指令接收 + 遥测发送
 *            Inverter_Control_*    软启动扫频 + 频率斜坡
 *            Led_Driver_Task       6 LED 心跳 + 闪烁
 *            Buzzer_Driver_Task    蜂鸣器调度
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Timer.h"
#include "Led_Driver.h"
#include "Tft_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Adc_Driver.h"
#include "Key_Driver.h"
#include "Esp8266_Driver.h"
#include "Ui_Controller.h"
#include "App_Network.h"
#include "Buzzer_Driver.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 阶段 1: 硬件层初始化 (MOE 关断, PowerContrl=OFF, 全桥零输出) */
    Pwm_Driver_Init();

    /* ── PB10 PowerContrl: 12V 动力电源闸, 初始关断, 待机零功耗 ── */
    {
        GPIO_InitTypeDef gpio;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        gpio.GPIO_Pin   = GPIO_Pin_10;
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOB, &gpio);
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);  /* OFF: 12V 动力电源断开 */
    }

    Tft_Driver_Init();
    Led_Driver_Init();
    Buzzer_Driver_Init();
    Adc_Driver_Init();
    Key_Driver_Init();

    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(3, 2, "无线充电", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(5, 3, "启动中...", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);

    /* 阶段 2: 系统时基 */
    Sys_Timer_Init();

    /* 阶段 2.5: 独立看门狗 (LSI 40kHz, 分频 64 → 1.6s 超时) */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1000);
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

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
        Buzzer_Driver_Task();

        IWDG_ReloadCounter();  /* 喂狗 */
        __WFI();               /* 休眠等 SysTick 中断 */
    }
}
