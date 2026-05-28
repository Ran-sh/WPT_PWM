/**
 ******************************************************************************
 * @file    System/Sys_Timer.c
 * @brief   系统时基模块 — 实现
 ******************************************************************************
 */

#include "Sys_Timer.h"

static volatile uint32_t s_sys_tick = 0;

/*
 * DWT 寄存器 (旧版 CMSIS core_cm3.h 不含此段, 手动定义)
 * Cortex-M3 DWT 基址 0xE0001000
 */
#define DWT_REG_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_REG_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DWT_BIT_CYCCNT_ENA  (1u << 0)

void Sys_Timer_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_REG_CYCCNT = 0;
    DWT_REG_CTRL  |= DWT_BIT_CYCCNT_ENA;
}

void Sys_Timer_Inc_Tick(void)
{
    s_sys_tick++;
}

uint32_t Sys_Timer_Get_Tick(void)
{
    return s_sys_tick;
}

uint32_t Sys_Timer_Get_Cycles(void)
{
    return DWT_REG_CYCCNT;
}

void Sys_Timer_Delay_Ms(uint32_t ms)
{
    uint32_t start = s_sys_tick;
    while ((s_sys_tick - start) < ms);
}
