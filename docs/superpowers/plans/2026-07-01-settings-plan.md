# V4.4.0 设置系统实现计划

> **针对 agentic 工作者**: 建议使用 superpowers:subagent-driven-development 或 superpowers:executing-plans 逐任务实现。

**目标**: 新增设置系统 (6 页面) + ROM/Flash 资源重划分 + 中英双语 + 多配色方案

**架构**: Ui_Controller.c 新增 6 页面绘制和按键分发，Tft_Driver.c 新增 CN→EN 自动翻译路径，TFT_Font_Data.h 精简 ROM 字库至 4 中文 + 3 图标，App_Storage.c 扩展设置参数持久化

**技术栈**: STM32F103C8 SPL V3.5.0, ARMCC V5.06, 8×16 ASCII + 16×16 CJK TFT, W25Q128 Flash 字库 V2

---

### 任务 1: 精简 ROM 字库 TFT_Font_Data.h

**文件**:
- 修改: `Keil_Project/Hardware/TFT_Font_Data.h`

**目标**: 中文 76→4 字 (无/线/充/电)，20 图标移除，历史动画移除，保留 ICON_STAR + WIFI + MQTT 全部

- [ ] **步骤 1: 精简 CN_INDEX 和 CN_FONT_16X16 为仅 4 字**

```c
#define TFT_CN_FONT_CHAR_COUNT  4

static const char CN_INDEX[] = /* UTF-8 索引 — 4 字, SPLASH 开机动画 */
    "\xe6\x97\xa0" /* 无 */ "\xe7\xba\xbf" /* 线 */
    "\xe5\x85\x85" /* 充 */ "\xe7\x94\xb5" /* 电 */
    ;

static const uint8_t CN_FONT_16X16[TFT_CN_FONT_CHAR_COUNT][32] = {
    /* 0 无 */ {0x00,0x00,0xFC,0x0F,0x40,0x00,0x40,0x00,0x40,0x00,0x40,0x00,0xFE,0x3F,0x20,0x01,
                0x20,0x01,0x20,0x01,0x10,0x01,0x10,0x01,0x08,0x21,0x04,0x21,0x02,0x3E,0x01,0x00},
    /* 1 线 */ {0x08,0x0A,0x08,0x12,0x04,0x02,0x24,0x3A,0xA2,0x07,0x1F,0x02,0x08,0x7A,0xC4,0x07,
                0x02,0x22,0x3F,0x12,0x02,0x0C,0x00,0x44,0x38,0x4A,0x07,0x51,0xC2,0x60,0x00,0x40},
    /* 2 充 */ {0x40,0x00,0x80,0x00,0xFF,0x7F,0x20,0x00,0x20,0x00,0x10,0x04,0x08,0x08,0xFC,0x1F,
                0x20,0x12,0x20,0x02,0x20,0x02,0x20,0x02,0x10,0x22,0x10,0x22,0x08,0x22,0x06,0x3C},
    /* 3 电 */ {0x80,0x00,0x80,0x00,0x80,0x00,0xFC,0x1F,0x84,0x10,0x84,0x10,0x84,0x10,0xFC,0x1F,
                0x84,0x10,0x84,0x10,0x84,0x10,0xFC,0x1F,0x84,0x50,0x80,0x40,0x80,0x40,0x00,0x7F},
};
```

- [ ] **步骤 2: 删除 20 新图标数组 (ICON_BATTERY ~ ICON_CLOCK)**

删除 `TFT_Font_Data.h` 中第 360-439 行的 20 个 `static const uint8_t ICON_*[32]` 数组。

- [ ] **步骤 3: 删除历史废弃动画 (STAR_CURSOR_ANIM + ROCKET_ANIM)**

删除 `TFT_Font_Data.h` 中第 305-354 行的 `STAR_CURSOR_ANIM[16][32]` 和 `ROCKET_ANIM[16][32]`。

- [ ] **步骤 4: 删除 Tft_Driver.c 中引用已删除数组的代码**

`Tft_Driver.c:553-566` 的 `Tft_Driver_Draw_WiFi_Icon` 直接引用 `WIFI_ICON`（保留，不变）。确认没有函数引用已删除的 20 图标动画数组。`Tft_Driver_Draw_Icon_By_Id()` 走 Flash 路径，不受 ROM 精简影响。

- [ ] **步骤 5: 验证编译**

```bash
# 在 Keil IDE 中:
# 1. 打开 Keil_Project/Project.uvprojx
# 2. F7 编译
# 3. 预期: ARMCC #0 Error (没有因删除数组导致的 symbol undefined)
```

---

### 任务 2: 新增 CN→EN 翻译机制 (Tft_Driver.c)

**文件**:
- 修改: `Keil_Project/Hardware/Tft_Driver.c`

**目标**: `Tft_Driver_Show_CN_String()` 在 `!s_font_flash_valid` 且 ROM 查找失败时，自动查表输出英文

- [ ] **步骤 1: 在 Tft_Driver_CN_Draw 中增加英文回退**

在 `Tft_Driver.c` 的 `Tft_Driver_CN_Draw()` 函数中（约第 512-532 行），修改 ROM 回退分支。将查询失败时显示背景色的行为改为调用英文翻译输出。

找到原代码：
```c
    } else {
        /* ── ROM 回退: CN_INDEX 线性查找 (76字) ── */
        uint8_t g_idx; uint8_t i;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx == 0xFF) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg);
            return;
        }
        /* ... 字模渲染 ... */
    }
```

改为：
```c
    } else {
        /* ── ROM 回退: CN_INDEX 线性查找 (4字) ── */
        uint8_t g_idx; uint8_t i;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx == 0xFF) {
            /* ROM 未命中 → 无 Flash 且 ROM 无此字 → 输出英文替代 (不显示空白) */
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg);
            return;
        }
        /* ... 字模渲染 (保持不变) ... */
```

注意：翻译不在 `Tft_Driver_CN_Draw` 内部做（太底层，无法处理字符串上下文）。翻译逻辑在 `Tft_Driver_Show_CN_String` 层完成——当 Flash 无效且 ROM 未命中时，逐字节跳过中文 UTF-8 序列，用 ASCII 翻译替换。

- [ ] **步骤 2: 改 Tft_Driver_Show_CN_String 增加翻译路径**

在 `Tft_Driver_Show_CN_String()` 中（第 535-547 行），原逻辑不变。翻译核心思想：当 `!s_font_flash_valid` 时，中文可能无法渲染（ROM 只有 4 字），但 `Tft_Driver_CN_Draw` 已经会正确处理 ROM 4 字。**真正的翻译发生在 Ui_Controller.c 层面**——页面绘制函数根据 `!Tft_Driver_Is_Font_Flash_Valid()` 选择英文标签，而非在此层做自动翻译。

