# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `5.0` |
| **版本** | V5.1.2 |
| **语言** | 中文交流，业务代码注释使用中文 |

> **V5.1.2** (2026-07-26) — 全链路频率、安全、生命周期与数据一致性优化
> **V5.1.1** (2026-07-26) — 显示、字库、配置、命令边界及功率安全路径加固
> **V5.1.0** (2026-07-22) — 五项设置 + 双档启动频率 + 全局菜单光标 + 递增式独立表盘
> **V5.0.2** (2026-07-19) — STM32 全面可靠性优化: 功率互锁 + 500Hz ADC + SPI1 仲裁 + Blackbox V2 + 中断发送 + 统一调度
> **V5.0.1** (2026-07-11) — GPIO 全量重映射 + 5键系统 + 四灯系统 + Bug修复
> **V5.0.0** (2026-07-11) — 初始 GPIO 重映射: PA12→TFT_BL, PB12→W25Q128_CS, KEY0-KEY4 五键, WIFI/POWER/STATUS/HEARTBEAT 四灯
> **V4.5.2** (2026-07-11) — SPI 时序回归修复

> **详细开发者指南**: `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` (V5.1.2)
> **架构师技能文件**: `Claude_Files/docs/embedded-architect-system-prompt.md`
> **W25Q128 Flash CH341A 烧录指南**: `ch341/README.md` (V5.1.2)

## 版本号规则 (全项目铁律)

```
Vx.y.z 三数字体系 (首位 x 固定为 5, 对应分支 5.0):
  x — 固定 5 (GPIO 全量重映射 + 5键系统升级)
  y — 中版本: 新增页面/大功能/全平台重写 时 +1
  z — 小版本: Bug修复/字库修正/底部栏调整/文档更新 时 +1

当前版本: V5.1.2

涉及版本号的位置 (全项目必须统一):
  STM32 .c 文件头注释: Keil_Project 下每个业务 .c 的 @brief/@note 行 → V5.1.2
  STM32 .h 文件: 第一行必须直接为 include guard，不添加文件头注释
  文档控制信息: 开发指南/技能文件的当前文档版本 → V5.1.2
  CLAUDE.md: 版本号 + 审查历史 + 当前架构说明 → V5.1.2
  README.md: badge + 版本历史 + 分支表 → V5.1.2
  CH341A 指南: 接线、CRC 和共享 SPI 说明 → V5.1.2
  历史版本记录保留原号，不做机械替换

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
| `Keil_Project/System/` 下全部 .c/.h | Sys_Timer: SysTick 1ms；Checksum: CRC32/CRC8 |
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
| `D:\Claude Code Project\WPT_PWM_V5.0\CLAUDE.md` | 版本号、文件结构、审查历史追加、新增/变更模块说明 |

### 第 4 条 — 更新开发指南

| 针对文件 | 写入内容 |
|:---|:---|
| `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` | 文档版本号、修改日志、引脚表（含W25Q128接线）、文件结构、UI 页面、EMA/Safety/网络协议等架构章节与当前代码对齐 |
| `ch341/README.md` | [V5.1.2] CH341A Flash 字库烧录完整操作指南: PB12接线/驱动安装/生成→备份→分区烧写→完整校验全流程 |

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
| 提交 | `git add -A && git commit -m "docs: Vxx — <变更摘要>" && git push origin 5.0` |

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
TIM1 PWM 20~200kHz          WiFi + MQTT 自动联网
ADC 双通道 + 64样本滑动窗口   STATUS:ONLINE 心跳
TFT/KEY/LED 人机交互        CMD:ON/OFF/SETFREQ 控制
W25Q128 16MB Flash 外挂      USART2 115200 纯文本 JSON
Sys_Safety 独立安全监测
        │                          │
        └────── USART2 JSON ───────┘
```

