# V4.4.0 — 设置页面 + ROM/Flash 资源重划分 + 多语言

> **状态**: 设计阶段 | **日期**: 2026-07-01 | **版本**: V4.4.0

## 1. 概述

为 STM32F103C8 TFT 人机界面新增**设置系统**，同时精简 ROM 内置字库至仅 SPLASH 开机动画所需的 4 个中文 + 3 个图标 + ASCII 95 字。其余中文字库和图标全部移至 W25Q128 Flash。当 Flash 检测无效时，界面自动切换为英文，3 个 ROM 图标保持可用。

## 2. ROM/Flash 资源重划分

### 2.1 ROM 内置（STM32 Flash, `TFT_Font_Data.h`）

| 资源 | 变更前 | 变更后 | 说明 |
|------|--------|--------|------|
| ASCII `TFT_FONT_8X16` | 95 字 | **不变** | 始终可用 |
| 中文 `CN_FONT_16X16` | 76 字 | **4 字** | 无/线/充/电，仅 SPLASH |
| `CN_INDEX` | 76 条目 | **4 条目** | 对应精简 |
| WIFI 图标 | 4 帧 + 动画 | **保留** | 顶部状态栏 |
| MQTT 图标 | 3 态 + 动画 | **保留** | 顶部状态栏 |
| ICON_STAR | 1 帧 | **保留** | 菜单光标前导 |
| 20 新图标 | 20 个 | **移除** | 移至 Flash |
| 历史动画 | STAR_CURSOR/ROCKET | **移除** | 已废弃 |

### 2.2 W25Q128 Flash（外部 2MB 字库分区）

| 资源 | 内容 |
|------|------|
| ASCII | 95 字（副本） |
| CJK | GB2312 一级 U+4E00~U+9FA0，共 20902 字 |
| 图标 | 全部 31 图标（54 帧） |

### 2.3 Flash 无效时的行为

| 功能 | Flash 无效时 |
|------|-------------|
| 中文字符渲染 | 自动输出英文翻译（内置映射表） |
| 图标（状态栏） | ROM 回退：WIFI/MQTT/STAR 可用 |
| 图标（其他） | 不可用，图标浏览页显示 "Flash required" |
| 设置持久化 | RAM 中生效，重启恢复默认 |
| 语言切换 | RAM 中生效，重启恢复默认（English） |

## 3. 中文 → 英文翻译映射表

ROM 中新增 `CN_TO_EN_MAP` 常量表。当 `g_font_flash_valid == 0` 时，`Tft_Driver_Show_CN_String()` 自动查表输出英文。

**映射方式**：整串匹配（非逐字翻译），key 为 UI 中实际使用的完整中文字符串，value 为英文翻译。

```c
/* key: UTF-8 完整字符串, value: 英文翻译 */
static const struct {
    const char* cn;    /* UTF-8 中文字符串 */
    const char* en;    /* ASCII 英文翻译 */
} s_cn_to_en_map[] = {
    {"\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5", "Wireless Charging"},  /* 无线充电 */
    {"\xe8\xae\xbe\xe7\xbd\xae", "Settings"},                                    /* 设置 */
    {"\xe8\xaf\xad\xe8\xa8\x80", "Language"},                                    /* 语言 */
    {"\xe9\xa2\x91\xe7\x8e\x87", "Frequency"},                                   /* 频率 */
    /* ... 覆盖 Ui_Controller.c 中所有 Show_CN_String 调用 */
};
```

翻译映射需覆盖 `Ui_Controller.c` 中所有 `Tft_Driver_Show_CN_String()` 调用的中文字符串。遗漏会导致运行时空白，需在实现阶段全量 grep 审计。

## 4. 页面架构

### 4.1 页面枚举扩展

