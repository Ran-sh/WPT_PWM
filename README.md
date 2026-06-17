# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Display](https://img.shields.io/badge/Display-ST7735%20160×128%20TFT-red)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-purple)]()
[![Firmware](https://img.shields.io/badge/Firmware-V4.2.0-brightgreen)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V4.2.0** (2026-06-17) — STM32F103C8T6 + ESP8266-01 的 LCC-S 谐振全桥无线供电系统。**Dual-MCU 双脑架构** + **系统全局状态机** + **圆弧能量条仪表盘** + OneNET MQTT 多端控制。应用场景：植入式医疗设备无线充电。

---

## 特性

- **圆弧能量条仪表盘**: 电压/电流/频率独立表盘, 1px Bres_Line 锐利刻度, 三行信息舱
- **全局系统状态机**: INIT→IDLE→SWEEP→RUNNING→FAULT, 按状态子集调度 Task
- **TFT 彩屏 9 页面**: 两级菜单 + 仪表盘 + 配网 + 故障, 200ms 增量刷新
- **双 EMA 滤波链**: Sys_Safety 安全级滤波 (V/I) + Ui_Controller 显示级滤波, 频率无 EMA 零迟滞
- **安全剥离**: PB10 电源控制 + 过流检测从 UI 解耦到 Sys_Safety 独立模块
- **4 键操作**: ON/OFF (单击/双击/长按), PAGE, F+, F-
- **6 LED 指示**: 系统心跳 + WiFi + PWM + COM + POWER + TEMP
- **远程多端控制**: Cloudflare Pages 网页 + 微信小程序 + OneNET 平台

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|:---|:---|:---|
| V4.2.0 | 2026-06-17 | 全平台版本号统一 + TFT字库修复 + 底部栏简化 + 16轮全链路审查 |
| V4.1.0 | 2026-06-11 | TFT 彩屏 9 页面 + 圆弧能量条仪表盘 + Sys_Safety 独立安全 + EMA双级滤波 |
| V4.0.0 | 2026-06-01 | 系统全局状态机 + Sys_Core 模块化 + 全链路数据一致性 |

---

## 系统架构

```
┌──────────────────────────────────────────────────────────┐
│                      ☁️ OneNET Studio                     │
│              MQTT 物模型 (V/I/F/Switch/SetFreq)           │
└────┬──────────────┬──────────────┬───────────────────────┘
     │ MQTT         │ HTTPS        │ HTTPS
     ▼              ▼              ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│ ESP8266  │  │ Cloudflare│  │ 微信小程序 │
│ Arduino  │  │ Pages    │  │ WeChat    │
│ MQTT 固件 │  │ 网页控制台 │  │ Mini App  │
└────┬─────┘  └──────────┘  └──────────┘
     │ USART2 115200
     │ 纯文本 JSON (零 AT 指令)
     ▼
┌──────────────────────────────────────┐
│         STM32F103C8T6 (物理脑)        │
│  • TIM1 全桥 PWM + PFM 调功          │
│  • ADC1 双通道 64 样本滑动窗口        │
│  • TFT 160×128 彩屏 9 页面 UI         │
│  • 圆弧能量条仪表盘                │
│  • Sys_Safety 独立安全监测            │
│  • 系统全局状态机                    │
│  • 非阻塞软启动 150k→100kHz           │
└──────────────────────────────────────┘
```

### 系统状态机

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

---

## 快速开始

1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. `keilkill.bat` 清理 → Rebuild (F7) → Download (F8)
3. 开机 → 默认自动联网 → 按 ON 启动 PWM → 仪表盘实时监测

---

## 分支

| 分支 | 版本 | 显示 | 架构 |
|:---|:---:|:---:|:---|
| `4.0TFT` | V4.2.0 | TFT 彩屏 | 全局状态机 + 能量条仪表盘 |

## 文档

| 文档 | 说明 |
|:---|:---|
| [CLAUDE.md](CLAUDE.md) | AI 开发规范 (命名/注释/安全/架构/画面布局) |
| [Keil_Project/](Keil_Project/) | STM32 固件源码 |

## 许可

MIT
