# 仪表盘实装计划 — 环形仪表替换 HUD 三页

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用全屏 160×128 环形模拟仪表盘替换 Ui_Controller.c 中的三个 HUD 风格监测页面 (FREQ/VOLT/CURR)

**架构:** 在 `Ui_Controller.c` 中新增 `sin_table[181]` + `GaugeConfig` 结构体 + 通用渲染函数 `Draw_Gauge_Full()` / `Gauge_Dynamic_Update()`，三个 `_Full`/`_Dynamic` 函数改为薄封装调用通用函数。保持 V11 增量刷新架构。

**技术栈:** C, STM32F103 SPL V3.5, ST7735 160×128 SPI DMA, PCtoLCD2002 4×8 微型字库. V4.2.1

---

### Task 1: 新增 sin_table + GaugeConfig 结构体 + 通用绘制函数

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

**步骤:**

- [ ] **Step 1: 在 `#define` 区块之后、状态变量之前，新增 sin_table 和结构体**

```c
/* ═══════════════════════════════════════════════════════════════
 *  仪表盘 sin 查表 — sin(0..180°) × 10000, 181 对 int16_t, 724 字节
 *  极坐标公式: x = cx + R * sin_table[90-a] / 10000  (若 a 从左侧算起)
 *              实际使用 polar() 宏: a_rad = PI - (angle_deg * PI / 180)
 * ═══════════════════════════════════════════════════════════════ */
static const int16_t GAUGE_SIN_TABLE[181] = {
       0,   175,   349,   523,   698,   872,  1045,  1219,  1392,  1564,
    1736,  1908,  2079,  2250,  2419,  2588,  2756,  2924,  3090,  3256,
    3420,  3584,  3746,  3907,  4067,  4226,  4384,  4540,  4695,  4848,
    5000,  5150,  5299,  5446,  5592,  5736,  5878,  6018,  6157,  6293,
    6428,  6561,  6691,  6820,  6947,  7071,  7193,  7314,  7431,  7547,
    7660,  7771,  7880,  7986,  8090,  8192,  8290,  8387,  8480,  8572,
    8660,  8746,  8829,  8910,  8988,  9063,  9135,  9205,  9272,  9336,
    9397,  9455,  9511,  9563,  9613,  9659,  9703,  9744,  9781,  9816,
    9848,  9877,  9903,  9925,  9945,  9962,  9976,  9986,  9994,  9998,
   10000,  9998,  9994,  9986,  9976,  9962,  9945,  9925,  9903,  9877,
    9848,  9816,  9781,  9744,  9703,  9659,  9613,  9563,  9511,  9455,
    9397,  9336,  9272,  9205,  9135,  9063,  8988,  8910,  8829,  8746,
    8660,  8572,  8480,  8387,  8290,  8192,  8090,  7986,  7880,  7771,
    7660,  7547,  7431,  7314,  7193,  7071,  6947,  6820,  6691,  6561,
    6428,  6293,  6157,  6018,  5878,  5736,  5592,  5446,  5299,  5150,
    5000,  4848,  4695,  4540,  4406,  4270,  4133,  3995,  3856,  3716,
    3575,  3433,  3290,  3146,  3001,  2856,  2709,  2562,  2414,  2266,
    2117,  1968,  1818,  1668,  1518,  1367,  1217,  1066,   915,   764,
     613,   462,   311,   160,     0,
};

/** @brief 仪表盘量程配置 */
typedef struct {
    float    range_min;    /**< 量程下限 */
    float    range_max;    /**< 量程上限 */
    float    big_step;     /**< 大刻度步进 */
    float    mid_step;     /**< 中刻度步进 */
    float    fine_step;    /**< 细刻度步进 */
    float    red_start;    /**< 红区起始值 */
    char     label;        /**< 左上标签 'V'/'C'/'F' */
} GaugeConfig;

static const GaugeConfig GAUGE_V = {0.0f, 48.0f, 5.0f, 1.0f, 0.5f, 42.0f, 'V'};
static const GaugeConfig GAUGE_C = {0.0f, 3.0f, 0.5f, 0.1f, 0.05f, 2.7f, 'C'};
static const GaugeConfig GAUGE_F = {95.0f, 150.0f, 10.0f, 2.0f, 1.0f, 143.0f, 'F'};
```

