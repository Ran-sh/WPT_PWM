/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   无线充电控制系统程序入口 — V5.1.1
 *
 *  系统总接线表（STM32F103C8T6，48脚封装）:
 *  +------------------------------------------------------------+
 *  |   引脚  功能               引脚  功能                       |
 *  |   ----  -----------------  ----  -----------------          |
 *  |    PA0   TFT_RST            PB0   ADC_CH8  (电流 CC6920BSO) |
 *  |    PA1   ESP8266 RST        PB1   ADC_CH9  (电压分压)       |
 *  |    PA2   USART2_TX          PB3   12V电源灯（绿色）         |
 *  |    PA3   USART2_RX          PB4   无线状态灯（蓝色）        |
 *  |    PA4   TFT_CS             PB5   KEY4（上拉，确定或启停）  |
 *  |    PA5   SPI1_SCK           PB6   KEY3（上拉，下移或减少）  |
 *  |    PA6   TFT_DC/W25Q_MISO   PB7   KEY2（上拉，上移或增加）  |
 *  |    PA7   SPI1_MOSI          PB8   KEY1（上拉，返回）        |
 *  |    PA8   TIM1_CH1（HINA）   PB9   KEY0（上拉，电源开关）    |
 *  |    PA9   TIM1_CH2（HINB）   PB10  12V电源使能               |
 *  |    PA12  TFT_BL（开关）     PB11  ESP8266使能               |
 *  |    PA15  PWM状态灯（黄色）  PB12  W25Q128片选               |
 *  |    PC13  程序心跳灯（蓝色） PB13  TIM1_CH1N（LINA）         |
 *  |                              PB14  TIM1_CH2N (LINB)          |
 *  |                              PB15  有源蜂鸣器                |
 *  |                                                             |
 *  |    电源：VDD、VDDA和VBAT均为3.3V                            |
 *  |    时钟：8MHz外部晶振经锁相环倍频至72MHz                   |
 *  |    调试：关闭JTAG，保留两线调试并释放PB3、PB4和PA15        |
 *  |    看门狗：超时窗口约1.6至2.4s，调试暂停时自动冻结         |
 *  +------------------------------------------------------------+
 *
 *  初始化固定顺序（禁止调整）:
 *    Sys_Clamp_ESP -> Sys_Timer_Init -> Sys_Hardware_Init ->
 *    W25Q_Driver_Init -> Tft_Driver_Font_Init ->
 *    App_Storage_Init -> Sys_Startup_Screen -> Sys_Post_Init
 *
 * @note    系统时基必须在开机动画之前初始化；扫频目标由当前锁定的双档配置决定。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Core.h"
#include "Key_Driver.h"
#include "Adc_Driver.h"
#include "App_Network.h"
#include "W25Q_Driver.h"
#include "Tft_Driver.h"
#include "Sys_Timer.h"
#include "App_Storage.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);         /* 中断优先级分为2位抢占和2位响应。 */

    Sys_Clamp_ESP();                                        /* 钳位ESP控制引脚，防止上电浮空误动作。 */
    Sys_Timer_Init();                                       /* 建立开机动画和后续调度需要的1ms时基。 */
    Sys_Hardware_Init();                                    /* 初始化PWM、彩屏、指示灯、蜂鸣器、采样和按键驱动。 */
    W25Q_Driver_Init();                                     /* 探测并确认W25Q128型号。 */
    Tft_Driver_Font_Init();                                 /* 校验外部字库头并选择完整或回退字库。 */
    App_Storage_Init();                                     /* 加载参数双副本并恢复黑匣子写入位置。 */

    Sys_Startup_Screen();                                   /* 显示开机动画和初始化结果。 */
    Sys_Timer_Delay_Ms(1000);                               /* 开机结果停留1s。 */
    Tft_Driver_Clear(TFT_COLOR_BLACK);                      /* 清除开机画面残留。 */

    Sys_Post_Init();                                        /* 启用看门狗、ESP和运行期服务。 */

    while (1) {                                             /* 主循环只按系统状态分发对应任务。 */
        switch (Sys_Core_Get_State()) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;      /* 空闲：PWM关闭并等待操作。 */
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;      /* 扫频：按本次锁定档位降至保存目标。 */
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;      /* 运行：执行调频、安全和公共调度。 */
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;      /* 故障：保持输出关闭并等待确认复位。 */
            case SYS_STATE_INIT:
            default:
                Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT);
                break;
        }
    }
}
