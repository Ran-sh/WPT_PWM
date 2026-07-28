# AGENTS.md

本文件是 WPT_PWM 仓库的唯一代理执行约束。当前版本 `V5.1.3`，主仓库分支 `5.0`。

## 1. 项目入口

| 项目 | 位置/地址 |
|:---|:---|
| 主仓库 | `https://github.com/Ran-sh/WPT_PWM` |
| 网页独立仓库 | `ONENETapp/` → `https://github.com/Ran-sh/WPT_Onenet_IoT` |
| 在线网页 | `https://wptonenet.483763727.workers.dev/` |
| 总操作手册 | `WPT无线充电系统-从零搭建完整操作手册.md` |
| 嵌入式技能 | `.agents/skills/embedded-architect/SKILL.md` |

处理 STM32、ESP8266、硬件、协议、TFT、字库或全栈同步时，必须先完整读取项目嵌入式技能。

## 2. 当前版本与范围

- 统一版本：`V5.1.3`，发布日期：2026-07-26。
- STM32：STM32F103C8、SPL V3.5.0、ARMCC V5、Keil MDK V5。
- ESP8266：Arduino 固件，负责 WiFiManager、OneNET MQTT 和串口桥接。
- 客户端：Cloudflare Workers 网页、微信小程序、可选本地 MQTT 桥接。
- 频率：20.0–99.9kHz/0.1kHz；100–200kHz/1kHz。
- 默认启动频率：低档 20.0kHz，高档 100kHz；两档独立保存。
- 当前修订重点：网页登录守卫、串口调试隔离、小程序预览状态、统一目录和文档。

任何活动代码、版本说明和测试不得落后于 STM32 的范围、步进、状态和协议。

## 3. 文件位置铁律

| 内容 | 正确位置 |
|:---|:---|
| STM32 工程 | `Keil_Project/` |
| ESP8266 固件 | `Arduino_Project/` |
| 网页端 | `ONENETapp/`（独立 Git 仓库） |
| 微信小程序/可选桥接 | `安卓app/` |
| W25Q128 字库工具 | `ch341/` |
| 活动脚本 | `tools/` |
| 跨平台检查 | `tests/` |
| STM32 静态检查 | `Keil_Project/tests/` |
| 技能 | `.agents/skills/` |
| 唯一用户操作文档 | 根目录总操作手册 `.md/.docx` |
| 历史报告、旧方案、旧图纸 | `NONFILE/` |

禁止重新创建 `Claude_Files/`、`docs/superpowers/`，禁止在根目录新建 `task-*-report.md`。`.claude/`、`.codex/`、`.worktrees/` 属于本地工具状态，不得移动、删除或提交其本地修改。

- `NONFILE/README.md` 是归档规则，不属于历史快照；其余归档文件名必须以 `-Vx.y.z` 作为扩展名前的尾缀。
- `node_modules/`、`__pycache__/`、生成字库镜像、Keil 中间产物和本机生成的 `Target *.BAT` 不得提交。
- 当前目录说明只能列出实际存在的入口；已删除路径只能作为明确的禁用约束，不得写成当前文件。

## 4. STM32 注释与编码

- 所有业务 `.c` 文件开头必须有中文 Doxygen 文件头，包含 `@file`、中文 `@brief`、`@note V5.1.3`。
- 所有业务 `.h` 第一行直接进入 include guard，文件开头禁止注释。
- 新增或修改的注释全部使用中文；只解释原因、边界、风险和并发约束。
- 禁止 `//`；使用 `/* */` 或 `/** */`。
- 公开函数声明写中文 `@brief`，按需要补 `@param`、`@retval`。
- ARMCC V5 按 C89 编写；变量在代码块开头声明，不引入 HAL 或 C99 专属语法。
- `.h` 只公开接口；`.c` 内部实现必须 `static`。
- 公开/静态函数均使用模块前缀；静态变量 `s_`，全局变量 `g_`。
- 禁止修改 `Keil_Project/Library/` 和 `Keil_Project/Start/` 的第三方基线，除非明确修复启动代码且有证据。

## 5. 固件架构铁律

```text
STM32（PWM/ADC/安全/UI） ←USART2→ ESP8266（WiFi/MQTT） ←→ OneNET ←→ Web/小程序
```

