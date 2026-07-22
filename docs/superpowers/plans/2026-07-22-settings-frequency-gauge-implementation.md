# 设置系统、双档启动频率与递增式表盘 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将STM32设置菜单重构为五项简洁设置，增加可迁移的双档启动频率和全局菜单光标，并把三个独立表盘改为分段递增刻度与大数值显示。

**Architecture:** 配置兼容和W25Q128持久化仍由 `App_Storage` 独占；`Inverter_Control` 只接收已经验证的档位参数并生成动态扫频；`Ui_Controller` 负责编辑副本、确认/取消交互和显示；`Pwm_Driver` 作为20–200kHz的最终硬边界。表盘共用一套分段映射与差分刷新算法，不复制电压、电流和频率算法。

**Tech Stack:** STM32F103C8、SPL V3.5.0、ARMCC V5.06/C89、TIM1互补PWM、ST7735 160×128、W25Q128、PowerShell静态回归检查、Keil uVision完整编译。

## 交付状态（V5.1.0，2026-07-22）

- [x] 任务1：配置V2、范围量化与V1无损迁移。
- [x] 任务2：20–200kHz边界与按档位锁定的非阻塞扫频。
- [x] 任务3：五项设置、双档编辑与八图标全局光标。
- [x] 任务4：共用递增刻度、2倍主数值与绘制失败恢复。
- [x] 静态契约、注释/调用边界检查和 ARMCC 完整重编译。
- [ ] 实机示波器、限流温升和故障注入检查（需接入硬件，未由静态检查替代）。

## Global Constraints

- 只修改 `Keil_Project/Hardware`、`Keil_Project/User`、必要工程配置及对应设计/实施文档；不得修改 `Keil_Project/Library`。
- 所有运行期任务继续使用无符号时间戳差值调度，不增加阻塞延时。
- PWM保持50%占空比、1000ns死区和互补输出安全顺序。
- PWM统一硬边界为20,000–200,000Hz；所有调用最终必须经过 `Pwm_Driver_Set_Frequency()`。
- 低频档范围20,000–99,900Hz、步进100Hz、默认20,000Hz。
- 高频档范围100,000–200,000Hz、步进1,000Hz、默认100,000Hz。
- 默认启用高频档；设置只影响下一次启动，不在运行中跳变。
- `.c` 文件保留中文文件头；`.h` 文件按用户要求直接以保护宏开始；禁止 `//` 注释。
- ARMCC V5/C89代码必须先声明变量再执行语句，不使用C99复合字面量、变长数组或行内声明。
- 每个任务只暂存本任务文件，不得包含工作区已有的无关改动。

---

## 文件职责与改动映射

- `Keil_Project/User/App_Storage.h`：定义第二版配置字段和扩展后的设置读写接口。
- `Keil_Project/User/App_Storage.c`：第一版配置兼容读取、第二版校验、默认值和后台持久化。
- `Keil_Project/Hardware/Pwm_Driver.h`：20–200kHz最终频率边界。
- `Keil_Project/Hardware/Inverter_Control.h`：启动档位、动态扫频参数和查询接口。
- `Keil_Project/Hardware/Inverter_Control.c`：按档位从99.9kHz或200kHz向目标下降。
- `Keil_Project/User/Sys_Core.c`：上电加载配置后把启动档位注入逆变控制器。
- `Keil_Project/Hardware/Ui_Controller.h`：设置加载接口扩展。
- `Keil_Project/Hardware/Ui_Controller.c`：五项设置菜单、启动频率编辑、八图标光标及递增式表盘。
- `Keil_Project/Hardware/Tft_Driver.h/.c`：复用现有8×16字模绘制2倍ASCII主数值。
- `Keil_Project/tests/verify_settings_frequency.ps1`：配置布局、频率边界、分段连续性和菜单契约回归检查。
- `Keil_Project/Project.uvprojx`：仅当新增测试外的固件源文件时更新；本计划不新增固件模块，因此预计无需修改。

---

### Task 1: 第二版配置与第一版无损迁移