```c
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,
    UI_PAGE_MONITOR_SUB_MENU   = 1,
    UI_PAGE_SWEEP              = 2,
    UI_PAGE_MONITOR_SUMMARY    = 3,
    UI_PAGE_MONITOR_FREQ       = 4,
    UI_PAGE_MONITOR_VOLT       = 5,
    UI_PAGE_MONITOR_CURR       = 6,
    UI_PAGE_WIFI_SETUP         = 7,
    UI_PAGE_FAULT              = 8,
    /* V4.4.0 新增 */
    UI_PAGE_SETTING            = 9,
    UI_PAGE_SETTING_LANG       = 10,
    UI_PAGE_SETTING_ICONS      = 11,
    UI_PAGE_SETTING_FONT       = 12,
    UI_PAGE_SETTING_BL         = 13,
    UI_PAGE_SETTING_COLOR      = 14,
} Ui_Page;
```

### 4.2 导航树

```
MAIN_MENU ───────────────────────────────────────────┐
  │ 0: 启停 (→ SWEEP or IDLE)                        │
  │ 1: 监测 (→ MONITOR_SUB_MENU)                     │
  │ 2: 配网 (→ WIFI_SETUP)                           │
  │ 3: 设置 (→ SETTING)  ← 新增                     │
  │ 4: [故障] (→ FAULT, 仅 SYS_FAULT 时显示)         │
  │                                                   │
  ├── MONITOR_SUB_MENU                                │
  │     ├── SUMMARY / FREQ / VOLT / CURR              │
  │     └── 5: 返回主菜单                              │
  │                                                   │
  ├── WIFI_SETUP                                      │
  ├── FAULT                                           │
  │                                                   │
  └── SETTING ────────────────────────────────────┐   │
        ├── 语言 Language      → SETTING_LANG     │   │
        ├── 图标浏览 Icons      → SETTING_ICONS    │   │
        ├── 字体大小 Font       → SETTING_FONT     │   │
        ├── 亮度调节 Brightness → SETTING_BL       │   │
        └── 颜色方案 Color      → SETTING_COLOR    │   │
                                                    │   │
        按键: PAGE=确认进入  ON/OFF=返回上级 ←─────┘   │
        所有光标前导: ICON_STAR (16×16) ←──────────────┘
```

## 5. 各页面详细设计

### 5.1 设置主菜单 (`UI_PAGE_SETTING`)

```
┌──────────────────────────┐
│         设置              │  Row 0: 标题居中 (中文/English 自适应)
│                          │
│   * 语言     Language    │  Row 2: ICON_STAR + 菜单项
│   * 图标浏览 Icons        │  Row 3
│   * 字体大小 Font         │  Row 4
│   * 亮度调节 Brightness   │  Row 5
│   * 颜色方案 Color        │  Row 6
│                          │
│                [ON返回]   │  Row 7: 右下角提示
└──────────────────────────┘
```

- 5 项光标，F_UP/F_DOWN 上下滚动
- PAGE 确认进入子页
- 标题/菜单项根据 `g_language` (0=中文, 1=English) 双语切换

### 5.2 语言切换 (`UI_PAGE_SETTING_LANG`)

```
┌──────────────────────────┐
│    语言 / Language       │  Row 0: 标题居中
│                          │
│                          │
│   * 中文                 │  Row 3: 当前选中 ●
│      English             │  Row 4
│                          │
│                          │
│                [ON返回]   │  Row 7
└──────────────────────────┘
```

- 上下键切换选中行，PAGE 确认生效
- 立即更新全局 `g_language` 变量，全屏重绘
- `g_language` 持久化到 Flash 参数区（Flash 无效时仅 RAM）

### 5.3 图标浏览 (`UI_PAGE_SETTING_ICONS`)

```
┌──────────────────────────┐
│ 图标 Icons        [1/2]  │  Row 0: 标题 + 页码
│                          │
│  [16×16][16×16][16×16]   │  Row 1-6: 图标网格
│  [16×16][16×16][16×16]   │  5 列 × 6 行 = 30 格/屏
│  [16×16][16×16][16×16]   │  每格 18×18 (含 2px 边距)
│  [16×16][16×16][16×16]   │  总宽: 5×18=90px, 居中
│  [16×16][16×16][16×16]   │  总高: 6×18=108px
│  [16×16][16×16][16×16]   │
│                          │
│ * ICON_BATTERY     [11]  │  Row 7: 当前图标名 + ID
└──────────────────────────┘
```