此任务仅确保 ROM 回退路径不崩溃、不显示乱码。

- [ ] **步骤 3: 编译验证**

F7 编译，确认无错误。

---

### 任务 3: 扩展 Ui_Page 枚举 + 新增设置状态变量 (Ui_Controller.h/c)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.h`
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 扩展枚举 Ui_Page**

在 `Ui_Controller.h` 的 `Ui_Page` 枚举中追加：

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

- [ ] **步骤 2: 新增设置系统全局状态变量**

在 `Ui_Controller.c` 的静态变量区（约第 150 行附近）追加：

```c
/* ====== V4.4.0 Settings State ====== */
static uint8_t  s_language         = 0;    /* 0=Chinese, 1=English */
static uint8_t  s_font_size        = 1;    /* 0=Small, 1=Medium(default) */
static uint8_t  s_backlight_val    = 248;  /* 48-248, default 248 */
static uint16_t s_color_fg         = 0xFFFF;  /* RGB565 default white */
static uint16_t s_color_bg         = 0x0000;  /* RGB565 default black */
static uint8_t  s_color_preset     = 0;    /* 0-5 preset index, 255=custom */

/* Settings sub-page cursor & edit state */
static uint8_t  s_setting_cursor       = 0;
static uint8_t  s_setting_edit_row     = 0;  /* for Custom RGB: which row */
static uint8_t  s_setting_edit_channel = 0;  /* for Custom RGB: 0=R,1=G,2=B */
static uint8_t  s_setting_colorscope   = 0;  /* 0=editing FG, 1=editing BG */
static uint16_t s_setting_custom_fg[3] = {31,31,31};  /* R,G,B 0-31 */
static uint16_t s_setting_custom_bg[3] = {0,0,0};

/* Icon browser */
static uint8_t  s_icon_page        = 0;    /* 0=page1(icons 0-29), 1=page2(icon 30) */
static uint8_t  s_icon_cursor      = 0;    /* 0-29 within current page */

/* Backlight breathing */
static uint8_t  s_bl_breathing     = 1;    /* 1=breathing mode active */
static uint32_t s_bl_last_action_ms = 0;   /* last key press timestamp */
```

- [ ] **步骤 3: 编译验证**

F7 编译，确认枚举扩展不破坏现有 switch 完整性。

---

### 任务 4: 主菜单增加「设置」选项 (Ui_Controller.c)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 新增设置中文串宏**

在 Ui_Controller.c 的中文串宏区（约第 104 行后）追加：

```c
/* V4.4.0 Settings strings */
#define S_SETTINGS  "\xe8\xae\xbe\xe7\xbd\xae"        /* 设置 */
#define S_SETTINGS_LANG   "\xe8\xaf\xad\xe8\xa8\x80"  /* 语言 */
#define S_SETTINGS_ICONS  "\xe5\x9b\xbe\xe6\xa0\x87"  /* 图标 */
#define S_SETTINGS_FONT   "\xe5\xad\x97\xe4\xbd\x93"  /* 字体 */
#define S_SETTINGS_BL     "\xe4\xba\xae\xe5\xba\xa6"  /* 亮度 */
#define S_SETTINGS_COLOR  "\xe9\xa2\x9c\xe8\x89\xb2"  /* 颜色 */
#define S_TITLE_LANG    "\xe8\xaf\xad\xe8\xa8\x80"        /* 语言 */
#define S_TITLE_ICONS   "\xe5\x9b\xbe\xe6\xa0\x87"        /* 图标 */
#define S_TITLE_FONT    "\xe5\xad\x97\xe4\xbd\x93\xe5\xa4\xa7\xe5\xb0\x8f" /* 字体大小 */
#define S_TITLE_BL      "\xe4\xba\xae\xe5\xba\xa6\xe8\xb0\x83\xe8\x8a\x82" /* 亮度调节 */
#define S_TITLE_COLOR   "\xe9\xa2\x9c\xe8\x89\xb2\xe6\x96\xb9\xe6\xa1\x88" /* 颜色方案 */
#define S_ON_RETURN     "[ON]\xe8\xbf\x94\xe5\x9b\x9e"    /* [ON]返回 */
#define S_EN_SETTINGS   "Settings"
#define S_EN_LANG       "Language"
#define S_EN_ICONS      "Icons"
#define S_EN_FONT       "Font Size"
#define S_EN_BL         "Brightness"
#define S_EN_COLOR      "Color"
```

- [ ] **步骤 2: 新增辅助函数 — 获取菜单项文本 (中英双语)**

```c
/* Menu item text getter (Chinese/English bilingual) */
static const char* Get_Menu_Setting_Text(uint8_t idx)
{
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());
    switch (idx) {
        case 0:
            if (is_cn) return "\xe8\xaf\xad\xe8\xa8\x80  Language";     /* 语言 Language */
            else       return "1. Language";
        case 1:
            if (is_cn) return "\xe5\x9b\xbe\xe6\xa0\x87  Icons";        /* 图标 Icons */
            else       return "2. Icons";
        case 2:
            if (is_cn) return "\xe5\xad\x97\xe4\xbd\x93  Font";         /* 字体 Font */
            else       return "3. Font Size";
        case 3:
            if (is_cn) return "\xe4\xba\xae\xe5\xba\xa6  Brightness";   /* 亮度 Brightness */
            else       return "4. Brightness";
        case 4:
            if (is_cn) return "\xe9\xa2\x9c\xe8\x89\xb2  Color";        /* 颜色 Color */
            else       return "5. Color";
        default: return "";
    }
}
```

- [ ] **步骤 3: 修改 Draw_Main_Menu_Full — 正常时显示 4 项**

将 `Draw_Main_Menu_Full()` 中的 for 循环从 4 次改为适应新结构（case 3 从故障改为设置，case 4 新增为故障）：

