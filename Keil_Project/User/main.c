/**
 ******************************************************************************
 * @file    User/Main.c
 * @brief   无线充电 PWM 系统 — 程序入口 (V9 TFT 版)
 * @note    V9: TFT 彩屏版, ST7735 160×128横屏 SPI 彩屏
 *          横屏 160×128, MADCTL=0xA0, 汉字 60 字宋体 LSB
 *
 *          上电流程: 硬件 Init → 系统时基 → 启动页 → 联网 → 主循环
 *
 *          主循环调度 (全非阻塞, 空闲休眠):
 *            Key_Driver_Task          10ms 按键轮询
 *            Adc_Driver_Filter_Task   ~2ms 模拟量采集
 *            Ui_Controller_Task       200ms 界面刷新
 *            App_Network_Task         指令接收 + 遥测发送
 *            Inverter_Control_*       软启动扫频 + 频率斜坡
 *            Led_Driver_Task          6 LED 心跳 + 闪烁
 *            Buzzer_Driver_Task       蜂鸣器调度
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

    /* ── 阶段0: 最早钳位 ESP8266 (RST=0+CH_PD=0) — 在所有外设初始化前执行, 防止 STM32 上电时 GPIO 默认高阻态导致 ESP 内部上拉误启动 ── */
    {
        GPIO_InitTypeDef gpio;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Pin   = GPIO_Pin_1;
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &gpio);
        GPIO_ResetBits(GPIOA, GPIO_Pin_1);   /* RST=0 */
        gpio.GPIO_Pin   = GPIO_Pin_11;
        GPIO_Init(GPIOB, &gpio);
        GPIO_ResetBits(GPIOB, GPIO_Pin_11);  /* CH_PD=0 */
    }

    /* ── 阶段1: 硬件层初始化 — TIM1 全关(MOE+CEN), PB10 拉低关 12V, 全桥零输出, TFT 显示启动页 ── */
    Pwm_Driver_Init();

    {
        GPIO_InitTypeDef gpio;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        gpio.GPIO_Pin   = GPIO_Pin_10;
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOB, &gpio);
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);   /* 初始拉低=关断, 高电平使能 12V 动力电源 */
    }

    Tft_Driver_Init();
    Led_Driver_Init();
    Buzzer_Driver_Init();
    Adc_Driver_Init();
    Key_Driver_Init();

    /* 启动页 */
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Show_CN_String(5, 3, "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad" "...", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);

    /* ── 阶段2: 系统时基 ── */
    Sys_Timer_Init();

    Led_Driver_Set_System(1);   /* 开启系统心跳灯 */

    /* ── 阶段3: IWDG 看门狗 (LSI 40kHz/64, reload=1000 → 1.6s) ── */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1000);
    IWDG_ReloadCounter();
    IWDG_Enable();
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;

    /* ── 阶段4: 开机默认无WIFI模式, 用户双击ON手动联网 ── */

    /* ══════════════════════════════════════════
     *  主循环 — 全非阻塞调度 + __WFI 休眠
     * ══════════════════════════════════════════ */

    while (1) {
        Key_Driver_Task();
        Adc_Driver_Filter_Task();
        Ui_Controller_Task();
        App_Network_Task();
        Inverter_Control_Soft_Start_Task();
        Inverter_Control_Freq_Ramp_Task();
        Led_Driver_Task();
        Buzzer_Driver_Task();

        IWDG_ReloadCounter();
        __WFI();
    }
}
