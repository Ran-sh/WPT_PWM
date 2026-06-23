# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **版本** | V4.3.1 |
| **语言** | 中文交流，代码注释中英混合 |

> **详细开发者指南**: `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` (V4.3.1)
> **架构师技能文件**: `Claude_Files/docs/embedded-architect-system-prompt.md`
> **频率斜坡设计**: `Claude_Files/docs/2026-05-24-freq-ramp-design.md`
> **W25Q128 Flash 集成设计**: `Claude_Files/docs/2026-06-22-w25q128-flash-integration-design.md`
> **OTA 字库推送设计**: `docs/superpowers/specs/2026-06-23-ota-font-push-design.md`
> **OTA 操作指南 (小白版)**: `Claude_Files/docs/OTA字库推送-小白操作指南.md`

## 版本号规则 (全项目铁律)

```
Vx.y.z 三数字体系 (首位 x 固定为 4, 对应目录 WPT_PWM_V4.0_ONENET_TFT):
  x — 固定 4 (仅当整个仓库升级到下一代显示方案才变 5)
  y — 中版本: 新增页面/大功能/全平台重写 时 +1
  z — 小版本: Bug修复/字库修正/底部栏调整/文档更新 时 +1

当前版本: V4.3.1

涉及版本号的位置 (全项目必须统一):
  文件头注释: 每个 .c/.h/.ino/.py 的 @brief/@note 行 → V4.3.1
  文档控制信息: 开发指南/技能文件的文档版本 → V4.3.1
  CLAUDE.md: 版本号 + 审查历史 + 文件结构行数注释 → V4.3.0
  README.md: badge + 版本历史 + 分支表 → V4.3.0
  操作手册/部署文档: 版本字段 → V4.3.0
  小程序: wxss/wxml/js 头部注释 → V4.3.0
  其他文档: ONENETapp/README, Railway_Deploy/README, plans/, specs/ → V4.3.0

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
2. `git add -A && git commit -m "..." && git push origin 4.0TFT`
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
| `Claude_Files/docs/2026-06-22-w25q128-flash-integration-design.md` | [V4.3.0] W25Q128 硬件接线/分区表/校验体系/软件架构/PC工具链 当前代码对齐 |

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
| `Railway_Deploy/README.md` | 桥接服务器状态 (当前为备选方案) |
| `Claude_Files/diagrams/README.md` | 图表文件说明 |

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
**V4.3.1 新增**: ESP8266 WiFiServer OTA 字库推送 (TCP→Serial 透传 + Base64 编码 + STM32 解码写 Flash), `Claude_Files/tools/ota_font_push.py` PC 端推送工具。
**V4.3.0 新增**: SPI1 分时复用 (TFT + W25Q128), PA5=SCK PA7=MOSI PA6=DC/MISO 动态切, PA4=TFT_CS PA12=FLASH_CS 双门控。

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
WPT_PWM_V4.0_ONENET_TFT/                        ← ~10957 行逻辑代码 (全平台)
├── Keil_Project/                               ← STM32 固件 — 5829 行 (含 User/System, 不含 Library/Start)
│   ├── Project.uvprojx                         ← 工程入口, F7编译→F8下载
│   ├── keilkill.bat                            ← 清理编译产物 (push前必执行)
│   ├── Hardware/                               ← 硬件驱动 — 4398 行
│   │   ├── Ui_Controller.c/h                   ← 9页面 UI 状态机 + 圆弧能量条仪表盘 (1722+39行)
│   │   ├── Tft_Driver.c/h                      ← ST7735 SPI1 全双工+DMA + Flash字库 (564+77行)
│   │   ├── W25Q_Driver.c/h                     ← [V4.3.1] 16MB SPI Flash 驱动 + L1-L4 四大防线 (206+62行)
│   │   ├── TFT_Font_Data.h                     ← ASCII 95字 + 中文 76字 + 图标 (356行, 待迁移至Flash)
│   │   ├── Esp8266_Driver.c/h                  ← USART2 + Try_Copy_Rx_Frame 原子接收 (257+49行)
│   │   ├── Adc_Driver.c/h                      ← ADC1 双通道 + 64样本滑动窗口 (162+20行)
│   │   ├── Inverter_Control.c/h                ← 软启动 + 频率斜坡 (146+67行)
│   │   ├── Key_Driver.c/h                      ← 4键 FSM + 批量事件读取 (137+40行)
│   │   ├── Led_Driver.c/h                      ← 6 LED 闪烁 (135+42行)
│   │   ├── Pwm_Driver.c/h                      ← TIM1 全桥 PWM 95-150kHz (113+33行)
│   │   └── Buzzer_Driver.c/h                   ← 蜂鸣器 (68+30行)
│   ├── User/                                   ← 应用层 — 1347 行
│   │   ├── App_Network.c/h                     ← WiFi OFFLINE 双模式+心跳+帧快照+遥测+OTA路由 (420+55行)
│   │   ├── App_Storage.c/h                     ← [V4.3.1] 字库索引+参数双副本+黑匣子+OTA推送 (400+120行)
│   │   ├── Sys_Core.c/h                        ← 状态枚举+初始化+安全(仅RUNNING) (205+44行)
│   │   ├── main.c                              ← 程序入口 (50行)
│   │   └── stm32f10x_it.c/h                    ← ISR (SysTick + USART2 ORE防锁死) (68+42行)
│   ├── System/ → Sys_Timer.c/h                 ← SysTick 1ms + DWT (48+36行)
│   ├── Start/  → CMSIS + system_stm32f10x
│   └── Library/ → SPL V3.5.0 (只读, 不可修改)
├── Arduino_Project/                            ← ESP8266 固件 — 522 行
│   └── ESP8266_MQTT_Firmware/...ino            ← WiFiManager+双MQTT+指令去抖+遥测+OFFLINE
├── ONENETapp/                                  ← 网页控制台 (Cloudflare Pages) — 3406 行
│   ├── index.html(410)/control.html(495)       ← 主页+控制+乐观更新+连接指示器
│   ├── monitoring.html(394)/history.html(527)  ← 监测+历史趋势图
│   ├── alerts.html(325)/settings.html(804)/login.html(149)
│   ├── js/onenet.js(188)                       ← OneNET API 核心 (安全: 无console/无Token泄露)
│   ├── js/config.js(65)/mobile-nav.js(31)      ← 数据模型+移动端导航
│   └── service-worker.js(28)                   ← PWA 离线回退 v3
├── 安卓app/                                    ← 微信小程序 — 1038 行 (6页面+Component)
│   ├── utils/config.js(47)/onenet.js(271)      ← 数据模型单一来源 + API层 (双请求在线检测)
│   ├── custom-tab-bar/                         ← 底部导航 Component (无高亮)
│   ├── pages/{index,monitoring,control,history,alerts,settings}/
│   ├── 操作手册.md / 部署文档.md               ← 小程序文档
│   └── docs/                                   ← 设计 spec
└── Claude_Files/                               ← AI 生成文档+工具
    ├── docs/                                   ← 开发者指南 + 技能文件 + specs
    ├── diagrams/                               ← Visio 流程图
    ├── tools/                                  ← generate_docx.js + ota_font_push.py 字库推送
    └── docs/                                    ← 设计 specs + 实施 plans
```

