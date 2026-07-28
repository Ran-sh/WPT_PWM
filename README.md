# WPT_PWM 无线电能传输控制系统

![Firmware V5.1.3](https://img.shields.io/badge/Firmware-V5.1.3-brightgreen)
![Branch 5.0](https://img.shields.io/badge/Branch-5.0-blue)
![STM32F103C8](https://img.shields.io/badge/MCU-STM32F103C8-03234B)
![SPL 3.5.0](https://img.shields.io/badge/Library-SPL%203.5.0-orange)

V5.1.3 是一套完整的无线电能传输控制与监测工程：STM32 负责全桥 PWM、采样、安全保护和 TFT 交互；ESP8266 负责 WiFi/OneNET；网页和微信小程序提供远程监测与控制。

- 在线控制台：[wptonenet.483763727.workers.dev](https://wptonenet.483763727.workers.dev/)
- 主仓库：[Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM)，当前分支 `5.0`
- 网页仓库：[Ran-sh/WPT_Onenet_IoT](https://github.com/Ran-sh/WPT_Onenet_IoT)
- 从零搭建、接线、烧录和部署：[完整操作手册](WPT无线充电系统-从零搭建完整操作手册.md)

> 功率电路具有触电、过流和器件损坏风险。首次调试应使用限流电源，先验证控制板，再连接全桥和线圈；CH341A 烧录 W25Q128 时目标板必须完全断电。

## 当前能力

- 总体频率范围 20–200kHz，并按两个实际可达档位量化。
- 20.0–99.9kHz 低频档，步进 0.1kHz。
- 100–200kHz 高频档，步进 1kHz。
- 两个档位独立保存启动频率，默认分别为 20.0kHz 和 100kHz。
- 非阻塞动态扫频、软启动和运行中平滑调频。
- TIM1 互补全桥 PWM、死区、原子更新和多入口安全关断。
- TIM3 触发 ADC 双通道采样，电压/电流双级滤波和 5.0A 过流保护。
- ST7735 15 页面 UI、五项设置、五键交互和三个独立递增式表盘。
- W25Q128 全字库、配置双副本和 Blackbox 故障日志。
- ESP8266 自动配网、OneNET MQTT、离线恢复、命令去抖和串口溢出保护。
- Cloudflare Workers 网页与微信小程序实时监测、历史、报警和控制。
- 网页登录守卫、小程序预览/离线区分、本地桥接鉴权和跨端协议回归。

## 系统架构

```text
┌───────────────────────────────────────────────────────────┐
│ Cloudflare Workers 网页 / 微信小程序                       │
└──────────────────────────┬────────────────────────────────┘
                           │ HTTPS / OneNET 物模型
┌──────────────────────────▼────────────────────────────────┐
│ OneNET                                                    │
└──────────────────────────┬────────────────────────────────┘
                           │ MQTT
┌──────────────────────────▼────────────────────────────────┐
│ ESP8266：WiFiManager、MQTT、命令转换                       │
└──────────────────────────┬────────────────────────────────┘
                           │ USART2 115200，一行一帧
┌──────────────────────────▼────────────────────────────────┐
│ STM32F103C8：PWM、ADC、安全、TFT、按键、存储               │
└───────────────────────────────────────────────────────────┘
```

设计边界：STM32 不发 AT 指令，ESP8266 不直接操作 PWM、功率使能或 ADC。

## 状态与数据一致性

| 状态 | S | PWM | 电压/电流 | 频率 F |
|:---|:---:|:---:|:---|:---:|
| IDLE | 0 | 关 | 实际采样 | 0 |
| SWEEP | 1 | 开 | 实际采样 | TIM1 实际 Hz |
| RUNNING | 2 | 开 | 滤波后的实际采样 | 实际 Hz |
| FAULT | 3 | 关 | 实际采样 | 0 |

STM32 到 ESP8266 的固定报文：

```text
{"V":12.50,"I":0.35,"F":100000,"S":2}\n
```

允许的核心命令：

```text
CMD:ON
CMD:OFF
CMD:SETFREQ:99900
```

网页和小程序显示 kHz；串口、MQTT 和 OneNET 属性使用 Hz。所有客户端均遵守与 STM32 相同的范围和步进。

## 目录结构

```text
WPT_PWM_V5.0/
├── Keil_Project/       STM32 固件、SPL 工程和静态检查
├── Arduino_Project/    ESP8266 固件
├── ONENETapp/          网页端独立 Git 仓库
├── 安卓app/            微信小程序和可选本地桥接
├── ch341/              W25Q128 字库生成与烧录
├── tools/              活动维护脚本
├── tests/              全栈协议与版本回归
├── .agents/skills/     项目技能
├── NONFILE/            按版本尾缀归档的历史报告、旧方案、旧图纸和重复文档
├── AGENTS.md           唯一开发约束
└── WPT无线充电系统-从零搭建完整操作手册.md
```

文件放置的详细规则见 [AGENTS.md](AGENTS.md) 和 [NONFILE/README.md](NONFILE/README.md)。`Claude_Files/` 与 `docs/superpowers/` 已停用，不应重新创建。

## 快速开始

### 1. 准备硬件

按总手册核对 STM32、ESP8266、ST7735、W25Q128、采样器件、全桥和五键/四灯接线。先只给控制板供电，确认 PB10 和 PWM 上电保持关闭。

### 2. 烧录 STM32

使用 Keil MDK-ARM V5 打开 `Keil_Project/Project.uvprojx`：

1. F7 编译。
2. 确认 0 Error，并处理所有新增 Warning。
3. 使用 ST-Link F8 下载。
4. 复位后确认开机画面显示 V5.1.3。

### 3. 烧录 ESP8266

打开 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`，填写自己的 OneNET 参数。烧录时 GPIO0 接地，完成后断开 GPIO0 并重新上电。首次启动连接热点 `STM32_WPT_Config` 完成配网。

### 4. 烧录字库

目标板完全断电，CH341A 选择 3.3V，按总手册连接 PB12/PA5/PA7/PA6/GND/3.3V，然后运行：

```powershell
python -m pip install -r ch341/requirements.txt
python ch341/burn_flash.py
```

工具会先备份 16MB 全片，只更新前 2MB 字库分区，并对前 2MB 完整读回校验。

### 5. 配置 OneNET 与客户端

在 OneNET 建立 V、I、F、Switch、SetFreq 物模型。网页按 [网页仓库说明](ONENETapp/README.md) 部署；微信开发者工具导入 `安卓app/`，在设置页填写 Product ID、Device Name 和 Token。

未配置 Token 时小程序进入安全预览模式：显示模拟数据，但不会标记在线、写入报警或下发控制。

## TFT 与五键交互

| 按键 | 默认作用 |
|:---|:---|
| KEY0 / PB9 | 电源控制 |
| KEY1 / PB8 | 返回；双击回主菜单 |
| KEY2 / PB7 | 上移/增加 |
| KEY3 / PB6 | 下移/减少 |
| KEY4 / PB5 | 确定/启停 |

设置页包含语言、启动频率、字间距、光标图标和配色；亮度设置已移除，PA12 背光固定开启。返回键退出当前设置页，确定键进入子页或保存当前值。

## 主要引脚

| 引脚 | 功能 | 引脚 | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | 电流 ADC |
| PA1 | ESP_RST | PB1 | 电压 ADC |
| PA2/PA3 | USART2 TX/RX | PB3/PB4 | POWER/WIFI LED |
| PA4 | TFT_CS | PB5–PB9 | KEY4–KEY0 |
| PA5/PA7 | SPI1 SCK/MOSI | PB10 | PowerCtrl |
| PA6 | TFT_DC / Flash MISO | PB11 | ESP_EN |
| PA8/PA9 | TIM1 CH1/CH2 | PB12 | W25Q128_CS |
| PA12 | TFT_BL | PB13/PB14 | TIM1 CH1N/CH2N |
| PA15 | STATUS LED | PB15 | 蜂鸣器 |
| PC13 | HEARTBEAT LED | — | — |

## 验证

无需硬件即可运行：

```powershell
node --test tests/client-model.test.cjs tests/bridge-core.test.mjs
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_release.ps1
powershell -ExecutionPolicy Bypass -File tests/verify_v5_1_3_fullstack.ps1
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_v5_1_3.ps1 -Scope Static
```

真实 STM32 构建仍需 Keil/ARMCC。推送前必须运行 `Keil_Project/keilkill.bat`，禁止提交 `.obj`、`.lst`、`.axf`、`.hex` 等编译产物。

## V5.1.3 变更

- 6 个网页业务页面实际接入登录守卫，登录/退出状态一致。
- 网页、PWA 缓存、manifest、脚本和样式统一到 V5.1.3。
- ESP8266 调试日志默认编译关闭，不再污染 STM32 协议串口。
- 小程序区分在线、离线、缓存和未配置预览，预览数据不再触发报警。
- STM32、ESP、网页、小程序、桥接和字库工具统一发布契约。
- 历史资料迁移到 NONFILE，活动脚本迁移到 tools，根目录只保留一个总操作文档。
- 清除可重建依赖、缓存和本机生成文件；归档文件统一使用版本号尾缀。
- 重写项目 `embedded-architect` 技能和仓库约束。

更完整的版本历史、接线、安全、字库、部署和故障排查见 [完整操作手册](WPT无线充电系统-从零搭建完整操作手册.md)。
