---
name: embedded-architect
description: WPT_PWM V5.1.3 的 STM32、ESP8266、TFT、OneNET、网页、小程序、字库及发布一致性约束。涉及本项目嵌入式代码、硬件、协议、安全、目录或全栈同步时必须使用。
---

# WPT_PWM 嵌入式系统架构技能（V5.1.3）

## 1. 使用顺序

1. 先读取根目录 `AGENTS.md` 和 `WPT无线充电系统-从零搭建完整操作手册.md`。
2. 查看 `git status`，不得覆盖用户已有修改。
3. 先运行相关回归检查复现问题，再修改生产代码。
4. 固件改动必须同步检查 ESP8266、网页、小程序、桥接、字库工具和文档。
5. 完成后清理 Keil 中间产物，验证两个仓库，再提交和推送。

## 2. 当前系统基线

- 当前统一版本：`V5.1.3`，主仓库分支：`5.0`。
- MCU：STM32F103C8，SPL V3.5.0，ARMCC V5，禁止引入 HAL。
- 联网：ESP8266 只负责 WiFi/MQTT；STM32 只负责 PWM、ADC、安全和本地交互。
- 显示：ST7735 160×128，SPI1 与 W25Q128 分时复用。
- 频率：20.0–99.9kHz 步进 0.1kHz；100–200kHz 步进 1kHz。
- 保存的默认启动值：低档 20.0kHz，高档 100kHz；每档都可独立修改。
- 安全上限：5.0A；任何异常入口必须先关闭 PWM 和功率使能。

## 3. 架构边界

```text
STM32 Hardware/System/User
        ↕ USART2 文本协议
ESP8266 WiFi/MQTT
        ↕ OneNET
Web / 微信小程序
```

- `Hardware` 不依赖 `User`，`System` 提供基础时钟/校验，`User` 负责编排。
- 模块私有状态必须为 `static`；禁止跨模块 `extern` 私有变量和 `#include ".c"`。
- 中断只做采样、搬运、置位或快速关断，禁止显示、网络、Flash 擦写和阻塞等待。
- 周期任务使用无符号时间差，运行阶段禁止 `Delay_Ms()`。
- SPI1 所有切换必须经过 `Spi1_Shared`；TFT 与 W25Q128 不得各自抢占总线。
- STM32 不发送 AT 指令，ESP8266 不直接控制 PWM、PB10 或 ADC。

## 4. 状态与协议

| 状态 | S | PWM | 遥测频率 F |
|:---|:---:|:---:|:---:|
| IDLE | 0 | 关闭 | 0 |
| SWEEP | 1 | 开启 | 可不发布过渡帧 |
| RUNNING | 2 | 开启 | 实际 Hz |
| FAULT | 3 | 关闭 | 0 |

STM32 到 ESP8266 的唯一遥测格式：

```text
{"V":12.50,"I":0.35,"F":100000,"S":2}\n
```

允许的控制命令：

```text
CMD:ON
CMD:OFF
CMD:SETFREQ:<Hz>
CMD:WIFI_DISC
CMD:CLEAR
```

- 命令必须整帧精确匹配，禁止前缀误判。
- `SETFREQ` 必须先校验 20000–200000Hz，再校验所属档位步进。
- 网页和小程序展示 kHz，云端和串口传输 Hz。
- V/I 在空闲和故障状态仍表示真实采样；PWM 未运行时 F 必须为 0。
- 未配置云端时，小程序预览数据必须同时标记 `_isMock=true`、`_isOnline=false`，且不得触发报警。

## 5. STM32 编码与注释

- 所有业务 `.c` 第一段必须是中文文件头，包含 `@file`、中文 `@brief`、`@note V5.1.3`。
- 所有业务 `.h` 第一行直接进入 include guard；文件开头禁止注释。
- 新增和修改的注释使用中文，只解释原因、边界、并发或硬件风险。
- 公开函数声明使用中文 Doxygen：`@brief`、需要时写 `@param` 和 `@retval`。
- 禁止 `//`；统一使用 `/* ... */` 或 `/** ... */`。
- ARMCC V5 按 C89 编写：变量在代码块开头声明，避免 C99 语法。
- 中文字符串必须使用项目既有的 UTF-8 十六进制转义策略，避免 ARMCC 多字节警告。
- 公开函数命名 `Module_Name_Verb_Noun()`；静态函数同样带模块前缀。
- 静态变量 `s_name`，全局变量 `g_name`，宏和枚举值全大写。