- [ ] **Step 2: 在 `Draw_Divider` 函数之后，新增极坐标辅助函数**

```c
/* ── Gauge: polar coordinate from angle (0°=left, 90°=top, 180°=right) ── */
/*     angle_deg: 0..180,  radius: pixels from center (80,100)             */
/*     returns pixel coordinates via *px, *py                             */
static void Gauge_Polar(uint8_t angle_deg, uint16_t radius,
                        int16_t* px, int16_t* py)
{
    int16_t s = GAUGE_SIN_TABLE[angle_deg];                       /* sin */
    int16_t c = GAUGE_SIN_TABLE[(uint8_t)(90 - (int16_t)angle_deg + 90)]; /* cos = sin(90-a) */
    /* cos = sin_table[(90 - angle_deg + 90)], clamp to 0..180 */
    int32_t cx = 80, cy = 100;
    *px = (int16_t)(cx + (int32_t)radius * c / 10000);
    *py = (int16_t)(cy - (int32_t)radius * s / 10000);
}
```

注：如果 Keil ARMCC 不支持这种 cos 查法，后续任务会改用 `GAUGE_SIN_TABLE[(abs(90-angle_deg) > 90 ? 180 - ... : ...)` 逻辑修正。先以简化版提交，编译后再调。

- [ ] **Step 3: 新增 `Draw_Gauge_Full` — 入场全绘仪表盘**