## 主循环

```c
int main(void) {
    Sys_Clamp_ESP(); Sys_Hardware_Init(); Sys_Startup_Screen(); Sys_Post_Init();
    g_sys_state = SYS_STATE_IDLE;
    while (1) {
        Key_Driver_Task(); Adc_Driver_Filter_Task(); App_Network_Task(); Sys_Safety_Task();
        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;
        }
        IWDG_ReloadCounter(); __WFI();
    }
}
```

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
- **CMD:CLEAR**: 清除配网凭证+ESP.restart(), 进入配网模式
- **指令去抖**: Mqtt_Task_Parse_Command 2s 窗口内相同 payload 直接丢弃
- **Switch 状态**: 仅 `s==2` (SS_DONE) 上报 true, `s==1` (SWEEP) 为过渡态不上报
- **遥测频率**: 仅在 running 时上报真实 F 值, 否则上报 0 (完全透传 STM32)
- **SetFreq 量化**: `(val/1000)*1000`, 与 STM32 PWM 1kHz 步进一致

## 网页端 (Cloudflare Pages)

- **乐观更新**: `setProperty` 成功后立即写 localStorage + 3s 乐观锁
- **重试**: `setProperty` 网络/业务错误各重试 3 次 (500ms/800ms)
- **连接指示**: 在线(绿) / 离线(黄) / 失败(红) / 未配置(灰), `/device/detail` 优先 + 数据非空兜底
- **数据模型**: `config.js` DEFAULT_DATA_MODEL → sensors(V/I/F) + controls(Switch/SetFreq)
- **频率映射**: `fromCloud: v => Math.floor(v/1000)` / `toCloud: v => v*1000`, Web 显示 kHz
- **安全**: 零 console 输出, 零硬编码 Token, login.html SHA-256 哈希验证
- **轮询**: 5s 间隔 setInterval + pagehide 清理, 无嵌套泄漏; 慢网下防重叠

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

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | 功能 | Pin | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | ADC_CH8 (电流, CC6920BSO) |
| PA1 | ESP8266 RST | PB1 | ADC_CH9 (电压) |
| PA2 | USART2_TX | PB3 | LED_PWM |
| PA3 | USART2_RX | PB4 | LED_WIFI |
| PA4 | TFT_CS | PB5 | PAGE 按键 (IPU) |
| PA5 | SPI1_SCK | PB6 | TFT 背光 TIM4_CH1 |
| **PA6** | **TFT_DC / Flash MISO (动态切)** | PB7 | F_DOWN 按键 (IPU) |
| PA7 | SPI1_MOSI | PB8 | F_UP 按键 (IPU) |
| PA8 | TIM1_CH1 | PB9 | ON/OFF 按键 (IPU) |
| PA9 | TIM1_CH2 | PB10 | PowerContrl (高=使能12V) |
| PA10 | LED_COM | PB11 | ESP8266 CH_PD (EN) |
| PA11 | LED_POWER | **PA12** | **W25Q128_CS (原 LED_TEMP 释放)** |
| PA15 | LED_SYSTEM | PB15 | 蜂鸣器 |

