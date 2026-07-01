/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   WPT_PWM V4.3.2 — 程序入口 (W25Q128 全字库)
 *
 *  系统总接线表 (全部使用引脚, 48 脚 LQFP):
 *  +------------------------------------------------------------+
 *  |   引脚  功能               引脚  功能                       |
 *  |   ----  -----------------  ----  -----------------          |
 *  |    PA0   TFT_RST            PB0   ADC_CH8  (电流 CC6920BSO) |
 *  |    PA1   ESP8266 RST        PB1   ADC_CH9  (电压分压)       |
 *  |    PA2   USART2_TX          PB3   LED_PWM  (绿, JTAG 释放)  |
 *  |    PA3   USART2_RX          PB4   LED_WIFI (蓝, JTAG 释放)  |
 *  |    PA4   TFT_CS             PB5   ON 按键 (IPU, 确定/启停)   |
 *  |    PA5   SPI1_SCK           PB6   TFT 背光 (TIM4_CH1)       |
 *  |    PA6   TFT_DC/Flash MISO  PB7   F_DOWN 按键 (IPU)         |
 *  |    PA7   SPI1_MOSI          PB8   F_UP 按键 (IPU)           |
 *  |    PA8   TIM1_CH1           PB9   PAGE 按键 (IPU, 翻页/返回) |
 *  |    PA9   TIM1_CH2           PB10  PowerCtrl (高=使能 12V)   |
 *  |    PA10  LED_COM (蓝)       PB11  ESP8266 CH_PD (EN)        |
 *  |    PA11  LED_POWER (绿)     PB13  TIM1_CH1N (全桥)          |
 *  |    PA12  W25Q128_CS         PB14  TIM1_CH2N (全桥)          |
 *  |    PA15  LED_SYSTEM (黄)    PB15  Buzzer (有源蜂鸣器)       |
 *  |                                                             |
 *  |    电源: VDD=3.3V, VDDA=3.3V, VBAT=3.3V                     |
 *  |    时钟: HSE=8MHz -> PLL=72MHz (SYSCLK)                     |
 *  |    JTAG 禁用: PB3/PB4/PB5/PA15 释放为 GPIO                  |
 *  |    看门狗: IWDG 1.6s, 调试自动暂停                          |
 *  +------------------------------------------------------------+
 *
 *  初始化铁序 (顺序不可改):
 *    Sys_Clamp_ESP -> Sys_Hardware_Init -> Sys_Timer_Init ->
 *    W25Q_Driver_Init -> Tft_Driver_Font_Init ->
 *    App_Storage_Init -> Sys_Startup_Screen(SPLASH) -> Sys_Post_Init ->
 *    Delay(1s) -> SYS_STATE_IDLE
 *
 * @note    V4.3.2: Sys_Timer_Init 必须在 SPLASH 之前
 *          (Tft_Driver_Show_Splash 使用 Sys_Timer_Delay_Ms 依赖 SysTick)
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

/** @brief 程序入口: 初始化 -> SPLASH -> 1s停留 -> 主循环按状态分发 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);         /* NVIC 优先级分组: 2位抢占 2位响应 */

    Sys_Clamp_ESP();                                        /* 钳位 ESP 控制脚, 防止浮空误触发 */
    Sys_Hardware_Init();                                    /* 硬件驱动初始化: Pwm/TFT/Led/Buzzer/Adc/Key */
    Sys_Timer_Init();                                       /* SysTick 1ms + DWT (SPLASH Delay_Ms 依赖此) */
    W25Q_Driver_Init();                                     /* W25Q128 JEDEC 校验 -> s_chip_ok */
    Tft_Driver_Font_Init();                                 /* Flash 字库头校验 -> s_font_flash_valid */
    App_Storage_Init();                                     /* 参数双副本加载 + 黑匣子指针恢复 */

    Sys_Startup_Screen();                                   /* 开机动画: 背光渐亮 + 逐字点亮 ~4.8s */
    Sys_Post_Init();                                        /* LED/ADC/WDG/ESP 联网启动 */
    Sys_Timer_Delay_Ms(1000);                               /* 开机画面停留 1s, 用户看清屏幕 */

    g_sys_state = SYS_STATE_IDLE;                           /* 切到空闲态, 开始正常调度 */

    while (1) {                                             /* 主循环 — 按状态分发 */
        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;      /* 空闲: PWM 关, 等待操作 */
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;      /* 扫频: 150kHz->100kHz 软启动 */
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;      /* 运行: 频率闭环 + 调度 */
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;      /* 故障: 过流保护 + 等待复位 */
        }
    }
}
