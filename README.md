# WPT_PWM — 物联网全桥谐振电源控制系统

[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-blue)]()
[![Library](https://img.shields.io/badge/Library-SPL%20V3.5.0-green)]()
[![IDE](https://img.shields.io/badge/IDE-Keil%20MDK--ARM%20V5-orange)]()
[![Display](https://img.shields.io/badge/Display-SSD1315%20128×64%20OLED-yellow)]()
[![WiFi](https://img.shields.io/badge/WiFi-ESP8266%20AT%20透传-purple)]()
[![Firmware](https://img.shields.io/badge/Firmware-V2.0.0-brightgreen)]()
[![Cloud](https://img.shields.io/badge/Cloud-巴法云%20TCP%20创客云-00B4D8)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

> **V2.0.0** (2026-05-14) — STM32F103C8T6 + ESP8266-01 的 LCC-S 谐振全桥无线供电系统。**三层分离架构** + **非阻塞软启动扫频** + **巴法云 TCP 创客云 WAN 远程控制**。应用场景：植入式医疗设备无线充电。

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|:---|:---|:---|
| V2.0.0 | 2026-05-14 | 系统重构: SysTimer 时基框架、App_Net 网络层剥离、main.c 极简化、三层架构分离 |
| V2.0.1 | 2026-05-16 | 文档更新 + 帧检测兼容 `\r` 分隔符 |
| V2.0.2 | 2026-05-16 | 竞态修复: ClearRxBuffer/WaitResponse/CIPSEND ISR 临界区保护 |
| V2.0.3 | 2026-05-16 | PWM 公开接口消除重复, App_Net/UI 统一调用, Delay 模块废弃 |
| V2.0.4 | 2026-05-18 | 死区 500→1000ns, PWM 预装载使能, 空闲态极性修复 |
| V2.1.0 | 2026-05-19 | 非阻塞软启动状态机 150→100kHz, MOE 安全上电, 95kHz 硬下限 |
| V2.1.1 | 2026-05-19 | 联网重试 + ADC 2ms 独立滤波 + CLOSED 非阻塞 + 四灯状态 |
| V2.2.0 | 2026-05-20 | 异步联网 9 态 AT 状态机 + 双重复位 + UDIS 原子更新 + SS_FAULT |
| V2.2.1 | 2026-05-20 | ESP8266 静默看门狗 (LAN 分支保留) |
| V2.3.0 | 2026-05-21 | 巴法云 TCP 创客云 WAN 接入, cmd=1 订阅 + cmd=2 遥测 + 删静默看门狗 |
| V2.3.1 | 2026-05-22 | ADC 浮点精度修复 + OLED 性能优化 + WiFi LED 常亮 |

> 版本号遵循 **Vx.y.z** 三阶格式。旧格式 V2.0~V3.5 已全部映射到 V2.x.x 体系，详见 [CLAUDE.md](CLAUDE.md)。

---

## 特性

- **巴法云 WAN 远程控制**: TCP 创客云接入, `CMD:ON`/`CMD:OFF` 指令, cmd=2 遥测每 2s 上报, 手机 APP/网页远程遥控
- **非阻塞软启动扫频**: 150kHz → 100kHz 自动扫频, 200Hz/10ms 步进, ~2.5s 完成, 防浪涌冲击
- **PFM 调功**: 95-150kHz 频率范围, 50% 固定占空比, 1000ns 可调死区
- **异步联网**: 9 态 AT 指令状态机, 支持 KEY1 取消, 3 次自动重试, 硬件+软件双重复位
- **双页 OLED UI**: 控制面板 + 锁屏监控, KEY0 双击切换, 状态切换自动清屏
- **四灯状态指示**: PC13 心跳 + PB3 WiFi (快闪/慢闪/常亮) + PB4 PWM + PB5 Ready
- **安全红线**: 95kHz 硬下限, 死区编译期断言 ≤127, 上电 MOE=OFF, 过流 SS_FAULT 锁存
- **三层分离架构**: Hardware → System → Application 单向依赖, SysTimer 统一时基

---

## 系统架构

```
                        ☁️ 巴法云 TCP 创客云
                   tcp.bemfa.com:8344
                  cmd=1 订阅 + cmd=2 遥测
                           │
                    TCP 透传 (零 MQTT)
                           │
┌──────────────────────────────────────────┐
│           ESP8266-01 (AT 固件)            │
│   9 态 AT 状态机 + TCP 透明传输           │
│   CIPSEND → 巴法云订阅/遥测              │
└────────────────┬─────────────────────────┘
                 │ USART2 PA2/PA3 115200
                 │
┌──────────────────────────────────────────┐
│         STM32F103C8T6 (Cortex-M3)         │
│                                           │
│  ┌─ User/ (应用层) ──────────────────┐   │
│  │  main.c · App_Net.c              │   │
│  ├─ System/ (系统服务) ──────────────┤   │
│  │  SysTimer.c (1ms 时基)           │   │
│  ├─ Hardware/ (硬件驱动) ────────────┤   │
│  │  PWM · ESP8266 · ADC · OLED      │   │
│  │  KEY · LED · UI                  │   │
│  └──────────────────────────────────┘   │
│                                           │
│  • TIM1 全桥 PWM + PFM 调功              │
│  • ADC1 双通道 DMA 扫描 + 16 样本滤波    │
│  • SSD1315 128×64 OLED 双页面 UI         │
│  • 非阻塞软启动 150k→100kHz              │
│  • SysTimer 时间戳差分发调度              │
└──────────────────────────────────────────┘
```

### 启动流程

```
上电 → PWM_Init(MOE=OFF) → OLED_Init → LED_Init → ADC_DMA → KEY
     → SysTimer_Init → OLED "Wireless Charge"
     → 主循环 (非阻塞):
         KEY_Task | UI_Task | App_Net_Task | Inverter_SoftStart_Task | LED_Task
```

---

## 快速开始

1. **Keil MDK-ARM V5** 打开 `Keil_Project/Project.uvprojx`
2. 修改 `User/App_Net.h` 中的配置:
   ```c
   #define WIFI_SSID       "YourWiFi"           // WiFi 名称
   #define WIFI_PASSWORD   "YourPassword"       // WiFi 密码
   #define BEMFA_UID       "your-uid-here"      // 巴法云用户私钥
   #define BEMFA_TOPIC     "WPT001"             // 主题名 (可自定义)
   ```
3. F7 编译 → F8 烧录 (ST-Link)
4. 上电 → OLED 显示 "Wireless Charge" → 按 **KEY0** 联网 (~20-30s)
5. 联网成功后 OLED 显示 IDLE 待机 → 已订阅巴法云主题
6. 按 **KEY0** 触发软启动扫频 或 巴法云 APP 发 `CMD:ON`

---

## 硬件

| 组件 | 型号 | 连接 |
|:---|:---|:---|
| MCU | STM32F103C8T6 | Cortex-M3, 64KB Flash, 20KB SRAM |
| WiFi | ESP8266-01 | USART2 (PA2-TX, PA3-RX), PB1-CH_PD |
| 显示 | SSD1315 128×64 OLED | 软件 I2C (PA11-SCL, PA12-SDA) |
| 栅极驱动 | IR2103S ×2 | TIM1 CH1/CH1N/CH2/CH2N (PA7/PA8/PA9/PB0) |
| 电流传感器 | CC6920-10A | PA0 (ADC_CH0) |
| 电压采样 | 20:1 分压 | PA1 (ADC_CH1) |
| 按键 | 2× 微动开关 | PB12 (KEY0), PB13 (KEY1) |
| LED | 4× 指示灯 | PC13 (心跳), PB3 (WiFi), PB4 (PWM), PB5 (Ready) |

### 引脚全表

| Pin | 功能 | 说明 |
|:---|:---|:---|
| PA0 | ADC_CH0 | 电流传感器 (CC6920-10A) |
| PA1 | ADC_CH1 | 电压采样 (20:1 分压) |
| PA2 | USART2_TX | ESP8266 RXD |
| PA3 | USART2_RX | ESP8266 TXD |
| PA7 | TIM1_CH1N | 半桥左桥下管 |
| PA8 | TIM1_CH1 | 半桥左桥上管 |
| PA9 | TIM1_CH2 | 半桥右桥上管 |
| PA11 | OLED SCL | 软件 I2C 时钟 |
| PA12 | OLED SDA | 软件 I2C 数据 |
| PB0 | TIM1_CH2N | 半桥右桥下管 |
| PB1 | CH_PD/EN | ESP8266 使能 (1000ms 硬件复位) |
| PB5 | Ready LED | 系统就绪指示 (高有效) |
| PB12 | KEY0 | 单击: 联网/扫频/关断/复位 · 双击: 切页 |
| PB13 | KEY1 | 单击: 关断/调频+1kHz/取消联网/复位 |
| PC13 | Heartbeat | 系统心跳 (500ms 翻转, 低有效) |

> ⚠️ **硬件注意事项**: ESP8266 需独立 3.3V LDO (≥500mA, AMS1117-3.3), 100μF+0.1μF 去耦。STM32 板载 3.3V 无法满足 WiFi 突发电流 (~300mA)。ESP8266 RST 接 3.3V 经 10kΩ 上拉。

---

## 按键操作

| 按键 | 状态 | 单击 | 双击 |
|:---|:---|:---|:---|
| KEY0 (PB12) | IDLE | 触发联网 → 扫频 | 切页 (控制面板 ↔ 锁屏) |
| | SWEEP | 关断 | — |
| | DONE | 关断 | — |
| | FAULT | 复位退出 | — |
| KEY1 (PB13) | IDLE | — | — |
| | SWEEP | 关断 | — |
| | DONE | 频率 +1kHz (循环) | — |
| | FAULT | 复位退出 | — |

---

## 远程控制

| 指令 | 方向 | 说明 |
|:---|:---|:---|
| `CMD:ON` | 云端 → 设备 | 触发软启动扫频 150k→100kHz, ~2.5s |
| `CMD:OFF` | 云端 → 设备 | PWM 立即关断, 全桥安全停止 |
| `CLOSED` | 设备 → 云端 | TCP 断开, 自动关断 PWM |

使用**巴法云 APP** (应用商店搜索 "Bemfa") 或网页控制台 (bemfa.com) 下发指令。遥测数据 `{"V":xx,"I":xx,"F":xx}` 每 2s 上报一次。

---

## 安全保护

| 场景 | 检测机制 | 响应 | PWM 动作 |
|:---|:---|:---|:---|
| TCP 正常断开 | ESP8266 发 `CLOSED` 帧 | < 1ms | 立即关断 |
| ESP8266 掉电 | TCP RST → CLOSED | < 1ms | 自动关断 |
| STM32 上电 | PWM_Init MOE=OFF | 硬件级 | 上电即关 |
| 过流 | SoftStart_Fault | < 1ms | 紧急关断 + FAULT 锁存 |
| 频率越界 | PWM_SetFrequency 硬钳位 | 即时 | 拒绝执行 |
| 死区越界 | 编译期断言 DTG ≤ 127 | 编译时 | — |

---

## 分支说明

> **仓库**: [github.com/Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM)

| 分支 | 本地目录 | 版本 | 显示 | 网络协议 | 说明 |
|:---|:---|:---:|:---:|:---|:---|
| `master` | `WPT_PWM_V0.0` | V1.0 | OLED | 无 (纯本地) | 裸机固件基版 |
| `LAN` | `WPT_PWM_NetAssistant_LAN_V1.0` | V2.x | OLED | NetAssist TCP | 局域网调试 |
| **`WAN`** | `WPT_PWM_Bemfa_WAN_V2.0` | **V2.0.0** | **OLED** | **巴法云 TCP** | **远程控制 (当前)** |
| `4.0TFT` | `WPT_PWM_V4.0_ONENET_TFT` | V4.x | TFT 彩屏 | OneNET MQTT | 主力彩屏分支 |

**分支间关系**: `master` 基版 → `LAN` 增加 ESP8266 + 局域网 → `WAN` 改为巴法云协议  
**差异**: `WAN` vs `LAN` 仅 `User/App_Net.h` 宏配置不同

---

## 项目结构

```
WPT_PWM_Bemfa_WAN_V2.0/
├── Keil_Project/                STM32 固件工程
│   ├── Hardware/                硬件驱动 (PWM · ESP8266 · ADC · OLED · KEY · LED · UI)
│   ├── System/                  系统服务 (SysTimer)
│   ├── User/                    应用层 (main · App_Net · stm32f10x_it)
│   ├── Library/                 SPL V3.5.0 (只读)
│   └── Start/                   启动文件 (CMSIS)
├── Claude_Files/                AI 辅助文件
│   ├── docs/                    开发者指南 + 操作手册
│   └── tools/                   docx 生成工具
└── CLAUDE.md                    项目 AI 开发规范
```

---

## 文档

| 文档 | 说明 |
|:---|:---|
| [CLAUDE.md](CLAUDE.md) | AI 辅助开发规范 (命名/注释/安全/架构/引脚) |
| [软件架构与开发者指南](Claude_Files/docs/软件架构与开发者指南.md) | 完整技术架构, 模块详解, 数据流 |
| [巴法云WAN远程联调操作指南](Claude_Files/docs/巴法云WAN远程联调操作指南.md) | 巴法云配置, 远程控制测试, 故障排查 |

---

## 许可

MIT
