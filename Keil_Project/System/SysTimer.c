/**
 ******************************************************************************
 * @file    System/SysTimer.c
 * @brief   系统时基模块 —— 实现
 * @note    存放路径: 项目根目录\System\
 *
 *          核心变量 s_SysTick:
 *            32 位无符号毫秒计数器, 上电从 0 开始递增。
 *            SysTick_Handler (1ms 中断) 调用 SysTimer_IncTick() 对其递增。
 *            声明为 static volatile, 外部只能通过 SysTimer_GetTick() 读取。
 *
 *          溢出安全性:
 *            uint32_t 最大值 ≈ 49.7 天, 溢出后回绕到 0。
 *            无符号减法自动处理溢出: (now - last) 在任何情况下均正确。
 *
 *          DWT 周期计数器:
 *            Cortex-M3 DWT 单元内含 32 位自由运行计数器 (CPU 时钟频率),
 *            用于亚毫秒级高精度定时。在 SysTimer_Init() 中自动使能。
 *
 *          依赖: STM32F10x 标准外设库 (SPL)
 ******************************************************************************
 */

#include "SysTimer.h"

/* ── 全局时基计数器 (ISR 写入, 主循环读取, 必须 volatile) ── */
static volatile uint32_t s_SysTick = 0;

/*
 * DWT 寄存器手动定义 (旧版 CMSIS core_cm3.h 不含 DWT 段)
 * Cortex-M3 DWT 基址 0xE0001000, CYCCNT 偏移 0x04
 */
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DWT_CTRL_CYCCNTENA  (1u << 0)

/**
 * @brief  初始化 SysTick 定时器为 1ms 中断周期 + 使能 DWT 周期计数器
 * @note   调用 SysTick_Config(时钟频率/1000) 将系统滴答配置为 1ms
 *         必须在所有依赖 SysTimer_GetTick() 的模块之前调用。
 */
void SysTimer_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);

    /* 使能 DWT 周期计数器, 用于亚毫秒定时 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

/**
 * @brief  毫秒计数器递增 (由 SysTick_Handler 在中断上下文中调用)
 * @note   每个 SysTick 中断周期 (1ms) 调用一次, 不做其他任何操作,
 *         保持中断服务函数极简, 符合 RTOS 级设计规范。
 */
void SysTimer_IncTick(void)
{
    s_SysTick++;
}

/**
 * @brief  获取当前毫秒时间戳
 * @retval 上电以来的毫秒数 (0 ~ 4294967295, 约 49.7 天后归零)
 */
uint32_t SysTimer_GetTick(void)
{
    return s_SysTick;
}

/**
 * @brief  基于 SysTick 的阻塞毫秒延时
 * @param  ms: 延时时间 (毫秒)
 * @note   此函数在等待期间不释放 CPU (纯轮询)。
 *         适用于初始化阶段的短暂延时 (如等待外设上电稳定)。
 *         运行期间应优先使用时间戳差值法进行非阻塞调度。
 */
void SysTimer_DelayMs(uint32_t ms)
{
    uint32_t start = s_SysTick;
    while ((s_SysTick - start) < ms);
}

/**
 * @brief  获取 DWT 周期计数器值 (自由运行, CPU 时钟频率)
 * @retval 上电以来的 CPU 周期数, 32 位, 约 59.7 秒回绕
 * @note   用于需要亚毫秒精度的定时场景,
 *         无符号减法自动处理溢出
 */
uint32_t SysTimer_GetCycles(void)
{
    return DWT_CYCCNT;
}