JTAG 禁用释放 PB3/PB4/PB5/PA15。

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
| SPI | Mode 3, 18MHz, DMA1_Channel3, **全双工** (V4.3.0: MISO 用于 Flash 读取) |
| 分辨率 | 160×128 横屏, MADCTL=0xA0 |
| SetWin 偏移 | X+1, Y+2 |
| 字库 | 8×16 ASCII (95) + 16×16 中文 (76) + 5×10 微数字 (12) |
| 字库位序 | 全部 LSB-first, 统一在 `TFT_Font_Data.h` |
| 图标 | WIFI(4+动画6帧), MQTT(3态+动画6帧), ICON_STAR |

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

所有页面统一: 左侧 `ON:确定` + 右侧 `PAGE:返回`。
SUB_MENU 和 FAULT 页面仅右侧 `PAGE:返回`。

## Safety

- **过流**: Sys_Safety 仅在 RUNNING 状态检测 >5.0A → SYS_FAULT + Buzzer BEEP
- **FAULT 恢复**: ON/OFF 单击 → `Soft_Start_Reset()` + `Sys_Safety_Reset_EMA()` → MAIN_MENU
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
| V4.3.1 | ESP8266 WiFiServer OTA 字库推送: PC→WiFi→ESP TCP透传→USART2→STM32 Base64解码+Page Program+CRC32校验, 首次上电自动灌字库的无线替代方案, 4处缓冲区128/256→512(容纳353B Base64帧), CRC32覆盖范围三计算点统一(data_size字段), seq越界RANGE拦截, ESP STATUS OTA门控防交叠 |
| V4.3.0 | W25Q128 16MB SPI Flash 集成: SPI1 分时复用(PA6动态切DC/MISO) + GB2312全字库(668KB)+开机画面区(1MB)+参数双副本CRC32(8KB)+黑匣子循环日志(4MB)+故障锁存前后5s + 四大硬件防线(L1写使能/L2 Busy死等/L3 DFF原子闪切/L4 发波禁擦) + ADC校准Flash固化+本地自测算B方案 + config.js getDataModel() undefined修复 + control.html clearInterval修复 + 遥测S字段对齐g_sys_state |
| V4.2.4 | 离线守卫全平台修复: Web+小程序 _isOnline 判定统一 + 缓存时序修正(延后到在线确认) + 在线兜底(data非空)+/device/detail覆写 + Web throw误触发修复 + 重复代码块清理 + 生命周期onHide/pagehide清理 |
| V4.2.3 | 全平台安全审查修复: 删除硬编码凭证+console清理+定时器泄漏修复+setInterval防重叠+小程序并行在线检测+STM32 Sys_Safety仅RUNNING+Key批量读取+login SHA-256 |
| V4.2.2 | WiFi OFFLINE 双模式(被动自动嗅探/主动手动恢复)+5次有限重试+BOOT_WAIT提前+MQTT超时+Bug修复8项 |
| V4.2.1 | 全项目 README 重写(4分支统一分支表) + CLAUDE.md 版本号规则流程扩展到全部文档 |
| V4.2.0 | TFT字库修复: CN_FONT[74..75] 失败→综合字模替换 + 底部栏简化 + 全平台版本号统一为 Vx.x.x |
| V4.1.0 | 小程序全重写: 单数据模型源+双API并行+动态卡片+底部栏Component+HTTP细化错误 |
| V4.0.0 | 9页面TFT UI + 全局状态机 + Sys_Safety独立安全 + EMA双级滤波 + 圆弧能量条仪表盘 + 16轮全链路审查 |