**铁律**: STM32 不发 AT 指令, ESP 不碰 PWM/ADC。开机自动联网。
**V5.0.2**: W25Q128 Flash 全字库 20897 字使用统一 `Checksum_CRC32()` 校验，无效时回退 ROM 4 个必要汉字；`Spi1_Shared` 统一管理 TFT/W25Q128 所有权、模式切换、双 CS 和超时恢复。`Tft_Driver_Font_Init()` 必须在 `W25Q_Driver_Init()` 之后调用。
**V5.0**: SPI1 分时复用 (TFT + W25Q128), PA5=SCK, PA7=MOSI, PA6=DC/MISO动态切, PA4=TFT_CS, PB12=FLASH_CS双门控；PA12仅作GPIO背光。

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
| SWEEP | 开 | + Soft_Start（按锁定低频或高频档降至保存目标） |
| RUNNING | 开 | + Freq_Ramp |
| FAULT | 关 | + FAULT UI, 取消所有斜坡 |

## 文件结构

```
WPT_PWM_V5.0/
├── Keil_Project/                               ← STM32 固件 (不含只读 Library/Start)
│   ├── Project.uvprojx                         ← 工程入口, F7编译→F8下载
│   ├── keilkill.bat                            ← 清理编译产物 (push前必执行)
│   ├── Hardware/
│   │   ├── Ui_Controller.c/h                   ← 15页面 UI + 双档设置 + 递增式独立表盘
│   │   ├── Tft_Driver.c/h                      ← ST7735 DMA + Flash/ROM 双路径字库 + SPLASH
│   │   ├── Spi1_Shared.c/h                     ← TFT/W25Q128 总线所有权与超时恢复
│   │   ├── W25Q_Driver.c/h                     ← 16MB Flash边界检查/超时/二分检索
│   │   ├── TFT_Font_Data.h                     ← ASCII 95字 + 中文4字 + 图标 (ROM回退)
│   │   ├── Esp8266_Driver.c/h                  ← USART2 RX帧队列 + TX中断环形缓冲
│   │   ├── Adc_Driver.c/h                      ← TIM3 500Hz + DMA + 64点显示/8点安全窗口
│   │   ├── Inverter_Control.c/h                ← 软启动 + 频率斜坡
│   │   ├── Key_Driver.c/h                      ← 5键 FSM + 独立双击/长按能力
│   │   ├── Led_Driver.c/h                      ← 4 LED (WIFI/POWER/STATUS/HEARTBEAT)
│   │   ├── Pwm_Driver.c/h                      ← TIM1 全桥 PWM 20-200kHz原子更新
│   │   └── Buzzer_Driver.c/h                   ← 蜂鸣器
│   ├── User/
│   │   ├── App_Network.c/h                     ← OFFLINE双模式+心跳+S=0/1/2/3遥测
│   │   ├── App_Storage.c/h                     ← 后台校验保存 + Blackbox V2
│   │   ├── Sys_Core.c/h                        ← 统一功率/故障API + 硬互锁 + 公共调度器
│   │   ├── main.c                              ← 程序入口和状态分发
│   │   └── stm32f10x_it.c/h                    ← SysTick/ADC DMA/USART2 ISR
│   ├── System/ → Sys_Timer.c/h + Checksum.c/h  ← SysTick 1ms + CRC32/CRC8
│   ├── Start/  → CMSIS + system_stm32f10x
│   └── Library/ → SPL V3.5.0 (只读, 不可修改)
├── Arduino_Project/                            ← ESP8266 固件
│   └── ESP8266_MQTT_Firmware/...ino            ← WiFiManager+双MQTT+指令去抖+遥测+OFFLINE
├── ONENETapp/                                  ← 网页控制台 (Cloudflare Pages)
│   ├── index.html/control.html                 ← 主页+控制+乐观更新+连接指示器
│   ├── monitoring.html/history.html            ← 监测+历史趋势图
│   ├── alerts.html/settings.html/login.html
│   ├── js/onenet.js                            ← OneNET API 核心 (超时/校验/跨日历史)
│   ├── js/config.js                            ← 数据模型迁移+XSS防护+频率换算
│   ├── js/mobile-nav.js
│   └── service-worker.js                       ← 可选缓存失败不阻断安装
├── 安卓app/                                    ← 微信小程序 (6页面+Component)
│   ├── utils/config.js/onenet.js               ← 数据模型迁移 + API层 (双请求在线检测)
│   ├── custom-tab-bar/                         ← 底部导航 Component (无高亮)
│   ├── pages/{index,monitoring,control,history,alerts,settings}/
│   ├── server/                                 ← 带API密钥和输入校验的可选HTTP-MQTT桥接
│   ├── 操作手册.md / 部署文档.md               ← 小程序文档
│   └── docs/                                   ← 设计 spec
├── ch341/                                      ← [V5.1.2] CH341A Flash 字库烧录工具链
│   ├── README.md                               ← 完整操作指南
│   ├── requirements.txt                        ← Python 依赖: pillow
│   ├── generate_font.py                        ← 全字库生成器 (20897 CJK + 95 ASCII + 31 图标)
│   ├── burn_flash.py                           ← 新备份→仅写2MB字库分区→完整读回校验
│   ├── layout.txt                              ← Flashrom 字库/保留区布局
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
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;  /* 扫频: 按锁定档位软启动 */
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;  /* 运行: 频率闭环 + 调度 */
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;  /* 故障: 过流保护 + 等待复位 */
        }
    }
}
```