- 31 个图标分 2 页（第 1 页 0-29，第 2 页 30）
- 高亮光标通过 `ICON_STAR` 或边框矩形标记当前位置
- F_UP 到顶 → 上一页；F_DOWN 到底 → 下一页
- 底部显示当前高亮图标名称（英文缩写）和 ID 编号
- Flash 无效时：居中显示 "Flash required"（英文）

### 5.4 字体大小 (`UI_PAGE_SETTING_FONT`)

```
┌──────────────────────────┐
│                          │
│   字体大小 / Font Size   │  Row 1: 标题居中
│                          │
│   * 中号 (默认)          │  Row 3: ● 当前选中
│     小号                 │  Row 4
│                          │
│  预览: 无线充电           │  Row 5: 实时预览行（当前字号）
│  Preview: WPT System     │  Row 6
│                          │
│                [ON返回]   │  Row 7
└──────────────────────────┘
```

- 两档：小号（紧凑间距，字模不变 8×16/16×16）/ 中号（默认间距）
- 上下键切换 ●，PAGE 确认即时生效
- 预览行实时反映当前字号和间距效果
- 设置持久化到 Flash

### 5.5 背光亮度 (`UI_PAGE_SETTING_BL`)

```
┌──────────────────────────┐
│                          │
│   亮度 / Brightness      │  Row 1: 标题居中
│                          │
│   ▓▓▓▓▓▓▓▓▓▓▓░░░░░░    │  Row 3: 进度条 (128px 宽)
│      192 / 255           │  Row 4: 数值居中
│                          │
│  [F_UP +]   [F_DOWN -]  │  Row 6: 操作提示居中
│                          │
│                [ON返回]   │  Row 7
└──────────────────────────┘
```

- 进入页面后自动启动呼吸模式（亮度在 `[当前值, 248]` 区间正弦渐变，周期 ~2.5s）
- 最小值不低于 48（保证可见），最大值 248
- 按 F_UP/F_DOWN 时退出呼吸模式，进入手动调节
  - 单击 ±8（细调，32 级）
  - 长按（>500ms）±32（加速）
  - 背光实时响应
- 2 秒无操作后恢复呼吸模式
- 设置持久化到 Flash

### 5.6 颜色方案 (`UI_PAGE_SETTING_COLOR`)

```
┌──────────────────────────┐
│  颜色方案 / Color        │  Row 0: 标题居左
│                          │
│   * 默认    Classic      │  Row 2: 6 种预设
│     琥珀    Amber        │  Row 3
│     青霓    Cyber        │  Row 4
│     护眼    EyeCare      │  Row 5
│     高对比  HiContrast   │  Row 6
│     暖白    Warm         │
│                          │
│                [ON返回]   │  Row 7
└──────────────────────────┘
```

**6 种预设方案：**

| 预设 | 背景色 | 前景色 | 强调色 |
|------|--------|--------|--------|
| 默认 Classic | `#000000` 黑 | `#FFFFFF` 白 | `#FFE000` 黄 |
| 琥珀 Amber | `#001A33` 深蓝 | `#FFA500` 琥珀 | `#FF8C00` |
| 青霓 Cyber | `#000A14` 极深蓝 | `#00FFFF` 青 | `#00FF88` |
| 护眼 EyeCare | `#1A1A1A` 深灰 | `#AACCAA` 浅绿灰 | `#88BB88` |
| 高对比 HiContrast | `#000000` 纯黑 | `#FFFFFF` 纯白 | `#00FF00` |
| 暖白 Warm | `#1C1810` 深棕 | `#FFE0C0` 暖白 | `#FFD088` |

