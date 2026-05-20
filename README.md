# WPT_PWM — 物联网全桥谐振电源控制系统

基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统，支持 OLED 本地控制与 WiFi 远程遥测。

## 硬件

| 组件 | 型号 |
|:---|:---|
| MCU | STM32F103C8T6 (Cortex-M3, 64KB Flash) |
| WiFi | ESP8266-01 (AT 指令透传) |
| 显示 | SSD1306 128×64 OLED (I2C) |
| 驱动 | IR2103S 栅极驱动, TIM1 全桥 PWM |
| 传感器 | CC6920-10A 霍尔电流, 20:1 分压 |

## 快速开始

1. **Keil MDK-ARM V5** 打开 `Project.uvprojx`
2. 编译 → ST-Link 烧录
3. 上电 → 按 KEY0 联网 → 再按 KEY0 软启动扫频 150k→100kHz
4. PC 端 NetAssist 配置 TCP Server 监听, 发送 `CMD:ON` / `CMD:OFF` 遥控

## 按键

| 按键 | 单击 | 双击 |
|:---|:---|:---|
| KEY0 (PB12) | 联网 / 触发扫频 / 关断 | 切页 |
| KEY1 (PB13) | 关断 / 频率+1kHz | — |

## 目录

```
├── User/          应用层 (main.c, App_Net.c)
├── Hardware/      硬件驱动 (PWM, ESP8266, ADC, LED, KEY, OLED, UI)
├── System/        系统服务 (SysTimer)
├── Library/       SPL V3.5.0 (只读)
├── Start/         启动文件
├── claude_code/   AI 生成文件 (docs, tools, superpowers)
└── CLAUDE.md      项目开发指南
```

## 技术要点

- **SPL V3.5.0** 标准外设库, 禁止 HAL/LL
- **非阻塞调度**: SysTimer 时间戳差值法, 5 个并行 Task
- **软启动扫频**: 150kHz→100kHz, 200Hz/10ms 步进, ~2.5s, 防浪涌
- **异步联网**: 非阻塞 9 态 AT 指令状态机, 可取消/自动重试 3 次
- **安全红线**: 频率硬下限 95kHz, 死区 1000ns (DEADTIME_NS 宏可调), BDTR≤127 断言
- **远程协议**: `CMD:ON` / `CMD:OFF`, JSON 遥测 `{"V":x,"I":x,"F":x}`

## 文档

| 文档 | 说明 |
|:---|:---|
| [软件架构与开发者指南](claude_code/docs/软件架构与开发者指南.md) | 完整技术架构 |
| [PC端联调操作指南](claude_code/docs/PC端联调操作指南.md) | NetAssist 配置 |
| [LabVIEW上位机构建指南](claude_code/docs/LabVIEW上位机构建指南.md) | LabVIEW 上位机 |
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 |

## 许可

MIT