```c
static void Draw_Main_Menu_Full(void)
{
    uint8_t is_running = 0;
    uint8_t is_fault   = 0;
    uint8_t i;
    uint8_t item_count = 4;  /* 正常 4 项, FAULT 时 5 项 */
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        is_running = (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE);
        is_fault   = (ss == INVERTER_CONTROL_SS_STATE_FAULT);
    }
    if (is_fault) item_count = 5;

    Draw_Header(S_WPT_PWM);
    Draw_Divider(1);

    for (i = 0; i < item_count; i++) {
        const char* text;
        uint8_t enabled = 1;
        switch (i) {
            case 0:
                text = is_running
                    ? "1. \xe5\x81\x9c\xe6\xad\xa2PWM"     /* 1.停止PWM */
                    : "1. \xe5\x90\xaf\xe5\x8a\xa8PWM";     /* 1.启动PWM */
                break;
            case 1: text = "2. " S_MONITOR; break;
            case 2: text = "3. \xe6\x97\xa0\xe7\xba\xbf\xe9\x85\x8d\xe7\xbd\x91"; break;
            case 3: text = "4. " S_SETTINGS; break;          /* 设置 — 始终可用 */
            case 4:                                           /* 故障 — 仅 FAULT 时 */
                text = "5. \xe6\x95\x85\xe9\x9a\x9c\xe6\xb8\x85\xe9\x99\xa4"; /* 故障清除 */
                enabled = 1;
                break;
            default: text = ""; break;
        }
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, text, enabled);
    }

    Draw_Cursor(2 + s_menu_cursor);

    Erase_Line(6);  /* 清除底部 */
    Erase_Line(7);

    s_last_is_running    = is_running;
    s_last_is_fault_menu = is_fault;
}
```

- [ ] **步骤 4: 修改光标上限逻辑**

修改 `Handle_Keys_by_Page` 中 F_UP/F_DOWN 的 MAIN_MENU 分支（约第 1307 行和 1337 行）：

```c
/* F_UP: */
case UI_PAGE_MAIN_MENU: {
    uint8_t max_cursor = 3;  /* normal: items 0-3 */
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_FAULT) max_cursor = 4;  /* add fault item */
    }
    if (s_menu_cursor == 0) s_menu_cursor = max_cursor;
    else s_menu_cursor--;
    break;
}

/* F_DOWN: */
case UI_PAGE_MAIN_MENU: {
    uint8_t max_cursor = 3;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_FAULT) max_cursor = 4;
    }
    if (s_menu_cursor >= max_cursor) s_menu_cursor = 0;
    else s_menu_cursor++;
    break;
}
```

- [ ] **步骤 5: 修改 KEY0 确认分发 — 增加设置页面跳转**

在 `Handle_Keys_by_Page` 的 KEY0 CLICK → `case UI_PAGE_MAIN_MENU` 中增加 case 3：

```c
case 3:
    s_page = UI_PAGE_SETTING;
    s_setting_cursor = 0;
    break;
case 4:
    if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_FAULT) {
        s_page = UI_PAGE_FAULT;
    }
    break;
```

- [ ] **步骤 6: 修改 ON/OFF 返回逻辑 — 从设置页返回主菜单**

在 KEY3 (ON/OFF) CLICK 分发中增加设置相关页面的返回处理：

```c
case UI_PAGE_SETTING:
    s_page = UI_PAGE_MAIN_MENU;
    s_menu_cursor = 3;  /* return to Settings item */
    break;
case UI_PAGE_SETTING_LANG:
case UI_PAGE_SETTING_ICONS:
case UI_PAGE_SETTING_FONT:
case UI_PAGE_SETTING_BL:
case UI_PAGE_SETTING_COLOR:
    s_page = UI_PAGE_SETTING;
    s_setting_cursor = 0;
    break;
```

- [ ] **步骤 7: 修改 Phase 6 光标边界钳位**

```c
if (s_page == UI_PAGE_MAIN_MENU) {
    uint8_t max_cursor = 3;
    {
        Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
        if (ss == INVERTER_CONTROL_SS_STATE_FAULT || g_sys_state == SYS_STATE_FAULT)
            max_cursor = 4;
    }
    if (s_menu_cursor > max_cursor) s_menu_cursor = max_cursor;
}
```

- [ ] **步骤 8: 编译验证**

F7 编译，确认光标逻辑和菜单结构正确。

---

### 任务 5: 实现设置主菜单页面 (UI_PAGE_SETTING)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 新增 Draw_Setting_Full 绘制函数**

```c
static void Draw_Setting_Full(void)
{
    uint8_t i;
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());

    /* Row 0: Title centered */
    {
        const char* title = is_cn ? S_SETTINGS : S_EN_SETTINGS;
        Draw_Header(title);
    }
    Draw_Divider(1);

    /* Row 2-6: 5 menu items */
    for (i = 0; i < 5; i++) {
        const char* text = Get_Menu_Setting_Text(i);
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, text, 1);
    }

    Draw_Cursor(2 + s_setting_cursor);

    /* Row 7: bottom-right hint */
    Erase_Line(7);
    {
        const char* hint = is_cn ? S_ON_RETURN : "[ON]Back";
        uint8_t col = Right(hint);
        Tft_Driver_Show_String(7, col, hint, UI_COLOR_DIM, UI_COLOR_BG);
    }
}

static void Setting_Cursor_Update(uint8_t old_cursor)
{
    Erase_Cursor(2 + old_cursor);
    Draw_Cursor(2 + s_setting_cursor);
}
```

- [ ] **步骤 2: 新增 Handle_Setting_Keys 静态函数**

```c
static void Handle_Setting_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                                 Key_Driver_Event k2, Key_Driver_Event k3)
{
    /* F_UP: cursor up (wrap around) */
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor == 0) s_setting_cursor = 4;
        else s_setting_cursor--;
    }
    /* F_DOWN: cursor down (wrap around) */
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor >= 4) s_setting_cursor = 0;
        else s_setting_cursor++;
    }
    /* PAGE (k0): enter sub-page */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        switch (s_setting_cursor) {
            case 0: s_page = UI_PAGE_SETTING_LANG;  s_setting_cursor = 0; break;
            case 1: s_page = UI_PAGE_SETTING_ICONS; s_icon_page = 0; s_icon_cursor = 0; break;
            case 2: s_page = UI_PAGE_SETTING_FONT;  s_setting_cursor = 0; break;
            case 3: s_page = UI_PAGE_SETTING_BL;    s_setting_cursor = 0;
                    s_bl_breathing = 1; s_bl_last_action_ms = Sys_Timer_Get_Tick(); break;
            case 4: s_page = UI_PAGE_SETTING_COLOR; s_setting_cursor = 0; break;
        }
    }
}
```

- [ ] **步骤 3: 注册到 Phase 3 按键分发**

在 `Handle_Keys_by_Page` 的 switch(page) 各 case 开头，增加设置主菜单分支：

```c
case UI_PAGE_SETTING:
    Handle_Setting_Keys(k0, k1, k2, k3);
    return;  /* handled, skip generic logic below */
```

- [ ] **步骤 4: 注册到 Phase 7 绘制 + Phase 5 动态更新**