- STM32 不发送 AT 指令；ESP8266 不碰 PWM、PB10 和 ADC。
- Hardware → System → User 单向分层，禁止跨层读取模块私有变量。
- SysTick 为 1ms；运行期周期任务使用无符号时间差，禁止阻塞延时。
- 中断只做快速关断、采样、搬运或置位；显示、网络和 Flash 写入放在主循环。
- SPI1 必须通过 `Spi1_Shared` 在 TFT 与 W25Q128 之间仲裁。
- 上电时 TIM1、MOE、PB10 默认关闭；异常入口第一动作关闭 PWM。
- 过流阈值 5.0A；FAULT 恢复必须重置软启动和安全滤波。
- TIM1 互补输出、50% 占空和死区基线不可擅自改变。

## 6. 状态、频率和协议

| 状态 | S | PWM | F |
|:---|:---:|:---:|:---:|
| IDLE | 0 | 关 | 0 |
| SWEEP | 1 | 开 | TIM1 实际 Hz |
| RUNNING | 2 | 开 | 实际 Hz |
| FAULT | 3 | 关 | 0 |

遥测固定为一行 JSON：

```text
{"V":xx,"I":xx,"F":xx,"S":x}\n
```

控制命令仅允许：`CMD:ON`、`CMD:OFF`、`CMD:SETFREQ:<Hz>`、`CMD:WIFI_DISC`、`CMD:CLEAR`。所有输入必须整帧匹配、先校验范围再校验步进。网页和小程序显示 kHz，云端和串口使用 Hz。

## 7. UI 与客户端约束

- TFT 设置页固定为语言、启动频率、字间距、光标图标、配色五项；亮度设置页已移除，PA12 背光固定开启。
- 两个启动频率档独立设置；低档从99.9kHz、高档从200kHz扫到所选档保存目标，启动时锁定本轮档位和目标。
- 电压、电流、频率表盘独立，刻度采用符合量程的分段递增设计。
- 网页和小程序的数据模型必须与 STM32 的 20–200kHz、双档步进、5A 边界一致。
- 未配置 OneNET 时，小程序只显示预览数据，不得标记在线或触发报警。
- 网页受保护页面必须在业务脚本前加载 `js/auth-guard.js`。
- 所有轮询防重入，并在隐藏、卸载或离开页面时清理定时器。
- Token、密码、API Key 不写入仓库、不输出到日志。

## 8. 版本同步铁律

当前版本只在“当前版本字段”和活动文件中统一；历史日志保留原版本。发版必须同步：

1. STM32 业务 `.c` 文件头和 TFT 开机版本。
2. ESP8266 文件头和协议说明。
3. 小程序所有活动 JS/WXML/WXSS、桥接 `package.json`。
4. 网页 7 页面元数据、JS、CSS、Service Worker、manifest 和网页 README。
5. ch341 生成/烧录脚本及说明。
6. 根 README、AGENTS、CLAUDE、总手册、项目嵌入式技能。
7. 版本和协议回归检查。

## 9. 验证顺序

```powershell
node --test tests/client-model.test.cjs tests/bridge-core.test.mjs
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_release.ps1
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_fullstack.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_3.ps1 -Scope Static
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_1_hardening.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_task4_gauge.ps1
```

- Keil 真实构建只能在可用的 Keil/ARMCC 环境执行；构建产物不存在时必须如实说明。
- Python 工具执行语法检查；JSON 全部解析；网页契约测试全部通过。
- 依赖审计若需要联网，必须明确执行结果，不能把未运行写成通过。

## 10. Git 提交与推送

每次主仓库推送前：

1. 运行 `cmd.exe /c Keil_Project\keilkill.bat`。
2. 检查无 `.obj`、`.lst`、`.axf`、`.hex`、`.map`、`._ia` 等 Keil 产物。
3. 先在 `ONENETapp/` 提交并推送 `master`，再快进 `gh-pages` 并推送。
4. 回到主仓库提交网页仓库指针和其他文件。
5. 推送 `origin 5.0`。

不得提交 `.claude/settings.local.json`、`.codex/`、本地进程状态、Token、密码或 API Key。

## 11. V5.1.3 变更记录

- 修复 6 个受保护网页缺少真实登录守卫的问题。
- 登录状态区分会话登录和最长 7 天持久登录，退出时同时清理。
- ESP8266 调试输出默认编译关闭，防止污染 STM32 串口协议。
- 小程序未配置云端时明确区分预览、离线和在线，不再误触发报警。
- 统一项目目录、工具路径、文档入口和 NONFILE 归档约束。
- 更新项目 `embedded-architect` 技能，补齐全栈同步和真实验证规则。
