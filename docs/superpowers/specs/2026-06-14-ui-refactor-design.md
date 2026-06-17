# UI 重构设计文档 — 两级菜单架构 (V4.2.1)

**日期**: 2026-06-14
**分支**: `4.0TFT`
**版本**: V4.2.1

---

## 1. 概述

将当前 6 态线性状态机 (`INIT → FAILED → READY → SWEEPING → RUNNING → FAULT`) 重构为**两级菜单架构**，分离导航逻辑与业务显示，支持主菜单 → 子模式/子菜单的栈式导航。

---

## 2. 导航树

```
┌─────────────────────────────────────────────────────────────┐
│  主菜单 (PAGE_MAIN_MENU)                                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ 1. 启动PWM / 停止PWM    ←→ 动态文字，就地切换         │   │
│  │    · 未发波 → KEY0 → 扫频页 → 完成→综合监测            │   │
│  │    · 发波中 → KEY0 → 停止PWM(留在主菜单)，文字切回     │   │
│  │ 2. 状态监测             → 监测子菜单                   │   │
│  │ 3. 无线配网             → 配网页                       │   │
│  │ 4. 故障清除             → 故障页   (未故障灰色不可选)   │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  故障触发: 过流>5.0A → 边沿触发 → 强制跳入故障页            │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  监测子菜单 (PAGE_MONITOR_SUB_MENU)                     │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │ 1. 综合监测       (F/V/I 同屏)                   │   │   │
│  │  │ 2. 监测频率       (仪表盘+能量条)                │   │   │
│  │  │ 3. 监测电压       (仪表盘+能量条)                │   │   │
│  │  │ 4. 监测电流       (仪表盘+能量条)                │   │   │
│  │  │ 5. 返回主菜单     ←→ KEY0 或 PAGE 均可返回       │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 状态机设计

### 3.1 页面枚举 (替代旧 `Ui_Controller_State`)

```c
typedef enum {
    UI_PAGE_MAIN_MENU          = 0,
    UI_PAGE_MONITOR_SUB_MENU   = 1,
    UI_PAGE_SWEEP              = 2,
    UI_PAGE_MONITOR_SUMMARY    = 3,   /* 综合监测 */
    UI_PAGE_MONITOR_FREQ       = 4,   /* 监测频率 */
    UI_PAGE_MONITOR_VOLT       = 5,   /* 监测电压 */
    UI_PAGE_MONITOR_CURR       = 6,   /* 监测电流 */
    UI_PAGE_WIFI_SETUP         = 7,
    UI_PAGE_FAULT              = 8
} Ui_Page;
```

### 3.2 页面状态变量

```c
static Ui_Page  s_page           = UI_PAGE_MAIN_MENU;
static uint8_t  s_menu_cursor    = 0;   /* 当前菜单光标 */
static uint8_t  s_last_pwm_state = 0;   /* 上一帧 PWM 是否在发波, 用于文字切换 */
static uint8_t  s_was_fault_state = 0;  /* 上一帧是否为故障态, 用于边沿计算 */
```

### 3.3 页面状态转换图

```
上电
 │
 ▼
MAIN_MENU ←─────────────────────────────────────────┐
 │                                                    │
 ├──[选1,未发波]→ SWEEP ──完成──→ MONITOR_SUMMARY ──┤ PAGE → MONITOR_SUB_MENU
 ├──[选1,发波中]→ 停止PWM → MAIN_MENU (就地)         │
 ├──[选2]→ MONITOR_SUB_MENU ────────────────────────┤
 │          ├──[选1]→ MONITOR_SUMMARY                │
 │          ├──[选2]→ MONITOR_FREQ ──────PAGE→ SUB   │
 │          ├──[选3]→ MONITOR_VOLT ──────PAGE→ SUB   │
 │          ├──[选4]→ MONITOR_CURR ──────PAGE→ SUB   │
 │          └──[选5]→ MAIN_MENU (同PAGE)             │
 ├──[选3]→ WIFI_SETUP ────PAGE→ MAIN_MENU            │
 └──[选4]→ FAULT (仅故障时可选) ──PAGE→ MAIN_MENU    │

