/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   无线充电 PWM 系统 —— 主程序入口
 * @note    存放路径: 项目根目录\User\
 *
 *          架构 (V1.0.0):
 *          - 上电: 硬件配置 → 时基 → 等待 KEY0 触发联网
 *          - 全桥默认关断 (MOE OFF), 由 Trigger 开启扫频
 *          - 非阻塞软启动状态机: 150kHz → 100kHz, ~5s
 *          - 触发源: KEY0 (联网 + 扫频) / PC "ON/OFF" 指令
 *
 *          依赖: STM32F10x 标准外设库 (SPL)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"
#include "LED.h"
#include "OLED.h"
#include "PWM.h"
#include "ADC.h"
#include "KEY.h"
#include "UI.h"
#include "App_Net.h"

int main(void)
{
    /*
     * NVIC 优先级分组: 2 位抢占 (0~3), 2 位子优先级 (0~3)
     * 须在任何中断使能前配置, 否则优先级嵌套行为不可预期
     */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* ═══════════════════════════════════════════════════════════
     *  阶段 1: 板载外设初始化 (硬件层, 全桥无输出)
     * ═══════════════════════════════════════════════════════════ */
    PWM_Init();           /* TIM1 配置完成, MOE 关, 安全态 */
    OLED_Init();
    LED_Init();
    ADC_DMA_Init();
    KEY_Init();

    OLED_Clear();
    OLED_ShowString(1, 1, "Wireless Charge");

    /* ═══════════════════════════════════════════════════════════
     *  阶段 2: 系统时基初始化 (SysTick 1ms)
     * ═══════════════════════════════════════════════════════════ */
    SysTimer_Init();

    /* ═══════════════════════════════════════════════════════════
     *  阶段 3: 主循环 (KEY0 触发联网 → 扫频/监控)
     * ═══════════════════════════════════════════════════════════ */
    while (1)
    {
        KEY_Task();
        ADC_Filter_Task();          /* 2ms 周期, 独立更新滑动平均 */
        UI_Task();
        App_Net_Task();
        App_Net_Connect_Task();      /* 非阻塞联网步进 */
        Inverter_SoftStart_Task();  /* 非阻塞扫频步进 (内部 10ms 节拍) */
        LED_Task();
    }
}