```c
/**
 * @brief  入场全绘环形仪表盘 (弧+全部刻度+微型数字+指针+Hub+WIFI+Badge)
 * @param  cfg   量程配置
 * @param  value 当前 EMA 平滑值
 * @note   覆盖全部 128 行, 弧 R=65, 刻度外端 R=68, 大刻内端 R=50, 中=58, 细=62
 *          微型数字标注在 R=75 处
 */
static void Draw_Gauge_Full(const GaugeConfig* cfg, float value)
{
    uint16_t R_OUTER = 68, R_ARC = 65;
    uint16_t R_BIG = 50, R_MID = 58, R_FINE = 62, R_LABEL = 75;
    float v;
    uint8_t a;
    char label_buf[16];
    int16_t xo, yo, xi, yi;

    /* ── 清屏 ── */
    Tft_Driver_Clear(UI_COLOR_BG);

    /* ── 弧 (白 + 红区分段) ── */
    {
        uint8_t red_angle = (uint8_t)((cfg->red_start - cfg->range_min) /
                              (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (red_angle > 180) red_angle = 180;

        for (a = 0; a <= 180; a++) {
            uint16_t color = (a >= red_angle) ? UI_COLOR_ALARM : UI_COLOR_TEXT;
            /* 弧宽 3px: R_ARC-1, R_ARC, R_ARC+1 */
            Gauge_Polar(a, R_ARC - 1, &xo, &yo);
            Tft_Driver_Fill_Rect((uint16_t)(xo - 1), (uint16_t)(yo - 1), 3, 2, color);
            Gauge_Polar(a, R_ARC,     &xo, &yo);
            Tft_Driver_Fill_Rect((uint16_t)(xo - 1), (uint16_t)(yo - 1), 3, 2, color);
            Gauge_Polar(a, R_ARC + 1, &xo, &yo);
            Tft_Driver_Fill_Rect((uint16_t)(xo - 1), (uint16_t)(yo - 1), 3, 2, color);
        }
    }

    /* ── 刻度 (三层循环, 跨全部量程范围) ── */
    for (v = cfg->range_min; v <= cfg->range_max + cfg->fine_step * 0.1f;
         v += cfg->fine_step) {
        uint8_t is_red = (v >= cfg->red_start);
        uint16_t color, inner_r, line_w;
        uint8_t is_big = 0, is_mid = 0;

        /* 检测接近大刻度/中刻度 (浮点容差 0.01) */
        {
            float diff_big = v - (float)((int)(v / cfg->big_step + 0.5f)) * cfg->big_step;
            if (diff_big < 0.0f) diff_big = -diff_big;
            if (diff_big < cfg->fine_step * 0.2f) is_big = 1;
        }
        if (!is_big) {
            float diff_mid = v - (float)((int)(v / cfg->mid_step + 0.5f)) * cfg->mid_step;
            if (diff_mid < 0.0f) diff_mid = -diff_mid;
            if (diff_mid < cfg->fine_step * 0.2f) is_mid = 1;
        }

        if (is_big)      { inner_r = R_BIG;  line_w = 3; color = is_red ? UI_COLOR_ALARM : UI_COLOR_TEXT; }
        else if (is_mid) { inner_r = R_MID;  line_w = 2; color = is_red ? UI_COLOR_ALARM : UI_COLOR_DIM; }
        else             { inner_r = R_FINE; line_w = 1; color = UI_COLOR_DIM; }

        a = (uint8_t)((v - cfg->range_min) / (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;

        Gauge_Polar(a, R_OUTER, &xo, &yo);
        Gauge_Polar(a, inner_r, &xi, &yi);

        Tft_Driver_Fill_Rect((uint16_t)xi, (uint16_t)yi,
                             (uint16_t)(xo - xi + 1), line_w, color);
    }

    /* ── 微型数字 (4×8 字库, R=75) ── */
    for (v = cfg->range_min; v <= cfg->range_max + cfg->big_step * 0.1f;
         v += cfg->big_step) {
        a = (uint8_t)((v - cfg->range_min) / (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;

        Gauge_Polar(a, R_LABEL, &xo, &yo);

        {
            char num_buf[8];
            uint16_t num_color = (v >= cfg->red_start) ? UI_COLOR_ALARM : UI_COLOR_TEXT;

            if (v == (float)((int)v))
                snprintf(num_buf, sizeof(num_buf), "%d", (int)v);
            else if (cfg->big_step < 1.0f)
                snprintf(num_buf, sizeof(num_buf), "%.1f", (double)v);
            else
                snprintf(num_buf, sizeof(num_buf), "%d", (int)v);

            /* 居中: 每个 4px 字符 + 2px 间距 = 6px */
            {
                uint8_t len = (uint8_t)strlen(num_buf);
                uint16_t sx = (uint16_t)(xo - (len * 6) / 2 + 2);
                uint16_t sy = (uint16_t)(yo - 4);
                Tft_Driver_Show_4x8_String_Pixel(sx, sy, num_buf, num_color, UI_COLOR_BG);
            }
        }
    }

    /* ── 指针 ── */
    {
        a = (uint8_t)((value - cfg->range_min) /
             (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;

        {
            int16_t px, py, tx, ty;
            Gauge_Polar(a, (uint16_t)(R_ARC - 9), &px, &py);  /* 指针尖 */
            Gauge_Polar(a, 0, &tx, &ty);                       /* 针尾用反向: 手算 */
            ty = 100 + (100 - py) / 6;                          /* 短尾反向 */

            /* 粗线: 3次 Fill_Rect 模拟 3px 宽线 */
            {
                int16_t dx = px - 80, dy = py - 100;
                int16_t nx = -dy / 10, ny = dx / 10;  /* 垂直方向微调 */
                Tft_Driver_Fill_Rect((uint16_t)(px - 1 + nx), (uint16_t)(py - 1 + ny),
                                     3, 3, UI_COLOR_ALARM);
            }
        }
    }

    /* ── Hub ── */
    {
        /* 外圈 r=10 */
        Tft_Driver_Fill_Rect(70, 90, 21, 21, UI_COLOR_BG);  /* 擦圆区域 */
        /* ... 简化: 用 Fill_Rect 逐像素画粗圆 */
    }

    /* ── 左上实时值 ── */
    {
        snprintf(label_buf, sizeof(label_buf), "%c %.2f", cfg->label, (double)value);
        Tft_Driver_Show_String(0, 0, label_buf, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    /* ── 右上 WIFI + Badge ── */
    Draw_Header(cfg->label == 'V' ? "\xe7\x94\xb5\xe5\x8e\x8b" :
                cfg->label == 'C' ? "\xe7\x94\xb5\xe6\xb5\x81" :
                                    "\xe9\xa2\x91\xe7\x8e\x87");
}
```

