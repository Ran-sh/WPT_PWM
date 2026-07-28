---
name: embedded-architect
description: 维护 WPT_PWM V5.1.3 的 STM32F103、ESP8266、TFT/W25Q128、OneNET、网页、微信小程序、桥接和发布一致性。处理本项目的嵌入式代码、功率安全、PWM/ADC、软启动扫频、串口协议、UI、目录清理、版本同步、回归验证或 Git 发布时使用。
---

# WPT_PWM 嵌入式架构

## 1. 开始前

1. 完整读取根目录 `AGENTS.md`；只读取总手册中与当前任务有关的章节。架构、文档整合或发布任务再通读总手册。
2. 查看主仓库和 `ONENETapp/` 的状态，区分用户已有修改、当前任务修改和生成物。
3. 先用最接近问题的回归检查复现，再修改生产代码；纯审查任务不得擅自写文件。
4. 以当前代码和可执行契约测试为事实来源；若活动文档与实现冲突，同时修正文档和本技能。
5. 不读取、覆盖或提交 Token、密码、API Key、`.claude/settings.local.json`、`.codex/`、`.worktrees/`。

事实优先级：生产代码与测试 > `AGENTS.md` > 总操作手册 > README > `NONFILE/` 历史快照。

## 2. 当前基线

- 项目版本 `V5.1.3`，主仓库分支 `5.0`。
- STM32F103C8 + SPL V3.5.0 + ARMCC V5/C89；禁止引入 HAL。
- ST7735 160×128；TFT 与 W25Q128 共用 SPI1，统一由 `Spi1_Shared` 仲裁。
- 低档 20.0–99.9kHz，设置步进 0.1kHz；高档 100–200kHz，设置步进 1kHz。
- 默认保存目标：低档 20.0kHz，高档 100kHz；两档独立持久化。
- 过流软件阈值 5.0A；异常入口先关 PWM/MOE，再关闭 PB10 功率使能。

## 3. 架构边界

```text
STM32（PWM/ADC/安全/UI/存储）
        ↕ USART2 一行一帧
ESP8266（WiFi/MQTT/命令转换）
        ↕ OneNET
网页 / 微信小程序
```

- 保持 `Hardware → System → User` 单向依赖；禁止跨模块 `extern` 私有变量和 `#include ".c"`。
- 模块私有状态使用 `static`；公开接口由头文件声明。
- STM32 不发送 AT 指令；ESP8266 不直接操作 PWM、PB10 或 ADC。
- 中断只做采样、搬运、置位或快速关断；禁止显示、联网、Flash 擦写和阻塞等待。
- 周期任务使用无符号时间差；初始化完成后禁止 `Delay_Ms()`。
- TFT/W25Q128 访问必须先取得共享 SPI 所有权，失败时释放双 CS 并恢复总线。

## 4. 状态、扫频和协议

| 状态 | S | PWM | Switch | 遥测 F |
|:---|:---:|:---:|:---:|:---:|
| IDLE | 0 | 关 | false | 0 |
| SWEEP | 1 | 开 | false | TIM1 实际 Hz |
| RUNNING | 2 | 开 | true | TIM1 实际 Hz |
| FAULT | 3 | 关 | false | 0 |

遥测固定为一行 JSON：

```text
{"V":12.50,"I":0.35,"F":100000,"S":2}\n
```

控制命令只允许完整匹配：

```text
CMD:ON
CMD:OFF
CMD:SETFREQ:<Hz>
CMD:WIFI_DISC
CMD:CLEAR
```

- V/I 在所有状态表示有效物理采样；IDLE/FAULT 只把 F 置 0。
- 低档软启动从 99.9kHz 扫到该档保存目标，高档从 200kHz 扫到该档保存目标；启动时锁定档位与目标。
- 运行期 `SETFREQ` 只改变本轮目标，不覆盖下次启动的两档保存值。
- 频率输入先检查 20000–200000Hz，再检查低档 100Hz/高档 1000Hz 步进。
- 网页和小程序显示 kHz；串口、MQTT 和 OneNET 属性使用 Hz。
- ESP8266 生产固件保持 `ESP8266_DEBUG_ENABLED=0`，协议串口不得混入调试文本。

## 5. 功率安全红线

- 上电默认关闭 TIM1、MOE 和 PB10；启动必须通过功率稳定、ADC 校准/新鲜度、配置语义和故障状态门控。
- 模拟看门狗快速关断与软件连续样本确认最终进入同一故障锁存路径。
- `FAULT`、HardFault、NMI、BusFault、UsageFault 的第一动作必须关闭 PWM。
- TIM1 保持互补输出、50% 占空、偶数周期、死区和原子更新；不得由 UI/网络直接写定时器。
- SWEEP 与 RUNNING 均执行过流和 ADC 新鲜度检查；FAULT 恢复同时重置软启动和安全滤波。
- Flash 擦写不得跨分区，且仅在确认 PWM/PB10 安全关闭的允许阶段执行。
- 所有长度、地址、频率、枚举、串口帧和云端输入必须先验证再使用。

## 6. STM32 编码与中文注释

