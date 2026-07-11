# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `5.0` |
| **版本** | V5.0.1 |
| **语言** | 中文交流，代码注释中英混合 |

> **V5.0.1** (2026-07-11) — GPIO 全量重映射 + 5键系统 + 四灯系统 + Bug修复
> **V5.0.0** (2026-07-11) — 初始 GPIO 重映射: PA12→TFT_BL, PB12→W25Q128_CS, KEY0-KEY4 五键, WIFI/POWER/STATUS/HEARTBEAT 四灯
> **V4.5.2** (2026-07-11) — SPI 时序回归修复

> **详细开发者指南**: `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` (V4.3.0)
> **架构师技能文件**: `Claude_Files/docs/embedded-architect-system-prompt.md`
> **W25Q128 Flash CH341A 烧录指南**: `ch341/README.md` (V4.3.2)`

## 版本号规则 (全项目铁律)

```
Vx.y.z 三数字体系 (首位 x 固定为 5, 对应分支 5.0):
  x — 固定 5 (GPIO 全量重映射 + 5键系统升级)
  y — 中版本: 新增页面/大功能/全平台重写 时 +1
  z — 小版本: Bug修复/字库修正/底部栏调整/文档更新 时 +1

当前版本: V5.0.1

涉及版本号的位置 (全项目必须统一):
  文件头注释: 每个 .c/.h/.ino/.py 的 @brief/@note 行 → V4.3.2
  文档控制信息: 开发指南/技能文件的文档版本 → V4.3.2
  CLAUDE.md: 版本号 + 审查历史 + 文件结构行数注释 → V4.3.2
  README.md: badge + 版本历史 + 分支表 → V4.3.2
  操作手册/部署文档: 版本字段 → V4.3.2
  小程序: wxss/wxml/js 头部注释 → V4.3.2
  ch341/ 工具链: Python 脚本头部 → V4.3.2
  Arduino 固件: .ino 头部 → V4.3.2
  其他文档: ONENETapp/README → V4.3.0

历史版本 → V4.x.x 完整映射:
  旧 V0.0/V1.0 → V1.0.x | 旧 V3.0     → V2.0.0
  旧 V5.0/V5.1/V5.2 → V2.x.x | 旧 V6.0/V6.4 → V2.4.0/V3.0.0
  旧 V9/V10/V11/V12/V13 → V3.x.x | 旧 V14/V15/V16 → V4.0.0
  旧 V25 → V4.1.0 | 旧 V26 → V4.2.4
  (SPL V3.5.0、ARMCC V5.06、Keil V5 是外部工具版本, 不在此范围)

禁止事项:
  禁止使用 V1~V26 等旧格式 (已全部映射到 V4.x.x)
  禁止在代码行内注释中添加版本标记 (如 "EMA 双级滤波链 (V26)")
  仅 SPL V3.5.0、ARMCC V5、Keil V5 等外部工具版本号例外
