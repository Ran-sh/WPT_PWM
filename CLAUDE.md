# CLAUDE.md

当前项目版本：`V5.1.3`，主仓库分支：`5.0`。

本文件只作为兼容入口，避免不同开发工具维护两套互相冲突的架构说明。

开始工作前必须依次读取：

1. `AGENTS.md`：唯一代码、目录、版本、验证和 Git 约束。
2. `.agents/skills/embedded-architect/SKILL.md`：STM32、ESP8266、协议、安全和全栈同步规则。
3. `WPT无线充电系统-从零搭建完整操作手册.md`：从接线、烧录、配网到网页/小程序部署的唯一用户文档。

关键约束：

- STM32 业务 `.c` 开头必须有中文文件头；`.h` 开头禁止注释，直接使用 include guard。
- 当前频率范围为 20–200kHz：低档 0.1kHz 步进，高档 1kHz 步进。
- 历史资料放在 `NONFILE/`；禁止恢复 `Claude_Files/` 和 `docs/superpowers/`。
- 网页端是 `ONENETapp/` 内的独立仓库，推送后还要更新主仓库指针。
- 提交前运行 V5.1.3 回归检查和 `Keil_Project/keilkill.bat`，不得上传 Keil 产物或本地凭证。