- 业务 `.c` 第一段必须是中文 Doxygen 文件头，含 `@file`、中文 `@brief`、`@note V5.1.3`。
- 业务 `.h` 第一行直接进入 include guard，文件开头禁止注释；公开声明在 guard 内写中文 Doxygen。
- 新增或修改的注释全部使用中文，只解释原因、边界、并发和硬件风险。
- 禁止 `//`，使用 `/* ... */` 或 `/** ... */`。
- 遵循 ARMCC V5/C89：变量在代码块开头声明，不使用 C99 `for` 声明或专属语法。
- 中文字符串沿用项目十六进制转义策略，避免 ARMCC 多字节字符警告。
- 公开函数和静态函数均使用模块前缀；静态变量 `s_`、全局变量 `g_`，宏和枚举值全大写。
- 不修改 `Keil_Project/Library/` 和 `Keil_Project/Start/` 第三方基线，除非有明确缺陷证据。

## 7. UI 与客户端一致性

- 设置菜单固定为：语言、启动频率、字间距、光标图标、配色；亮度设置页已移除，PA12 背光保持固定开启。
- 返回键取消/退出，确定键进入或保存；KEY1 双击回主菜单，KEY0 只控制 PB10。
- 电压、电流、频率使用独立的分段递增表盘；频率量程跟随本轮锁定档位。
- 网页、小程序和桥接必须跟随 STM32 的范围、步进、单位和 S=0/1/2/3 语义。
- 小程序未配置 OneNET 时只显示预览：`_isMock=true`、`_isOnline=false`，不得报警、记录在线历史或开放控制。
- 受保护网页先加载 `js/auth-guard.js`；持久登录最长 7 天，退出同时清理会话与持久状态。
- 所有轮询防重入，并在隐藏、卸载或离开页面时清理；日志不得输出凭证。

## 8. 目录与清理约束

| 内容 | 唯一位置 |
|:---|:---|
| STM32 / ESP8266 / Web | `Keil_Project/` / `Arduino_Project/` / `ONENETapp/` |
| 小程序与可选桥接 | `安卓app/` |
| 字库工具 / 活动脚本 | `ch341/` / `tools/` |
| 回归检查 | `tests/`、`Keil_Project/tests/` |
| 项目技能 | `.agents/skills/` |
| 唯一总手册 | 根目录同名 `.md/.docx` |
| 历史资料 | `NONFILE/` |

- 禁止恢复 `Claude_Files/`、`docs/superpowers/` 或根目录任务报告。
- `NONFILE/README.md` 之外的归档文件名必须以对应历史版本 `-Vx.y.z` 结尾；历史内容不机械升级。
- `node_modules/`、`__pycache__/`、字库镜像、Keil 产物和 `Target *.BAT` 不得被 Git 跟踪。
- 清理前先核对引用、Git 归属和绝对路径；递归删除目标必须位于工作区内。
- 不删除 Flash `backup_*.bin`、用户 IDE 配置或本地工具状态，除非用户明确点名。
- 当前目录树只列实际存在的入口；归档代码不得被活动代码导入或执行。

## 9. 同步范围

- 改 STM32 范围、协议或状态：检查 ESP8266、网页、小程序、桥接、README、AGENTS、总手册和契约测试。
- 改 UI/设置：检查持久化结构、按键路由、TFT 页面数、README 和总手册。
- 改网页：只在 `ONENETapp/` 独立仓库提交，并验证受保护页面、PWA 缓存和线上入口。
- 改目录：同步脚本、测试、文档链接、技能路径和 `.gitignore`。
- 发版：同步活动代码版本、TFT 开机版本、PWA/manifest、桥接/工具版本和根文档；历史快照保留原版本。

## 10. 按风险验证

目录或技能变更至少执行：

```powershell
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_release.ps1
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_fullstack.ps1
```

STM32 代码、设置或表盘变更再执行：

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_3.ps1 -Scope Static
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_1_hardening.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_task4_gauge.ps1
```

协议、客户端或桥接变更再执行：

```powershell
node --test tests/client-model.test.cjs tests/bridge-core.test.mjs
```

- 用 `skill-creator/scripts/quick_validate.py` 验证技能格式；验证本项目安装副本与项目副本内容一致。
- Node 测试需要依赖时按锁文件临时安装；依赖可留在本机但不得重新纳入 Git。
- 能使用 Keil/ARMCC 时才声明真实构建结果；无实机时不得声称波形、温升或故障注入通过。
- 推送前运行 `Keil_Project/keilkill.bat` 并确认无 `.obj/.lst/.axf/.hex/.map/._ia`。

## 11. Git 发布

- 仅在网页仓库实际变化时提交并同步其 `master` 与 `gh-pages`；不要制造空提交。
- 主仓库提交前排除 `.claude/settings.local.json`、`.codex/`、本地凭证和进程状态。
- 保留用户已有修改，不使用破坏性重置；提交主仓库后只推送 `origin 5.0`。
- 最终报告区分：已验证、未运行、需实机验证，不把静态检查冒充真实编译或硬件验收。