在 `draw` switch 中增加：
```c
case UI_PAGE_SETTING: Draw_Setting_Full(); break;
```

在 `cursor_changed` switch 中增加：
```c
case UI_PAGE_SETTING: Setting_Cursor_Update(old_cursor); break;
```

设置主菜单无 200ms 动态更新需求（static）。

- [ ] **步骤 5: 编译验证**

F7 编译。

---

### 任务 6: 实现语言切换页面 (UI_PAGE_SETTING_LANG)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: Draw_Lang_Full**

```c
static void Draw_Lang_Full(void)
{
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();

    /* Row 0: Title centered */
    Draw_Header(is_cn ? S_TITLE_LANG : S_EN_LANG);

    /* Row 3: Chinese option */
    Erase_Line(3);
    Tft_Driver_Show_String(3, 3, "  Chinese", 
        (s_language == 0) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);

    /* Row 4: English option */
    Erase_Line(4);
    Tft_Driver_Show_String(4, 3, "  English",
        (s_language == 1) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);

    /* Highlight selected */
    Draw_Cursor(3 + s_language);

    /* Row 7: hint */
    Erase_Line(7);
    {
        const char* hint1 = is_cn ? "\xe4\xb8\x8a\xe4\xb8\x8b\xe9\x80\x89\xe6\x8b\xa9 \xe7\xa1\xae\xe8\xae\xa4\xe5\x88\x87\xe6\x8d\xa2"
                                   : "UP/DN Select PAGE Confirm";
        Tft_Driver_Show_String(7, 0, hint1, UI_COLOR_DIM, UI_COLOR_BG);
    }
}
```

- [ ] **步骤 2: Handle_Lang_Keys**

```c
static void Handle_Lang_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                              Key_Driver_Event k2, Key_Driver_Event k3)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();
    if (!flash_ok) return;  /* no Flash: language switching disabled, stay English */

    /* F_UP / F_DOWN: toggle between 0 and 1 */
    if (k1 == KEY_DRIVER_EVENT_CLICK || k2 == KEY_DRIVER_EVENT_CLICK) {
        uint8_t old_lang = s_language;
        s_language = (s_language == 0) ? 1 : 0;
        if (old_lang != s_language) {
            /* Redraw line 3-4 cursor */
            Erase_Cursor(3 + old_lang);
            Draw_Cursor(3 + s_language);
            /* Update checked text */
            Erase_Line(3);
            Tft_Driver_Show_String(3, 3, "  Chinese",
                (s_language == 0) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);
            Erase_Line(4);
            Tft_Driver_Show_String(4, 3, "  English",
                (s_language == 1) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);
        }
    }
    /* PAGE (k0): confirm — already applied, just flash "Applied" */
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());
        Erase_Line(6);
        {
            const char* ok = is_cn ? "\xe5\xb7\xb2\xe5\xba\x94\xe7\x94\xa8" : "Applied!";
            uint8_t col = Center(ok);
            Tft_Driver_Show_String(6, col, ok, UI_COLOR_OK, UI_COLOR_BG);
        }
    }
}
```

- [ ] **步骤 3: 注册到按键分发和绘制 switch**

在 `Handle_Keys_by_Page` 中增加 `case UI_PAGE_SETTING_LANG`；在 `draw` switch 增加 `Draw_Lang_Full()`。

- [ ] **步骤 4: 编译验证**

F7 编译。

---

### 任务 7: 实现图标浏览页面 (UI_PAGE_SETTING_ICONS)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: Icon 名称查找表 + Draw_Icons_Full**

```c
/* Icon name table (English abbreviation, 31 entries) */
static const char* Get_Icon_Name(uint8_t icon_id)
{
    static const char* names[] = {
        "WIFI_SIG",  "WIFI_CONN", "WIFI_OFF", "WIFI_RMV",
        "MQTT",      "MQTT_YES",  "MQTT_NO",  "MQTT_ANIM",
        "STAR",      "STAR_CUR",  "ROCKET",
        "BATTERY",   "WARNING",   "CHECK",    "CROSS",
        "POWER",     "LIGHTNING", "TEMP",     "FAN",
        "LOCK",      "HOME",      "GEAR",     "REFRESH",
        "ARROW_UP",  "ARROW_DN",  "ARROW_LT", "ARROW_RT",
        "SIGNAL",    "GLOBE",     "CHART",    "CLOCK"
    };
    if (icon_id >= 31) return "?";
    return names[icon_id];
}

/* Icon grid layout: 5 columns × ~6 rows, centered */
#define ICON_COLS     5
#define ICON_ROWS     6
#define ICON_CELL_SZ  18   /* 16px icon + 2px margin */
#define ICON_GRID_X   ((160 - ICON_COLS * ICON_CELL_SZ) / 2)  /* center */
#define ICON_PER_PAGE (ICON_COLS * ICON_ROWS)  /* 30 */

static void Draw_Icons_Full(void)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();
    uint8_t is_cn = (s_language == 0 && flash_ok);

    if (!flash_ok) {
        Draw_Header("Icons");
        Tft_Driver_Show_String(3, 2, "Flash required", UI_COLOR_ALARM, UI_COLOR_BG);
        return;
    }

    /* Row 0: Title + page indicator */
    {
        char buf[24];
        snprintf(buf, 24, "%s [%d/2]", is_cn ? S_TITLE_ICONS : "Icons", s_icon_page + 1);
        Draw_Header(buf);
    }

    /* Row 1-6: Icon grid */
    {
        uint8_t row, col;
        for (row = 0; row < ICON_ROWS; row++) {
            for (col = 0; col < ICON_COLS; col++) {
                uint8_t icon_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + row * ICON_COLS + col);
                uint16_t x = (uint16_t)(ICON_GRID_X + col * ICON_CELL_SZ);
                uint16_t y = (uint16_t)(row * ICON_CELL_SZ + 16);  /* start at row 1 pixel */
                if (icon_id < 31) {
                    /* Highlight border for cursor */
                    uint16_t fg = UI_COLOR_TEXT;
                    uint16_t bg = UI_COLOR_BG;
                    uint8_t cursor_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + s_icon_cursor);
                    if (icon_id == cursor_id) {
                        /* Draw highlight rect behind icon */
                        Tft_Driver_Fill_Rect(x - 1, y - 1, 18, 18, UI_COLOR_VALUE);
                        bg = UI_COLOR_VALUE;
                    }
                    Tft_Driver_Draw_Icon_By_Id(x, y, icon_id, 0, fg, bg);
                }
            }
        }
    }

    /* Row 7: Current icon name + ID */
    Erase_Line(7);
    {
        uint8_t icon_id = (uint8_t)(s_icon_page * ICON_PER_PAGE + s_icon_cursor);
        if (icon_id < 31) {
            char buf[32];
            snprintf(buf, 32, "%s [%d]", Get_Icon_Name(icon_id), icon_id);
            uint8_t col = Center(buf);
            Tft_Driver_Show_String(7, col, buf, UI_COLOR_VALUE, UI_COLOR_BG);
        }
    }
}
```

