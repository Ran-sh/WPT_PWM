# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Display](https://img.shields.io/badge/Display-ST7735%20160×128%20TFT-red)]()
[![ESP8266](https://img.shields.io/badge/ESP8266-Arduino%20MQTT-purple)]()
[![Firmware](https://img.shields.io/badge/Firmware-V9-brightgreen)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20Studio-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V9** (2026-06-11) — 基于 STM32F103C8T6 + ESP8266-01 的 100kHz LCC-S 谐振全桥无线供电系统。采用 **Dual-MCU 双脑架构**：STM32 (SPL V3.5.0) 全桥 PFM 发波与保护，ESP8266 (Arduino) 独立 MQTT 固件连接 **OneNET 物模型**。支持 **TFT 160×128 彩屏** 6 态 UI 状态机 + 动态能量条本地控制、Cloudflare Pages 网页控制台、微信小程序远程遥控。应用于植入式医疗设备无线充电。

---

## 特性

- **TFT 彩屏**: ST7735 160x128 横屏, RGB565, 6 态 UI + 4 子页仪表盘
- **动态能量条**: 像素级绿→红渐变, 数值范围自适应 (替代传统 '#' 刻度条)
- **WIFI 角标动画**: 右上角实时状态, 蓝色逐字闪烁连接中, 绿色在线, 红色离线
- **4 键操作**: ON/OFF (单击/双击/长按), PAGE (单击/双击), F+, F-
- **6 LED 指示**: 系统心跳 + WiFi + PWM + COM + POWER + TEMP
- **PB10 智能电源**: 电压 > 12V 自动使能 12V 动力电源, 与 PWM 独立控制
- **开机安全**: TIM1 全关 (CEN+MOE), 12V 关断, ESP 不自动联网, 用户手动控制
- **远程指令安全**: 前缀匹配防误触发, 无WIFI模式拒绝远程控制, CMD:OFF 禁止清除故障
- **能量条**: 绿→红渐变, 分段自适应

---

## 版本历史

| 版本 | 日期 | 分支 | 主要变更 |
|:---|:---|:---|:---|
| V0.0 | 2026 | `master` | 裸机基版, 全桥 PWM + OLED + 按键 |
| V3.5 | 2026 | `2.0WAN` | 巴法云 MQTT TCP 协议 |
| V5.0 | 2026 | `3.0ONENET` | OneNET MQTT 物模型 + Dual-MCU 架构 |
| V6.0 | 2026 | `3.0ONENET` | 全模块命名规范 + 显式状态枚举 |
| V8 | 2026-06-09 | `4.0TFT` | TFT 彩屏 7 态 UI + WiFi 实时检测 + 按键表 + LED 规则 |
| **V9** | **2026-06-11** | **`4.0TFT`** | **6 态 + 能量条 + 代码审查 37 项修复 + 开机安全强化** |

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
│  • TIM1 全桥 PWM + PFM 调功           │
│  • ADC1 双通道 64 样本滤波            │
│  • TFT 160x128 彩屏 6 态 UI            │
│  • 4 键 FSM + 6 LED + 蜂鸣器         │
│  • 动态能量条 绿→红渐变               │
│  • 非阻塞软启动 150k→100kHz           │
│  • 过流保护 5A + IWDG 1.6s            │
└──────────────────────────────────────┘
```

### 通信协议

**STM32 → ESP8266 (遥测, 每 500ms)**:
```json
{"V":12.50,"I":1.23,"F":100000}
```

**ESP8266 → STM32 (指令)**:
```
CMD:ON          → 启动扫频
CMD:OFF         → 关断逆变器
CMD:SETFREQ:x   → 频率渐变到目标
STATUS:ONLINE   → ESP 已联网
```

---

## 硬件配置

| 组件 | 型号 | 说明 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM, 72MHz |
| WiFi | ESP8266-01 | Arduino MQTT 固件, 独立 3.3V LDO |
| 显示 | ST7735 1.8" TFT | SPI Mode 3, 160x128 横屏, RGB565, 背光 PWM |
| 栅极驱动 | IR2103S | 高低侧驱动, LIN 低有效, 1000ns 死区 |
| 电流传感器 | CC6920BSO | 霍尔效应, 132mV/A, 零点 1.65V |
| 电压采样 | 分压网络 | 100k+4.7k 分压比 ~22.28, VREF=3.30V |
| 动力电源控制 | PB10 | 低=使能 12V, 高=关断, 独立于 PWM |

---

## 主循环

```c
while (1) {
    Key_Driver_Task();                  // 10ms  4 键 FSM
    Adc_Driver_Filter_Task();           // ~2ms  ADC 滤波
    Ui_Controller_Task();               // 200ms TFT UI + PB10 + 过流
    App_Network_Task();                 //       ESP + 指令 + 遥测
    Inverter_Control_Soft_Start_Task(); // 10ms  扫频状态机
    Inverter_Control_Freq_Ramp_Task();  // 10ms  频率渐变
    Led_Driver_Task();                  //       LED 闪烁
    Buzzer_Driver_Task();               //       蜂鸣器
    IWDG_ReloadCounter();               //       看门狗喂狗
    __WFI();
}
```

---

## 按键操作

| 按键 | 事件 | 状态 | 功能 |
|:---|:---|:---|:---|
| ON/OFF | 单击 | READY | 启动扫频 |
| ON/OFF | 单击 | SWEEPING/RUNNING | 停止 PWM |
| ON/OFF | 单击 | FAULT | 复位故障 |
| ON/OFF | 双击 | 全部 | 智能 WiFi 切换 |
| ON/OFF | 长按 | 全部 | 清除 WiFi 配网 |
| PAGE | 单击 | SWEEPING/RUNNING | 切子页 |
| PAGE | 双击 | SWEEPING/RUNNING | 回综合监测 |
| F+ / F- | 单击 | RUNNING | 频率 ±1kHz |

---

## LED 指示

| LED | 引脚 | 逻辑 |
|:---|:---|:---|
| SYSTEM | PA15 | 500ms 心跳 |
| WiFi | PB4 | 离线慢闪/连接中快闪/在线常亮 |
| PWM | PB3 | 扫频快闪/其余灭 |
| COM | PA10 | MQTT 在线常亮 |
| POWER | PA11 | 电压>12V 常亮 |
| TEMP | PA12 | 暂未启用 |

---

## 快速开始

1. Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`
2. Rebuild (F7) → ST-Link Download (F8)
3. 开机 → 默认无 WIFI 模式 → 双击 ON 连接 WiFi → 按 ON 启动扫频

---

## 分支

| 分支 | 版本 | 协议 | 显示 |
|:---|:---:|:---|:---:|
| `master` | V0.0 | — | OLED |
| `3.0ONENET` | V6.2 | OneNET MQTT | OLED |
| **`4.0TFT`** | **V9** | **OneNET MQTT** | **TFT 彩屏** |

---

## 文档

| 文档 | 说明 |
|:---|:---|
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 (架构/编码/安全/画面布局) |
| [Keil_Project/](Keil_Project/) | STM32 固件源码 |

## 许可

MIT
