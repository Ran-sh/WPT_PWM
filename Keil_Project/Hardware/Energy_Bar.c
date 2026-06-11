/**
 ******************************************************************************
 * @file    Hardware/Energy_Bar.c
 * @brief   动态能量条 — 实现
 * @note    RGB565 多段颜色渐变, 宽度直接正比于数值占比 (value/max)
 *          颜色方案: 绿 → 黄 → 红 (预制表, 零运行时计算)
 ******************************************************************************
 */

#include "Energy_Bar.h"
#include "Tft_Driver.h"

/* 8 段预制颜色表: 绿 → 黄 → 红 (RGB565) */
static const uint16_t EB_COLOR_TABLE[8] = {
    0x07E0,  /* 纯绿 */
    0x2FE0,  /* 黄绿 */
    0x5FE0,  /* 柠檬 */
    0x87E0,  /* 绿黄 */
    0xFF80,  /* 橙黄 */
    0xFD00,  /* 橘色 */
    0xF900,  /* 橙红 */
    0xF800   /* 纯红 */
};

/* ═══════════════════════════════════════════════════════════════
 *  公开: 绘制能量条 — 宽度 = (value / max_val) * max_w, 绿→红渐变
 * ═══════════════════════════════════════════════════════════════ */
void Energy_Bar_Draw(uint16_t x, uint16_t y, uint16_t max_w, uint16_t h,
                     float value, float min_val, float max_val,
                     Energy_Bar_Metric metric, uint16_t bg_color)
{
    uint16_t total_w;
    uint8_t  seg_count, i;
    uint16_t seg_w, seg_x;

    /* ── 1. 数值→宽度: (value - min) / (max - min) * max_w ── */
    {
        float range = max_val - min_val;
        float ratio;
        if (range <= 0.0f) {
            /* 调用者传参错误: min >= max → 擦除后返回 */
            Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color);
            return;
        }
        ratio = (value - min_val) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        total_w = (uint16_t)(ratio * (float)max_w);
    }
    if (total_w == 0) total_w = 0;   /* 值为零 → 只擦除不画 */

    /* ── 2. 擦除整个条形区域 ── */
    Tft_Driver_Fill_Rect(x, y, max_w, h, bg_color);
    if (total_w == 0) return;

    /* ── 3. 分段绘制: 段数自适应宽度, 每段至少4px ── */
    seg_count = (uint8_t)(total_w / 4);
    if (seg_count < 1) seg_count = 1;
    if (seg_count > 8) seg_count = 8;
    seg_w = total_w / seg_count;

    for (i = 0; i < seg_count; i++) {
        seg_x = x + i * seg_w;
        {
            uint16_t w = seg_w;
            /* 最后一段补齐余数 */
            if (i == seg_count - 1)
                w = (x + total_w) - seg_x;
            if (w > 0) {
                /* 从预制颜色表中按比例取色 */
                uint8_t ci = (uint8_t)(((uint16_t)i * 8) / seg_count);
                if (ci >= 8) ci = 7;
                Tft_Driver_Fill_Rect(seg_x, y, w, h, EB_COLOR_TABLE[ci]);
            }
        }
    }
}