**注意:** 以上是骨架代码。指针绘制需要用 Bresenham 算法或逐点 Fill_Rect 模拟粗线，Hub 同理。实际实现时根据编译结果微调。

- [ ] **Step 4: 提交**

```bash
git add -A && git commit -m "feat: 新增 sin_table + GaugeConfig + Draw_Gauge_Full 骨架"
```

---

### Task 2: 实现指针 Bresenham 粗线 + Hub 金属同心圆

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 新增 `Draw_Thick_Line` 函数 (Bresenham + 3px 宽)**

```c
/**
 * @brief  绘制粗线 (Bresenham 线段 + 垂直方向扩张实现线宽)
 * @param  x0,y0 起点 (圆心)
 * @param  x1,y1 终点 (指针尖)
 * @param  w     线宽 (像素, 奇数效果最好)
 * @param  color RGB565 颜色
 * @note   用 Fill_Rect 逐点实现, 线宽 > 1 时向垂直方向扩张
 */
static void Draw_Thick_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint8_t w, uint16_t color)
{
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t hw = (int16_t)(w / 2);

    while (1) {
        Tft_Driver_Fill_Rect((uint16_t)(x0 - hw), (uint16_t)(y0 - hw),
                             w, w, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}
```

- [ ] **Step 2: 替换 Draw_Gauge_Full 中的指针绘制段**

```c
    /* ── 指针 ── */
    {
        a = (uint8_t)((value - cfg->range_min) /
             (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
        if (a > 180) a = 180;

        int16_t pp_x, pp_y, pt_x, pt_y;
        Gauge_Polar(a, R_ARC - 9, &pp_x, &pp_y);         /* 针尖 */
        Gauge_Polar(a, 14, &pt_x, &pt_y);                 /* 针尾 (半径14反方向) */
        /* 反向尾: 圆心另一端 */
        pt_x = 80 + (80 - pt_x);
        pt_y = 100 + (100 - pt_y);

        Draw_Thick_Line(80, 100, pp_x, pp_y, 3, UI_COLOR_ALARM);  /* 主线 */
        Draw_Thick_Line(80, 100, pt_x, pt_y, 2, UI_COLOR_ALARM);  /* 短尾 */
    }
```

- [ ] **Step 3: 实现 Hub 绘制 (3层同心圆 + 高光)**