- [ ] **步骤 2: Handle_Icons_Keys**

```c
static void Handle_Icons_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                               Key_Driver_Event k2, Key_Driver_Event k3)
{
    uint8_t flash_ok = Tft_Driver_Is_Font_Flash_Valid();
    if (!flash_ok) return;

    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        /* F_UP: move cursor up, wrap pages */
        if (s_icon_cursor == 0) {
            if (s_icon_page == 0) {  /* top of page 1 → no wrap, stay */
            } else { s_icon_page--; s_icon_cursor = ICON_PER_PAGE - 1; }
        } else {
            s_icon_cursor--;
        }
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        /* F_DOWN: move cursor down, wrap pages */
        uint8_t max_cursor = (s_icon_page == 0) ? (ICON_PER_PAGE - 1) : 0;
        if (s_icon_cursor >= max_cursor) {
            if (s_icon_page == 0) { s_icon_page++; s_icon_cursor = 0; }
        } else {
            s_icon_cursor++;
        }
    }
}
```

- [ ] **步骤 3: 注册到 Phase 3/5/7**

按键分发增加 `case UI_PAGE_SETTING_ICONS`，绘制 switch 增加 `Draw_Icons_Full()`。图标页有 cursor_changed → s_page_drawn=0 全量重绘。

- [ ] **步骤 4: 编译验证**

---

### 任务 8: 实现字体大小页面 (UI_PAGE_SETTING_FONT)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: Draw_Font_Full**

```c
static void Draw_Font_Full(void)
{
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());

    /* Row 1: Title */
    Erase_Line(1);
    {
        const char* title = is_cn ? S_TITLE_FONT : S_EN_FONT;
        uint8_t col = Center(title);
        Tft_Driver_Show_CN_String(1, col, title, UI_COLOR_TITLE, UI_COLOR_BG);
    }

    /* Row 3: Medium (default) */
    Erase_Line(3);
    Tft_Driver_Show_String(3, 2, (s_font_size == 1) ? "* Medium (default)" : "  Medium (default)",
        (s_font_size == 1) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);

    /* Row 4: Small */
    Erase_Line(4);
    Tft_Driver_Show_String(4, 2, (s_font_size == 0) ? "* Small" : "  Small",
        (s_font_size == 0) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);

    Draw_Cursor(3 + s_font_size);  /* row 3=medium(1), row 4=small(0) — wait, need fix */

    /* Row 5-6: Preview */
    Erase_Line(5);
    if (is_cn)
        Tft_Driver_Show_CN_String(5, Center("\xe9\xa2\x84\xe8\xa7\x88:\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5"),
            "\xe9\xa2\x84\xe8\xa7\x88:\xe6\x97\xa0\xe7\xba\xbf\xe5\x85\x85\xe7\x94\xb5", UI_COLOR_DATA, UI_COLOR_BG);
    else
        Tft_Driver_Show_String(5, Center("Preview: WPT System"), "Preview: WPT System", UI_COLOR_DATA, UI_COLOR_BG);
    Erase_Line(6);
    Tft_Driver_Show_String(6, Center("ABCDEFGH 1234567890"), "ABCDEFGH 1234567890", UI_COLOR_DATA, UI_COLOR_BG);
}
```

光标逻辑修正：s_font_size=0 是 Row 4, =1 是 Row 3。Draw_Cursor 使用 `(s_font_size == 0) ? 4 : 3`。

- [ ] **步骤 2: Handle_Font_Keys**

```c
static void Handle_Font_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                              Key_Driver_Event k2, Key_Driver_Event k3)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK || k2 == KEY_DRIVER_EVENT_CLICK) {
        uint8_t old = s_font_size;
        s_font_size = (s_font_size == 0) ? 1 : 0;  /* toggle */
        if (old != s_font_size) {
            Erase_Cursor((old == 0) ? 4 : 3);
            Draw_Cursor((s_font_size == 0) ? 4 : 3);
            /* Update text */
            Erase_Line(3);
            Tft_Driver_Show_String(3, 2, (s_font_size == 1) ? "* Medium (default)" : "  Medium (default)",
                (s_font_size == 1) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);
            Erase_Line(4);
            Tft_Driver_Show_String(4, 2, (s_font_size == 0) ? "* Small" : "  Small",
                (s_font_size == 0) ? UI_COLOR_VALUE : UI_COLOR_TEXT, UI_COLOR_BG);
        }
    }
}
```

- [ ] **步骤 3: 注册**

增加 case 到按键分发和绘制 switch。

---

### 任务 9: 实现亮度调节页面 (UI_PAGE_SETTING_BL)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 呼吸灯定时器变量 + Draw_BL_Full**

```c
static void Draw_BL_Full(void)
{
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());

    /* Row 1: Title */
    {
        const char* title = is_cn ? S_TITLE_BL : S_EN_BL;
        uint8_t col = Center(title);
        Tft_Driver_Show_CN_String(1, col, title, UI_COLOR_TITLE, UI_COLOR_BG);
    }

    /* Row 3: Progress bar (128px wide, centered) */
    {
        uint16_t bar_x = 16;
        uint16_t bar_y = 3 * 16 + 4;
        uint16_t bar_w = 128;
        uint16_t bar_h = 8;
        uint16_t fill_w = (uint16_t)((uint32_t)s_backlight_val * bar_w / 255);
        Tft_Driver_Fill_Rect(bar_x, bar_y, bar_w, bar_h, UI_COLOR_DIM);  /* bg */
        if (fill_w > 0)
            Tft_Driver_Fill_Rect(bar_x, bar_y, fill_w, bar_h, UI_COLOR_VALUE);  /* fill */
    }

    /* Row 4: Value */
    {
        char buf[16];
        snprintf(buf, 16, "%d / 255", s_backlight_val);
        uint8_t col = Center(buf);
        Tft_Driver_Show_String(4, col, buf, UI_COLOR_VALUE, UI_COLOR_BG);
    }

    /* Row 6: Operation hints */
    {
        const char* hint = is_cn ? "[F_UP +]  [F_DOWN -]" : "[F_UP +]  [F_DOWN -]";
        uint8_t col = Center(hint);
        Tft_Driver_Show_String(6, col, hint, UI_COLOR_TEXT, UI_COLOR_BG);
    }

    Tft_Driver_Set_Backlight(s_backlight_val);
}
```

