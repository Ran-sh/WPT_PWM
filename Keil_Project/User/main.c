/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   WPT_PWM V5.0.2 — 程序入口 (GPIO 重映射 + 5键系统)
 *
 *  系统总接线表 (全部使用引脚, 48 脚 LQFP):
 *  +------------------------------------------------------------+
 *  |   引脚  功能               引脚  功能                       |
 *  |   ----  -----------------  ----  -----------------          |
 *  |    PA0   TFT_RST            PB0   ADC_CH8  (电流 CC6920BSO) |
 *  |    PA1   ESP8266 RST        PB1   ADC_CH9  (电压分压)       |
 *  |    PA2   USART2_TX          PB3   LED_POWER (绿, 12V指示)   |
 *  |    PA3   USART2_RX          PB4   LED_WIFI  (蓝, WiFi状态)  |
 *  |    PA4   TFT_CS             PB5   KEY4 (IPU, 确定/启停)     |
 *  |    PA5   SPI1_SCK           PB6   KEY3 (IPU, DOWN/减)       |
 *  |    PA6   TFT_DC/Flash MISO  PB7   KEY2 (IPU, UP/加)         |
 *  |    PA7   SPI1_MOSI          PB8   KEY1 (IPU, 返回)          |
 *  |    PA8   TIM1_CH1 (HINA)    PB9   KEY0 (IPU, 电源开关)      |
 *  |    PA9   TIM1_CH2 (HINB)    PB10  PowerCtrl (KEY0 手动)     |
 *  |    PA12  TFT_BL (GPIO)      PB11  ESP8266 EN                |
 *  |    PA15  LED_STATUS (黄)    PB12  W25Q128_CS                |
 *  |    PC13  LED_HEARTBEAT (蓝)  PB13  TIM1_CH1N (LINA)          |
 *  |                              PB14  TIM1_CH2N (LINB)          |
 *  |                              PB15  Buzzer (有源蜂鸣器)       |
 *  |                                                             |
 *  |    电源: VDD=3.3V, VDDA=3.3V, VBAT=3.3V                     |
 *  |    时钟: HSE=8MHz -> PLL=72MHz (SYSCLK)                     |
 *  |    JTAG 禁用: PB3/PB4/PA15 释放为 GPIO                      |
 *  |    看门狗: IWDG 1.6~2.4s, 调试自动暂停                      |
 *  +------------------------------------------------------------+
 *
 *  初始化铁序 (顺序不可改):
 *    Sys_Clamp_ESP -> Sys_Timer_Init -> Sys_Hardware_Init ->
 *    W25Q_Driver_Init -> Tft_Driver_Font_Init ->
 *    App_Storage_Init -> Sys_Startup_Screen(SPLASH) -> Sys_Post_Init ->
 *    Delay(1s) -> SYS_STATE_IDLE
 *
 * @note    V5.0.2: GPIO 全面重映射, 5 键系统 (KEY0-4), 四灯系统
 *          Sys_Timer_Init 必须在 SPLASH 之前 (Delay_Ms 依赖 SysTick)
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
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);         /* NVIC 优先级分组: 2位抢占 2位响应 */

    Sys_Clamp_ESP();                                        /* 钳位 ESP 控制脚, 防止浮空误触发 */
    Sys_Timer_Init();                                       /* SysTick 1ms时基 (SPLASH Delay_Ms依赖此) */
    Sys_Hardware_Init();                                    /* 硬件驱动初始化: Pwm/TFT/Led/Buzzer/Adc/Key */
    W25Q_Driver_Init();                                     /* W25Q128 JEDEC 校验 -> s_chip_ok */
    Tft_Driver_Font_Init();                                 /* Flash 字库头校验 -> s_font_flash_valid */
    App_Storage_Init();                                     /* 参数双副本加载 + 黑匣子指针恢复 */

    Sys_Startup_Screen();                                   /* SPLASH ~4.8s */
    Sys_Timer_Delay_Ms(1000);                               /* 停留 1s */
    Tft_Driver_Clear(TFT_COLOR_BLACK);                      /* 一次清屏, 消除 SPLASH 残留 */

    Sys_Post_Init();                                        /* IWDG+ESP (IWDG 1.6~2.4s) */

    while (1) {                                             /* 主循环 — 按状态分发 */
        switch (Sys_Core_Get_State()) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;      /* 空闲: PWM 关, 等待操作 */
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;      /* 扫频: 150kHz->100kHz 软启动 */
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;      /* 运行: 频率闭环 + 调度 */
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;      /* 故障: 过流保护 + 等待复位 */
            case SYS_STATE_INIT:
            default:
                Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT);
                break;
        }
    }
}
