> #### Word 级排版视觉规范声明
>
> | 元素 | 字体 | 字号 | 颜色 | 行距/样式 |
> |:---|:---|:---|:---|:---|
> | 文档标题 (Title) | 微软雅黑/黑体 | 二号 | #1F4E79 | 段前24磅, 段后24磅, 居中, 加粗 |
> | 一级标题 (H1) | 微软雅黑/黑体 | 三号 | #1F4E79 | 段前18磅, 段后12磅, 左对齐, 加粗, 下方1磅实线边框 |
> | 二级标题 (H2) | 微软雅黑/黑体 | 四号 | #000000 | 段前12磅, 段后6磅, 左对齐, 加粗 |
> | 三级标题 (H3) | 宋体/楷体 | 小四 | #333333 | 段前6磅, 段后6磅, 左对齐, 加粗 |
> | 正文 | 宋体/微软雅黑 | 五号 | #000000 | 1.5倍行距, 首行缩进2字符, 两端对齐 |
> | 代码块/报文 | Consolas | 小五 | #1A365D | 单倍行距, 浅灰底色 #F2F2F2, 段前段后0 |
> | 表格 | 宋体 | 小五 | #333333 | 单元格上下居中, 粗实线表头, 浅色隔行填充 |
> | 提示/警告框 | 黑体 | 五号 | — | 左侧缩进, 10磅内边距, 左侧加粗色带 |

---

## 文档控制信息