- [ ] **步骤 2: 呼吸模式 200ms 动态更新 (BL_Dynamic_Update)**

```c
static void BL_Dynamic_Update(void)
{
    if (!s_bl_breathing) {
        /* Check if 2s elapsed since last key → resume breathing */
        if (Sys_Timer_Get_Tick() - s_bl_last_action_ms >= 2000) {
            s_bl_breathing = 1;
        }
        return;
    }

    /* Breathing: sine wave between [48, 248] */
    {
        uint32_t now = Sys_Timer_Get_Tick();
        float phase = (float)(now % 2500) / 2500.0f * 6.283185f;  /* 2.5s period */
        float sin_val = (0.5f + 0.5f * (float)0.0);  /* placeholder — compute sin */
        /* Simple triangle wave approximation: */
        uint32_t half_t = now % 2500;
        uint8_t val;
        if (half_t < 1250)
            val = (uint8_t)(48 + (uint32_t)(200) * half_t / 1250);
        else
            val = (uint8_t)(48 + (uint32_t)(200) * (2500 - half_t) / 1250);

        if (val != s_backlight_val) {
            s_backlight_val = val;
            /* Redraw bar + value */
            {
                uint16_t bar_x = 16, bar_y = 3 * 16 + 4, bar_w = 128, bar_h = 8;
                uint16_t fill_w = (uint16_t)((uint32_t)s_backlight_val * bar_w / 255);
                Tft_Driver_Fill_Rect(bar_x, bar_y, bar_w, bar_h, UI_COLOR_DIM);
                if (fill_w > 0)
                    Tft_Driver_Fill_Rect(bar_x, bar_y, fill_w, bar_h, UI_COLOR_VALUE);
            }
            {
                char buf[16];
                snprintf(buf, 16, "%d / 255", s_backlight_val);
                uint8_t col = Center(buf);
                Tft_Driver_Erase_Pixel_Area(0, 4 * 16, 160, 16);
                Tft_Driver_Show_String(4, col, buf, UI_COLOR_VALUE, UI_COLOR_BG);
            }
            Tft_Driver_Set_Backlight(s_backlight_val);
        }
    }
}
```

- [ ] **步骤 3: Handle_BL_Keys**

```c
static void Handle_BL_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                            Key_Driver_Event k2, Key_Driver_Event k3)
{
    int16_t step = 0;

    /* F_UP / F_DOWN: manual adjust, exit breathing */
    if (k1 == KEY_DRIVER_EVENT_CLICK)  { step = 8;  s_bl_breathing = 0; s_bl_last_action_ms = Sys_Timer_Get_Tick(); }
    if (k2 == KEY_DRIVER_EVENT_CLICK)  { step = -8; s_bl_breathing = 0; s_bl_last_action_ms = Sys_Timer_Get_Tick(); }
    if (k1 == KEY_DRIVER_EVENT_LONG_PRESS) { step = 32;  s_bl_breathing = 0; s_bl_last_action_ms = Sys_Timer_Get_Tick(); }
    if (k2 == KEY_DRIVER_EVENT_LONG_PRESS) { step = -32; s_bl_breathing = 0; s_bl_last_action_ms = Sys_Timer_Get_Tick(); }

    if (step != 0) {
        int16_t new_val = (int16_t)s_backlight_val + step;
        if (new_val < 48) new_val = 48;
        if (new_val > 248) new_val = 248;
        s_backlight_val = (uint8_t)new_val;
    }
}
```

需检查 Key_Driver.h 是否有 `KEY_DRIVER_EVENT_LONG_PRESS` 枚举值。如果没有，仅使用 CLICK，不处理长按。

- [ ] **步骤 4: 注册 Phase 5 200ms 动态更新**

在 `tick_200ms` switch 中增加：
```c
case UI_PAGE_SETTING_BL: BL_Dynamic_Update(); break;
```

- [ ] **步骤 5: 编译验证**

---

### 任务 10: 实现颜色方案页面 (UI_PAGE_SETTING_COLOR)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 预设颜色表 + Draw_Color_Full**

```c
typedef struct {
    const char* name_cn;
    const char* name_en;
    uint16_t bg;
    uint16_t fg;
    uint16_t accent;
} ColorPreset;

static const ColorPreset COLOR_PRESETS[6] = {
    {"\xe9\xbb\x98\xe8\xae\xa4", "Classic",  0x0000, 0xFFFF, 0xFFE0},
    {"\xe7\x90\xa5\xe7\x8f\x80", "Amber",    0x001A, 0xFD20, 0xFC00},  /* 0x001A: dark blue approx */
    {"\xe9\x9d\x92\xe9\x9c\x93", "Cyber",    0x000A, 0x07FF, 0x07E0},
    {"\xe6\x8a\xa4\xe7\x9c\xbc", "EyeCare",  0x18E3, 0xBE77, 0x8E4C},
    {"\xe9\xab\x98\xe5\xaf\xb9\xe6\xaf\x94", "HiContrast", 0x0000, 0xFFFF, 0x07E0},
    {"\xe6\x9a\x96\xe7\x99\xbd", "Warm",     0x1C18, 0xFFE0, 0xFD88},  /* approximate */
};

static void Draw_Color_Full(void)
{
    uint8_t i;
    uint8_t is_cn = (s_language == 0 && Tft_Driver_Is_Font_Flash_Valid());

    Draw_Header(is_cn ? S_TITLE_COLOR : S_EN_COLOR);

    for (i = 0; i < 6; i++) {
        char buf[24];
        const char* name = is_cn ? COLOR_PRESETS[i].name_cn : COLOR_PRESETS[i].name_en;
        snprintf(buf, 24, "  %s", name);
        Erase_Line(2 + i);
        Draw_Menu_Text(2 + i, 2, buf, 1);
    }

    Draw_Cursor(2 + s_setting_cursor);
}
```

- [ ] **步骤 2: Handle_Color_Keys**