**V5.0.2 调度**: `Sys_Core_Run_Common()` 统一执行不变量检查、ADC、安全、按键/UI、网络、Blackbox、后台存储、LED、蜂鸣器、IWDG 和 WFI；各 `Sys_Run_*()` 只注入状态专属动作。

## Sys_Safety (安全监测, 独立于 UI)

- **采样链**: TIM3 TRGO 500Hz触发 ADC1+DMA；64点窗口用于显示，8点窗口用于快速安全电流
- **PB10 电源**: 只由 KEY0 手动切换；关电顺序固定为先停 TIM1/MOE，再拉低 PB10
- **启动门控**: 仅 IDLE、PB10 已开、ADC校准 READY、采样新鲜且无故障锁存时允许启动
- **过流检测**: SWEEP/RUNNING 均检测安全电流，连续3个新样本 >5.0A 才锁存FAULT
- **故障处理**: 首故障锁存原因并冻结快照，PWM和12V均强制关闭，KEY0不能绕过故障重新上电

## EMA 双级滤波链

| 层级 | 模块 | 滤波对象 | 用途 |
|:---|:---|:---|:---|
| 安全级 | `Adc_Driver` 8点窗口 | DMA原始电流 | SWEEP/RUNNING快速过流保护 |
| 显示级 | `Adc_Driver` 64点窗口 + UI EMA | DMA原始V/I | UI仪表盘 + 综合监测页 |
| 数字量 | `Pwm_Driver_Get_Frequency()` | 无滤波 | 频率（零迟滞, 保证调频跟手） |

## App_Storage / Blackbox V2

| 区域 | 地址 | 规则 |
|:---|:---|:---|
| 配置A/B | `0x300000` / `0x301000` | RAM请求、仅IDLE写入、回读CRC32校验 |
| 元数据A/B | `0x310000` / `0x311000` | 双扇区generation日志，启动择新并前向恢复 |
| 循环日志 | `0x312000` ~ `0x6CFFFF` | 12B/条、CRC8、200ms、每60条检查点 |
| 故障槽 | `0x6D0000` ~ `0x70FFFF` | 64×4KB，故障前25条+后25条（各5秒） |

Flash擦写约束：SWEEP/RUNNING禁止擦除；配置保存只在IDLE后台推进；故障快照仅在TIM1和PB10均确认关闭后落盘。所有CRC32调用统一委托`System/Checksum`。

## App_Network WiFi 连接与离线