**Files:**
- Create: `Keil_Project/tests/verify_settings_frequency.ps1`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/User/App_Storage.c`

**Interfaces:**
- Produces: `APP_STORAGE_FREQ_BAND_LOW/HIGH`。
- Produces: `startup_low_freq_hz`、`startup_high_freq_hz`、`startup_freq_band`、`menu_cursor_icon`。
- Produces: 扩展后的 `App_Storage_Load_Settings()` 与 `App_Storage_Request_Save_Settings()`。

- [ ] **Step 1: 建立配置契约失败检查**

创建 `Keil_Project/tests/verify_settings_frequency.ps1`：

```powershell
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$storageH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.h"
$storageC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.c"

function Assert-Contains([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

Assert-Contains $storageH '#define CFG_VERSION\s+2U' '配置版本必须升级为2'
Assert-Contains $storageH 'uint32_t\s+startup_low_freq_hz;' '缺少低频档字段'
Assert-Contains $storageH 'uint32_t\s+startup_high_freq_hz;' '缺少高频档字段'
Assert-Contains $storageH 'uint8_t\s+startup_freq_band;' '缺少当前档位字段'
Assert-Contains $storageH 'uint8_t\s+menu_cursor_icon;' '缺少全局光标字段'
Assert-Contains $storageC 'App_Storage_Config_V1' '缺少第一版迁移结构'
Assert-Contains $storageC 'App_Storage_Migrate_V1' '缺少第一版迁移函数'
Write-Host '配置契约检查通过'
```

- [ ] **Step 2: 运行检查并确认失败原因正确**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
```

Expected: FAIL，首个错误为“配置版本必须升级为2”。

- [ ] **Step 3: 定义第二版配置字段与档位常量**

在 `App_Storage.h` 中把版本改为2并在CRC字段前增加：

```c
#define CFG_VERSION 2U

#define APP_STORAGE_FREQ_BAND_LOW   0U
#define APP_STORAGE_FREQ_BAND_HIGH  1U

uint32_t startup_low_freq_hz;   /* 低频档启动值，20,000至99,900Hz */
uint32_t startup_high_freq_hz;  /* 高频档启动值，100,000至200,000Hz */
uint8_t  startup_freq_band;     /* 当前启动档位 */
uint8_t  menu_cursor_icon;      /* 八个候选图标中的索引 */
uint16_t reserved_v2;           /* 保持四字节对齐，写入前清零 */
```

保留第一版所有既有字段，第二版结构总长度必须小于或等于256字节。

- [ ] **Step 4: 实现默认值、范围校验和第一版迁移**

在 `App_Storage.c` 内定义与旧196字节布局完全一致的私有 `App_Storage_Config_V1`。实现：

```c
static void App_Storage_Set_Frequency_Defaults(App_Storage_Config *cfg)
{
    cfg->startup_low_freq_hz = 20000U;
    cfg->startup_high_freq_hz = 100000U;
    cfg->startup_freq_band = APP_STORAGE_FREQ_BAND_HIGH;
    cfg->menu_cursor_icon = 0U;
    cfg->reserved_v2 = 0U;
}

static uint8_t App_Storage_Is_Frequency_Config_Valid(
    const App_Storage_Config *cfg)
{
    if (cfg->startup_low_freq_hz < 20000U ||
        cfg->startup_low_freq_hz > 99900U ||
        (cfg->startup_low_freq_hz % 100U) != 0U) return 0U;
    if (cfg->startup_high_freq_hz < 100000U ||
        cfg->startup_high_freq_hz > 200000U ||
        (cfg->startup_high_freq_hz % 1000U) != 0U) return 0U;
    if (cfg->startup_freq_band > APP_STORAGE_FREQ_BAND_HIGH) return 0U;
    if (cfg->menu_cursor_icon >= 8U) return 0U;
    return 1U;
}
```

`App_Storage_Migrate_V1()` 必须逐字段复制SSID、密码、MQTT密钥、ADC校准、频率微调、语言、字符间距、配色与兼容字段，然后调用 `App_Storage_Set_Frequency_Defaults()`；不得使用整结构强制转换。

- [ ] **Step 5: 扩展设置加载与保存接口**

接口增加以下参数：

```c
void App_Storage_Load_Settings(uint8_t *lang, uint8_t *font, uint8_t *bl,
    uint8_t *spacing, uint8_t *preset, uint16_t *fg, uint16_t *bg,
    uint32_t *low_freq_hz, uint32_t *high_freq_hz,
    uint8_t *freq_band, uint8_t *cursor_icon);

void App_Storage_Request_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
    uint8_t spacing, uint8_t preset, uint16_t fg, uint16_t bg,
    uint32_t low_freq_hz, uint32_t high_freq_hz,
    uint8_t freq_band, uint8_t cursor_icon);
```

保存入口先执行与加载相同的范围和量化校验；无效输入不得覆盖当前有效配置。

- [ ] **Step 6: 运行契约检查和Keil编译**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
& 'D:\Keil5\UV4\UV4.exe' -b 'Keil_Project\Project.uvprojx' -j0
Select-String -Path Keil_Project/Objects/Project.build_log.htm -Pattern 'Error\(s\)|Warning\(s\)'
```

Expected: 配置契约PASS；ARMCC输出 `0 Error(s), 0 Warning(s)`。

- [ ] **Step 7: 提交配置迁移**

```powershell
git add Keil_Project/tests/verify_settings_frequency.ps1 Keil_Project/User/App_Storage.h Keil_Project/User/App_Storage.c
git commit -m "feat: 增加双档启动频率配置迁移"
```

---

### Task 2: 20–200kHz边界与动态分档扫频

**Files:**
- Modify: `Keil_Project/tests/verify_settings_frequency.ps1`
- Modify: `Keil_Project/Hardware/Pwm_Driver.h`
- Modify: `Keil_Project/Hardware/Inverter_Control.h`
- Modify: `Keil_Project/Hardware/Inverter_Control.c`
- Modify: `Keil_Project/User/Sys_Core.c`

**Interfaces:**
- Consumes: Task 1经过校验的配置字段；硬件层不得包含 `App_Storage.h`。
- Produces: `Inverter_Control_Configure_Startup()`。
- Produces: `Inverter_Control_Get_Sweep_Start_Freq()` 与 `Inverter_Control_Get_Sweep_Target_Freq()`。

- [ ] **Step 1: 增加频率控制失败检查**

向验证脚本追加：

```powershell
$pwmH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Pwm_Driver.h"
$invH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Inverter_Control.h"
Assert-Contains $pwmH 'PWM_DRIVER_FREQ_MIN_HZ\s+20000' 'PWM下限不是20kHz'
Assert-Contains $pwmH 'PWM_DRIVER_FREQ_MAX_HZ\s+200000' 'PWM上限不是200kHz'
Assert-Contains $invH 'Inverter_Control_Configure_Startup' '缺少启动档位配置接口'
Assert-Contains $invH 'Inverter_Control_Get_Sweep_Start_Freq' '缺少扫频起点接口'
Assert-Contains $invH 'Inverter_Control_Get_Sweep_Target_Freq' '缺少扫频终点接口'
```

Run脚本并确认FAIL于“PWM下限不是20kHz”。

- [ ] **Step 2: 修改PWM最终硬边界**

```c
#define PWM_DRIVER_FREQ_MIN_HZ   20000U
#define PWM_DRIVER_FREQ_MAX_HZ  200000U
```

保留偶数周期计数、UDIS临界更新、50%占空比和1000ns死区逻辑。

- [ ] **Step 3: 增加启动档位配置与查询接口**

在 `Inverter_Control.h` 声明：

```c
typedef enum {
    INVERTER_CONTROL_STARTUP_LOW = 0,
    INVERTER_CONTROL_STARTUP_HIGH = 1
} Inverter_Control_Startup_Band;

void Inverter_Control_Configure_Startup(Inverter_Control_Startup_Band band,
    uint32_t low_freq_hz, uint32_t high_freq_hz);
uint32_t Inverter_Control_Get_Sweep_Start_Freq(void);
uint32_t Inverter_Control_Get_Sweep_Target_Freq(void);
```

在 `.c` 中保存验证后的档位快照。低频档起点为99,900Hz、步进100Hz；高频档起点为200,000Hz、步进1,000Hz。非法配置回退到高频100,000Hz。该模块只使用自己的 `Inverter_Control_Startup_Band`，不得依赖User层的存储常量。

- [ ] **Step 4: 用动态起点、目标和步进替换固定扫频常量**

触发启动时锁定本轮参数：

```c
if (s_startup_band == INVERTER_CONTROL_STARTUP_LOW) {
    s_sweep_start_freq = 99900U;
    s_sweep_target_freq = s_startup_low_freq;
    s_sweep_step_hz = 100U;
} else {
    s_sweep_start_freq = 200000U;
    s_sweep_target_freq = s_startup_high_freq;
    s_sweep_step_hz = 1000U;
}
s_ss_current_freq = s_sweep_start_freq;
Pwm_Driver_Set_Frequency(s_ss_current_freq);
```

任务推进使用饱和减法，禁止无符号下溢：

```c
if (s_ss_current_freq <= s_sweep_target_freq + s_sweep_step_hz) {
    s_ss_current_freq = s_sweep_target_freq;
    Pwm_Driver_Set_Frequency(s_ss_current_freq);
    Inverter_Control_Set_State_Atomic(INVERTER_CONTROL_SS_STATE_DONE);
} else {
    s_ss_current_freq -= s_sweep_step_hz;
    Pwm_Driver_Set_Frequency(s_ss_current_freq);
}
```

- [ ] **Step 5: 在系统初始化时注入配置**

`Sys_Post_Init()` 成功加载或生成默认配置后调用：

```c
Inverter_Control_Configure_Startup(
    (s_sys_config.startup_freq_band == APP_STORAGE_FREQ_BAND_LOW) ?
        INVERTER_CONTROL_STARTUP_LOW : INVERTER_CONTROL_STARTUP_HIGH,
    s_sys_config.startup_low_freq_hz,
    s_sys_config.startup_high_freq_hz);
```

UI保存启动频率后也调用同一接口，但只更新下一次触发使用的配置；当前扫频快照不改变。

- [ ] **Step 6: 验证与提交**

运行契约脚本和Keil完整编译，预期全部PASS且0错误0警告。随后：

```powershell
git add Keil_Project/tests/verify_settings_frequency.ps1 Keil_Project/Hardware/Pwm_Driver.h Keil_Project/Hardware/Inverter_Control.h Keil_Project/Hardware/Inverter_Control.c Keil_Project/User/Sys_Core.c
git commit -m "feat: 支持20至200kHz双档软启动"
```

---

### Task 3: 五项设置菜单与全局光标图标

**Files:**
- Modify: `Keil_Project/tests/verify_settings_frequency.ps1`
- Modify: `Keil_Project/Hardware/Ui_Controller.h`
- Modify: `Keil_Project/Hardware/Ui_Controller.c`

**Interfaces:**
- Consumes: Task 1设置读写接口。
- Consumes: Task 2 `Inverter_Control_Configure_Startup()`。
- Produces: 五项设置菜单、启动频率总览/编辑状态和全局光标绘制。

- [ ] **Step 1: 增加菜单失败检查**

验证脚本追加：

```powershell
$uiC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Ui_Controller.c"
Assert-Contains $uiC 'UI_PAGE_SETTING_FREQUENCY' '缺少启动频率页面'
Assert-Contains $uiC 's_startup_low_freq_hz' 'UI缺少低频编辑副本'
Assert-Contains $uiC 's_startup_high_freq_hz' 'UI缺少高频编辑副本'
Assert-Contains $uiC 's_menu_cursor_icon' 'UI缺少全局光标状态'
Assert-Contains $uiC 's_cursor_icon_ids\[8\]' '候选光标不是固定八项'
```

运行并确认FAIL于缺少启动频率页面。

- [ ] **Step 2: 增加频率页面和五项菜单**

在页面枚举中增加 `UI_PAGE_SETTING_FREQUENCY`，设置菜单顺序固定为语言、启动频率、字符间距、光标图标、配色方案。所有页面索引和最大值使用枚举或宏，不写裸数字4。

- [ ] **Step 3: 实现总览与编辑副本**

增加状态：

```c
static uint32_t s_startup_low_freq_hz = 20000U;
static uint32_t s_startup_high_freq_hz = 100000U;
static uint8_t s_startup_freq_band = APP_STORAGE_FREQ_BAND_HIGH;
static uint8_t s_frequency_edit_band = APP_STORAGE_FREQ_BAND_LOW;
static uint32_t s_frequency_edit_value = 20000U;
static uint8_t s_frequency_editing = 0U;
```

进入编辑时复制保存值；返回键只丢弃副本；确定键通过扩展后的 `App_Storage_Request_Save_Settings()` 请求保存并更新 `Inverter_Control_Configure_Startup()`。

- [ ] **Step 4: 实现统一退出语义**

- 编辑状态KEY4保存并回总览。
- 编辑状态KEY1取消并回总览。
- 总览KEY1回设置菜单。
- 任一设置子页面KEY1双击直接回主菜单。
- 设置主菜单KEY1回主菜单。
- “已保存”提示由时间戳控制，禁止调用延时函数。

- [ ] **Step 5: 把图标浏览收敛为八项光标选择**

```c
static const uint8_t s_cursor_icon_ids[8] = {
    ICON_ID_STAR, ICON_ID_CHECK, ICON_ID_ROCKET_ANIM,
    ICON_ID_LIGHTNING, ICON_ID_HOME, ICON_ID_GEAR,
    ICON_ID_REFRESH, ICON_ID_ARROW_RT
};
```

所有候选固定绘制第0帧。确认保存候选索引而非原始图标编号；加载时索引大于7回退0。外部字库无效或图标绘制失败时使用片内星标。

- [ ] **Step 6: 统一全部菜单的前导光标**

新增私有函数：

```c
static void Ui_Controller_Draw_Menu_Cursor(uint8_t row, uint8_t selected)
{
    uint8_t icon_id;
    Tft_Driver_Erase_Pixel_Area(0U,
        (uint16_t)row * TFT_FONT_HEIGHT, 16U, TFT_FONT_HEIGHT);
    if (selected == 0U) return;
    icon_id = s_cursor_icon_ids[s_menu_cursor_icon];
    if (Tft_Driver_Draw_Icon_By_Id(0U,
        (uint16_t)row * TFT_FONT_HEIGHT, icon_id, 0U,
        Ui_Controller_Get_Value_Color(),
        Ui_Controller_Get_Background_Color()) == 0U) {
        Tft_Driver_Draw_Icon_By_Id(0U,
            (uint16_t)row * TFT_FONT_HEIGHT, ICON_ID_STAR, 0U,
            Ui_Controller_Get_Value_Color(),
            Ui_Controller_Get_Background_Color());
    }
}
```

主菜单、设置菜单和所有二级菜单文本统一从第2字符列开始，未选中行仍保留16像素前导区。

- [ ] **Step 7: 验证与提交**

运行契约脚本和Keil完整编译，检查0错误0警告；人工代码检查确认设置页面没有运行期 `Sys_Timer_Delay_Ms()`。随后提交本任务文件。

```powershell
git add Keil_Project/tests/verify_settings_frequency.ps1 Keil_Project/Hardware/Ui_Controller.h Keil_Project/Hardware/Ui_Controller.c
git commit -m "feat: 重构设置菜单与全局光标"
```

---

### Task 4: 共用递增刻度与2倍主数值

**Files:**
- Modify: `Keil_Project/tests/verify_settings_frequency.ps1`
- Modify: `Keil_Project/Hardware/Tft_Driver.h`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/Hardware/Ui_Controller.c`

**Interfaces:**
- Produces: `Tft_Driver_Show_Char_2X()` 和 `Tft_Driver_Show_String_2X()`。
- Produces: 私有 `Ui_Controller_Gauge_Value_To_Angle()` 分段映射。

- [ ] **Step 1: 增加表盘契约与数学连续性失败检查**

验证脚本追加：

```powershell
$tftH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Tft_Driver.h"
Assert-Contains $tftH 'Tft_Driver_Show_String_2X' '缺少2倍字符串接口'
Assert-Contains $uiC 'Ui_Controller_Gauge_Value_To_Angle' '缺少共用分段映射'

function Map-Segmented([double]$value, [double]$minimum,
    [double[]]$ends, [int[]]$cells) {
    $start = $minimum; $used = 0; $total = ($cells | Measure-Object -Sum).Sum
    for ($i = 0; $i -lt $ends.Count; $i++) {
        if ($value -le $ends[$i]) {
            return 180.0 * ($used + $cells[$i] * ($value - $start) /
                ($ends[$i] - $start)) / $total
        }
        $used += $cells[$i]; $start = $ends[$i]
    }
    return 180.0
}

$checks = @(
    @{ Name='电压20V'; Min=0.0; Ends=@(20.0,40.0,50.0); Cells=@(10,4,1); Edge=20.0 },
    @{ Name='电压40V'; Min=0.0; Ends=@(20.0,40.0,50.0); Cells=@(10,4,1); Edge=40.0 },
    @{ Name='电流1A'; Min=0.0; Ends=@(1.0,3.0,5.0); Cells=@(10,4,2); Edge=1.0 },
    @{ Name='电流3A'; Min=0.0; Ends=@(1.0,3.0,5.0); Cells=@(10,4,2); Edge=3.0 },
    @{ Name='低频50kHz'; Min=20.0; Ends=@(50.0,80.0,100.0); Cells=@(6,3,1); Edge=50.0 },
    @{ Name='低频80kHz'; Min=20.0; Ends=@(50.0,80.0,100.0); Cells=@(6,3,1); Edge=80.0 },
    @{ Name='高频140kHz'; Min=100.0; Ends=@(140.0,180.0,200.0); Cells=@(8,4,1); Edge=140.0 },
    @{ Name='高频180kHz'; Min=100.0; Ends=@(140.0,180.0,200.0); Cells=@(8,4,1); Edge=180.0 }
)
foreach ($check in $checks) {
    $left = Map-Segmented $check.Edge $check.Min $check.Ends $check.Cells
    $right = Map-Segmented ($check.Edge + 0.000001) $check.Min $check.Ends $check.Cells
    if ([Math]::Abs($left-$right) -gt 0.001) {
        throw "$($check.Name)分段不连续"
    }
}
```

首次运行应因缺少2倍字符串接口失败。

- [ ] **Step 2: 在TFT驱动增加2倍ASCII绘制**

复用现有8×16字模，每个源像素绘制2×2矩形；输出字符为16×32像素。接口：

```c
void Tft_Driver_Show_Char_2X(uint16_t x, uint16_t y, char ch,
    uint16_t fg, uint16_t bg);
void Tft_Driver_Show_String_2X(uint16_t x, uint16_t y, const char *text,
    uint16_t fg, uint16_t bg);
```

函数必须裁剪屏幕边界、拒绝空字符串指针，并在共享总线绘制阻塞后立即停止后续字符。

- [ ] **Step 3: 用配置表描述分段刻度**

```c
typedef struct {
    float end_value;
    uint8_t cells;
} GaugeSegment;

typedef struct {
    float range_min;
    float range_max;
    const GaugeSegment *segments;
    uint8_t segment_count;
    float warn_start;
    float red_start;
    char label;
} GaugeConfig;
```

配置必须精确对应设计：电压 `(20,10格)(40,4格)(50,1格)`；电流 `(1,10格)(3,4格)(5,2格)`；低频从20kHz起 `(50,6格)(80,3格)(100,1格)`；高频从100kHz起 `(140,8格)(180,4格)(200,1格)`。

- [ ] **Step 4: 实现连续的值到角度映射**

`Ui_Controller_Gauge_Value_To_Angle()` 先钳位，再累计已完成格数，并在当前分段内插值。返回0–180的 `uint16_t`。完整绘制和差分绘制必须调用同一函数，禁止保留旧的全范围线性公式。

- [ ] **Step 5: 重排极简信息舱**

- 外围只绘制最小值、各分段端点和最大值标签。
- 中央数值使用2倍ASCII，单位用普通8×16字模。
- 底部状态使用中文短词。
- PWM停止时频率显示0和“待机”。
- 扫频显示“扫频”，完成显示“运行”。
- 超过量程时圆弧保持180度，数值保持真实值，状态显示“超量程”。
- 故障状态优先显示 `Sys_Core_Get_Fault()` 对应原因。

- [ ] **Step 6: 根据本轮启动档位锁定频率表盘配置**

启动扫频时缓存低频或高频表盘指针；直到停止回到空闲才允许跟随新的设置档位。切换页面、配色、档位或绘制故障恢复时清除表盘缓存并执行一次完整重绘。

- [ ] **Step 7: 验证与提交**

运行PowerShell脚本，确认所有分段连续性断言PASS；运行Keil完整编译确认0错误0警告；检查 `git diff --check`。随后提交TFT、UI和验证脚本文件。

```powershell
git add Keil_Project/tests/verify_settings_frequency.ps1 Keil_Project/Hardware/Tft_Driver.h Keil_Project/Hardware/Tft_Driver.c Keil_Project/Hardware/Ui_Controller.c
git commit -m "feat: 增加递增式独立表盘"
```

---

### Task 5: 全链路验证、版本同步与交付检查

**Files:**
- Modify: 所有本轮改动的STM32 `.c/.h` 文件中的版本说明（`.h` 不增加文件头）
- Modify: `AGENTS.md`
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-07-22-settings-frequency-redesign.md`
- Modify: `docs/superpowers/plans/2026-07-22-settings-frequency-gauge-implementation.md`

**Interfaces:**
- Consumes: Tasks 1–4全部功能。
- Produces: 可编译、可追溯的V5.1.0交付状态。

- [x] **Step 1: 执行全部自动检查**

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
rg -n "//" Keil_Project/Hardware Keil_Project/System Keil_Project/User -g '*.c' -g '*.h'
rg -n "TIM1->ARR|TIM_Cmd\(TIM1|TIM_CtrlPWMOutputs\(TIM1" Keil_Project/Hardware/Ui_Controller.c Keil_Project/User/App_Network.c
git diff --check
```

Expected: 契约脚本PASS；两个 `rg` 命令均无输出；`git diff --check`无错误。

- [x] **Step 2: 执行最终ARMCC全量编译**

```powershell
& 'D:\Keil5\UV4\UV4.exe' -b 'D:\Claude Code Project\WPT_PWM_V5.0\Keil_Project\Project.uvprojx' -j0
Select-String -Path Keil_Project/Objects/Project.build_log.htm -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
```

Expected: `0 Error(s), 0 Warning(s)`，并记录程序尺寸。

- [ ] **Step 3: 执行设置交互人工检查清单**（待实机）

- 语言、启动频率、字符间距、光标图标、配色五项顺序正确。
- 两档值分别编辑、确认、取消和持久化。
- KEY1单击逐级返回，双击从子页面回主菜单。
- 八个光标在所有菜单生效；Flash无效时回退星标。
- 保存提示非阻塞，按键和安全任务继续运行。

- [ ] **Step 4: 执行示波器与功率级检查清单**（待实机）

- 在限流低压条件下依次验证20.0、99.9、100和200kHz。
- 检查CH1/CH1N、CH2/CH2N互补关系、50%占空比和约1μs死区。
- 验证低频档99.9kHz向目标下降、高频档200kHz向目标下降。
- 在200kHz持续运行期间监测栅极驱动器和MOSFET温升。
- 人工制造ADC超时与过流条件，确认PWM和PB10安全关断。

- [x] **Step 5: 同步版本与文档**

本功能新增设置页面、双档启动模型和全表盘重构，按项目规则升级为V5.1.0。更新STM32 `.c` 文件说明、AGENTS版本/审查历史、README版本历史以及本设计与计划的完成状态；不得给 `.h` 恢复文件头注释。

- [x] **Step 6: 最终独立代码审查**

调用STM32/C代码审查员检查：配置迁移、C89兼容、频率边界、无符号扫频下溢、故障关断、SPI绘制失败恢复和设置取消语义。修复所有CRITICAL/HIGH问题后重新执行Steps 1–4。

- [x] **Step 7: 提交最终集成**

```powershell
git add Keil_Project AGENTS.md README.md docs/superpowers/specs/2026-07-22-settings-frequency-redesign.md docs/superpowers/plans/2026-07-22-settings-frequency-gauge-implementation.md
git commit -m "feat: V5.1.0 重构设置频率与递增式表盘"
```

提交前不得包含 `.obj`、`.lst`、`.axf`、`.hex`、`.map` 或其他Keil编译产物；若准备推送，必须先按仓库规则运行 `Keil_Project/keilkill.bat`。