过流>5.0A (边沿) ──强制跳入→ FAULT (锁存)
FAULT 中 KEY0 → Soft_Start_Reset → MAIN_MENU
```

### 3.4 关键防止

| 问题 | 方案 |
|:---|:---|
| 故障自动跳转无限循环 | **边沿触发**: `current_fault && !s_was_fault_state` 时跳转。进入 FAULT 后 s_was_fault_state=1，只有故障解除再重来才可新跳 |
| 故障页 WIFI 图标需统一 | 保留 Draw_Header("!!!故障!!!")，标题居中红色 + WIFI/MQTT 图标右对齐，与其他页统一样式 |
| 故障复位后去向 | KEY0 复位后直接回 MAIN_MENU |
| 未发波时监测值无意义 | 频率显示 `---.-kHz`，电压电流仍可显示(传感器独立工作) |

---

## 4. 按键总线映射 (终版)

```
+========+===========+===========+===============+=============+
|  页面   |    F+     |    F-     |     KEY0      |    PAGE     |
+========+===========+===========+===============+=============+
| 主菜单  | 光标-1    | 光标+1    | 进入选中项     |     —       |
| 监测子  | 光标-1    | 光标+1    | 进入/返回(项5) | →主菜单     |
| 扫频页  |    —      |    —      | 停止扫频       | →主菜单     |
| 综合-发波| 频率+1k   | 频率-1k   |      —        | →监测子菜单  |
| 综合-不发波|   —     |    —      |      —        | →监测子菜单  |
| 频率-发波| 频率+1k   | 频率-1k   |      —        | →监测子菜单  |
| 频率-不发波|  —     |    —      |      —        | →监测子菜单  |
| 监测电压  |    —      |    —      |      —        | →监测子菜单  |
| 监测电流  |    —      |    —      |      —        | →监测子菜单  |
| 配网页   |    —      |    —      |      —        | →主菜单     |
| 故障页   |    —      |    —      | 复位→主菜单   | →主菜单     |
+========+===========+===========+===============+=============+
```

**规则**:
- 只有 `PWM 发波中` 时 F+/F- 才做频率微调 (±1kHz)
- `停止PWM` 是主菜单 item 1 的就地动作，不跳转页面
- PAGE 在所有子页 = "返回上一层"，不做其他业务逻辑

---

## 5. 完整画面布局 (16×8 行，中文16px占2列)

### 5.1 主菜单 (状态 A: 光标在 item 1)

```
Line 0: WPT-PWM                [云] [WIFI]
Line 1: --------------------
Line 2: ▶ 1. 启动PWM                (青)
Line 3:   2. 状态监测                (白)
Line 4:   3. 无线配网                (白)
Line 5:   4. 故障清除                (白/灰)
Line 6: --------------------
Line 7: [F+/F-:上下 KEY0:确定]    (居中白)
```

- 第 0 行: 标题 `WPT-PWM` 左对齐黄色 + MQTT 云图标(x=128) + WIFI 图标(x=144)
- 第 1/6 行: `--------------------` 20字符分割线 灰色
- 第 2~5 行: 4 项菜单，选中项 `▶` 前缀 + 青色高亮，其余白色
- 第 4 行故障清除: `Inverter_Control_SS_Get_State() != FAULT` 时灰色降级不可选
- 第 1 行文字: PWM 未发波=`启动PWM`, 发波中=`停止PWM`
- WIFI 角标沿用现有逻辑 (在线绿/连接中蓝/离线红)
- 光标边界: 0~(3 或 2,故障不存在时上限-1)

### 5.2 监测子菜单 (光标在 item 1)

```
Line 0: 状态监测                [云] [WIFI]
Line 1: --------------------
Line 2: ▶ 1. 综合监测                (青)
Line 3:   2. 监测频率                (白)
Line 4:   3. 监测电压                (白)
Line 5:   4. 监测电流                (白)
Line 6: --------------------
Line 7:   5. 返回主菜单              (白)
```

- 光标在项5时也支持 KEY0 返回 (与 PAGE 等价)
- 第 7 行底栏取消通用操作提示，直接显示菜单项

### 5.3 扫频页

```
Line 0: 扫频页                  [云] [WIFI]
Line 1: --------------------
Line 2: 频率F: 120.4kHz              (青, EMA)
Line 3: [■■■■■■■■■■      ]  60%      (能量条+百分比)
Line 4: 电压V: 12.45V                (蓝)
Line 5: 电流I:+0.352A                (蓝)
Line 6: --------------------
Line 7: [KEY0:停止  PAGE:返回]      (右对齐白)
```

- 能量条 14×TFT_FONT_WIDTH 宽, 8px 高, 绿→红渐变
- EMA 平滑: V/I/F 全部用 `Update_EMA()` 值
- KEY0 停止 → 立即关 PWM (MOE+计数器) → 显示变化 → 等待用户 PAGE 返回
- 扫频完成 → 自动跳入综合监测 (不去监测子菜单, 直接到数据页)

### 5.4 综合监测 (未发波 / 发波中)

**未发波:**
```
Line 0: 综合监测              [云] [WIFI]
Line 1: --------------------
Line 2: 频率F: ---.-kHz              (青)
Line 3: 电压V: 12.15V                (青)
Line 4: 电流I:+0.352A                (青)
Line 5:                               (空)
Line 6: --------------------
Line 7: [PAGE:返回]                  (右对齐白)
```

**发波中:**
```
Line 0: 综合监测              [云] [WIFI]
Line 1: --------------------
Line 2: 频率F: 100.00kHz             (青, EMA)
Line 3: 电压V: 12.15V                (青, EMA)
Line 4: 电流I:+1.234A                (青, EMA)
Line 5: [全宽电压/电流能量条]         (绿→红)
Line 6: --------------------
Line 7: [F+/F-:调频  PAGE:返回]     (右对齐白)
```

- 发波中: 第 5 行用能量条展示功率强度，第 7 行提示 F+/F- 调频
- 未发波: 频率占位 `---.-kHz`，第 5 行空，第 7 行仅提示返回
- PAGE → 回监测子菜单

### 5.5 监测频率 (仪表盘)

```
Line 0: 监测频率              [云] [WIFI]
Line 1: --------------------
Line 2: 频率F: 100.0kHz              (青, EMA)
Line 3:
Line 4: [■■■■■■■■■■■■■■■■■■■■]       (能量条, 95~150kHz)
Line 5: 95                           150
Line 6: --------------------
Line 7: [PAGE:返回]                  (右对齐白)
```

- 未发波时 `---.-kHz` + 能量条宽度为 0
- 发波中 F+/F- 调频
- PAGE → 回监测子菜单

### 5.6 监测电压 (仪表盘)

```
Line 0: 监测电压              [云] [WIFI]
Line 1: --------------------
Line 2: 电压V: 12.15V               (青, EMA)
Line 3:
Line 4: [■■■■■■■■■■■■■■■■■■■■]       (能量条, 0~48V)
Line 5: 0                            48
Line 6: --------------------
Line 7: [PAGE:返回]                  (右对齐白)
```

- 电压传感器独立于 PWM，任何状态均可读数

### 5.7 监测电流 (仪表盘)

```
Line 0: 监测电流              [云] [WIFI]
Line 1: --------------------
Line 2: 电流I:+1.234A               (青, EMA)
Line 3:
Line 4: [■■■■■■■■■■■■■■■■■■■■]       (能量条, 0~3A)
Line 5: 0                             3
Line 6: --------------------
Line 7: [PAGE:返回]                  (右对齐白)
```

### 5.8 无线配网

```
Line 0: 启动页                  [云] [WIFI]
Line 1: --------------------
Line 2: 无线状态: 已连线上线           (白)
Line 3: 重试 0/3                     (青, 仅连接中显示)
Line 4:                              (空)
Line 5: 长按ON:清除WIFI              (红, 危险操作提示)
Line 6: --------------------
Line 7: [PAGE:返回]                  (右对齐白)
```

- 状态文字: `已连线上线` / `连接中` / `连接失败`
- 清除 WiFi 操作: 用户长按 KEY0 触发 (保留现有长按逻辑)
- PAGE → 回主菜单

### 5.9 故障清除

```
Line 0: !!!故障!!!                        (居中红)
Line 1: --------------------
Line 2: 过流保护                          (居中红)
Line 3: PWM已关断                         (居中白)
Line 4:                                   (空)
Line 5: 按KEY0复位重启                    (居中青)
Line 6: --------------------
Line 7: [PAGE:返回]                      (右对齐白)
```

- 自动跳入 (边沿)，无需用户确认
- KEY0 → `Soft_Start_Reset()` → 回主菜单
- PAGE → 回主菜单 (不复位! 仅查看)
- 标题行沿用 Draw_Header 统一布局: "!!!故障!!!" 居中红 + 云图标 + WIFI 图标

---

## 6. 代码模块变更

### 6.1 Ui_Controller.h

| 变更 | 说明 |
|:---|:---|
| 新增 `Ui_Page` 枚举 | 9 页枚举，替代旧 `Ui_Controller_State` |
| 删除 `Ui_Controller_State` | 6 态枚举移除 |
| 删除 `Ui_Controller_Get_Bridge_State()` | 不再需要桥状态查询 |
| 保留 `Ui_Controller_Get_Page()` | 新名称，返回 `Ui_Page` |
| 新增 `Ui_Controller_Get_Page()` | 新接口，返回当前所在 `Ui_Page` |
| 保留 `Ui_Controller_Is_No_WiFi_Mode()` | WiFi 模式判断保留 |

### 6.2 Ui_Controller.c

| 模块 | 变更 |
|:---|:---|
| `Draw_Header()` | 保留，WIFI/MQTT 图标逻辑不变，标题由调用者传入 |
| `Calc_Ui_State()` | **删除** — 不再需要状态推导，页面由按键直接控制 |
| `Handle_Keys()` | **重写** — 改为按 `s_page` + `s_menu_cursor` 分发 |
| `Draw_Main_Menu()` | **新增** — 4 项菜单 + 动态文字渲染 |
| `Draw_Monitor_Sub_Menu()` | **新增** — 5 项子菜单渲染 |
| `Draw_Sweep_Page()` | **重写** — 新增分割线 + 底栏 |
| `Draw_Monitor_Summary()` | **重写** — 分割线 + 未发波占位处理 |
| `Draw_Monitor_Freq/Volt/Curr()` | **重写** — 独立仪表盘页 |
| `Draw_WiFi_Setup()` | **新增** — 配网详情页 |
| `Draw_Fault_Page()` | **重写** — 分割线 + KEY0 复位 + PAGE 返回 |
| `Ui_Controller_Task()` | **重写** — 主调度: 渲染 → 按键 → 边沿检测 → LED/Buzzer |

### 6.3 保留不变的渲染图元

- `Energy_Bar_Draw()` — 不变
- `Fmt_V()` / `Fmt_I()` / `Fmt_F()` — 不变
- `Update_EMA()` / `Reset_EMA()` — 不变
- `Center()` / `Right()` — 不变
- TFT 字库、图标文件 — 不变

### 6.4 主循环调度不变

```c
while (1) {
    Key_Driver_Task();
    Adc_Driver_Filter_Task();
    Ui_Controller_Task();           // 200ms, 内部实现全部重写
    App_Network_Task();
    Inverter_Control_Soft_Start_Task();
    Inverter_Control_Freq_Ramp_Task();
    Led_Driver_Task();
    Buzzer_Driver_Task();
    IWDG_ReloadCounter();
    __WFI();
}
```

### 6.5 外部接口 (跨模块调用)

| 函数 | 用途 | 文件 |
|:---|:---|:---|
| `Ui_Controller_Get_Page()` | 获取当前页面 | 外部查询 |
| `Ui_Controller_Is_No_WiFi_Mode()` | 是否无WiFi模式 | 远程指令门控 |

---

## 7. 实现风险与注意事项

### 7.1 故障跳转线程安全

- 故障检测 (`Inverter_Control_Soft_Start_Get_State() == FAULT`) 在主调度中每 200ms 轮询一次
- 边沿检测: `current_fault && !s_was_fault_state` → 跳入故障页 → `s_was_fault_state = 1`
- 只有故障先解除 (`Get_State() != FAULT`) 再重新触发时 `s_was_fault_state` 才为 0，才能再次跳转
- 避免 ISR 或非周期上下文中直接修改 UI 状态

### 7.2 页面状态持久化

- `s_page` / `s_menu_cursor` 仅在 `Ui_Controller_Task()` 的 200ms 周期中修改
- 不跨模块写入，外部只读 `Ui_Controller_Get_Page()`
- `s_was_fault_state` 在主调度中计算: `current_fault = (Inverter_Control_SS_Get_State() == FAULT)`, 边沿 = `current_fault && !s_was_fault_state`, 跳转后设置 `s_was_fault_state = current_fault`

### 7.3 PWM 发波状态判定

- 用 `Inverter_Control_Soft_Start_Get_State()` 判断:
  - `SWEEP` / `DONE` → 发波中
  - `IDLE` / `FAULT` → 未发波
- 主菜单 item 1 文字切换基于此

### 7.4 画面全刷 vs 局部刷新

- 页面切换: `Tft_Driver_Clear()` + 全量重绘
- 菜单光标移动: 局部刷新 (仅重绘上文/本行)
- 监测数据更新 (200ms): 非菜单页的 V/I/F 行 + 能量条重绘
- WIFI 图标帧切换: 局部刷新第 0 行

---

## 8. 向上兼容

| 旧接口 | 新接口 | 兼容方案 |
|:---|:---|:---|
| `Ui_Controller_State` | `Ui_Page` | 删除旧枚举, `.c` 中完全替换 |
| `Ui_Controller_Get_State()` | `Ui_Controller_Get_Page()` | 返回新枚举, 外部如有引用需同步更新 |
| `Ui_Controller_Get_Bridge_State()` | — | 删除, 外部改为直接查 `Inverter_Control` |
| `Ui_Controller_Is_No_WiFi_Mode()` | 同名保留 | 不变 |

---

## 9. 文件大小预估

| 文件 | 旧行数 | 新预估 | 方向 |
|:---|:---|:---|:---|
| `Ui_Controller.h` | 30 | ~45 | 枚举变更 + 新增接口 |
| `Ui_Controller.c` | 740 | ~900 | 8 个 Draw 函数 + 按键分发 + 菜单管理 |