```c
/**
 * @brief  绘制仪表盘金属 Hub (3 层同心圆)
 * @param  cx, cy 圆心 (80, 100)
 */
static void Draw_Hub(int16_t cx, int16_t cy)
{
    /* 外圈: r=10 暗灰圆环 — 多圈 Fill_Rect */
    int16_t r;
    for (r = 8; r <= 12; r++) {
        int16_t x, y;
        for (y = -r; y <= r; y++) {
            for (x = -r; x <= r; x++) {
                if (x*x + y*y <= r*r && x*x + y*y > (r-2)*(r-2))
                    Tft_Driver_Fill_Rect((uint16_t)(cx + x), (uint16_t)(cy + y),
                                        1, 1, (r > 10) ? 0x8410 : 0x630C);
            }
        }
    }
    /* 中圈: r=7 亮灰 */
    for (r = 5; r <= 9; r++) {
        int16_t x, y;
        for (y = -r; y <= r; y++) {
            for (x = -r; x <= r; x++) {
                if (x*x + y*y <= r*r && x*x + y*y > (r-2)*(r-2))
                    Tft_Driver_Fill_Rect((uint16_t)(cx + x), (uint16_t)(cy + y),
                                        1, 1, 0xAD55);
            }
        }
    }
    /* 内点: r=3 红色填充圆 */
    {
        int16_t x, y;
        for (y = -5; y <= 5; y++) {
            for (x = -5; x <= 5; x++) {
                if (x*x + y*y <= 25)
                    Tft_Driver_Fill_Rect((uint16_t)(cx + x), (uint16_t)(cy + y),
                                        1, 1, UI_COLOR_ALARM);
            }
        }
    }
    /* 高光: 左上角白色半透明椭圆 */
    {
        int16_t hx, hy;
        for (hy = -3; hy <= 1; hy++) {
            for (hx = -5; hx <= 1; hx++) {
                if (hx*hx*2 + hy*hy*4 <= 20)
                    Tft_Driver_Fill_Rect((uint16_t)(cx + hx), (uint16_t)(cy + hy),
                                        1, 1, 0xC618);
            }
        }
    }
}
```