- **状态机**: IDLE→WIFI→MQTT→ONLINE, 新增 OFFLINE_PASSIVE(被动断开自动嗅探)/OFFLINE_ACTIVE(主动断开需手动ON)
- **被动离线**: 热点断开后重试 5 次耗尽→OFFLINE_PASSIVE→被动监听 ESP STATUS 帧, 热点恢复自动重连
- **主动离线**: 用户按键/配网页 断开→OFFLINE_ACTIVE→忽略所有帧, 需手动 ON 恢复
- **重试**: 5次有限重试，前3次间隔5s、后2次间隔15s；耗尽后进入被动离线，不复位仍在运行的ESP
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
- **遥测频率**: SWEEP/RUNNING透传真实F值，IDLE/FAULT为0；Switch仍仅S=2时为true
- **SetFreq 量化**: 20.0–99.9kHz按100Hz量化，100–200kHz按1kHz量化，越界值拒绝
- **安全加固**: Token占位符化、配网热点加密码、公共MQTT默认关闭且启用时必须配置鉴权密钥；串口超长帧整帧丢弃，命令严格匹配

## 网页端 (Cloudflare Pages)

- **登录守卫**: 所有 6 个受保护页面顶部内置 `lastLoginTime` 检查，7 天过期后自动跳转 `/login.html`
- **XSS 防护**: `config.js` 提供 `escapeHtml()` 函数，所有 `innerHTML` 插值前必须通过此函数转义用户可控字符串
- **乐观更新**: `setProperty` 成功后立即写 localStorage + 3s 乐观锁; **V4.5.1: 重试全部失败后回滚乐观缓存**
- **重试**: `setProperty` 网络/业务错误各重试 3 次 (500ms/800ms)
- **连接指示**: 在线(绿) / 离线(黄) / 失败(红) / 未配置(灰), `/device/detail` 优先 + 数据非空兜底
- **数据模型**: `config.js` DEFAULT_DATA_MODEL → sensors(V/I/F) + controls(Switch/SetFreq)
- **频率映射**: `fromCloud`保留0.1kHz精度，`toCloud`换算为Hz；允许20–200kHz并由ESP按双档步进量化
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
- **可选桥接**: 本地桥接默认要求API密钥，限制请求体、来源和命令范围；启动/停止脚本只管理自身PID

## 全链路数据一致性铁律

| 状态 | STM32 遥测 | ESP 上报 | Web/小程序显示 |
|:---|:---|:---|:---|
| IDLE | V=真实,I=真实,F=0,S=0 | Switch=false, V/I=真实, F=0 | 停机/V/I 正常/F=0 |
| SWEEP | V/I=显示滤波,F=真实Hz,S=1 | Switch=false, V/I/F=真实 | 扫频中/实时值 |
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
- `.c` 文件开头必须带中文 `@file` + `@brief` + `@note`
- `.h` 第一行必须直接为 `#ifndef`，文件开头不添加注释；公开接口的中文Doxygen注释放在声明前
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
| **字库路径** | **Flash 20897 字 (`Checksum_CRC32`, refin=false) → ROM 4 个必要汉字 (自动回退)** |
| 字库位序 | 全部 LSB-first, 统一在 `TFT_Font_Data.h` / `generate_font.py` (无 bit_reverse) |
| 图标 | WIFI(4+动画6帧), MQTT(3态+动画6帧), ICON_STAR, 20 新图标 |
| **开机动画** | **SPLASH: 纯代码实现 (背光渐亮 + 逐字点亮 ~4.8s), 不依赖 W25Q, 版本号右下角** |

ROM 中文表只保留启动/故障回退所需 4 字；完整中文显示依赖通过 CRC32 校验的 W25Q128 字库。

## PWM 基线 (不可改)

- TIM1 CH1=PWM1 + CH2=PWM2, Up 模式, 50% 占空, `TIM_OCNPolarity_Low`
- 死区 1000ns, 20-200kHz, UDIS 原子更新
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

### GaugeConfig 四套分段表

| 表盘 | 第1段 | 第2段 | 第3段 | 警告/报警起点 |
|:---|:---|:---|:---|:---|
| 电压 V | 0–20V / 格2V | 20–40V / 格5V | 40–50V / 格10V | 36V / 42V |
| 电流 C | 0–1A / 格0.1A | 1–3A / 格0.5A | 3–5A / 格1A | 4A / 4.5A |
| 低频 F | 20–50kHz / 格5kHz | 50–80kHz / 格10kHz | 80–100kHz / 格20kHz | 无红区 |
| 高频 F | 100–140kHz / 格5kHz | 140–180kHz / 格10kHz | 180–200kHz / 格20kHz | 无红区 |