## 6. 安全审查清单

- 上电默认 TIM1、MOE、PB10 全部关闭。
- 启动前必须经过上电稳定门控和配置语义校验。
- 过流快速路径与软件路径最终都进入相同的安全关断结果。
- `FAULT`、HardFault、NMI、BusFault、UsageFault 首动作必须关 PWM。
- TIM1 更新使用原子策略，互补输出、死区和 50% 占空基线不可破坏。
- ADC DMA/触发窗口、EMA、报警阈值的单位必须一致。
- Flash 写入不得跨分区；运行时禁止破坏字库、配置和黑匣子边界。
- 串口环形缓冲必须处理溢出、整帧快照和 ORE，不能让调试文字污染协议。
- 所有长度、地址、频率、枚举和云端输入先校验再使用。

## 7. UI 与客户端一致性

- 设置页保留语言、字间距、图标、亮度和配色；返回键退出，确定键进入或保存。
- 两个启动频率档独立保存；进入运行后从当前档保存值开始动态扫频。
- 电压、电流、频率使用各自独立、分段递增且符合量程的表盘。
- 网页与小程序必须跟随 STM32 的真实范围、步进、状态和单位。
- 受保护网页先加载 `js/auth-guard.js`；持久登录最长 7 天，会话登录随浏览器会话结束。
- Token、密码、API Key 不得写入仓库；日志不得输出凭证。
- 轮询必须防重入，并在页面隐藏、卸载或离开时清理定时器。

## 8. 文件位置约束

| 内容 | 唯一允许位置 |
|:---|:---|
| STM32 工程 | `Keil_Project/` |
| ESP8266 固件 | `Arduino_Project/` |
| 网页端独立仓库 | `ONENETapp/` |
| 微信小程序与可选桥接 | `安卓app/` |
| 字库生成和烧录 | `ch341/` |
| 回归检查 | `tests/`、`Keil_Project/tests/` |
| 主动维护脚本 | `tools/` |
| 项目技能 | `.agents/skills/` |
| 唯一总操作手册 | 根目录 `WPT无线充电系统-从零搭建完整操作手册.md/.docx` |
| 历史报告、旧方案、旧图纸 | `NONFILE/` |

禁止重新创建 `Claude_Files/`、`docs/superpowers/`，禁止把任务报告散落到根目录。

## 9. 版本同步规则

逻辑修复或安全修复提升补丁号；新增跨平台大功能提升中版本。发版时至少同步：

- STM32 业务 `.c` 文件头和 TFT 开机版本；
- ESP8266 文件头；
- 小程序所有活动 JS/WXML/WXSS；
- 网页页面元数据、JS、CSS、PWA 缓存和网页 README；
- 本地桥接 `package.json`；
- 字库生成/烧录脚本；
- 根 README、AGENTS、CLAUDE、总手册和本技能。

历史记录可以保留旧版本号，不允许把历史版本机械替换成当前版本。

## 10. 验证与推送

至少执行：

```powershell
node --test tests/client-model.test.cjs tests/bridge-core.test.mjs
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_release.ps1
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_fullstack.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_3.ps1 -Scope Static
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_task4_gauge.ps1
```

- 能使用 Keil 时再做真实构建；不能构建必须明确说明，禁止伪造“零错误”。
- 推送前运行 `Keil_Project/keilkill.bat`，确认无 `.obj`、`.lst`、`.axf`、`.hex` 等产物。
- 先提交并推送 `ONENETapp` 的 `master` 与 `gh-pages`，再提交主仓库的子模块指针。
- 主仓库只推送 `origin 5.0`；不得提交 `.claude/settings.local.json`、`.codex/` 或本地凭证。

## 11. 本轮执行教训

- 文档声称存在登录守卫不等于页面实际加载了守卫；必须用页面级契约测试验证。
- 模拟数据、缓存数据、离线数据必须使用不同标记，不能靠“字段不存在”推断状态。
- ESP8266 与 STM32 共用协议串口时，调试输出必须编译期关闭并集中封装。
- 文件迁移后必须更新脚本、测试、README 和技能里的路径，不能只移动目录。
- “全面同步”以协议、范围、状态和测试为准，不以相同版本字符串为准。