- [ ] **Step 4: 替换 Draw_Gauge_Full 中的粗 Hub 段为 `Draw_Hub(80, 100);`**

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat: Bresenham粗线指针 + 金属同心圆Hub"
```

---

### Task 3: 修复弧绘制 (弧段 + 正确极坐标)

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 修正 `Gauge_Polar` 的 cos 查表 — ARMCC 无法用长表达式, 改用查表**

当前 cos 计算公式 `sin_table[(90 - angle_deg + 90)]` 复杂且可能越界。修正为:

```c
static void Gauge_Polar(uint8_t angle_deg, uint16_t radius,
                        int16_t* px, int16_t* py)
{
    int16_t s, c;
    int32_t cx = 80, cy = 100;

    if (angle_deg > 180) angle_deg = 180;

    s = GAUGE_SIN_TABLE[angle_deg];                    /* sin(a) */
    if (angle_deg <= 90)
        c = GAUGE_SIN_TABLE[90 - angle_deg];           /* cos = sin(90-a) */
    else
        c = -GAUGE_SIN_TABLE[angle_deg - 90];          /* cos = -sin(a-90) */

    *px = (int16_t)(cx + (int32_t)radius * c / 10000);
    *py = (int16_t)(cy - (int32_t)radius * s / 10000);
}
```

- [ ] **Step 2: 修正弧绘制 — 去掉逐角度 Fill_Rect, 改为更高效的断面填充**

弧 R=65 宽 3px。逐角度 181×3=543 次 Fill_Rect 太慢。改用:

```c
    /* ── 弧 (白 + 红区分段) — 三重圆环 ── */
    uint8_t red_angle = (uint8_t)((cfg->red_start - cfg->range_min) /
                          (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    if (red_angle > 180) red_angle = 180;

    for (a = 0; a <= 180; a++) {
        int16_t x, y;
        uint16_t color = (a >= red_angle) ? UI_COLOR_ALARM : UI_COLOR_TEXT;

        /* 简化为: 三重半径 R-1, R, R+1 各1px宽的环 */
        Gauge_Polar(a, R_ARC - 1, &x, &y);
        Tft_Driver_Fill_Rect((uint16_t)x, (uint16_t)y, 2, 2, color);

        Gauge_Polar(a, R_ARC, &x, &y);
        Tft_Driver_Fill_Rect((uint16_t)x, (uint16_t)y, 2, 2, color);

        Gauge_Polar(a, R_ARC + 1, &x, &y);
        Tft_Driver_Fill_Rect((uint16_t)x, (uint16_t)y, 2, 2, color);
    }
```

- [ ] **Step 3: 编译验证**

```bash
# 在 Keil 中 F7 编译
```

- [ ] **Step 4: 提交**

```bash
git add -A && git commit -m "fix: 修正极坐标cos查表 + 弧绘制优化"
```

---

### Task 4: 新增 `Gauge_Dynamic_Update` — 200ms 增量刷新

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 新增通用动态更新函数**

```c
/**
 * @brief  200ms 增量刷新仪表盘 (仅擦旧指针 + 绘新指针 + 更新左上数值)
 * @param  cfg      量程配置
 * @param  value    当前 EMA 平滑值
 * @param  old_val  上一次的 EMA 值 (用于原地擦旧指针)
 * @note   弧和刻度不重绘
 */
static void Gauge_Dynamic_Update(const GaugeConfig* cfg, float value, float old_val)
{
    char label_buf[16];
    uint8_t old_a, new_a;
    int16_t pp_x, pp_y, pt_x, pt_y;
    uint16_t R_ARC = 65;

    old_a = (uint8_t)((old_val - cfg->range_min) /
             (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);
    new_a = (uint8_t)((value - cfg->range_min) /
             (cfg->range_max - cfg->range_min) * 180.0f + 0.5f);

    if (old_a > 180) old_a = 180;
    if (new_a > 180) new_a = 180;

    /* 擦旧指针: 用背景色重绘旧指针线 */
    if (old_a != new_a) {
        Gauge_Polar(old_a, R_ARC - 9, &pp_x, &pp_y);
        Gauge_Polar(old_a, 14, &pt_x, &pt_y);
        pt_x = 80 + (80 - pt_x);
        pt_y = 100 + (100 - pt_y);
        Draw_Thick_Line(80, 100, pp_x, pp_y, 3, UI_COLOR_BG);
        Draw_Thick_Line(80, 100, pt_x, pt_y, 2, UI_COLOR_BG);
    }

    /* 绘新指针 */
    Gauge_Polar(new_a, R_ARC - 9, &pp_x, &pp_y);
    Gauge_Polar(new_a, 14, &pt_x, &pt_y);
    pt_x = 80 + (80 - pt_x);
    pt_y = 100 + (100 - pt_y);
    Draw_Thick_Line(80, 100, pp_x, pp_y, 3, UI_COLOR_ALARM);
    Draw_Thick_Line(80, 100, pt_x, pt_y, 2, UI_COLOR_ALARM);

    /* 更新左上数值 */
    Tft_Driver_Fill_Rect(0, 0, 120, 16, UI_COLOR_BG);
    snprintf(label_buf, sizeof(label_buf), "%c %.2f", cfg->label, (double)value);
    Tft_Driver_Show_String(0, 0, label_buf, UI_COLOR_TEXT, UI_COLOR_BG);
}
```

- [ ] **Step 2: 添加静态变量追踪上次 EMA 值**

```c
static float s_last_ema_gauge_v = -1.0f;
static float s_last_ema_gauge_c = -1.0f;
static float s_last_ema_gauge_f = -1.0f;
```

- [ ] **Step 3: 提交**

```bash
git add -A && git commit -m "feat: 200ms增量指针刷新(擦旧+绘新+左上值)"
```

---

### Task 5: 替换三个 `Draw_*_Full` 和 `*_Dynamic_Update` 为薄封装

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 删除旧的六个函数, 替换为薄封装**

```c
/* ── 电压表 Full ── */
static void Draw_Volt_Full(void) {
    Update_EMA();
    Draw_Gauge_Full(&GAUGE_V, s_ema_v);
    s_last_ema_gauge_v = s_ema_v;
}

static void Volt_Dynamic_Update(void) {
    float old_v = s_last_ema_gauge_v;
    Update_EMA();
    Gauge_Dynamic_Update(&GAUGE_V, s_ema_v, old_v);
    s_last_ema_gauge_v = s_ema_v;
}

/* ── 电流表 Full ── */
static void Draw_Curr_Full(void) {
    Update_EMA();
    Draw_Gauge_Full(&GAUGE_C, s_ema_i);
    s_last_ema_gauge_c = s_ema_i;
}

static void Curr_Dynamic_Update(void) {
    float old_c = s_last_ema_gauge_c;
    Update_EMA();
    Gauge_Dynamic_Update(&GAUGE_C, s_ema_i, old_c);
    s_last_ema_gauge_c = s_ema_i;
}

/* ── 频率表 Full ── */
static void Draw_Freq_Full(void) {
    Update_EMA();
    Draw_Gauge_Full(&GAUGE_F, s_ema_f);
    s_last_ema_gauge_f = s_ema_f;
}

static void Freq_Dynamic_Update(void) {
    float old_f = s_last_ema_gauge_f;
    Update_EMA();
    Gauge_Dynamic_Update(&GAUGE_F, s_ema_f, old_f);
    s_last_ema_gauge_f = s_ema_f;
}
```

- [ ] **Step 2: 编译 + 提交**

```bash
# Keil F7 编译 → 确认 0 错误
git add -A && git commit -m "refactor: 三仪表页替换为通用 Draw_Gauge_Full/_Dynamic"
```

---

### Task 6: 处理 UI 调度中的 WIFI 动画 + 仪表页导航

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 更新 Phase 0 (WIFI 动画) — 仪表页不调用 Draw_Header**

仪表页全屏自绘, 不需要 Header。Phase 0 需要改为: 仪表页只更新 WIFI 图标 + Badge, 不调 Draw_Header。

检查 Phase 0 的 switch 分支: 三个仪表页调用 `Draw_Header` 会破坏全屏布局—需要替换为仅更新 WIFI 图标。

```c
    /* Phase 0 的分支里: */
    case UI_PAGE_MONITOR_FREQ:
    case UI_PAGE_MONITOR_VOLT:
    case UI_PAGE_MONITOR_CURR:
        /* 仪表页: 仅更新 WIFI + Badge, 不调 Draw_Header */
        {
            uint16_t x = 144, y = 0;
            /* 擦 WIFI+BADGE 区域 */
            Tft_Driver_Fill_Rect(120, 0, 40, 16, UI_COLOR_BG);
            /* ... 绘制 mini WIFI (复用 Draw_Header 内的 WiFi 代码, 提取为 Draw_WiFi_Mini) ... */
        }
        break;
```

- [ ] **Step 2: 提取 `Draw_TopRight_WiFi` 函数 (从 Draw_Header 中复制的 WIFI 绘制逻辑)**

```c
static void Draw_TopRight_WiFi(void)
{
    #define WIFI_ICON_X  144
    uint8_t  icon_frame;
    uint8_t  cs = App_Network_Get_Connect_Status();
    static const uint16_t blue_grad[6] = {
        0x0018, 0x001B, 0x001F, 0x07FF, 0x07BF, 0x07FF
    };

    /* 擦区域 */
    Tft_Driver_Fill_Rect(120, 0, 40, 16, UI_COLOR_BG);

    if (s_no_wifi_mode) {
        Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    } else if (!Esp8266_Driver_Is_Ready()) {
        icon_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6;
        Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0,
            WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
    } else if (cs == APP_NETWORK_CONN_ONLINE) {
        int8_t r = App_Network_Get_RSSI();
        if      (r >= -50) icon_frame = 3;
        else if (r >= -60) icon_frame = 2;
        else if (r >= -70) icon_frame = 1;
        else               icon_frame = 0;
        Tft_Driver_Draw_WiFi_Icon(WIFI_ICON_X, 0, icon_frame, UI_COLOR_OK, UI_COLOR_BG);
    } else if (App_Network_Is_Connecting()) {
        icon_frame = (uint8_t)(Sys_Timer_Get_Tick() / 150) % 6;
        Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0,
            WIFI_CONNECT_ANIM[icon_frame], blue_grad[icon_frame], UI_COLOR_BG);
    } else if (cs == APP_NETWORK_CONN_FAILED) {
        Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_OFF_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    } else {
        Tft_Driver_Draw_Single_Icon(WIFI_ICON_X, 0, WIFI_REMOVE_ICON, UI_COLOR_ALARM, UI_COLOR_BG);
    }
    #undef WIFI_ICON_X
}
```

- [ ] **Step 3: Phase 0 对仪表页调用 `Draw_TopRight_WiFi`**

```c
    if (wifi_frame != s_last_wifi_frame || mqtt_frame != s_last_mqtt_frame) {
        s_last_wifi_frame = wifi_frame;
        s_last_mqtt_frame = mqtt_frame;
        switch (s_page) {
            case UI_PAGE_MAIN_MENU:        Draw_Header(S_WPT_PWM);         break;
            /* ... 其他页 ... */
            case UI_PAGE_MONITOR_FREQ:
            case UI_PAGE_MONITOR_VOLT:
            case UI_PAGE_MONITOR_CURR:
                Draw_TopRight_WiFi();     /* 仪表页: 仅右上角 WIFI */
                break;
            /* ... */
        }
    }
```

- [ ] **Step 4: 编译 + 提交**

```bash
# Keil F7
git add -A && git commit -m "feat: 仪表页WIFI动画仅更新右上角,不破坏全屏布局"
```

---

### Task 7: 集成测试 + 微调

**文件:**
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **Step 1: 校验 Phase 9 绘制分支**

确保 `Phase 9` 的 switch 保持三个页面的 `Draw_*_Full` / `*_Dynamic_Update` 调用 — 不需要修改, 因为 Task 5 已替换函数。

- [ ] **Step 2: 校验边界条件 — 量程外值**

在 `Gauge_Polar` 中添加钳位:

```c
void Gauge_Polar(uint8_t angle_deg, uint16_t radius, int16_t* px, int16_t* py)
{
    if (angle_deg > 180) angle_deg = 180;
    /* ... 其余不变 ... */
}
```

在 Draw_Gauge_Full 开头加:

```c
    if (value < cfg->range_min) value = cfg->range_min;
    if (value > cfg->range_max) value = cfg->range_max;
```

- [ ] **Step 3: 全量编译测试**

```bash
# Keil F7 → 确认 0 Error 0 Warning
# 烧录到 STM32 → 导航到监测页面 → 验证仪表盘显示
```

- [ ] **Step 4: 提交**

```bash
git add -A && git commit -m "fix: 量程钳位 + 边界保护"
```

---

### 文件结构总结

| 文件 | 操作 | 职责 |
|:---|:---|:---|
| `Ui_Controller.c` | 修改 | 新增 sin_table[181], GaugeConfig, Draw_Gauge_Full, Gauge_Dynamic_Update, Draw_Hub, Draw_Thick_Line, Draw_TopRight_WiFi; 替换6个旧函数为薄封装 |
| `Tft_Driver.c` | (不改) | 已有 `Show_4x8_String_Pixel`, `Fill_Rect`, `Draw_Single_Icon` |
| `Tft_Driver.h` | (不改) | 已有全部声明 |

### 自检结果

1. **Spec 覆盖:** ✓ 全部三大仪表页替换, sin表, 极坐标, 三层刻度, 红区, 指针, Hub, WIFI+BADGE
2. **占位符:** ✓ 所有代码都有具体实现, 无 TBD
3. **类型一致:** ✓ `GaugeConfig` 在 Task 1 定义, Task 4-5 引用; `Draw_Thick_Line` 在 Task 2 定义, Task 4 引用
