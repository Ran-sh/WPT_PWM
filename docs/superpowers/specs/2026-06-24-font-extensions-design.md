# CH341 字库扩展 — 开机动画 + 新图标 + 多字号 设计文档

> **状态**: 已确认 | **日期**: 2026-06-24 | **版本**: V1.0

## 1. 目标

在已完成的 GB2312 全字库 (20897 CJK + 95 ASCII + 54 icon frames) 基础上，新增三个子系统：
- **A. 开机启动动画** — 5 帧 fade-in 全屏动画，存储在 SPLASH 分区
- **B. 新功能图标** — 20 个 16×16 像素图标 (电池/警告/箭头/齿轮/温度等)
- **C. 12×12 小字库** — CJK 全字库 12pt 版本，用于信息栏/状态行

## 2. Flash 布局 (不变)

```
┌────────────────┐ 0x000000
│ FONT (2MB)     │  16×16 CJK 20897字 (~780KB) ✅ 已有
│                │  + 新增 20 icon (~10KB)         ← B (扩展 generate_font.py ICON_SPEC)
│                │  + 12×12 CJK 20897字 (~612KB)   ← C (追加在 16×16 之后)
├────────────────┤ 0x200000
│ SPLASH (1MB)   │  开机动画 5帧 (200KB)            ← A (独立 bin 文件)
├────────────────┤ 0x300000
│ CFG (8KB)      │  (不变)
├────────────────┤ 0x310000
│ BLACKBOX (4MB) │  (不变)
└────────────────┘
```

## 3. 子项目 A: 开机启动动画

### 3.1 参数

| 参数 | 值 |
|:---|:---|
| 画面尺寸 | 160×128 全屏，RGB565 |
| 帧数 | 5 帧 (fade-in 渐进式亮度) |
| 单帧大小 | 160×128×2 = 40KB |
| 总大小 | 5×40KB = 200KB |
| 存储位置 | W25Q128 SPLASH 分区 (0x200000) |
| 生成方式 | Python PIL 渲染 WPT-PWM LOGO → splash.bin |
| 烧录方式 | flashrom 写到 0x200000 (不擦 FONT 分区) |
| 显示时长 | ~250ms (50ms/帧) |
| 跳过机制 | 任意按键按下 → 立即结束动画 |

### 3.2 实现

**Python 端** (`ch341/generate_splash.py`):
- PIL 渲染 WPT-PWM 标题 + 无线充电图标 + 版本号 → 160×128 RGB565 单帧
- 5 帧副本，每帧递增亮度 (opacity 0.2→1.0)
- 输出 `splash.bin` (200KB)

**STM32 端** (`Sys_Core.c` Sys_Startup_Screen 改造):
```c
void Sys_Startup_Screen(void) {
    Tft_Driver_Clear(TFT_COLOR_BLACK);
    /* 检测 SPLASH 分区魔法数 */
    if (g_splash_valid) {
        for (frame = 0; frame < 5; frame++) {
            /* DMA 全屏泵送一帧 (160×128×2=40960像素, 分两次 DMA) */
            W25Q_Driver_Read(SPLASH_BASE + frame*40960, buf, 40960);
            Tft_SPI_16bit();
            SetWin(0, 0, 159, 127);
            Tft_DMA_Send(buf, 20480);  // 第一批 20480 像素
            Tft_DMA_Send(buf+20480, 20480);  // 第二批
            Sys_Timer_Delay_Ms(50);
            if (Key_Driver_Any_Pressed()) break;
        }
    } else {
        /* 回退到当前纯文本启动画面 */
        Tft_Driver_Show_CN_String(3, 3, "WPT-PWM", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
        Tft_Driver_Show_CN_String(5, 3, "...", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    }
    Tft_Driver_Set_Backlight(255);
}
```

> 注意: TFT_DMA_MAX_PIXELS=65535 完全覆盖 20480。s_dma_buf[256] 需要扩容到至少 512 或直接复用 20480 栈上数组。

**SPLASH Header (32B, 在 splash.bin 开头)**:
```
0x00: magic    = 0x5350 ("SP")      // 2B
0x02: version  = 1                  // 1B
0x03: n_frames = 5                  // 1B
0x04: width    = 160                // 2B
0x06: height   = 128                // 2B
0x08: frame_bytes = 40960           // 4B
0x0C: reserved[20]                  // 20B
```

## 4. 子项目 B: 新增图标

### 4.1 图标列表