- 上下键滚动选择（6 项可翻页），PAGE 确认即时生效
- 生效后全屏重绘
- 底部短暂显示 "Applied ✓" (~500ms)
- 额外 `* 自定义 RGB...` 选项：进入后分别调字色和底色

**自定义 RGB 子界面：**

```
┌──────────────────────────┐
│  自定义 / Custom         │
│                          │
│  字色 FG: R[24] G[16] B[0]│  Row 2-3: RGB 通道
│  底色 BG: R[ 0] G[ 0] B[0]│  Row 4-5
│                          │
│ 预览: 无线充电 WPT       │  Row 6: 实时预览
│                          │
│                [ON返回]   │  Row 7
└──────────────────────────┘
```

- F_UP/F_DOWN 切换编辑行，PAGE 进入通道编辑
- 通道编辑：F_UP/F_DOWN ±1（范围 0-31），PAGE 确认退出编辑
- 实时预览行反映当前颜色
- 持久化到 Flash

## 6. 全局状态变量

```c
/* 新增全局变量 */
static uint8_t  s_language;         /* 0=中文, 1=English */
static uint8_t  s_font_size;        /* 0=小号, 1=中号(默认) */
static uint8_t  s_backlight_val;    /* 48-248, 默认 248 */
static uint16_t s_color_fg;         /* RGB565 前景默认 #FFFFFF */
static uint16_t s_color_bg;         /* RGB565 背景默认 #000000 */
static uint8_t  s_color_preset;     /* 0-5 预设索引, 255=自定义 */

/* 持久化到 W25Q Flash 参数区 (W25Q_ADDR_CFG_A/B) */
/* 使用 App_Storage 现有 CRC32 双副本机制 */
```

## 7. 按键映射（全设置子系统统一）

| 按键 | 设置主菜单 | 子页面 | 编辑模式 |
|------|-----------|--------|---------|
| F_UP (PB8) | 光标上移 | 光标上移 / 值+ | 通道值+1 |
| F_DOWN (PB7) | 光标下移 | 光标下移 / 值- | 通道值-1 |
| PAGE (PB5) | 进入子页 | 确认/生效 | 退出编辑 |
| ON/OFF (PB9) | 返回上级 | 返回上级 | 返回上级 |

**长按加速**：亮度调节页 F_UP/F_DOWN 长按 >500ms 触发 ±32 加速。

## 8. 文件变更清单

| 文件 | 变更 |
|------|------|
| `TFT_Font_Data.h` | 中文 76→4 字，20 新图标移除，新增 `CN_TO_EN_MAP` |
| `TFT_Driver.h` | 无新增公开接口 |
| `Tft_Driver.c` | `Show_CN_String()` 增加 Flash 无效时的翻译路径 |
| `Ui_Controller.h` | 新增 6 个页面枚举值 |
| `Ui_Controller.c` | 新增 6 个页面绘制 + 按键分发 + 设置状态变量 |
| `App_Storage.c/h` | 新增设置参数读写接口 |
| `generate_font.py` | ROM 4 字匹配，图标 ID 对齐 |
| `W25Q_Driver.h` | 无变更 |
| `CLAUDE.md` | 版本号 V4.3.2→V4.4.0，文件结构行数更新 |
| `ch341/README.md` | 字库精简说明 |

## 9. 版本号

```
V4.4.0 — 中版本 (y+1):
  新增设置系统 (6 页面) + ROM/Flash 资源重划分 + 中英文双语 + 多配色方案
```

## 10. 风险与约束

- **Flash 无效时翻译覆盖率**：需确保所有 UI 中文都有对应的 `CN_TO_EN_MAP` 条目，遗漏会导致空白
- **ROM 空间**：移除 72 个中文字模 (~2.3KB) 和 20 个图标 (~640B) 后 ROM 空间增加 ~3KB
- **Flash 参数区**：复用现有 `W25Q_ADDR_CFG_A/B` 双副本 CRC32 机制，设置数据需在现有基础上扩展
- **SPLASH**：4 中文 "无线充电" 保留在 ROM，不受 Flash 影响