```

## Git 推送前置钩子

**每次 `git push` 之前，必须按顺序执行：**

1. 运行 `Keil_Project/keilkill.bat` 清理全部 Keil 编译中间产物
2. `git add -A && git commit -m "..." && git push origin 5.0`
3. **铁律**: 禁止将 `.obj` `.lst` `.axf` 等编译产物上传到 GitHub

## "更新全部内容" 执行流程

> 触发词 `更新全部内容` 必须严格按以下 9 步执行，每步标注了 **【针对文件】**，
> 路径规则: 写目录路径表示该目录下全部文件 (含子目录)，禁止跳过、禁止处理不在列表中的文件。

### 第 1 条 — 全局代码审查

| 针对文件 | 检查内容 |
|:---|:---|
| `Keil_Project/Hardware/` 下全部 .c/.h | 12 模块: ADC/Buzzer/Esp8266/Inverter/Key/Led/Pwm/TFT_Font/Tft/Ui，函数签名/参数/枚举/宏 |
| `Keil_Project/System/` 下全部 .c/.h | Sys_Timer: SysTick 1ms、DWT |
| `Keil_Project/User/` 下全部 .c/.h | Sys_Core (5状态+Safety+EMA)、App_Network (重试/心跳/遥测/CMD)、main、stm32f10x_it |
| `Arduino_Project/` 下全部 .ino | ESP8266: WiFiManager、双 MQTT、指令去抖、遥测 |
| `ONENETapp/` 下全部 .html + `ONENETapp/js/` 下全部 .js | 网页端 6页面 + OneNET API + 数据模型 |
| `安卓app/` 下全部文件 (排除 node_modules/ server/ .superpowers/ minitest/ sourcemap.zip) | 小程序: app.* + utils/ + custom-tab-bar/ + pages/ + 操作手册 + 部署文档 + docs/ |

### 第 2 条 — 修复发现的问题

| 针对文件 | 操作 |
|:---|:---|
| 第 1 条检测到的所有差异文件 | 逐一修复，记录 diff |

### 第 3 条 — 更新 CLAUDE.md

| 针对文件 | 写入内容 |
|:---|:---|
| `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT\CLAUDE.md` | 版本号、文件结构+精确行数、审查历史追加、新增/变更模块说明 |

### 第 4 条 — 更新开发指南

| 针对文件 | 写入内容 |
|:---|:---|
| `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` | 文档版本号、修改日志、引脚表（含W25Q128接线）、文件结构、UI 页面、EMA/Safety/网络协议等架构章节与当前代码对齐 |
| `ch341/README.md` | [V4.3.2] CH341A Flash 字库烧录完整操作指南: 硬件接线/驱动安装/生成→烧写→校验全流程 |

### 第 5 条 — 更新技能文件

| 针对文件 | 写入内容 |
|:---|:---|
| `Claude_Files/docs/embedded-architect-system-prompt.md` | 版本号、审查历史、执行教训(第 4 节) |

### 第 6 条 — 更新 Claude_Files 下全部文档

| 针对文件 | 操作 |
|:---|:---|
| `Claude_Files/docs/` 下全部 .md | 与当前代码对齐 (开发指南 + 技能文件 + specs) |
| `Claude_Files/docs/` 下全部 .docx | `cd Claude_Files && node tools/generate_docx.js` 对所有 .md 重新生成 |
| `Claude_Files/diagrams/` 下全部 .vsdx .py .md | 检查架构图是否与当前代码一致 |
| `Claude_Files/tools/` 下全部 .js .ps1 | 检查路径/依赖是否有效 |
| `Claude_Files/package.json` | 检查依赖版本 |

### 第 7 条 — 更新项目 README

| 针对文件 | 写入内容 |
|:---|:---|
| `README.md` | 版本号、架构图、功能列表与当前代码对齐 |
| `ONENETapp/README.md` | 网页端部署信息 |
| `ch341/README.md` | CH341A Flash 烧录工具链说明 |

### 第 8 条 — 清理 Keil 编译产物 + Git 提交推送

| 针对文件 | 操作 |
|:---|:---|
| `Keil_Project/Objects/` `Keil_Project/Listings/` 下全部 .obj .lst .axf .uvopt .uvgui.* .__i .crf .d .o .htm .lnp .sct .dep .map .hex .build_log.htm .dbgconf .scvd | `cmd.exe /c Keil_Project\keilkill.bat` |
| **验证** | `git status` 确认零编译产物残留 |
| 提交 | `git add -A && git commit -m "docs: Vxx — <变更摘要>" && git push origin 4.0TFT` |

### 第 9 条 — 追加执行教训

| 针对文件 | 写入内容 |
|:---|:---|
| `Claude_Files/docs/embedded-architect-system-prompt.md` (第 4 节) | 本轮问题描述 + 根因 + 预防规则 |

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only
- **Project**: `Keil_Project/Project.uvprojx`, F7 编译 → F8 下载, 无 CLI 编译
- ARMCC V5 不支持 `--multibyte_chars`, UTF-8 中文必须 hex escape (`\xE6\x97\xA0...`)
- 字符串拼接 `"\xe5\x8f\x8c\xe5\x87\xbb" "Back"` 可避免 ARMCC #27-D 警告
- ARMCC #1293-D: `if ((p = strstr(...))` 触发警告, 改用 `if ((p = strstr(...)) != 0`

### ESP8266 (Arduino IDE)

- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU
- **Project**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- **烧录**: GPIO0 接 GND → 上电 → 上传 → 断开 GPIO0-GND → 重新上电
- **配网**: 首次上电开热点 `STM32_WPT_Config` → 手机连上输 WiFi 密码 → 自动连 OneNET

## Architecture: Dual-MCU

```
STM32 (物理脑)               ESP8266 (联网脑)
TIM1 PWM 95~150kHz          WiFi + MQTT 自动联网
ADC 双通道 + 64样本滑动窗口   STATUS:ONLINE 心跳
TFT/KEY/LED 人机交互        CMD:ON/OFF/SETFREQ 控制
W25Q128 16MB Flash 外挂      USART2 115200 纯文本 JSON
Sys_Safety 独立安全监测
        │                          │
        └────── USART2 JSON ───────┘
```

**铁律**: STM32 不发 AT 指令, ESP 不碰 PWM/ADC。开机自动联网。
**V4.3.2**: W25Q128 Flash 全字库 20897 字 (CH341A 烧录, CRC32 STM32 refin=false) — 启动自动校验启用, 无效则 ROM 76字回退。SPLASH 开机动画改为纯代码(8帧背光渐亮, 存 STM32 ROM)。`Tft_Driver_Font_Init()` 必须在 `W25Q_Driver_Init()` 之后调用。`Sys_Timer_Init()` 提前到 SPLASH 之前。
**V4.3.0**: SPI1 分时复用 (TFT + W25Q128), PA5=SCK PA7=MOSI PA6=DC/MISO 动态切, PA4=TFT_CS PA12=FLASH_CS 双门控。

## 系统全局状态机

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

| 状态 | PWM | Task 子集 |
|:---|:---|:---|
| INIT | 关 | 初始化阶段 |
| IDLE | 关 | Key+ADC+Network+Safety+UI |
| SWEEP | 开 | + Soft_Start (150k→100kHz) |
| RUNNING | 开 | + Freq_Ramp |
| FAULT | 关 | + FAULT UI, 取消所有斜坡 |

## 文件结构

```
WPT_PWM_V4.0_ONENET_TFT/                        ← ~14110 行逻辑代码 (全平台 含 ch341 工具链 679 行)
├── Keil_Project/                               ← STM32 固件 — 5926 行 (Hardware 3946 + User/System 1980, 不含 Library/Start)
│   ├── Project.uvprojx                         ← 工程入口, F7编译→F8下载
│   ├── keilkill.bat                            ← 清理编译产物 (push前必执行)
│   ├── Hardware/                               ← 硬件驱动 — 3946 行
│   │   ├── Ui_Controller.c/h                   ← 15页面 UI 状态机 + 圆弧能量条仪表盘 + 设置系统 (2260+45行)
│   │   ├── Tft_Driver.c/h                      ← ST7735 SPI1 全双工+DMA + Flash/ROM 双路径字库 + SPLASH (742+98行)
│   │   ├── W25Q_Driver.c/h                     ← [V4.3.2] 16MB SPI Flash + CS翻转二分搜索 + BSRR防毛刺 (294+106行)
│   │   ├── TFT_Font_Data.h                     ← ASCII 95字 + 中文 76字 + 图标 (441行, ROM 回退后备)
│   │   ├── Esp8266_Driver.c/h                  ← USART2 + Try_Copy_Rx_Frame 原子接收 (272+49行)
│   │   ├── Adc_Driver.c/h                      ← ADC1 双通道 + 64样本滑动窗口 (203+28行)
│   │   ├── Inverter_Control.c/h                ← 软启动 + 频率斜坡 (164+65行)
│   │   ├── Key_Driver.c/h                      ← 4键 FSM + 批量事件读取 (151+40行)
│   │   ├── Led_Driver.c/h                      ← 5 LED 闪烁 (PA12 已让给 Flash CS) (158+42行)
│   │   ├── Pwm_Driver.c/h                      ← TIM1 全桥 PWM 95-150kHz (131+33行)
│   │   └── Buzzer_Driver.c/h                   ← 蜂鸣器 (80+30行)
│   ├── User/                                   ← 应用层 — 1240 行
│   │   ├── App_Network.c/h                     ← WiFi OFFLINE 双模式+心跳+帧快照+遥测 (357+51行)
│   │   ├── App_Storage.c/h                     ← [V4.3.2] CRC32 extern + 参数双副本+黑匣子日志 (271+87行)
│   │   ├── Sys_Core.c/h                        ← 状态枚举+初始化+安全(仅RUNNING)+启动Status (300+44行)
│   │   ├── main.c                              ← 程序入口 (77行, 注释全量 + Task 移入状态机)
│   │   └── stm32f10x_it.c/h                    ← ISR (SysTick + USART2 ORE防锁死) (88+87行)
│   ├── System/ → Sys_Timer.c/h                 ← SysTick 1ms + DWT (72+36行)
│   ├── Start/  → CMSIS + system_stm32f10x
│   └── Library/ → SPL V3.5.0 (只读, 不可修改)
├── Arduino_Project/                            ← ESP8266 固件 — 522 行
│   └── ESP8266_MQTT_Firmware/...ino            ← WiFiManager+双MQTT+指令去抖+遥测+OFFLINE
├── ONENETapp/                                  ← 网页控制台 (Cloudflare Pages) — 3444 行
│   ├── index.html(429)/control.html(497)       ← 主页+控制+乐观更新+重试防堆积+连接指示器
│   ├── monitoring.html(405)/history.html(529)  ← 监测+历史趋势图
│   ├── alerts.html(326)/settings.html(805)/login.html(151)
│   ├── js/onenet.js(192)                       ← OneNET API 核心 (安全: 无console/无Token泄露)
│   ├── js/config.js(79)                        ← 数据模型+escapeHtml() XSS防护+移动端导航
│   ├── js/mobile-nav.js(31)
│   └── service-worker.js(31)                   ← PWA 离线回退 v3 (BASE 相对路径)
├── 安卓app/                                    ← 微信小程序 — 3303 行 (6页面+Component)
│   ├── utils/config.js(47)/onenet.js(271)      ← 数据模型单一来源 + API层 (双请求在线检测)
│   ├── custom-tab-bar/                         ← 底部导航 Component (无高亮)
│   ├── pages/{index,monitoring,control,history,alerts,settings}/
│   ├── 操作手册.md / 部署文档.md               ← 小程序文档
│   └── docs/                                   ← 设计 spec
├── ch341/                                      ← [V4.3.2] CH341A Flash 字库烧录工具链 — 679 行
│   ├── README.md                               ← 完整操作指南
│   ├── requirements.txt                        ← Python 依赖: pillow
│   ├── generate_font.py                        ← GB2312 全字库生成器 (20897 CJK + 95 ASCII + 31 图标, 438行)
│   ├── burn_flash.py                           ← 字库烧录编排 (生成→备份→融合→烧写16MB→逐字节校验, 241行)
│   └── flashrom-1.4/                           ← flashrom 1.4.0 + Zadig 2.8 + WinUSB 驱动
└── Claude_Files/                               ← AI 生成文档+工具
    ├── docs/                                   ← 开发者指南 + 技能文件
    ├── diagrams/                               ← Visio 流程图
    └── tools/                                  ← generate_docx.js + 桥接脚本
```

## 主循环

```c
int main(void) {
    Sys_Clamp_ESP();            /* 钳位 ESP 控制脚 */
    Sys_Hardware_Init();        /* Pwm/TFT/Led/Buzzer/Adc/Key */
    Sys_Timer_Init();           /* SysTick 在 SPLASH 之前! (Delay_Ms 依赖 SysTick) */
    W25Q_Driver_Init();         /* JEDEC 校验 -> s_chip_ok */
    Tft_Driver_Font_Init();     /* Flash Font Header + CRC32 (必须在 W25Q 之后!) */
    App_Storage_Init();         /* 参数加载 + 黑匣子指针恢复 */
    Sys_Startup_Screen();       /* SPLASH 逐字渐亮 ~4.8s */
    Sys_Post_Init();            /* LED + ADC + WDG + ESP 启动 */
    Sys_Timer_Delay_Ms(1000);   /* 开机画面停留 1s */
    g_sys_state = SYS_STATE_IDLE;
    while (1) {                 /* 主循环 — 按状态分发 */
        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;  /* 空闲: PWM 关, 等待操作 */
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;  /* 扫频: 150k->100kHz 软启动 */
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;  /* 运行: 频率闭环 + 调度 */
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;  /* 故障: 过流保护 + 等待复位 */
        }
    }
}
```

**V4.3.2 架构变更**: Key/ADC/Network/Safety 4 个 Task + IWDG + WFI 已移入每个 `Sys_Run_*()` 状态函数内部, 主循环只剩 switch 分发。

## Sys_Safety (安全监测, 独立于 UI)

- **EMA 滤波**: α=0.25, τ≈800ms, 仅 SYS_STATE_RUNNING 时每圈主循环更新 V/I
- **PB10 电源**: 电压 >12V → 拉高使能, ≤12V → 拉低关断
- **过流检测**: `s_safety_ema_i > 5.0A` → `Inverter_Control_Soft_Start_Fault()` + `Buzzer BEEP` + `g_sys_state = SYS_FAULT`
- **防误触发**: 非 RUNNING 状态跳过安全检测 (PWM 已关, 无过流可能)

## EMA 双级滤波链

| 层级 | 模块 | 滤波对象 | 用途 |
|:---|:---|:---|:---|
| 安全级 | `Sys_Safety_Update_EMA()` | ADC 原始 V/I | 过流保护, PB10 阈值 |
| 显示级 | `Ui_Controller_Update_EMA()` | Sys_Safety 输出 | UI 仪表盘 + 综合监测页 |
| 数字量 | `Pwm_Driver_Get_Frequency()` | 无滤波 | 频率（零迟滞, 保证调频跟手） |

## App_Network WiFi 连接与离线

- **状态机**: IDLE→WIFI→MQTT→ONLINE, 新增 OFFLINE_PASSIVE(被动断开自动嗅探)/OFFLINE_ACTIVE(主动断开需手动ON)
- **被动离线**: 热点断开后重试 5 次耗尽→OFFLINE_PASSIVE→被动监听 ESP STATUS 帧, 热点恢复自动重连
- **主动离线**: 用户按键/配网页 断开→OFFLINE_ACTIVE→忽略所有帧, 需手动 ON 恢复
- **重试**: 指数退避 5s→15s→30s→60s→2min→5min→30min, 5次上限, 不发硬件 RST (ESP 已在运行)
- **断连指令**: CMD:WIFI_DISC (断开但保留凭证) / CMD:CLEAR (清除凭证+ESP 重启)
- **MQTT 超时**: MQTT 状态 30s 无 ESP 帧→回退 WIFI 重试, 防 broker 不可达死锁
- **BOOT_WAIT 加速**: ESP 串口有数据即提前结束等待, 避免 4s 窗口丢 STATUS 帧
- **心跳超时**: 8s 无任何 ESP 帧仅检测 ONLINE 状态→判定离线开始重试
- **远程指令**: CMD:ON/OFF → `Ui_Controller_Force_Page_And_Reset()` 同步复位页面+光标
- **帧处理安全**: `Try_Copy_Rx_Frame` 消除 TOCTOU; `ss_cmd`/`conn_cs` 帧内快照防 ELSE-IF 链竞态

## ESP8266 固件

- **状态机对齐**: Conn_State 与 STM32 App_Network 一致 (IDLE/WIFI/MQTT/ONLINE/OFFLINE_PASSIVE/OFFLINE_ACTIVE)
- **持续重试**: ESP 侧不设重试上限, 上限判断由 STM32 负责; WiFi 断开后每 3s 重试 WiFi.begin()
- **CMD:WIFI_DISC**: 断开 WiFi 但保留凭证, 进入 OFFLINE_PASSIVE 持续嗅探恢复
- **CMD:CLEAR**: 清除配网凭证+ESP.restart(), 进入配网模式; **V4.5.1: 二次确认 (5s 窗口, 第一次回复 CLEAR_CONFIRM? 第二次才执行)**
- **指令去抖**: Mqtt_Task_Parse_Command 2s 窗口内相同 payload 直接丢弃
- **Switch 状态**: 仅 `s==2` (SS_DONE) 上报 true, `s==1` (SWEEP) 为过渡态不上报
- **遥测频率**: 仅在 running 时上报真实 F 值, 否则上报 0 (完全透传 STM32)
- **SetFreq 量化**: `(val/1000)*1000`, 与 STM32 PWM 1kHz 步进一致
- **V4.5.1 安全加固**: Token 占位符化 (部署前替换) + 配网热点加密码 (WIFI_AP_PASSWORD) + 公共 MQTT Broker 门控 (PUBLIC_MQTT_ENABLED) + 指令鉴权预留 (PUBLIC_CMD_AUTH_KEY) + WiFiManager debug 生产关闭

## 网页端 (Cloudflare Pages)

- **登录守卫**: 所有 6 个受保护页面顶部内置 `lastLoginTime` 检查，7 天过期后自动跳转 `/login.html`
- **XSS 防护**: `config.js` 提供 `escapeHtml()` 函数，所有 `innerHTML` 插值前必须通过此函数转义用户可控字符串
- **乐观更新**: `setProperty` 成功后立即写 localStorage + 3s 乐观锁; **V4.5.1: 重试全部失败后回滚乐观缓存**
- **重试**: `setProperty` 网络/业务错误各重试 3 次 (500ms/800ms)
- **连接指示**: 在线(绿) / 离线(黄) / 失败(红) / 未配置(灰), `/device/detail` 优先 + 数据非空兜底
- **数据模型**: `config.js` DEFAULT_DATA_MODEL → sensors(V/I/F) + controls(Switch/SetFreq)
- **频率映射**: `fromCloud: v => Math.floor(v/1000)` / `toCloud: v => v*1000`, Web 显示 kHz
- **安全**: 零 console 输出, 零硬编码 Token, login.html SHA-256 哈希验证
- **轮询**: 5s 间隔 setInterval + pagehide 清理, 无嵌套泄漏; 慢网下防重叠
- **SW**: 使用 BASE 相对路径兼容根路径/子路径部署; 仅在 login.html 注册 (autoLogin 跳转后尚未注册，已知限制)

## 微信小程序

- **架构**: 6 页面 + `custom-tab-bar` Component + `utils/` (单数据模型来源)
- **API 层**: `getLatestData()` 双请求并行 (`/thingmodel` + `/device/detail`), 在线判定优先 /device/detail + 数据非空兜底, HTTP 细化错误
- **安全**: 零硬编码凭证, 配置从 app.js getOneNetConfig() 获取
- **控制页**: 离线→强制安全默认值(OFF/100); API 失败→wx.getStorageSync 缓存回填
- **首页**: `_applyData` 统一更新连接状态, `onHide` 清理定时器, `_clearTimers` 防泄漏
- **底部栏**: 无高亮 (selected=-1), 5 tab: ⌂ ◉ ⊛ 🗂 ⚙, `templates/` 已删除
- **存储键**: `wpt_latest`, `wpt_history`(1440max), `wpt_alerts`(50max), `wpt_alarm_states`, `wpt_control_locks`, `wpt_onenet_config`, `wpt_data_model`

## 全链路数据一致性铁律

| 状态 | STM32 遥测 | ESP 上报 | Web/小程序显示 |
|:---|:---|:---|:---|
| IDLE | V=真实,I=真实,F=0,S=0 | Switch=false, V/I=真实, F=0 | 停机/V/I 正常/F=0 |
| SWEEP | 不发送遥测 | (无数据) | (上一帧缓存) |
| RUNNING | V=EMA,I=EMA,F=真实Hz,S=2 | Switch=true, V/I/F=真实 | 运行中/实时值 |
| FAULT | V=真实,I=真实,F=0,S=3 | Switch=false, V/I=真实, F=0 | 故障/实时V/I/F=0 |

**核心**: V/I 始终上报真实物理量 (任何状态 ADC 均可采), 仅 F 在 PWM 未运行时强制为 0。

Telemetry JSON 全链路格式: `{"V":xx,"I":xx,"F":xx,"S":x}\n`

## Pin Mapping (STM32F103C8 LQFP-48) — V5.0

| Pin | 功能 | Pin | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | ADC_CH8 (电流, CC6920BSO) |
| PA1 | ESP8266 RST | PB1 | ADC_CH9 (电压) |
| PA2 | USART2_TX | PB3 | LED_POWER (绿, 12V指示) |
| PA3 | USART2_RX | PB4 | LED_WIFI (蓝) |
| PA4 | TFT_CS | PB5 | KEY4 (IPU, 确定/启停) |
| PA5 | SPI1_SCK | PB6 | KEY3 (IPU, DOWN/减) |
| **PA6** | **TFT_DC / Flash MISO (动态切)** | PB7 | KEY2 (IPU, UP/加) |
| PA7 | SPI1_MOSI | PB8 | KEY1 (IPU, 返回) |
| PA8 | TIM1_CH1 (HINA) | PB9 | KEY0 (IPU, 电源开关) |
| PA9 | TIM1_CH2 (HINB) | PB10 | PowerCtrl (KEY0 手动) |
| PA12 | TFT_BL (GPIO) | PB11 | ESP8266 EN |
| PA15 | LED_STATUS (黄) | **PB12** | **W25Q128_CS** |
| **PC13** | **LED_HEARTBEAT (板载, 运行灯)** | PB13 | TIM1_CH1N (LINA) |
| | | PB14 | TIM1_CH2N (LINB) |
| | | PB15 | Buzzer |

JTAG 禁用释放 PB3/PB4/PA15。V5.0: PA10/PA11 移除, PA12→TFT_BL, PB12→W25Q128_CS, PB6→KEY3, PC13→板载心跳灯

## 编码规范

### 命名 (零容忍)

| 层次 | 规则 | 正确 | 违规 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` 强制前缀 | `Sys_Run_Led_Tick()` | `Led_Tick()` |
| 静态变量 | `s_description` | `s_gauge_val_str` | `uiState` |
| 全局变量 | `g_description` | `g_sys_state` | `Sys_State_Global` |
| 枚举值 | `MODULE_NAME_VALUE` 全大写 | `SYS_STATE_IDLE` | `State_Idle` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |
| 头文件保护 | `MODULE_NAME_H` 无前导下划线 | `SYS_CORE_H` | `_SYS_CORE_H` |

### 注释

- 公开函数必须带 `@brief` + `@param`/`@retval`
- `.h` 顶部必须带 `@file` + `@brief` + `@note`
- 禁止 `//` 双斜杠 (ARMCC V5), 统一用 `/** */` 或 `/* */`
- 只写 WHY, 不写 HOW

### 调度 + 架构铁律

- 周期任务: `Sys_Timer_Get_Tick() - last >= PERIOD` (uint32_t 回绕安全)
- `Sys_Timer_Delay_Ms()` 仅初始化阶段, 运行时禁止阻塞
- `.h` 只公开接口, `.c` 全 static 内部实现
- 禁止 `extern` 访问模块私有变量, 禁止 `#include ".c"`
- 分层单向: Hardware → System → Application

## TFT 驱动 (ST7735 Green Tab)

| 参数 | 值 |
|:---|:---|
| SPI | Mode 3, **18MHz**, DMA1_Channel3, **全双工** (V4.3.0: MISO 用于 Flash 读取) |
| 分辨率 | 160×128 横屏, MADCTL=0xA0 |
| SetWin 偏移 | X+1, Y+2 |
| **DMA 超时** | **V4.5.1: 4个忙等循环加 200ms 超时护底, 超时强制释放 CS 防系统硬锁** |
| **字库路径** | **Flash 20897 字 (CRC32 STM32 refin=false 校验) → ROM 76 字 (自动回退)** |
| 字库位序 | 全部 LSB-first, 统一在 `TFT_Font_Data.h` / `generate_font.py` (无 bit_reverse) |
| 图标 | WIFI(4+动画6帧), MQTT(3态+动画6帧), ICON_STAR, 20 新图标 |
| **开机动画** | **SPLASH: 纯代码实现 (背光渐亮 + 逐字点亮 ~4.8s), 不依赖 W25Q, 版本号右下角** |

CN_INDEX 与 CN_FONT_16X16 严格一一对应 (76字, 索引 0-75), 末尾为 综(74)+合(75)。

## PWM 基线 (不可改)

- TIM1 CH1=PWM1 + CH2=PWM2, Up 模式, 50% 占空, `TIM_OCNPolarity_Low`
- 死区 1000ns, 95-150kHz, UDIS 原子更新
- 开机: TIM_Cmd(DISABLE) + MOE(DISABLE), 零输出

## 圆弧能量条仪表盘

| 参数 | 值 |
|:---|:---|
| 圆心 | G_CX=80, G_CY=84 |
| 半径 | R_TICK=56 (外), R_BIG=50 (主6px), R_FINE=53 (细3px) |
| 绘制 | 1px Bres_Line, 高亮色/暗灰槽(0x18C3) |
| **信息舱** | Row 4(Y=64): 状态 OK/WRN/HI/SWP/DON/IDL 居中 |
| | Row 5(Y=80): 纯数值居中黄色 |
| | Row 6(Y=96): 标签居中青色 |
| Phase 0 | WIFI@X=128 + MQTT@X=144 |

### GaugeConfig 三表

| 参数 | 电压 V | 电流 C | 频率 F |
|:---|:---|:---|:---|
| range | 0→50 | 0→2 | 90→150 |
| big_step | 10 | 0.5 | 10 |
| fine_step | 1 | 0.1 | 1 |
| red_start | 42 | 1.8 | 140 |
| 停机 | — | — | val=0 能量条归零, 灰"0" |

### UI Phase 架构

| Phase | 职责 |
|:---|:---|
| 0 | Global Top-Right Icons Manager |
| 1 | System Fault detection (监听 g_sys_state) |
| 2 | Sweep complete → auto-jump SUMMARY |
| 3 | Key scan + dispatch → 可改 s_page/g_sys_state |
| 4 | Page change → tracking invalidated |
| 5 | 200ms tick → dynamic incremental update |
| 6 | Cursor boundary clamp |
| 7 | Draw — full page only when s_page_drawn==0 |

### 底部栏

所有页面统一: 底部栏已删除 (V4.3.2), Row 6/7 空白。

## Safety

- **过流**: Sys_Safety 仅在 RUNNING 状态检测 >5.0A → SYS_FAULT + Buzzer BEEP
- **FAULT 恢复**: PAGE(确定) 单击 → `Soft_Start_Reset()` + `Sys_Safety_Reset_EMA()` → MAIN_MENU
- **FAULT 防重触发**: EMA 电流清零 + 重新初始化; 非 RUNNING 状态跳过安全检测
- **远程启停 UI 同步**: CMD:ON/OFF → `Ui_Controller_Force_Page_And_Reset()`
- **上电**: TIM1 全关, PB10 拉低关 12V
- **看门狗**: IWDG 1.6s, 调试自动暂停
- **HardFault/...**: 先关 PWM 再死循环

## 环形仪表盘速查

```c
#define G_CX   80  /* 圆心 */    #define R_TICK 56  /* 外圈 */
#define G_CY   84                #define R_BIG  50  /* 主刻度 (6px) */
                                 #define R_FINE 53  /* 细刻度 (3px) */

Bres_Line(x0,y0,x1,y1,color);              /* 1px 能量条刻度 */
Gauge_Polar(angle_deg, radius, &x, &y);     /* sin 查表极坐标 */
Draw_Gauge_Full(cfg, val);                   /* 入场全绘 */
Gauge_Dynamic_Update(cfg, val, old_val);     /* 200ms 增量差分 */
Draw_TopRight_Icons();                       /* WIFI@128 MQTT@144 */

/* 三表配置 */
GAUGE_V = { 0, 50, 10, 5,    1,  42, 'V'};
GAUGE_C = { 0,  2,0.5,0.25,0.1, 1.8, 'C'};
GAUGE_F = {90,150, 10, 5,    1, 140, 'F'};

/* 信息舱: 状态(Row4) → 数值(Row5,黄) → 标签(Row6,青) */
```

## 审查历史

| 版本 | 重点修复 |
|:---|:---|
| V5.0 | **GPIO 全量重映射 + 5键系统 + 四灯系统**: PA12→TFT_BL(GPIO), PB12→W25Q128_CS, PB6→KEY3, PB9→KEY0(电源开关协调PB10), PB8→KEY1(返回,双击主菜单), PB7→KEY2(UP), PB6→KEY3(DOWN), PB5→KEY4(确定); PA15→STATUS(PWM指示), PB3→POWER(12V), PC13→HEARTBEAT(板载运行); PA10/PA11 移除; TIM4 停用; PB10 手动(去自动电压阈值); Sys_Power_Control_Handle 新增; Key_Driver 4→5键+WITH_DOUBLE; Led_Driver 5→4灯(COM/PWR/TMP/PWM→STATUS/HEARTBEAT); Ui_Controller MENU UP键 wrapping 修复(<=1→==0) |
| V4.5.2 | **SPI时序回归+DMA修复+EMA修复**: DMA超时操作数反转修复(根治花屏), DMA TC3残留标志清理, SPI恢复18MHz原始配置(去dummy/CS NOP), Flash字模批量读(16次→1次), 中文/图标ROM优先, 默认英文界面(W25Q手动切中文), Sys_Safety EMA全状态更新(V/I在IDLE下不再显示0), CS脉冲简化, NVIC Flash读临界区保护, Write_Enable SPI模式防护, s_language静态初值统一, Ui_Controller Pick_CN_EN遗漏修复 |
| V4.5.1 | **全平台安全审查修复 (16项)**: ESP8266 Token占位符化(防泄露)+配网热点加密码+CMD:CLEAR二次确认(5s窗口)+公共MQTT Broker门控+WIFI_CONN死代码修复+WiFiManager debug关; STM32 Tft_Driver DMA/SPI忙等4循环200ms超时护底; Esp8266_Driver 3槽环形缓冲(防连续帧丢失); App_Storage 黑匣子指针每60条回写Block0(重启可恢复)+故障锁存扇区跨页保护; App_Network strtol替代atol+OFFLINE恢复STATUS正向过滤; USART2 RXNE先于ORE(防有效字节丢弃); Ui_Controller 扫频进度条+WiFi行变更检测(消除200ms闪烁); Web setProperty重试失败回滚乐观缓存+SW BASE路径修复; 编译0错误0警告 |
| V4.5.0 | 设置系统重构: 8页设置(语言/字间距/图标/亮度二级菜单手动+呼吸灯/颜色方案) + PIC预览模型(PAGE=确定/ON=取消) + 字体大小→字间距(0/2/4/6px真实像素差) + 亮度1-100%滚动翻阅即时生效 + 色彩6预设全屏重绘 + Tft_Driver 纯像素间隙渲染 + Center/Right 自适应间距布局 + Draw_Header 自动画图标(dedup) + Key_Driver ID命名去歧义(PAGE=0/ON=3) + ARMCC V5 hex-escape兼容(零#870-D警告) + App_Storage_Config 结构体196B校验 + 死代码清理(Key_GetEvent/font_size/BL_Dynamic) |
| V4.3.2 | W25Q128 全字库修复 + 开机动画重写: SPLASH 逐字渐亮~4.8s(背光渐变+两行逐字+版本号右下角) + 按键交换(PB5=PAGE确定/PB9=ON返回) + 底部栏全删 + Task/IWDG/WFI 移入状态机 + main.c 每行注释 + UI 页面标题中文化 + 接线图纯 ASCII(ARMCC V5 C89兼容) + CRC32/CS翻转/bit_reverse/generate_font.py 修正 |
| V4.3.1 | CH341+Python Flash 字库烧录: generate_font.py(GB2312 6763字+图标 2MB镜像) + burn_flash.py(flashrom 备份+擦除+烧写+逐字节校验) + W25Q_Font_Index_Binary_Search(总线独占二分检索 5.85μs/字) + Tft_Driver Flash/ROM 双路径(单字单检索 16×提速) + Font_Header CRC32 小端序铁律 |
| V4.3.0 | W25Q128 16MB SPI Flash 集成: SPI1 分时复用(PA6动态切DC/MISO) + GB2312全字库(668KB)+开机画面区(1MB)+参数双副本CRC32(8KB)+黑匣子循环日志(4MB)+故障锁存前后5s + 四大硬件防线(L1写使能/L2 Busy死等/L3 DFF原子闪切/L4 发波禁擦) + ADC校准Flash固化+本地自测算B方案 + config.js getDataModel() undefined修复 + control.html clearInterval修复 + 遥测S字段对齐g_sys_state |
| V4.2.4 | 离线守卫全平台修复: Web+小程序 _isOnline 判定统一 + 缓存时序修正(延后到在线确认) + 在线兜底(data非空)+/device/detail覆写 + Web throw误触发修复 + 重复代码块清理 + 生命周期onHide/pagehide清理 |
| V4.2.3 | 全平台安全审查修复: 删除硬编码凭证+console清理+定时器泄漏修复+setInterval防重叠+小程序并行在线检测+STM32 Sys_Safety仅RUNNING+Key批量读取+login SHA-256 |
| V4.2.2 | WiFi OFFLINE 双模式(被动自动嗅探/主动手动恢复)+5次有限重试+BOOT_WAIT提前+MQTT超时+Bug修复8项 |
| V4.2.1 | 全项目 README 重写(4分支统一分支表) + CLAUDE.md 版本号规则流程扩展到全部文档 |
| V4.2.0 | TFT字库修复: CN_FONT[74..75] 失败→综合字模替换 + 底部栏简化 + 全平台版本号统一为 Vx.x.x |
| V4.1.0 | 小程序全重写: 单数据模型源+双API并行+动态卡片+底部栏Component+HTTP细化错误 |
| V4.0.0 | 9页面TFT UI + 全局状态机 + Sys_Safety独立安全 + EMA双级滤波 + 圆弧能量条仪表盘 + 16轮全链路审查 |