| ID | 名称 | 说明 | 帧数 | 字节 |
|:---|:---|:---|:---|:---|
| 11 | ICON_BATTERY | 电池电量 | 1 | 32 |
| 12 | ICON_WARNING | 三角警告 | 1 | 32 |
| 13 | ICON_CHECK | 勾选 | 1 | 32 |
| 14 | ICON_CROSS | 叉号 | 1 | 32 |
| 15 | ICON_POWER | 电源符号 | 1 | 32 |
| 16 | ICON_LIGHTNING | 闪电/高压 | 1 | 32 |
| 17 | ICON_TEMP | 温度计 | 1 | 32 |
| 18 | ICON_FAN | 风扇/散热 | 1 | 32 |
| 19 | ICON_LOCK | 锁定 | 1 | 32 |
| 20 | ICON_HOME | 主页 | 1 | 32 |
| 21 | ICON_GEAR | 齿轮/设置 | 1 | 32 |
| 22 | ICON_REFRESH | 刷新 | 1 | 32 |
| 23 | ICON_ARROW_UP | 上箭头 | 1 | 32 |
| 24 | ICON_ARROW_DN | 下箭头 | 1 | 32 |
| 25 | ICON_ARROW_LT | 左箭头 | 1 | 32 |
| 26 | ICON_ARROW_RT | 右箭头 | 1 | 32 |
| 27 | ICON_SIGNAL | 信号强度 | 1 | 32 |
| 28 | ICON_GLOBE | 地球/网络 | 1 | 32 |
| 29 | ICON_CHART | 柱状图 | 1 | 32 |
| 30 | ICON_CLOCK | 时钟 | 1 | 32 |
| **合计** | | | **20** | **640B** |

### 4.2 实现

**Python 端**:
- PIL 渲染每个图标为 16×16 1-bit 位图 → 32B LSB-first
- 追加到 `generate_font.py` 的 `ICON_SPEC` 列表
- 编入 `icon_id` 11~30

**STM32 端**:
- `TFT_Font_Data.h` 新增 20 个静态字模 (Flash 路径通过 icon_id 查 Icon Table)
- `Tft_Driver.h` 中新增 `Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y, uint8_t icon_id, uint16_t fg, uint16_t bg)`

## 5. 子项目 C: 12×12 小字库

### 5.1 技术参数

| 参数 | 值 |
|:---|:---|
| 点阵 | 12×12 px |
| 每字字节 | 2B/行 × 12行 = 24B |
| CJK 总数 | 20897 字 × 24B = ~490KB |
| ASCII 总数 | 95 字 × 16B (保持 8×16) |
| Index | 20897 × 6B = ~122KB |
| 合计 | ~612KB |
| Flash 位置 | FONT 分区，紧跟 16×16 CJK 之后 |

### 5.2 解码路径 (新增 Decode_CN_Row_12)

```c
static void Decode_CN_Row_12(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg, uint16_t* out) {
    /* 12×12: 低 12bit 有效，hi 仅低 4bit 有效 */
    uint8_t b;
    for (b = 0; b < 8; b++) {
        out[b] = (lo & (0x01 << b)) ? fg : bg;
        if (b < 4) out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
        else      out[b + 8] = bg;  // 行尾 4px 空白
    }
}
```

### 5.3 API

```c
void Tft_Driver_Show_CN_12x12(uint8_t line, uint8_t col, const char* str,
                               uint16_t fg, uint16_t bg);
/* 12×12 字符间距: 14px (2px padding), 每行最多 11 字 (160/14) */
```

## 6. 实施文件清单

| 文件 | 操作 | 子项目 |
|:---|:---|:---|
| `ch341/generate_splash.py` | **新建** | A |
| `ch341/burn_splash.py` | **新建** | A |
| `ch341/generate_font.py` | **修改** (追加 icon 20个 + 12×12 字模) | B, C |
| `Keil_Project/User/Sys_Core.c` | **修改** (Sys_Startup_Screen 改造) | A |
| `Keil_Project/Hardware/TFT_Font_Data.h` | **修改** (追加 icon bitmap) | B |
| `Keil_Project/Hardware/Tft_Driver.h` | **修改** (新 API) | B, C |
| `Keil_Project/Hardware/Tft_Driver.c` | **修改** (12×12 解码 + icon_id 查表) | B, C |
| `Keil_Project/Hardware/W25Q_Driver.h` | **修改** (SPLASH magic) | A |
| `ch341/README.md` | **修改** (更新操作指南) | ALL |

## 7. 实施顺序与优先级

| 顺序 | 子项目 | 理由 |
|:---|:---|:---|
| ① B | 新图标 | 最小改动 (640B), 立即丰富 UI 表现力 |
| ② A | 开机动画 | 用户感知最强，单独 SPLASH 分区不影响字库 |
| ③ C | 12×12 小字 | 最复杂 (612KB Flash + 新解码逻辑)，靠后 |

## 8. 风险

| 风险 | 缓解 |
|:---|:---|
| TFT DMA buf[256] 不够装全屏帧 | 循环 8 次 DMA 发送, 每次 5120 像素 → 复用 s_dma_buf[256] 多次小批量 |
| 12×12 与 16×16 Index 重复 | 独立 Index 区域，Header 新增 cjk12_index_offset/cjk12_data_offset 字段 |
| SPLASH 分区烧录误擦 FONT | burn_splash.py 只写 0x200000~0x232000 (200KB)，精确边界 |
| Font_Header 不兼容 V1 | version 递增为 2，新增字段仅 Python/STM32 协商读取 |