频率页按当前启动档位选择低频或高频表；停机时能量条归零并显示灰色“0”。

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

- **过流**: SWEEP/RUNNING 使用8点安全窗口，连续3个新样本 >5.0A → SYS_FAULT + Buzzer BEEP
- **FAULT 恢复**: KEY4单击走 `Sys_Core_Reset_Fault()`，保持PB10关闭并回到MAIN_MENU
- **FAULT 防重触发**: 首故障原因锁存；故障态禁止KEY0重新接通12V
- **远程启停 UI 同步**: CMD:ON/OFF → `Ui_Controller_Force_Page_And_Reset()`
- **上电**: TIM1 全关, PB10 拉低关 12V
- **看门狗**: IWDG按LSI容差约1.6~2.4s, 调试自动暂停
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

/* 分段配置：每个视觉格等角度，各段改变每格代表的物理量 */
GAUGE_V_SEGMENTS      = {{0,20,2}, {20,40,5}, {40,50,10}};
GAUGE_C_SEGMENTS      = {{0,1,0.1}, {1,3,0.5}, {3,5,1}};
GAUGE_F_LOW_SEGMENTS  = {{20,50,5}, {50,80,10}, {80,100,20}};
GAUGE_F_HIGH_SEGMENTS = {{100,140,5}, {140,180,10}, {180,200,20}};

/* 信息舱: 状态(Row4) → 数值(Row5,黄) → 标签(Row6,青) */
```

## 审查历史

| 版本 | 重点修复 |
|:---|:---|
| V5.1.2 | **全链路优化**: ESP8266双档频率量化、指令精确匹配与串口溢出保护；小程序/网页数据模型迁移、跨日历史和轮询恢复；本地桥接鉴权、输入验证及最小化CORS；CH341A每次新备份、完整2MB校验和字库分区写入；项目说明与回归检查同步。 |
| V5.1.1 | **全面加固**: 外置图标动态布局、主题擦除和配色越界修复；字库V2完整负载CRC；配置语义校验；整帧命令解析；200ms上电稳定门控、ADC模拟看门狗、异常最小化关断、2KB栈和实际输出频率跟踪。 |
| V5.1.0 | **设置与表盘重构**: 设置菜单固定为语言、启动频率、字符间距、光标图标、配色方案；V2配置保存低频20.0–99.9kHz与高频100–200kHz双档、当前档位和全局光标并可迁移V1；PWM边界统一为20–200kHz；软启动按低档99.9kHz/100Hz或高档200kHz/1kHz每10ms降频；独立表盘采用共享分段递增映射和2倍主数值差分刷新。 |
| V5.0.2 | **STM32全面优化**: TIM1原子更新；统一功率/故障API与PB10硬互锁；SWEEP/RUNNING连续3样本过流；TIM3 500Hz ADC双窗口+校准/新鲜度门控；SPI1共享仲裁；W25Q边界/超时；后台校验保存；Blackbox V2双元数据+循环恢复+故障前后5秒快照；按键双击/长按能力拆分；14页UI与GPIO背光清理；TFT增量刷新；USART2 TX中断环；遥测S=0/1/2/3；统一调度与超时/C89清理 |
| V5.0.1 | **GPIO 全量重映射 + 5键系统 + 四灯系统**: PA12→TFT_BL(GPIO), PB12→W25Q128_CS, PB6→KEY3, PB9→KEY0(电源开关协调PB10), PB8→KEY1(返回,双击主菜单), PB7→KEY2(UP), PB6→KEY3(DOWN), PB5→KEY4(确定); PA15→STATUS(PWM指示), PB3→POWER(12V), PC13→HEARTBEAT(板载运行); PA10/PA11 移除; TIM4 停用; PB10 手动(去自动电压阈值); Key_Driver 4→5键; Led_Driver 5→4灯; Ui_Controller MENU UP键 wrapping 修复(<=1→==0) |
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
