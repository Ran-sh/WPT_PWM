/**
 ******************************************************************************
 * @file    Hardware/Energy_Bar.h
 * @brief   动态能量条 — 带脉动流光效果的像素级彩色条形图
 * @note    替代原有 '#' 字符刻度条，增强界面科技感
 *
 *          核心算法:
 *            - 32 点正弦查表 (Q7.8 定点), ~1.6s 完整脉动周期
 *            - 8 段预计算颜色表, 绿→黄→红渐变 (RGB565)
 *            - 每次刷新: 擦除 + 逐段 Fill_Rect, 总耗时 < 3ms
 *
 *          依赖: Tft_Driver_Fill_Rect, Sys_Timer_Get_Tick
 ******************************************************************************
 */

#ifndef ENERGY_BAR_H
#define ENERGY_BAR_H

#include "stm32f10x.h"

/* 指标类型 */
typedef enum {
    ENERGY_BAR_METRIC_FREQ  = 0,  /* 频率: 0~150 kHz */
    ENERGY_BAR_METRIC_VOLT  = 1,  /* 电压: 0~30 V */
    ENERGY_BAR_METRIC_CURR  = 2   /* 电流: 0~5 A */
} Energy_Bar_Metric;

/**
 * @brief  绘制能量条 — 宽度 = (value - min_val) / (max_val - min_val) * max_w
 * @param  x        左上角像素 X (0~159)
 * @param  y        左上角像素 Y (0~127)
 * @param  max_w    条形图最大像素宽度
 * @param  h        条形图像素高度
 * @param  value    当前数值 (EMA 平滑后)
 * @param  min_val  刻度范围下限 (如频率 95)
 * @param  max_val  刻度范围上限 (如频率 150)
 * @param  metric   指标类型 (影响颜色策略)
 * @param  bg_color 背景色 (擦除用)
 */
void Energy_Bar_Draw(uint16_t x, uint16_t y, uint16_t max_w, uint16_t h,
                     float value, float min_val, float max_val,
                     Energy_Bar_Metric metric, uint16_t bg_color);

#endif /* ENERGY_BAR_H */