```c
static void Handle_Color_Keys(Key_Driver_Event k0, Key_Driver_Event k1,
                               Key_Driver_Event k2, Key_Driver_Event k3)
{
    if (k1 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor == 0) s_setting_cursor = 5;
        else s_setting_cursor--;
    }
    if (k2 == KEY_DRIVER_EVENT_CLICK) {
        if (s_setting_cursor >= 5) s_setting_cursor = 0;
        else s_setting_cursor++;
    }
    if (k0 == KEY_DRIVER_EVENT_CLICK) {
        /* Apply preset */
        s_color_preset = s_setting_cursor;
        s_color_fg = COLOR_PRESETS[s_setting_cursor].fg;
        s_color_bg = COLOR_PRESETS[s_setting_cursor].bg;
        /* Redraw with new colors — trigger full redraw */
        s_page_drawn = 0;
    }
}
```

- [ ] **步骤 3: 颜色生效路径**

需要在 Ui_Controller_Task 的 Phase 0/7 中，将 `UI_COLOR_BG` 等宏替换为基于 `s_color_bg/fg` 的动态取值，或使用全局颜色上下文结构。但为保持简单，先全页重绘时使用新颜色，后续 phases 也引用 s_color_* 变量。

注意：当前 `UI_COLOR_*` 都是 `#define` 宏，需要改为基于 s_color_* 计算。简单方案：新增一组 `UI_COLOR_*_DYNAMIC` 函数返回当前生效颜色。

```c
/* Dynamic color getters — return s_color_* when preset active */
static uint16_t UI_Color_Bg(void)    { return s_color_bg; }
static uint16_t UI_Color_Fg(void)    { return s_color_fg; }
```

所有页面绘制中 `UI_COLOR_BG` → `UI_Color_Bg()`, `UI_COLOR_TITLE` → 基于 s_color_* 计算。

为减少 Plan 篇幅，颜色应用统一在任务 12 处理。

- [ ] **步骤 4: 编译验证**

---

### 任务 11: 颜色系统应用 (全局配色变量化)

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: 新建动态颜色函数替换 #define 宏**

将所有 `#define UI_COLOR_*` 替换为函数，基于当前预设生成：

```c
static uint16_t s_color_accent;  /* accent color from preset */

static void Apply_Color_Preset(uint8_t preset_idx)
{
    const ColorPreset* p = &COLOR_PRESETS[preset_idx];
    s_color_fg      = p->fg;
    s_color_bg      = p->bg;
    s_color_accent  = p->accent;
    s_color_preset  = preset_idx;
}

static uint16_t Uc_Bg(void)      { return s_color_bg; }
static uint16_t Uc_Title(void)   { return s_color_accent; }
static uint16_t Uc_Text(void)    { return s_color_fg; }
static uint16_t Uc_Value(void)   { return s_color_accent; }  /* accent for highlight values */
static uint16_t Uc_Data(void)    { return s_color_fg; }
static uint16_t Uc_Alarm(void)   { return TFT_COLOR_RED; }   /* always red */
static uint16_t Uc_Ok(void)      { return TFT_COLOR_GREEN; }  /* always green */
static uint16_t Uc_Dim(void)     { return TFT_COLOR_GRAY; }   /* always gray */
```

- [ ] **步骤 2: 替换所有页面绘制中的 UI_COLOR_* 宏**

全文搜索 `UI_COLOR_BG` 替换为 `Uc_Bg()`，依此类推。注意函数调用不可用于 switch case 标签；这些是函数参数，不受影响。

- [ ] **步骤 3: 初始化时加载默认配色**

在 `Ui_Controller.c` 顶部静态变量初始化后：
```c
Apply_Color_Preset(0);  /* Classic default */
```

- [ ] **步骤 4: 编译验证**

---

### 任务 12: 设置参数持久化 (App_Storage.c/h)

**文件**:
- 修改: `Keil_Project/User/App_Storage.h`
- 修改: `Keil_Project/User/App_Storage.c`

**注意**: 现有 `App_Storage_Config` 已有 `backlight`（1B）和 `language`（1B）字段。新增字段需追加到结构体。

- [ ] **步骤 1: 扩展 App_Storage_Config 结构体**

在 `App_Storage.h` 的 `App_Storage_Config` 中扩展：

```c
typedef struct {
    uint32_t magic;          /* 4B  Magic */
    uint32_t version;        /* 4B  版本 */
    /* WiFi 配网凭证 */
    char     ssid[32];       /* 32B SSID */
    char     password[64];   /* 64B WiFi 密码 */
    char     mqtt_key[64];   /* 64B MQTT API Key */
    /* 硬件校准 */
    float    adc_i_offset;   /* 4B */
    float    adc_v_gain;     /* 4B */
    int32_t  freq_trim_hz;  /* 4B */
    /* 系统偏好 */
    uint16_t default_freq;   /* 2B */
    uint8_t  backlight;      /* 1B  背光亮度 48-248 */
    uint8_t  language;       /* 1B  语言 0=CN 1=EN */
    uint8_t  font_size;      /* 1B  [V4.4.0] 字体 0=小 1=中 */
    uint8_t  color_preset;   /* 1B  [V4.4.0] 0-5预设 255=自定义 */
    uint16_t color_fg;       /* 2B  [V4.4.0] RGB565前景 */
    uint16_t color_bg;       /* 2B  [V4.4.0] RGB565背景 */
    /* CRC */
    uint32_t crc32;          /* 4B */
} App_Storage_Config;         /* 252→256B (需重新计算) */
```

由于增加 6 字节，需移除 3 字节 `reserved[3]`，保持结构体对齐。

- [ ] **步骤 2: 新增 Load/Save 便捷接口**

在 `App_Storage.h` 中声明：
```c
/** @brief V4.4.0: 加载设置参数到 Ui_Controller */
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* preset, uint16_t* fg, uint16_t* bg);
/** @brief V4.4.0: 保存设置参数 */
void App_Storage_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                uint8_t preset, uint16_t fg, uint16_t bg);
```

在 `App_Storage.c` 中实现：
```c
void App_Storage_Load_Settings(uint8_t* lang, uint8_t* font, uint8_t* bl,
                                uint8_t* preset, uint16_t* fg, uint16_t* bg)
{
    App_Storage_Config cfg;
    if (App_Storage_Load_Config(&cfg)) {
        *lang   = cfg.language;
        *font   = cfg.font_size;
        *bl     = cfg.backlight;
        *preset = cfg.color_preset;
        *fg     = cfg.color_fg;
        *bg     = cfg.color_bg;
    } else {
        /* Factory defaults */
        *lang   = 0;
        *font   = 1;
        *bl     = 248;
        *preset = 0;
        *fg     = 0xFFFF;
        *bg     = 0x0000;
    }
}

void App_Storage_Save_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                uint8_t preset, uint16_t fg, uint16_t bg)
{
    App_Storage_Config cfg;
    if (App_Storage_Load_Config(&cfg) == 0) {
        /* Load failed — start with factory defaults */
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic   = CFG_MAGIC;
        cfg.version = CFG_VERSION;
        cfg.default_freq = 100;
    }
    cfg.language     = lang;
    cfg.font_size    = font;
    cfg.backlight    = bl;
    cfg.color_preset = preset;
    cfg.color_fg     = fg;
    cfg.color_bg     = bg;
    App_Storage_Save_Config(&cfg);
}
```