| 字段 | 内容 |
|:---|:---|
| **文档版本** | V5.1.0 |
| **最后更新** | 2026-07-26 |
| **对应固件版本** | V5.1.0 (分支 `5.0`) |
| **GitHub 主仓库** | [Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM) |
| **网页端仓库** | [Ran-sh/WPT_Onenet_IoT](https://github.com/Ran-sh/WPT_Onenet_IoT) (Cloudflare Pages) |
| **桥接服务器仓库** | [Ran-sh/WPT_Railway](https://github.com/Ran-sh/WPT_Railway) (小程序桥接, 备选) |
| **作者** | Rssss |

### 修改日志

| 版本 | 日期 | 变更说明 |
|:---|:---|:---|
| V5.1.0 | 2026-07-22 | **设置、启动频率与独立表盘重构**: 五项设置、配置V2双档启动频率与全局光标；PWM边界20–200kHz；低档99.9kHz/100Hz与高档200kHz/1kHz动态扫频；15页面递增式独立表盘。 |
| V5.0.2 | 2026-07-19 | **STM32全面可靠性优化**: PB10/PWM/FAULT硬互锁、TIM1原子更新、TIM3 500Hz ADC双窗口、SPI1共享仲裁、W25Q边界/超时、后台参数保存、Blackbox V2双元数据与故障前后5秒快照、5键能力拆分、14页UI、USART2中断发送、遥测S=0/1/2/3、统一调度与C89清理 |
| V4.5.2 | 2026-07-11 | **SPI时序回归+DMA修复+EMA修复**: DMA超时操作数反转修复(根治花屏), DMA TC3残留清理, SPI恢复18MHz, Flash批量读(16次→1次), CN/Icon ROM优先策略, 默认EN界面(W25Q手动切中文), Sys_Safety EMA全状态更新, CS脉冲简化, NVIC临界区保护, Pick_CN_EN遗漏修复 |
| V4.5.1 | 2026-07-02 | **全平台安全审查修复 (16项)**: ESP8266 Token占位符化+配网密码+CMD:CLEAR二次确认+公共MQTT门控 + STM32 DMA超时护底+环形缓冲+黑匣子指针持久化+故障锁存跨页擦除+strtol防溢出+USART2 RXNE优先 + 扫频进度条防闪烁 + Web乐观缓存回滚+SW BASE路径修复 |
| V4.5.0 | 2026-07-02 | 设置系统重构: 8页设置(语言/字间距/图标/亮度二级/颜色6预设) + PIC预览+确认模型 + 字间距纯像素间隙0-6px + 亮度1-100%滚动翻阅 + 颜色全屏重绘 + Key_Driver ID命名去歧义 + ARMCC V5 hex-escape兼容 |
| V2.2.0 | 2026-05-24 | 全篇重构: 调试避坑模块、配图标注、双主题小程序、频率渐变斜坡、Cloudflare部署 |
| V4.3.2 | 2026-06-29 | W25Q128 全字库修复: 初始化铁序修正 (TFT→SysTick→W25Q→Font→SPLASH) + Tft_Driver_Font_Init 拆分 + SPLASH 纯代码8帧渐亮 (不依赖 W25Q 位图分区) + 二分搜索 CS 翻转 + CRC32 算法修正 (Python zlib→STM32 refin=false) + bit_reverse_byte 删除 (字模不再镜像) + ch341 工具链精简 (仅字库烧录) |
| V4.3.0 | 2026-06-22 | W25Q128 16MB SPI Flash 集成: SPI1 分时复用 + GB2312 全字库 + 黑匣子循环日志 + 四大硬件防线 |
| V4.2.4 | 2026-06-18 | 离线守卫全平台修复: _isOnline 三层判定(兜底data非空→/device/detail覆写→cache延写) + Web throw误触发 + 重复代码块 + 生命周期onHide/pagehide清理 + 全平台代码审查 |
| V4.2.1 | 2026-06-17 | 全项目 README 重写(4分支统一分支表) + 版本号规则全文档对齐 |
| V4.2.0 | 2026-06-17 | 全平台版本号统一为 Vx.x.x 体系 + TFT字库 76字精准对齐(综/合字模修复) + 底部栏简化(仅ON:确定+PAGE:返回) + EMA双级滤波链 |
| V4.1.0 | 2026-06-11 | OLED→TFT彩屏 + 9页面UI + 动态能量条 + 4键6LED蜂鸣器 + 37项审查修复 |

---

# WPT 无线充电全桥谐振控制系统 — 开发者保姆级复盘指南

> **阅读指引**: 这是一份"写给我自己"的复盘手稿。踩过的坑比跑通的代码还多。如果你正在做类似项目——STM32发PWM、ESP8266联网、OneNET云平台、配上网页和小程序——这篇文档就是写给你的。

[📍此处需配图：系统整体实拍，左边电源+STM32+全桥板+ESP8266+TFT，右边手机显示网页控制台]

---

## 1. 项目概述

### 1.1 这是什么

把这几个东西想象成一个团队:

- **STM32** = 工厂车间主任。管着PWM发波（决定输出多大功率/什么频率）、ADC采集（实时盯着电压电流）、遇到故障立刻拉闸。
- **ESP8266** = 车间的前台文员。只管一件事：把车间主任给的数据发到云上，把云上的指令传回车间。它不碰任何生产设备。
- **OneNET** = 公司的云办公系统。所有数据在这里汇总，远程指令从这里发出。
- **网页/小程序** = 你的手机。你在任何地方打开就能看到车间情况，还能远程下命令。

**能做什么**:
- 手机/电脑实时看电压、电流、频率
- 远程开关机
- 远程调频率 (20kHz~200kHz, 步进到实际可达值)
- TFT 彩屏本地显示: 递增式分段表盘 + 15页面 UI
- 看历史数据曲线
- 过流自动保护

### 1.2 整体架构图

```
┌─────────────────────────────────────────────────────┐
│                  你的手机 / 电脑                       │
│    网页: wptonenet.483763727.workers.dev             │
│    小程序: 微信扫码 (直连 OneNET API)                  │
└──────────────┬──────────────────────────────────────┘
               │ HTTPS
┌──────────────┴──────────────────────────────────────┐
│   Cloudflare Pages (网页) / 微信小程序 (直连 API)     │
└──────────────┬──────────────────────────────────────┘
               │ HTTP API / MQTT
┌──────────────┴──────────────────────────────────────┐
│              OneNET 云平台 (中国移动)                  │
│    物模型属性: V(电压) I(电流) F(频率)               │
│              Switch(开关) SetFreq(设置频率)           │
└──────────────┬──────────────────────────────────────┘
               │ MQTT (mqtts.heclouds.com:1883)
┌──────────────┴──────────────────────────────────────┐
│          ESP8266-01 (联网脑)                          │
│  • WiFiManager网页配网 • MQTT双连接                   │
│  • JSON串口透传 • 指令转发+去抖 • 自动重连             │
└──────────────┬──────────────────────────────────────┘
               │ USART2 115200bps (纯文本JSON, 零AT指令)
┌──────────────┴──────────────────────────────────────┐
│        STM32F103C8T6 (物理脑)                        │
│  • TIM1全桥PWM (20k~200kHz, 50%占空比, 1000ns死区)   │
│  • ADC双通道+64样本滑动滤波 • 非阻塞软启动扫频          │
│  • TFT 160×128彩屏+5键+4LED+蜂鸣器                   │
│  • ADC显示/安全双窗口+UI EMA • 频率渐变斜坡 • 过流保护   │
└─────────────────────────────────────────────────────┘
```

### 1.3 核心功能清单

| 编号 | 功能 | 一句话说明 |
|:---:|:---|:---|
| F1 | 全桥 PWM 驱动 | TIM1 四通道互补输出, 50% 占空比锁定, PFM 调功 |
| F2 | 非阻塞双档软启动扫频 | 低档99.9kHz→保存目标(100Hz/10ms)；高档200kHz→保存目标(1kHz/10ms) |
| F3 | 防偏磁保护 | 周期ticks强制偶数 + UDIS影子寄存器原子更新 |
| F4 | 频率渐变斜坡 | SETFREQ后 1000Hz/10ms 平滑过渡, 100kHz/s |
| F5 | ADC 采集滤波 | TIM3 500Hz硬件触发, 64点显示窗口 + 8点安全窗口 |
| F6 | TFT 15页面 UI | 五项设置 + 递增式独立表盘 + EMA显示级滤波 |
| F7 | 远程监控 | OneNET物模型上报 + 网页/小程序实时查看 |
| F8 | 远程控制 | 开关机 + 直接设频率 (CMD:SETFREQ), UI自动同步 |
| F9 | WiFi 配网 | WiFiManager网页配网, 存闪存, 指数退避自动重连 |
| F10 | 故障保护 | SWEEP/RUNNING连续3样本过流确认，PWM→PB10安全关断并锁存FAULT |

### 1.4 引脚分配总表

[📍此处需配图：STM32F103C8T6引脚图，用彩色标出本系统用到的每一根引脚]

| Pin | 功能 | 接哪里 | 绝对不能接错的事 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | ST7735 复位脚 | — |
| PA1 | ESP8266 RST | ESP8266 RST | 硬件复位控制 |
| PA2 | USART2_TX | **ESP8266 RXD** | 交叉! TX接对方的RX |
| PA3 | USART2_RX | **ESP8266 TXD** | 交叉! RX接对方的TX |
| PA4 | TFT_CS | ST7735 片选 | — |
| PA5 | SPI1_SCK | ST7735 SCK | — |
| PA6 | TFT_DC / Flash_MISO | TFT数据/命令与W25Q128 DO动态切换 | 必须由共享SPI模块切换 |
| PA7 | SPI1_MOSI | ST7735 SDA | — |
| PA8 | TIM1_CH1 | IR2103S 上管1 | — |
| PA9 | TIM1_CH2 | IR2103S 上管2 | — |
| PA12 | TFT_BL | TFT背光GPIO开关 | V5.0不支持硬件PWM调光 |
| PA15 | LED_STATUS | PWM状态灯 | JTAG禁用后才释放 |
| PC13 | LED_HEARTBEAT | STM32板载心跳灯 | 低电平点亮 |
| PB0 | ADC_CH8 | CC6920-10A 电流传感器 | — |
| PB1 | ADC_CH9 | 20:1 分压网络 (200k+10k) | 分压比不对会烧ADC! |
| PB3 | LED_POWER | 12V电源指示 | JTAG禁用后才释放 |
| PB4 | LED_WIFI | WiFi LED | JTAG禁用后才释放 |
| PB5 | KEY4 确定 | 按键→GND (IPU) | 确定/PWM启停；WiFi页长按 |
| PB6 | KEY3 DOWN/减 | 按键→GND (IPU) | — |
| PB7 | KEY2 UP/加 | 按键→GND (IPU) | — |
| PB8 | KEY1 返回 | 按键→GND (IPU) | 单击返回，双击主菜单 |
| PB9 | KEY0 电源 | 按键→GND (IPU) | 只控制PB10，不直接启PWM |
| PB10 | PowerControl | 12V 使能 (高=开) | 初始必须拉低；关断先停PWM |
| PB11 | ESP8266 CH_PD | ESP8266 EN | 硬件使能控制 |
| PB12 | W25Q128_CS | Flash片选 | 上电最先钳位为高 |
| PB13 | TIM1_CH1N | IR2103S 下管1 | — |
| PB14 | TIM1_CH2N | IR2103S 下管2 | — |
| PB15 | 蜂鸣器 | 有源蜂鸣器 | — |

> **⚠️ 铁律**: STM32 和 ESP8266 之间只需要 **5 根线**: PA2(→RXD)、PA3(→TXD)、PA1(→RST)、PB11(→CH_PD/EN)、GND(→GND)。TXD 和 RXD 是交叉连接的, **千万别接成 TX→TX、RX→RX**。

---

## 2. 硬件准备与接线

### 2.1 物料清单

[📍此处需配图：全部物料平铺摆放的实物照片，标注每个元件的名称]

| 物料 | 型号 | 数量 | 用途 | 参考价 |
|:---|:---|:---|:---|:---|
| 主控板 | STM32F103C8T6 最小系统板 | 1 | 物理脑 | ¥10~20 |
| WiFi模块 | ESP8266-01 | 1 | 联网脑 | ¥5~10 |
| TFT彩屏 | ST7735 160×128 0.96寸 SPI | 1 | 本地显示 | ¥15~25 |
| **独立LDO** | **AMS1117-3.3V** | 1 | ESP8266独立供电 | ¥1~2 |
| USB-TTL | CH340G | 1 | 烧录ESP8266 | ¥5~10 |
| ST-Link | ST-Link V2 | 1 | 烧录STM32 | ¥10~20 |
| **SPI Flash** | **W25Q128 16MB SOIC-8** | 1 | **[V4.3.0]** 外挂字库+黑匣子+参数存储 | ¥3~5 |
| 全桥驱动 | IR2103S + MOSFET×4 | 1 | 功率输出 | 自制 |
| 电流传感器 | CC6920-10A | 1 | 电流采集 | ¥5~10 |
| 按键 | 轻触开关 6×6mm | 4 | F+/F-/ON/PAGE | ¥2 |
| LED | 3mm ×6 | 6 | 状态指示 | ¥3 |
| 蜂鸣器 | 有源蜂鸣器 5V | 1 | 故障报警 | ¥2 |
| 杜邦线 | 母对母 20cm | 若干 | 接线 | ¥2 |

### 2.2 接线

**[📍此处需配图：手绘或软件画的接线示意图，用不同颜色标注，特别圈出"独立供电"和"交叉连接"]**

**STM32 ↔ ESP8266 五线连接**:
```
STM32 PA2 (TX)  ──── 橙线 ──── ESP8266 RXD     ← 数据: STM32→ESP8266
STM32 PA3 (RX)  ──── 绿线 ──── ESP8266 TXD     ← 数据: ESP8266→STM32
STM32 PA1       ──── 黄线 ──── ESP8266 RST     ← 硬件复位控制
STM32 PB11      ──── 蓝线 ──── ESP8266 CH_PD   ← 硬件使能控制
STM32 GND       ──── 黑线 ──── ESP8266 GND     ← 必须共地!
```

**ESP8266 独立供电**:
```
5V电源 → AMS1117-3.3 Vin
AMS1117-3.3 Vout → ESP8266 VCC + CH_PD(上拉10kΩ)
AMS1117-3.3 GND → 电源GND + ESP8266 GND
Vin并100μF+0.1μF, Vout并100μF+0.1μF
```

> ---
> ### 🛠️ 调试避坑与排错指南 #1: 供电
>
> **⚠️ 最常见死法**: 直接用 STM32 核心板上的 3.3V 给 ESP8266 供电。
>
> **现象**: ESP8266 一上电就反复重启, 串口循环打印 `[System] ESP8266 Booting...`。
>
> **根因**: ESP8266 WiFi 发包瞬间电流 ~300mA。STM32 核心板上的 AMS1117 给 STM32 自用后只剩不到 100mA, 根本带不动。
>
> **解决**: **必须加一片独立的 AMS1117-3.3V 给 ESP8266 单独供电**, 输入输出各并 100μF + 0.1μF 电容。
>
> **验证方法**: 拿万用表量 ESP8266 VCC 对 GND 的电压。WiFi 发包时如果掉到 3.0V 以下, 就是供电不足。
> ---

### 2.3 烧录 ESP8266 时的特殊接线

烧录 ESP8266 固件时, 需要把 ESP8266-01 从 STM32 旁边拆下来, 接到 USB-TTL 模块上:

| USB-TTL | ESP8266-01 | 备注 |
|:---|:---|:---|
| TXD | RXD | 交叉连接 |
| RXD | TXD | 交叉连接 |
| 3.3V | VCC + CH_PD | — |
| GND | GND + **GPIO0** | **GPIO0 必须接地才能进烧录模式** |

> **⚠️ GPIO0 必须接 GND** 才能进入烧录模式。烧录完成后, **断开 GPIO0 和 GND 之间的连线**, 重新上电 ESP8266 才会运行你的程序。

[📍此处需配图：USB-TTL接ESP8266-01的实物照片，红色箭头指向GPIO0-GND的短接线]

---

## 3. OneNET 云平台配置

### 3.1 注册与创建产品

1. 打开 [open.iot.10086.cn](https://open.iot.10086.cn) → 注册(免费) → 登录
2. 开发者中心 → 创建产品
3. 参数填写:

| 字段 | 值 | 说明 |
|:---|:---|:---|
| 产品名称 | WPT 无线充电 | 随便起 |
| 产品协议 | **MQTT (新版 Studio)** | 别选错, 不是旧版 MQTT |
| 节点类型 | 直连设备 | — |
| 联网方式 | Wi-Fi | — |

4. 创建后 → 设备管理 → 添加设备 → 设备名称填 `20260001`
5. **立刻记下这三样东西**, 后面全要用:

| 字段 | 示例值 | 用途 |
|:---|:---|:---|
| 产品ID (Product ID) | `1iS397oJFL` | 设备唯一标识的一部分 |
| 设备名称 | `20260001` | 自定义, 但固件里必须一致 |
| Token | `version=2018-10-31&res=...` | MQTT连接密码, **注意devices是复数!** |

[📍此处需配图：OneNET控制台截图，红框标出产品ID、设备名称、Token三个关键字段]

### 3.2 物模型定义

物模型就是告诉 OneNET "我的设备有哪些可以查看和控制的属性"。**标识符大小写必须和 ESP8266 固件里一致**。

在 OneNET Studio → 物模型 → 添加属性:

| 标识符 | 名称 | 数据类型 | 取值范围 | 步长 | 单位 | 读写 | 说明 |
|:---|:---|:---|:---|:---|:---|:---|:---|
| V | 电压 | double | 0~50 | 0.01 | V | 读写 | 外部输入直流电压 |
| I | 电流 | double | 0~10 | 0.001 | A | 读写 | 外部输入直流电流 |
| F | 显示频率 | int32 | 20000~200000 | 实际可达值 | Hz | 读写 | 实际输出频率 |
| Switch | 开关 | bool | true/false | 1 | — | 读写 | PWM启停控制 |
| SetFreq | 设置频率 | int32 | 20000~200000 | 1000 | Hz | 读写 | 运行期目标频率，不修改下次启动档位 |

> **⚠️ 关键**: `V` 和 `I` 是**外部电源输入**到逆变器的直流电压和电流, 不是谐振腔内部的高频高压。它们通过 STM32 的 ADC 引脚直接测量得到, 反映的是电源适配器的实际输出情况。

> ---
> ### 🛠️ 调试避坑与排错指南 #2: Token 与认证
>
> **⚠️ 最隐蔽的坑**: Token 里 `devices` 写成了 `device`（少一个 s）。
>
> **现象**: ESP8266 串口日志显示 `MQTT Connect failed, rc=-2` 或网页显示 `authentication failed: invalid res`, 让你以为服务器挂了或者网络不行。
>
> **根因**: OneNET 资源路径是 `products/{pid}/devices/{dname}`, 注意是复数的 **devices**, 不是 device。这个拼写错误在 URL 编码的 Token 里完全看不出来, 只能靠复制粘贴的正确性。
>
> **排查**: 把 Token 里的 `%2F` 替换成 `/` 后肉眼检查。正确格式: `res=products/xxx/devices/xxx`, 错误格式: `res=products/xxx/device/xxx`。
>
> **解决**: **直接从 OneNET 设备详情页复制 Token**, 不要手打, 不要从别的地方复制。
> ---

---

## 4. STM32 固件开发

### 4.1 开发环境

- Keil MDK-ARM V5 (uVision)
- 器件包: Keil.STM32F1xx_DFP.2.2.0
- 固件库: **SPL V3.5.0 (标准外设库, 不是HAL!)**
- 编译器: ARMCC V5.06 update 5
- 工程文件: `Keil_Project/Project.uvprojx`

打开工程 → F7 编译 → 确认 `0 Error(s)` → F8 烧录。

> **ARMCC V5 不支持 `--multibyte_chars`**, UTF-8 中文字符串必须用 hex escape: `"\xe7\x94\xb5\xe5\x8e\x8b"` 表示"电压"。

### 4.2 工程结构

```
Keil_Project/
├── Hardware/               ← 硬件驱动 (不可跨模块访问私有变量)
│   ├── Ui_Controller.c/h   ← 15页面 UI 状态机 + 五项设置 + 递增式独立表盘
│   ├── Tft_Driver.c/h      ← ST7735 SPI+DMA + Flash/ROM 双路径字库
│   ├── Spi1_Shared.c/h     ← TFT/W25Q128总线所有权、切换与超时恢复
│   ├── W25Q_Driver.c/h     ← 16MB Flash边界检查、超时和二分检索
│   ├── TFT_Font_Data.h     ← ASCII 95字 + 中文4字 + 图标 (ROM回退)
│   ├── Esp8266_Driver.c/h  ← USART2 RX帧队列 + TX中断环形缓冲
│   ├── App_Network.c/h     ← WiFi+心跳+帧快照+遥测门控 (以前在Hardware, 现移至User)
│   ├── Adc_Driver.c/h      ← TIM3 500Hz触发 + 64点显示/8点安全窗口
│   ├── Inverter_Control.c/h← 软启动 + 频率斜坡 (146行)
│   ├── Key_Driver.c/h      ← 5键 FSM + 独立双击/长按能力
│   ├── Led_Driver.c/h      ← 4 LED状态指示
│   ├── Pwm_Driver.c/h      ← TIM1 全桥 PWM 20-200kHz原子更新
│   └── Buzzer_Driver.c/h   ← 蜂鸣器
├── User/
│   ├── Sys_Core.c/h        ← 5状态 + 功率/故障API + 硬互锁 + 公共调度
│   ├── App_Network.c/h     ← 网络状态机 + S=0/1/2/3遥测
│   ├── App_Storage.c/h     ← 后台参数保存 + Blackbox V2
│   ├── main.c              ← 程序入口和状态分发
│   └── stm32f10x_it.c/h    ← SysTick/ADC DMA/USART2 ISR
├── System/
│   ├── Checksum.c/h        ← CRC32/CRC8统一校验服务
│   └── Sys_Timer.c/h       ← SysTick 1ms时基
├── Library/                ← SPL V3.5.0 (只读, 不可修改)
└── Start/                  ← CMSIS + system_stm32f10x
```

### 4.3 核心代码精讲

#### 4.3.1 Sys_Timer — 程序的脉搏

整个程序只有**一个时基**。`SysTick_Handler` 里**只能有一行** `Sys_Timer_IncTick()`, 不能放任何业务代码。

所有周期任务都用一个模式:

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
        last = Sys_Timer_GetTick();
        // 你的业务逻辑
    }
}
```

利用 uint32_t 减法溢出自动回绕, 不需要特殊处理。即使程序连续跑 49.7 天也不会出问题。

#### 4.3.2 PWM — 全桥的心脏

**频率公式**: `ticks = 72000000 / freq_Hz`, 如果 ticks 是奇数就 +1（强制偶数 = 正负半周对称 = 不偏磁）。

**这导致频率量化**: 你设 103kHz → `72000000/103000 = 699`(奇→700) → 实际 = `72000000/700 = 102.857kHz` → 显示 **102kHz**。

不是 bug, 是定时器只能用整数分频的物理限制。网页端和小程序都做了预计算修正, 你选的数字就是实际会显示的。

**死区** `DEADTIME_NS = 1000ns`, 编译期自动换算寄存器值。BDTR 寄存器线性段 0~127 断言确保不过界。

**原子更新 ARR+CCR**:

```c
TIM1->CR1 |= TIM_CR1_UDIS;     // 暂停影子寄存器
TIM_SetAutoreload(TIM1, arr);
TIM_SetCompare1(TIM1, ccr);
TIM_SetCompare2(TIM1, ccr);
TIM1->EGR  |= TIM_EGR_UG;      // 软件触发, 一次性全部更新
TIM1->CR1  &= ~TIM_CR1_UDIS;   // 恢复
```

不这么做的话, 在 ARR 和 CCR 写入之间可能触发 Update Event, 新周期配上旧占空比 → 偏磁 → 炸管。

[📍此处需配图：示波器抓取的100kHz PWM波形图，标注死区、50%占空比、互补输出]

#### 4.3.3 软启动扫频 — 非阻塞状态机

```
SS_IDLE ──Trigger──→ SS_SWEEP(按锁定档位起点) ──降频──→ SS_DONE(保存目标)
   ↑                    │                           │
   └──Stop──────────────┘                           │
   │                                                 │
   └───── Fault(过流) → SS_FAULT (锁存, 按键复位) ────┘
```

低频档保存范围20.0–99.9kHz，起点99.9kHz、步长100Hz、节拍10ms；高频档保存范围100–200kHz，起点200kHz、步长1kHz、节拍10ms。每次启动锁定当前档位与目标，设置写入只在下一次启动生效。全程**非阻塞**——扫频时 TFT 照刷、按键照样响应、过流保护照样生效。

PWM统一硬边界为20–200kHz；功率级允许工作范围仍须由实机谐振参数、限流条件和温升测试确认。

#### 4.3.4 频率渐变斜坡

当收到 `CMD:SETFREQ:100000` 时, 不是瞬间跳频, 而是每 10ms 向目标方向步进 **1000Hz**, 速率 **100kHz/s**。50kHz 的跨度大约 0.5 秒到达。

这不仅听起来优雅, 更重要的是**物理上安全**——谐振腔需要时间适应新的工作点, 瞬间大跨度跳频可能引起过流或失谐。

#### 4.3.5 ADC — TIM3 500Hz硬件触发 + 双窗口滤波

100kHz 强磁场下软件启动ADC容易产生采样抖动。V5.0.2由TIM3 TRGO每2ms硬件触发ADC1双通道扫描，DMA完成中断只复制快照，主循环再做O(1)滑动窗口:

```c
old = buf[idx];           // 保存最旧
buf[idx] = new_sample;    // 覆盖新值
accum += buf[idx];        // 加入新值
if (filled >= 64) accum -= old;  // 去掉最旧
```

64点窗口提供稳定的显示V/I，8点窗口提供响应更快的安全电流。采样超过20ms未更新会被视为不新鲜；SWEEP/RUNNING中会直接锁存ADC故障。校准状态机只允许在PB10关闭时推进。

#### 4.3.6 显示/安全分离滤波链

| 层级 | 模块 | 滤波对象 | α | 用途 |
|:---|:---|:---|:---|:---|
| 安全级 | `Adc_Driver` 8点窗口 | DMA原始电流 | — | SWEEP/RUNNING快速过流保护 |
| 显示级 | `Adc_Driver` 64点窗口 + UI EMA | DMA原始V/I | 0.25 | UI仪表盘 + 综合监测页 |
| 数字量 | `Pwm_Driver_Get_Frequency()` | 无滤波 | — | 频率(零迟滞, 保证调频跟手) |

显示和安全链路解耦的关键原因：UI需要稳定，保护需要快速；任何页面切换都不能影响安全窗口和过流计数。

#### 4.3.7 Sys_Safety — 独立安全监测

Sys_Safety由统一公共调度器调用, 与 UI 完全解耦:

- **PB10手动电源**: KEY0只切换12V；关闭时严格先停PWM/MOE再拉低PB10
- **启动门控**: 仅IDLE、PB10已开、ADC校准READY、采样新鲜且无FAULT时允许KEY4/远程ON启动
- **过流检测**: SWEEP和RUNNING都使用8点安全电流，连续3个新样本 >5.0A才锁存FAULT
- **FAULT闭锁**: 首故障原因锁存并冻结故障快照；PWM与12V强制关闭，KEY0不能绕过FAULT

#### 4.3.8 TFT 显示 (ST7735 Green Tab)

| 参数 | 值 |
|:---|:---|
| SPI | Mode 3, 18MHz, DMA1_Channel3；由Spi1_Shared切换TFT/Flash所有权和PA6方向 |
| 分辨率 | 160×128 横屏, MADCTL=0xA0 |
| SetWin 偏移 | X+1, Y+2 (Green Tab 特有) |
| 字库 | W25Q128全字库20897字；ROM回退为ASCII 95字 + 必要中文4字 + 图标 |
| 字库位序 | 全部 LSB-first, `TFT_Font_Data.h` 统一管理 |
| 图标 | WIFI(4帧+动画6帧), MQTT(3态+动画6帧), ICON_STAR |
| 总线恢复 | 访问超时或模式异常时释放双CS、复位SPI状态并返回错误，不永久卡死主循环 |

#### 4.3.9 UI — 15页面五项设置 + 递增式独立表盘

**页面枚举 (Ui_Page)**:

| 页面 | 枚举值 | 说明 |
|:---|:---|:---|
| MAIN_MENU | 0 | 主菜单 — 4项: 启动/停止PWM, 监测, 无线配网, 故障清除 |
| MONITOR_SUB_MENU | 1 | 监测子菜单 — 5项: 综合监测, 频率, 电压, 电流, 返回 |
| SWEEP | 2 | 扫频页 — 实时频率+进度 (F/V/I) |
| MONITOR_SUMMARY | 3 | 综合监测 — F/V/I 同屏显示 |
| MONITOR_FREQ | 4 | 频率仪表盘 — 递增式分段表盘 |
| MONITOR_VOLT | 5 | 电压仪表盘 — 递增式分段表盘 |
| MONITOR_CURR | 6 | 电流仪表盘 — 递增式分段表盘 |
| WIFI_SETUP | 7 | 无线配网 — 状态+清除 |
| FAULT | 8 | 故障清除 — 过流锁存 |
| SETTING | 9 | 设置主菜单 |
| SETTING_LANG | 10 | 中英文切换 |
| SETTING_FREQUENCY | 11 | 低频/高频启动档位总览与编辑 |
| SETTING_SPACING | 12 | 字符间距 |
| SETTING_ICONS | 13 | 全局菜单光标图标 |
| SETTING_COLOR | 14 | 颜色方案 |

**底部栏**: 已删除。页面操作全部由KEY0~KEY4完成，避免旧按键提示与V5.0 PCB不一致。

**UI Phase 架构**: 7 个 Phase 依次执行 — Global Icons(0) → Fault detection(1) → Sweep→Summary(2) → Key dispatch(3) → Page tracking(4) → 200ms incremental(5) → Cursor clamp(6) → Draw(7)。

**五项设置**: 语言、启动频率、字符间距、光标图标、配色方案。启动频率页保存低频档20.0–99.9kHz与高频档100–200kHz；确认保存副本，返回取消本次编辑，KEY1双击从任意设置子页回主菜单。PA12背光保留GPIO开关能力，不提供设置项。

**递增式独立表盘**: 电压按0–20V/2V、20–40V/5V、40–50V/10V分段；电流按0–1A/0.1A、1–3A/0.5A、3–5A/1A分段；频率随锁定档位使用20–50/5、50–80/10、80–100/20kHz或100–140/5、140–180/10、180–200/20kHz分段。每格角度相同，跨分段连续；中央主数值由8×16字模2倍绘制，动态周期仅差分刷新圆弧、数值和状态。

#### 4.3.10 KEY — 5键 FSM

| 按键 | 引脚 | 功能 |
|:---|:---|:---|
| KEY0 | PB9 | 单击切换PB10 12V；不直接启动PWM |
| KEY1 | PB8 | 单击返回上一页；双击回主菜单 |
| KEY2 | PB7 | 单击UP/加 |
| KEY3 | PB6 | 单击DOWN/减 |
| KEY4 | PB5 | 单击确定/PWM启停；仅WiFi页允许长按清凭证 |

#### 4.3.11 LED — 4灯状态指示

| LED | 引脚 | 用途 |
|:---|:---|:---|
| LED_WIFI | PB4 | 在线常亮、重连慢闪、离线熄灭 |
| LED_POWER | PB3 | PB10 12V开启时常亮 |
| LED_STATUS | PA15 | SWEEP慢闪、RUNNING常亮、其余熄灭 |
| LED_HEARTBEAT | PC13 | STM32主程序500ms闪烁，低电平点亮 |

#### 4.3.12 App_Network — 网络层

**启动流程**: `main.c` → `Sys_Post_Init()` → `App_Network_Start_Connect()` → 非阻塞硬件初始化。

**指数退避重试**:
```
重试 0-2:   5s     (必须 >4s ESP启动时间)
重试 3-7:   15s
重试 8-13:  30s
重试 14-21: 60s
重试 22-31: 2min
重试 32-46: 5min
重试 47+:   30min
永不 FAILED — 自动恢复
```

**热点加速**: RSSI ≥ -35 (极近) → 直接重置为 3s 级快速直连。

**心跳超时**: 8s 无 ESP 帧 → 判定离线 → 自动重连。

**帧处理安全**:
- `Try_Copy_Rx_Frame`: 原子拷贝消除 TOCTOU 竞态
- `ss_cmd` / `conn_cs` 帧内快照: 防 ELSE-IF 链间状态变化

**遥测策略**: 网络在线时每500ms发送一帧，与当前TFT页面无关；S=0/1/2/3分别表示IDLE、SWEEP、RUNNING、FAULT。

**远程指令 UI 同步**: CMD:ON/OFF → `Ui_Controller_Force_Page_And_Reset()` 同时复位页面+光标。

#### 4.3.13 App_Storage — 后台参数保存与 Blackbox V2

配置A/B仍位于 `0x300000` / `0x301000`。保存请求先复制到RAM，只有IDLE调度才执行擦写，并在写后回读CRC32校验；功率运行期间不会在业务调用栈里阻塞擦除。

| 区域 | 地址 | 用途 |
|:---|:---|:---|
| 配置A/B | 0x300000 / 0x301000 | 参数双副本，CRC32校验 |
| 元数据A/B | 0x310000 / 0x311000 | 递增generation双扇区日志，掉电择新恢复 |
| 循环日志 | 0x312000 ~ 0x6CFFFF | 12B/条、CRC8、200ms采样、可跨页恢复 |
| 故障快照 | 0x6D0000 ~ 0x70FFFF | 64个4KB槽，25条故障前 + 25条故障后样本 |

正常日志写指针每60条做一次元数据检查点。启动时先选有效且generation最新的元数据，再向前扫描未入账记录；损坏条目不会让整段日志失效。故障发生时只在RAM中冻结前5秒并继续收集后5秒，确认PWM和PB10均关闭后才写Flash。

### 4.4 系统全局状态机

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

| 状态 | PWM | 主循环调度 |
|:---|:---|:---|
| INIT | 关 | 初始化阶段, 一次性 |
| IDLE | 关 | Key+ADC+Network+Safety+UI |
| SWEEP | 开 | + Soft_Start（按锁定档位降至保存目标） |
| RUNNING | 开 | + Freq_Ramp |
| FAULT | 关 | + FAULT UI, 取消所有斜坡 |

### 4.5 主循环

```c
int main(void) {
    Sys_Clamp_ESP(); Sys_Timer_Init(); Sys_Hardware_Init();
    W25Q_Driver_Init(); Tft_Driver_Font_Init(); App_Storage_Init();
    Sys_Startup_Screen(); Sys_Post_Init();
    while (1) {
        switch (Sys_Core_Get_State()) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;
            default: Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT); break;
        }
    }
}
```

四个 `Sys_Run_*()` 都复用 `Sys_Core_Run_Common()`：统一执行控制不变量、ADC、安全、按键/UI、网络、Blackbox、后台存储、LED/蜂鸣器、IWDG和WFI。状态函数只负责软启动或调频等专属动作，避免漏跑安全任务。

> ---
> ### 🛠️ 调试避坑与排错指南 #3: PWM 不输出 / 周期不对
>
> **现象**: TFT 频率在变, 但示波器上看不到 PWM 波形, 或者波形不是 50% 占空比。
>
> **排查步骤**:
> 1. 先用示波器量 PA8/PA9, 确认有没有方波 (有 → 检查 IR2103S 驱动电路; 没有 → 往下)
> 2. 检查 `TIM_CtrlPWMOutputs(TIM1, ENABLE)` 是否在软启动 Trigger 时调用了
> 3. 确认 `TIM1->BDTR` 里 MOE 位是否为 1。如果被刹车意外触发, MOE 会被硬件清零
> 4. 如果是有输出但实际频率和设置值差很多, 检查 `TIM1_CLK_HZ` 是不是 72M
>
> **⚠️ 千万注意**: App_Net 和 UI 模块**禁止直接操作 `TIM_Cmd`/`TIM_CtrlPWMOutputs`/`TIM1->ARR`**, 必须通过 PWM 模块的公开接口!
> ---

### 4.6 编码规范 (零容忍)

| 层次 | 规则 | 正确 | 违规 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` 强制前缀 | `Sys_Run_Led_Tick()` | `Led_Tick()` |
| 静态变量 | `s_description` | `s_gauge_val_str` | `uiState` |
| 全局变量 | `g_description` | `g_sys_state` | `Sys_State_Global` |
| 枚举值 | `MODULE_NAME_VALUE` 全大写 | `SYS_STATE_IDLE` | `State_Idle` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |

- 公开函数必须带 `@brief` + `@param`/`@retval` 注释
- 禁止 `//` 双斜杠 (ARMCC V5), 统一用 `/** */`
- 周期任务用 `Sys_Timer_Get_Tick() - last >= PERIOD`, 禁止运行时阻塞
- `.h` 只公开接口, `.c` 全 static 内部实现, 禁止 `extern` 私有变量
- 分层单向: Hardware → System → Application

---

## 5. ESP8266 固件开发

### 5.1 环境搭建

1. 下载 [Arduino IDE](https://www.arduino.cc/en/software)
2. 文件→首选项→附加开发板管理器→填入 `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. 工具→开发板管理器→搜 `ESP8266`→安装 3.x
4. 库管理安装: `ArduinoJson` v7, `PubSubClient`, `WiFiManager` v2.x
5. 开发板选 **Generic ESP8266 Module**, Flash 1M, CPU 80MHz

### 5.2 核心逻辑

[📍此处需配图：ESP8266固件流程图，从setup→loop→ensureConnected→串口收发的完整流程]

#### 5.2.1 WiFiManager 配网

```cpp
WiFiManager wifiManager;
wifiManager.setConfigPortalTimeout(180);
if (!wifiManager.autoConnect("STM32_WPT_Config")) {
    wifiManager.startConfigPortal("STM32_WPT_Config");
}
```

首次上电→开热点 `STM32_WPT_Config` (密码 `wpt2026conf`)→手机连上→浏览器弹配网页→选WiFi输密码→存闪存→重启。以后自动连。

**换WiFi时**: 进入STM32的WiFi配网页，长按 KEY4 >3秒发起 `CMD:CLEAR` 二次确认；确认后ESP8266清除凭证并重启。其他页面长按KEY4无效，KEY0始终只负责12V电源。

#### 5.2.2 双 MQTT 连接 + 指令去抖

| 连接 | 地址 | 用途 |
|:---|:---|:---|
| OneNET MQTT | `mqtts.heclouds.com:1883` | 物模型数据上报 + 接收指令 |
| EMQX 公共 | `broker.emqx.io:1883` | Web端直读(可选) |

**非阻塞重连**: 每 5 秒检查一次, 断开自动重连, 不阻塞 loop。

**指令去抖**: `Mqtt_Task_Parse_Command` 2s 窗口内相同 payload 直接丢弃, 防止 MQTT 重复投递导致误触发。

**Switch 状态上报**: 仅 `s==2` (SS_DONE) 上报 true, `s==1` (SWEEP) 为过渡态不上报运行。

**SetFreq 量化**: `(val/1000)*1000`, 与 STM32 PWM 1kHz 步进一致。

**遥测频率**: 仅在 running 时上报真实 F 值, 否则上报 0。完全透传 STM32 决策。

#### 5.2.3 JSON 转换 + 全链路数据一致性

STM32 发来 `{"V":12.50, "I":1.23, "F":100000, "S":2}` → ESP8266 转为 OneNET 格式上报。

| 状态 | STM32 遥测 | ESP 上报 | Web/小程序显示 |
|:---|:---|:---|:---|
| IDLE | V=真实,I=真实,F=0,S=0 | Switch=false, V/I=真实, F=0 | 停机/V/I 正常/F=0 |
| SWEEP | V/I=显示滤波,F=真实Hz,S=1 | Switch=false, V/I/F=真实 | 扫频中/实时值 |
| RUNNING | V/I=显示滤波,F=真实Hz,S=2 | Switch=true, V/I/F=真实 | 运行中/实时值 |
| FAULT | V=真实,I=真实,F=0,S=3 | Switch=false, V/I=真实, F=0 | 故障/实时V/I/F=0 |

**核心原则**: V/I 始终上报真实物理量 (任何状态下 ADC 均可采集), 仅 F 在 PWM 未运行时强制为 0。

> ---
> ### 🛠️ 调试避坑与排错指南 #4: ESP8266 反复重启 / 连不上 WiFi
>
> **现象分类**:
> - **串口循环输出 `Booting...`**: 供电不足, 或 WiFiManager 没有已存凭据又没有 startConfigPortal 兜底
> - **WiFi 连上了但 OneNET 离线**: MQTT 地址错误 或 Token 错误
> - **OneNET 命令"响应超时"**: 固件里缺少 set_reply 应答逻辑
>
> **手把手排查**:
> 1. 先测 ESP8266 VCC: 必须是稳定的 3.3V (万用表量, 不是看电源标签)
> 2. 串口监视器 115200 看日志: `[WiFi] Connected! IP: xxx` 说明WiFi OK
> 3. `[MQTT] >>> OneNET Connected successfully! <<<` 说明MQTT OK
> 4. 两项都 OK 但 OneNET 还离线 → 进 OneNET 控制台看设备状态, 可能是 Token/产品ID 填错了
> 5. 从串口发 `{"V":12.5,"I":1.2,"F":100000}\n` → OneNET 数据流应立刻看到
> ---

### 5.3 ESP8266 烧录

1. USB-TTL 接 ESP8266-01 (**GPIO0 必须接 GND**)
2. Arduino IDE → 上传(→), 等待 ~30 秒
3. 烧录完成后 **断开 GPIO0-GND** → 重新上电
4. 串口监视器 115200 看日志

---

## 6. 网页控制台 (Cloudflare Pages)

### 6.1 部署

1. 确保代码在 GitHub: `https://github.com/Ran-sh/WPT_Onenet_IoT`
2. [pages.cloudflare.com](https://pages.cloudflare.com) → GitHub 登录
3. Create project → 选仓库 → 不设构建命令(静态站点) → Deploy
4. 得到 `wptonenet.483763727.workers.dev` 公网地址

### 6.2 关键设计

- **乐观更新**: `setProperty` 成功后立即写 localStorage + 3s 乐观锁, 轮询同步时忽略云端旧值
- **重试**: `setProperty` 网络/业务错误各重试 3 次 (500ms/800ms 间隔)
- **连接指示**: 同步成功(绿) / 设备离线(黄) / 失败(红)
- **数据模型**: `config.js` DEFAULT_DATA_MODEL → sensors(V/I/F) + controls(Switch/SetFreq)
- **频率映射**: `fromCloud: v => Math.floor(v/1000)` / `toCloud: v => v*1000`, Web 显示 kHz

### 6.3 各页面功能

| 页面 | 功能 | 特别说明 |
|:---|:---|:---|
| 首页 | V/I/F 实时数据卡片 + 连接状态 | 5s 轮询, 指数退避 |
| 控制 | 启停开关 + 频率设置(kHz) + 操作记录 | 5s 同步, Switch防抖 |
| 监测 | 实时折线图 | Y轴动态缩放 |
| 历史 | 历史数据表 + 长期变化曲线 | 数据筛选+导出 |
| 报警 | 报警记录列表 | 过滤+已读+清空 |
| 设置 | OneNet配置 / 数据模型管理 | 支持传感器/控制器动态增删 |

---

## 7. 微信小程序 (6页面+Component)

### 7.1 架构

V4.2.0 小程序**直连 OneNET HTTP API**, 与网页端完全相同的后端逻辑:

```
小程序 ──HTTPS── OneNET API ──MQTT── ESP8266 ── STM32
```

- **6 页面**: index / monitoring / control / history / alerts / settings
- **custom-tab-bar** Component: 5 tab (⌂ ◉ ⊛ 🗂 ⚙), 无高亮 (selected=-1), 纯导航
- **数据模型**: `utils/config.js` 单一来源 (DEFAULT_DATA_MODEL)
- **API 层**: `utils/onenet.js` — `getLatestData()` 双请求并行, HTTP 细化错误 (401/403/404/429/503), Mock fallback
- **首页**: 动态渲染传感器卡片 + 控制卡片 (`buildCards()` → WXML 遍历)
- **控制页**: Switch 防抖 (`_switchPending` 立即重置) + 频率 Swiper + 操作日志
- **历史页**: Canvas 2D 曲线 + 数据表(圆圈状态 ✓/✕) + CSV 导出

### 7.2 关键禁忌

- Component 方法名不能叫 `switchTab` (与 `wx.switchTab` API 冲突), 用 `onSwitchTab`
- `templates/` 目录已删除, 不可引用
- 存储键: `wpt_latest`, `wpt_history`(1440max), `wpt_alerts`(50max) 等

---

## 8. 联调指南

### 8.1 上电顺序

> **⚠️ 关键**: **先给 ESP8266 上电, 等 5 秒, 再给 STM32 上电**。

1. ESP8266 独立 3.3V 上电 → 等 ~5s WiFi+MQTT 连好
2. STM32 上电 → TFT 显示启动画面 → `Sys_Post_Init()` 启动联网
3. ESP8266 发 `STATUS:ONLINE` → TFT 显示主菜单
4. 网页/小程序应该能看到数据

### 8.2 逐环节验证

| 步骤 | 检查什么 | 怎么算通过 | 如果没过 |
|:---|:---|:---|:---|
| 1 | STM32 串口 | 按 KEY0 后串口助手 115200 收到 `{"V":...}` | 检查 PA2/PA3 接线+USART2 |
| 2 | ESP8266 WiFi | 串口监视器 `Connected! IP:` | 检查供电+WiFiManager 配网 |
| 3 | ESP8266 MQTT | 串口监视器 `OneNET Connected!` | 检查 Token+产品ID |
| 4 | OneNET 在线 | 控制台设备状态 "在线" | 回头查步骤 2-3 |
| 5 | OneNET 数据 | 控制台数据流有 V/I/F | 检查串口 JSON 格式 |
| 6 | 网页数据 | 首页显示实时数据 | 检查设置页 OneNet 配置 |
| 7 | 网页控制 | 点开关, TFT 响应 | 检查 CMD 指令链路 |

### 8.3 调试工具速查

| 工具 | 干什么用 | 怎么用 |
|:---|:---|:---|
| 串口助手 115200 8N1 | 连 STM32 看 JSON 输出 | PA2/PA3 接 USB-TTL |
| Arduino 串口监视器 | 看 ESP8266 连接日志 | USB-TTL 接 ESP8266 |
| OneNET 控制台 | 看设备状态+数据流 | [open.iot.10086.cn](https://open.iot.10086.cn) |
| 浏览器 F12 Console | 看网页 API 请求 | 打开网页按 F12 |
| curl 命令 | 直接测 OneNET API | 见下面示例 |

```bash
# 直接测试 OneNET API 是否正常
curl "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=你的产品ID&device_name=你的设备名" \
  -H "Authorization: 你的Token"
```

---

## 9. 故障速查总表

| 症状 | 第一反应去查什么 | 大概率是 |
|:---|:---|:---|
| ESP8266 反复重启 | 万用表量 VCC | 供电不足 |
| OneNET 离线 (rc=-2) | MQTT 服务器地址 | `mqtts.heclouds.com` 写错了 |
| OneNET 在线但无数据 | 串口助手看 STM32 | STM32 没发 JSON 或 TX/RX 接反 |
| TFT 中文乱码/错字 | CN_INDEX vs CN_FONT 对齐 | 字库索引与字模不匹配 |
| 网页"未连接" | F12 Console 看 API 响应 | Token 或设备名填错 |
| 设了频率 TFT 不对 | 正常现象 | 频率量化, 网页端已自动修正 |
| 网页端 404 | Cloudflare 分支 | 推送到了错误分支 |
| 控制页开关状态不对 | ESP8266 是否上报了 S 字段 | 需烧录最新固件 |
| Switch 命令只能发一次 | `_switchPending` 延迟过长 | 立即重置, 不要 setTimeout |
| TFT "综合监测"显示"失败" | CN_FONT[74..75] 字模 | CN_INDEX 与 CN_FONT 不对齐 |

---

## 10. 踩坑全记录 (避坑指南精华)

### 10.1 频率控制方案的四次迭代

| 版本 | 怎么做 | 为什么不好 |
|:---|:---|:---|
| V1 CMD:F_UP/DOWN | 每次 ±1kHz | 小程序点一下触发两次 |
| V2 OneNET FreqAdd/FreqSub | 虚拟按键点动 | 物模型复杂, 两个布尔键 |
| V3 CMD:SETFREQ (跳变) | 瞬间设频率 | 跳频太暴力, 不安全 |
| **V4 SETFREQ + 渐变** ✅ | 1000Hz/10ms 斜坡到达 | 平滑, 可中断, **当前方案** |

### 10.2 网页部署的四代方案

| 阶段 | 用了什么 | 为什么换了 |
|:---|:---|:---|
| V1 | 本地 Node.js 服务 | 只能自己电脑用 |
| V2 | Netlify | 免费带宽耗尽被暂停 |
| V3 | GitHub Pages | 路由有 bug, 404 |
| **V4** | **Cloudflare Pages** ✅ | 免费不限量, 原生 SPA 路由 |

### 10.3 `device` vs `devices` — 一个字母的惨案

Token 里是 `devices` (复数), 不是 `device`。写成单数会让 OneNET 返回 `authentication failed: invalid res`, 让你怀疑人生半小时。

### 10.4 TFT 中文"综合"变"失败"

**现象**: TFT 上"综合监测"菜单项显示为"失败监测"(实际是"回失监测")。

**根因**: CN_INDEX[73..74]="综合", 但 CN_FONT_16X16[73..74]="失败"。CNLookup 按索引查字模, 找到"综"返回索引73, 但 CN_FONT[73] 的字模是"败"的前一个重复字。

**修复**: 用户提供标准宋体 16×16 字模, 替换 CN_FONT[74]→"综", CN_FONT[75]→"合"。现在 76 字完全对齐。

### 10.5 WiFiManager v2.x `autoConnect` 不自动开 AP

加 `startConfigPortal()` 兜底, 否则没凭据就不开热点, ESP8266 反复重启。

### 10.6 CORS 不是问题的"问题"

一直以为是跨域问题, F12 查了半天 CORS 头。最后发现 OneNET API 本来就返回 `Access-Control-Allow-Origin: *`, 真正的问题是 Token 里 `device` 少了一个 `s`。

### 10.7 审查历史

| 版本 | 关键修复 |
|:---|:---|
| V5.1.0 | 五项设置、配置V2双档启动频率与全局光标；20–200kHz边界、低档99.9kHz/高档200kHz动态扫频；15页面递增式独立表盘。 |
| V5.0.2 | 功率硬互锁 + 500Hz ADC双窗口 + SPI1仲裁 + Blackbox V2 + 14页面UI + USART2中断发送 + 统一调度 |
| V4.2.0 | TFT字库修复 + 底部栏简化 + 全平台版本号统一 |
| V4.1.0 | 9页面TFT UI + 全局状态机 + Sys_Safety + 圆弧能量条仪表盘 |
| V4.0.0 | 小程序全重写 + 网页乐观更新 + ESP去抖 + 数据一致性铁律 |

---

> **全文完**。本文已与 V5.1.0 固件同步。如果你跟着做遇到问题, 回头翻第 9 章故障速查表；涉及功率部分时务必先断开12V并确认PB10为低。祝你的全桥不发烫, ESP8266 不掉线, OneNET 不报401, TFT不显示乱码。