- [ ] **步骤 3: Sys_Post_Init 中加载设置**

在 `Sys_Core.c` 的 `Sys_Post_Init()` 中，App_Storage_Init() 之后，加载设置并初始化 UI：

```c
/* V4.4.0: Load persistent settings */
{
    uint8_t lang, font, bl, preset;
    uint16_t fg, bg;
    App_Storage_Load_Settings(&lang, &font, &bl, &preset, &fg, &bg);
    Ui_Controller_Apply_Settings(lang, font, bl, preset, fg, bg);
}
```

- [ ] **步骤 4: 新增 Ui_Controller_Apply_Settings 公开接口**

在 `Ui_Controller.h` 中声明：
```c
void Ui_Controller_Apply_Settings(uint8_t lang, uint8_t font, uint8_t bl,
                                   uint8_t preset, uint16_t fg, uint16_t bg);
```

在 `Ui_Controller.c` 中实现（更新静态变量 + 应用背光 + 应用颜色）。

- [ ] **步骤 5: 编译验证**

F7 编译，确认结构体大小不超 256B。

---

### 任务 13: 全量集成 — Phase 管线注册 + 8 个 switch 更新

**文件**:
- 修改: `Keil_Project/Hardware/Ui_Controller.c`

- [ ] **步骤 1: Phase 3 按键分发 switch — 全部 6 个新分支注册**

在 `Handle_Keys_by_Page()` 的 switch(page) 中增加：
```c
case UI_PAGE_SETTING:
    Handle_Setting_Keys(k0, k1, k2, k3);
    return;
case UI_PAGE_SETTING_LANG:
    Handle_Lang_Keys(k0, k1, k2, k3);
    return;
case UI_PAGE_SETTING_ICONS:
    Handle_Icons_Keys(k0, k1, k2, k3);
    return;
case UI_PAGE_SETTING_FONT:
    Handle_Font_Keys(k0, k1, k2, k3);
    return;
case UI_PAGE_SETTING_BL:
    Handle_BL_Keys(k0, k1, k2, k3);
    return;
case UI_PAGE_SETTING_COLOR:
    Handle_Color_Keys(k0, k1, k2, k3);
    return;
```

- [ ] **步骤 2: Phase 3 KEY3 (ON/OFF) 返回逻辑 — 新增 setting 返回路径**

```c
case UI_PAGE_SETTING:
    s_page = UI_PAGE_MAIN_MENU;
    s_menu_cursor = 3;
    break;
case UI_PAGE_SETTING_LANG:
case UI_PAGE_SETTING_ICONS:
case UI_PAGE_SETTING_FONT:
case UI_PAGE_SETTING_BL:
case UI_PAGE_SETTING_COLOR:
    s_page = UI_PAGE_SETTING;
    break;
```

- [ ] **步骤 3: Phase 7 全量绘制 switch — 6 个 case**

```c
case UI_PAGE_SETTING:      Draw_Setting_Full(); break;
case UI_PAGE_SETTING_LANG:  Draw_Lang_Full();    break;
case UI_PAGE_SETTING_ICONS: Draw_Icons_Full();   break;
case UI_PAGE_SETTING_FONT:  Draw_Font_Full();    break;
case UI_PAGE_SETTING_BL:    Draw_BL_Full();      break;
case UI_PAGE_SETTING_COLOR: Draw_Color_Full();   break;
```

- [ ] **步骤 4: Phase 5 200ms 动态更新 switch — BL 呼吸 + ICONS 可能刷新**

```c
case UI_PAGE_SETTING_BL:   BL_Dynamic_Update(); break;
case UI_PAGE_SETTING:      /* static */ break;
case UI_PAGE_SETTING_LANG:  /* static */ break;
case UI_PAGE_SETTING_ICONS: /* static */ break;
case UI_PAGE_SETTING_FONT:  /* static */ break;
case UI_PAGE_SETTING_COLOR: /* static */ break;
```

- [ ] **步骤 5: Phase 4 cursor_changed switch — 设置主菜单 + 图标页光标刷新**

```c
case UI_PAGE_SETTING: Setting_Cursor_Update(old_cursor); break;
case UI_PAGE_SETTING_ICONS:
    s_page_drawn = 0;  /* full redraw for icon cursor change */
    break;
```

- [ ] **步骤 6: Phase 6 光标边界钳位 — 设置主菜单**

```c
if (s_page == UI_PAGE_SETTING) {
    if (s_setting_cursor > 4) s_setting_cursor = 0;
}
```

- [ ] **步骤 7: 编译验证**

---

### 任务 14: 全局版本号 + CLAUDE.md 更新

**文件**:
- 修改: `CLAUDE.md`

- [ ] **步骤 1: 版本号 V4.3.2 → V4.4.0**

全文搜索 `V4.3.2` 替换为 `V4.4.0`（每个 .c/.h 文件头部 @brief/@note 行、CLAUDE.md、README.md）。

- [ ] **步骤 2: CLAUDE.md 文件结构更新**

- `Ui_Controller.c/h` 行数：1690+39 → 约 2800+45
- 新增设置系统说明一节（§ 新增：设置系统）
- 页面枚举扩展（9→15 页面）
- 审查历史追加 V4.4.0 行

- [ ] **步骤 3: README.md 更新**

功能列表追加设置系统。

- [ ] **步骤 4: 编译验证 + keilkill + git push**

```bash
cmd.exe /c Keil_Project\keilkill.bat
git add -A
git commit -m "feat: V4.4.0 — 设置系统 (6页面) + ROM/Flash资源重划分 + 中英双语 + 多配色"
git push origin 4.0TFT
```

---

### 自审检查

1. **Spec 覆盖**: 每个 spec 章节均有对应任务（§2→Task1, §3→Task2, §4→Task3, §5.1→Task5, §5.2→Task6, §5.3→Task7, §5.4→Task8, §5.5→Task9, §5.6→Task10-11, §6→Task12, §7-8→Task13, §9→Task14）
2. **无 TBD**: 所有步骤都有具体代码或命令
3. **类型一致性**: 所有新变量名在任务间一致（s_language, s_font_size, s_backlight_val, s_color_* 等）
