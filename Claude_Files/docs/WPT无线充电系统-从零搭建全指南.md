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
| **文档版本** | V5.1 |
| **最后更新** | 2026-05-25 |
| **对应固件版本** | V5.1 |
| **GitHub 主仓库** | [Ran-sh/WPT_PWM](https://github.com/Ran-sh/WPT_PWM) (分支 `ONENET`) |
| **网页端仓库** | [Ran-sh/WPT_Onenet_IoT](https://github.com/Ran-sh/WPT_Onenet_IoT) (Cloudflare Pages 部署源) |
| **桥接服务器仓库** | [Ran-sh/WPT_Railway](https://github.com/Ran-sh/WPT_Railway) (小程序桥接) |
| **作者** | Rssss |

### 修改日志

| 版本 | 日期 | 变更说明 |
|:---|:---|:---|
| V5.0 | 2026-05-24 | 全篇重构：新增调试避坑模块、配图标注、双主题小程序、频率渐变斜坡、网页端Cloudflare部署、1.6万字扩写 |
| V5.1 | 2026-05-25 | STM32: 7界面状态机(INIT→CONNECTING→READY→SWEEPING→RUNNING→FAULT), 上电自动连WiFi+3次重试, OneNET遥测门控(仅>=READY发送), 新LED逻辑。ESP8266: SetFreq防覆盖, Switch命令后跳变次遥测, DEBUG关。小程序: fetchAll合并轮询2s, 在线检测(10s超时), Swiper首次同步后锁定。网页: 修复SetFreq下发崩溃 |

---

# WPT 无线充电全桥谐振控制系统 — 开发者保姆级复盘指南

> **阅读指引**: 这是一份"写给我自己"的复盘手稿。写这份文档时我刚从一个月的硬件联调地狱里爬出来，踩过的坑比跑通的代码还多。如果你正在做一个类似的项目——STM32发PWM、ESP8266联网、OneNET云平台、配上网页和小程序——这篇文档就是写给你的。

[📍此处需配图：系统整体实拍，左边电源+STM32+全桥板+ESP8266+OLED，右边手机显示网页控制台]

---

## 1. 项目概述

### 1.1 这是什么

把这几个东西想象成一个团队：

- **STM32** = 工厂车间主任。管着PWM发波（决定输出多大功率/什么频率）、ADC采集（实时盯着电压电流）、遇到故障立刻拉闸。
- **ESP8266** = 车间的前台文员。只管一件事：把车间主任给的数据发到云上，把云上的指令传回车间。它不碰任何生产设备。
- **OneNET** = 公司的云办公系统。所有数据在这里汇总，远程指令从这里发出。
- **网页/小程序** = 你的手机。你在任何地方打开就能看到车间情况，还能远程下命令。

**能做什么**:
- 手机/电脑实时看电压、电流、频率
- 远程开关机
- 远程调频率 (95kHz~150kHz, 步进到实际可达值)
- 看历史数据曲线
- 过流自动保护

### 1.2 整体架构图

```
┌─────────────────────────────────────────────────────┐
│                  你的手机 / 电脑                       │
│    网页: wptonenet.483763727.workers.dev             │
│    小程序: 微信扫码 (需Railway桥接)                    │
└──────────────┬──────────────────────────────────────┘
               │ HTTPS
┌──────────────┴──────────────────────────────────────┐
│   Cloudflare Pages (网页) / Railway (小程序桥接)      │
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
│  • WiFiManager网页配网 • MQTT双连上报                 │
│  • JSON串口透传 • 指令转发 • 自动重连                  │
└──────────────┬──────────────────────────────────────┘
               │ USART2 115200bps (纯文本JSON, 零AT指令)
┌──────────────┴──────────────────────────────────────┐
│        STM32F103C8T6 (物理脑)                        │
│  • TIM1全桥PWM (95k~150kHz, 50%占空比, 1000ns死区)   │
│  • ADC双通道采集+滑动滤波 • 非阻塞软启动扫频            │
│  • KEY/OLED/LED人机交互 • 频率渐变斜坡 • 过流保护      │
└─────────────────────────────────────────────────────┘
```

### 1.3 核心功能清单

| 编号 | 功能 | 一句话说明 |
|:---:|:---|:---|
| F1 | 全桥 PWM 驱动 | TIM1 四通道互补输出, 50% 占空比锁定, PFM 调功 |
| F2 | 非阻塞软启动扫频 | 150kHz→100kHz, 200Hz/10ms, ~2.5s, 不怕堵车 |
| F3 | 防偏磁保护 | 周期ticks强制偶数 + 影子寄存器原子更新 |
| F4 | 频率渐变斜坡 | SETFREQ后500Hz/10ms平滑过渡, 50kHz/s |
| F5 | ADC 采集滤波 | 16样本O(1)滑动平均, 2ms独立任务 |
| F6 | OLED 双页 UI | 控制面板 + 锁屏监控, 128×64 |
| F7 | 远程监控 | OneNET物模型上报+网页/小程序实时查看 |
| F8 | 远程控制 | 开关机 + 直接设频率 (CMD:SETFREQ) |
| F9 | WiFi 配网 | WiFiManager网页配网, 存闪存, 自动重连 |
| F10 | 故障保护 | 过流→SS_FAULT锁存; 95kHz硬下限; 上电MOE=OFF |

### 1.4 引脚分配总表

[📍此处需配图：STM32F103C8T6引脚图，用彩色标出本系统用到的每一根引脚]

| Pin | 功能 | 接哪里 | 绝对不能接错的事 |
|:---|:---|:---|:---|
| PA0 | ADC_CH0 (电流采集) | CC6920-10A霍尔输出 | — |
| PA1 | ADC_CH1 (电压采集) | 20:1分压网络 (200k+10k) | 分压比不对会烧ADC! |
| PA2 | USART2_TX | **ESP8266 RXD** | 交叉! TX接对方的RX |
| PA3 | USART2_RX | **ESP8266 TXD** | 交叉! RX接对方的TX |
| PA7/8/9 | TIM1 CH1N/CH1/CH2 | IR2103S栅极驱动 | 上下管接反直接炸管 |
| PA11/12 | OLED SCL/SDA | SSD1315 0.96寸OLED | 开漏输出, 4.7k上拉 |
| PB0 | TIM1 CH2N | IR2103S栅极驱动 | — |
| PB1 | **GPIO 推挽输出** | ESP8266 CH_PD/EN | 控制ESP8266硬件复位 |
| PB3/4/5 | WiFi/PWM/Ready LED | LED+限流电阻→GND | JTAG禁用后才释放 |
| PB12/13 | KEY0/KEY1 | 按键→GND | 内部上拉, 按下=0 |
| PC13 | Heartbeat LED | LED→3.3V | 低电平有效 |

> **⚠️ 铁律**: STM32 和 ESP8266 之间只需要 **4 根线**: PA2(→RXD)、PA3(→TXD)、PB1(→CH_PD)、GND(→GND)。TXD 和 RXD 是交叉连接的, **千万别接成 TX→TX、RX→RX**。

---

## 2. 硬件准备与接线

### 2.1 物料清单

[📍此处需配图：全部物料平铺摆放的实物照片，标注每个元件的名称]

| 物料 | 型号 | 数量 | 用途 | 参考价 |
|:---|:---|:---|:---|:---|
| 主控板 | STM32F103C8T6 最小系统板 | 1 | 物理脑 | ¥10~20 |
| WiFi模块 | ESP8266-01 | 1 | 联网脑 | ¥5~10 |
| OLED屏 | SSD1315 128×64 0.96寸 I2C | 1 | 本地显示 | ¥8~15 |
| **独立LDO** | **AMS1117-3.3V** | 1 | ESP8266独立供电 | ¥1~2 |
| USB-TTL | CH340G | 1 | 烧录ESP8266 | ¥5~10 |
| ST-Link | ST-Link V2 | 1 | 烧录STM32 | ¥10~20 |
| 全桥驱动 | IR2103S + MOSFET×4 | 1 | 功率输出 | 自制 |
| 电流传感器 | CC6920-10A | 1 | 电流采集 | ¥5~10 |
| 按键 | 轻触开关 6×6mm | 2 | KEY0/KEY1 | ¥0.5 |
| LED | 3mm 红/绿/蓝各1 | 4 | 状态指示 | ¥1 |
| 杜邦线 | 母对母 20cm | 若干 | 接线 | ¥2 |

### 2.2 接线

**[📍此处需配图：手绘或软件画的接线示意图，四根线用不同颜色标注，特别圈出"独立供电"和"交叉连接"]**

**STM32 ↔ ESP8266 四线连接**:
```
STM32 PA2 (TX) ──── 橙线 ──── ESP8266 RXD    ← 数据: STM32→ESP8266
STM32 PA3 (RX) ──── 绿线 ──── ESP8266 TXD    ← 数据: ESP8266→STM32
STM32 PB1       ──── 黄线 ──── ESP8266 CH_PD  ← 硬件复位控制
STM32 GND       ──── 黑线 ──── ESP8266 GND    ← 必须共地!
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
| I | 电流 | double | 0~10 | 0.01 | A | 读写 | 外部输入直流电流 |
| F | 显示频率 | int32 | 95000~150000 | 1000 | Hz | 读写 | 实际输出频率 |
| Switch | 开关 | bool | true/false | 1 | — | 读写 | PWM启停控制 |
| SetFreq | 设置频率 | int32 | 95000~150000 | 1000 | Hz | 读写 | 目标频率 |

> **⚠️ 关键**: `V` 和 `I` 是**外部电源输入**到逆变器的直流电压和电流, 不是谐振腔内部的高频高压。它们通过 STM32 的 ADC 引脚直接测量得到, 反映的是电源适配器的实际输出情况。

[📍此处需配图：OneNET物模型页面截图，箭头标注每个属性的标识符要求大小写一致]

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
- 工程文件: `Keil_Project/Project.uvprojx`

打开工程 → F7 编译 → 确认 `0 Error(s)` → F8 烧录。

### 4.2 工程结构

```
Keil_Project/
├── Hardware/          ← 硬件驱动 (不改架构, 只增删.c/.h)
│   ├── PWM.c/.h       (全桥PWM+软启动+频率渐变)
│   ├── ADC.c/.h       (ADC采集+滑动滤波)
│   ├── KEY.c/.h       (八态FSM按键)
│   ├── OLED.c/.h      (SSD1315 I2C显示)
│   ├── LED.c/.h       (四灯状态)
│   ├── UI.c/.h        (双页UI调度)
│   └── ESP8266.c/.h   (ESP8266硬件驱动)
├── System/
│   └── SysTimer.c/.h  (SysTick时基, 整个系统的脉搏)
├── User/
│   ├── main.c         (初始化+主循环)
│   ├── App_Net.c/.h   (网络应用层)
│   └── stm32f10x_it.c (中断向量, 只能有SysTimer_IncTick!)
├── Library/           (SPL V3.5.0, 只读不修改)
└── Start/             (启动文件)
```

### 4.3 核心代码精讲 (挑最重要的说)

#### 4.3.1 SysTimer — 程序的脉搏

整个程序只有**一个时基**。`SysTick_Handler` 里**只能有一行** `SysTimer_IncTick()`, 不能放任何业务代码。

所有周期任务都用一个模式:

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (SysTimer_GetTick() - last >= PERIOD_MS) {
        last = SysTimer_GetTick();
        // 你的业务逻辑
    }
}
```

利用 uint32_t 减法溢出自动回绕, 不需要特殊处理。即使程序连续跑 49.7 天也不会出问题。

#### 4.3.2 PWM — 全桥的心脏

**频率公式**: `ticks = 72000000 / freq_Hz`, 如果 ticks 是奇数就 +1（强制偶数 = 正负半周对称 = 不偏磁）。

**这导致一个"玄学"现象——频率量化**:

你设 103kHz → `72000000/103000 = 699`(奇→700) → 实际 = `72000000/700 = 102.857kHz` → OLED 显示 **102kHz**。

不是 bug, 是定时器只能用整数分频的物理限制。网页端和小程序都做了预计算修正, 你选的数字就是 OLED 会显示的数字, 不会出现"我设了 103, OLED 显示 102"的困惑。

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
SS_IDLE ──Trigger──→ SS_SWEEP(150kHz) ──250步──→ SS_DONE(100kHz)
   ↑                    │                           │
   └──Stop──────────────┘                           │
   │                                                 │
   └───── Fault(过流) → SS_FAULT (锁存, 按键复位) ────┘
```

参数: 步长 200Hz, 节拍 10ms, 250 步 ≈ 2.5 秒。全程**非阻塞**——扫频时 OLED 照刷、按键照样响应、过流保护照样生效。

**频率硬下限 95kHz** 是容性区红线。低于这个频率谐振腔进入容性区, MOSFET 承受极大开关应力。

#### 4.3.4 频率渐变斜坡

当收到 `CMD:SETFREQ:100000` 时, 不是瞬间跳频, 而是每 10ms 向目标方向步进 **500Hz**, 速率 **50kHz/s**。50kHz 的跨度大约 1 秒到达。

这不仅听起来优雅, 更重要的是**物理上安全**——谐振腔需要时间适应新的工作点, 瞬间大跨度跳频可能引起过流或失谐。

#### 4.3.5 ADC — O(1)滑动平均

100kHz 强磁场下 DMA 瞬时值噪声严重。16 样本滑动平均, 每次只做一次加法+一次减法, 不随窗口增大而增加计算量:

```c
old = buf[idx];           // 保存最旧
buf[idx] = new_sample;    // 覆盖新值
accum += buf[idx];        // 加入新值
if (filled >= 16) accum -= old;  // 去掉最旧
```

`ADC_Filter_Task` 以 2ms 独立节拍运行, 不跟 UI/网络的节奏走。`Get_Real_Voltage()` / `Get_Real_Current()` 直接返回已经算好的值, O(1) 零延迟。

> ---
> ### 🛠️ 调试避坑与排错指南 #3: PWM 不输出 / 周期不对
>
> **现象**: OLED 频率在变, 但示波器上看不到 PWM 波形, 或者波形不是 50% 占空比。
>
> **排查步骤**:
> 1. 先用示波器量 PA8/PA9, 确认有没有方波 (有 → 检查 IR2103S 驱动电路; 没有 → 往下)
> 2. 检查 `TIM_CtrlPWMOutputs(TIM1, ENABLE)` 是否在软启动 Trigger 时调用了 (MOE = Main Output Enable = 总开关, 忘了开就没有输出)
> 3. 确认 `TIM1->BDTR` 里 MOE 位是否为 1。如果被刹车 (Break) 意外触发, MOE 会被硬件清零
> 4. 如果是有输出但实际频率和设置值差很多, 检查 `TIM1_CLK_HZ` 是不是 72M (APB2 不分频时就是 72M)
>
> **⚠️ 千万注意**: App_Net 和 UI 模块**禁止直接操作 `TIM_Cmd`/`TIM_CtrlPWMOutputs`/`TIM1->ARR`**, 必须通过 PWM 模块的公开接口!
> ---

#### 4.3.6 OLED — 双页界面

- **Page 0 — 控制面板**: 可查看状态、可按键操作。日常 200ms 刷新用 16 字符全宽行覆盖, **不调用 OLED_Clear** (清屏需要 ~100ms, 会阻塞保护任务)
- **Page 1 — 锁屏监控**: 只读, KEY0 双击切回

SSD1315 128×64, 软件模拟 I2C (PA11=SCL, PA12=SDA, 开漏+4.7k上拉), 8×16 ASCII 字体, 4行×16字符。

> **⚠️ Cortex-M3 无硬件 FPU**, double 运算极慢。OLED 显示浮点数用 `float` 而非 `double`, pow10 函数用查找表替代运行时循环。

#### 4.3.7 KEY — 八态 FSM + 长按

八态按键状态机: IDLE → DEBOUNCE → PRESSED → WAIT_RELEASE → WAIT_DOUBLE → WAIT_DOUBLE_REL → **LONG_PRESS**, 10ms 扫描节拍。

| 按键 | 单击 | 双击 | 长按 (>3s) |
|:---|:---|:---|:---|
| KEY0 (PB12) | HW初始化 / Trigger / Stop | 切页 | **清除ESP8266配网凭据** |
| KEY1 (PB13) | Stop / +1kHz | — | — |

`KEY_Get_Event()` 阅后即焚: 1=单击, 2=双击, 3=长按。

#### 4.3.8 App_Net — 网络层

> **V5.1 重大重构**: 上电自动连 WiFi、3 次重试、UI 状态门控遥测

**启动流程** (`App_Net_StartConnect()`): `main.c` 直接调用 → 阻塞 ~3s (CH_PD 硬件复位) → 设 `s_connecting=1` → LED 慢闪。不再需要手动按 KEY0 触发联网。

**重试机制** (`App_Net_CheckRetry()`): 每 15s 超时 → `ESP8266_Init()` 硬件复位重试, 最多 3 次。3 次耗尽 → 切换到界面1(初始)显示错误。用户可按 KEY0 再次启动。

**遥测门控** (`App_Net_Task()`): **仅在 UI 状态 >= 界面3(READY) 时发送遥测**。界面1(初始)和界面2(连接中)不发数据, OneNET 设备保持离线。这样小程序/网页端看到的"设备在线" = 用户已可操作。

**状态查询**:
- `App_Net_GetConnectStatus()`: 0=空闲, 1=连接中, 2=已连接, 3=失败
- `App_Net_SoftReset()`: CMD:CLEAR 后调用, 不触发硬件复位 (ESP8266 已重启)
- `App_Net_IsConnected()`: `ESP8266_IsReady() && s_network_online`

**JSON 遥测格式**:
```json
{"V":12.50, "I":1.23, "F":100000, "S":2}
```
其中 `S` = 软启动状态 (0=IDLE, 1=SWEEP, 2=DONE, 3=FAULT)。

**指令协议**:
```
CMD:ON\n             → 开启软启动 (仅 IDLE 状态)
CMD:OFF\n            → 关断逆变器 (任意状态)
CMD:SETFREQ:100000\n → 频率渐变到 100kHz (仅 DONE 状态)
CMD:CLEAR\n          → STM32→ESP8266: 清除WiFi配网凭据 (KEY0长按触发)
STATUS:ONLINE\n      → ESP8266→STM32: "我已联网" (STM32收到后切LED常亮)
```

#### 4.3.9 UI — 7 界面状态机 (V5.1)

V5.1 重构为 7 界面状态机, 替代旧的三状态 (DISCONN/NoWiFi/Online):

```
上电 → main.c 调用 App_Net_StartConnect() → 界面2(连接中)
         ├─ 收到 STATUS:ONLINE → 界面3(已连接) → KEY0 Start
         └─ 15s×3超时 → 界面1(初始) + 底部错误 "WiFi Failed x3"
                        └─ 按 KEY0 → 重新 StartConnect
```

| 界面 | 状态 | OLED 显示 | 按键操作 |
|:---|:---|:---|:---|
| 1 | INIT | "Press KEY0 WiFi" | KEY0=联网, KEY0长按=清除配网 |
| 2 | CONNECTING | "Retry: X/3" | 无 (等待中) |
| 3 | READY | "Press KEY0 Start" | KEY0=触发扫频 |
| 4 | SWEEPING | 实时频率+进度条 | KEY0/KEY1=停止扫频 |
| 5 | RUNNING | V/I/F + "K0:Stop K1:+1k" | KEY0=停止, KEY1=+1kHz |
| — | FAULT | "!!! Over Current !!!" | KEY0/KEY1=复位 |
| 6/7 | 双击切换 | 控制面板/监测模式 | 在界面3/4/5均可双击 |

**LED 逻辑 (V5.1)**:
| LED | 等待/失败 | 连接中 | 已连接/待机 | 扫频 | 运行中 | 故障 |
|:---|:---|:---|:---|:---|:---|:---|
| PB3 WiFi | 慢闪 | **快闪** | 常亮 | 常亮 | 常亮 | 常亮/慢闪 |
| PB4 (Start) | 灭 | 灭 | **亮** | 灭 | 灭 | 灭 |
| PB5 (KEY1) | 灭 | 灭 | 灭 | 灭 | **亮** | **亮** |

- PB4 = Start 按钮可操作 (仅 READY 界面)
- PB5 = KEY1 可 +1kHz 或复位 (RUNNING/FAULT)

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
    // v2.x 无凭据时不自动开AP, 必须兜底!
    wifiManager.startConfigPortal("STM32_WPT_Config");
}
```

首次上电→开热点 `STM32_WPT_Config` (无密码)→手机连上→浏览器弹配网页→选WiFi输密码→存闪存→重启。以后自动连。

**换WiFi时**: 长按STM32的KEY0 > 3秒 → STM32发 `CMD:CLEAR\n` → ESP8266 `WiFiManager.resetSettings()` + `ESP.restart()` → 重新进入配网模式。

#### 5.2.2 双 MQTT 连接

| 连接 | 地址 | 用途 |
|:---|:---|:---|
| OneNET MQTT | `mqtts.heclouds.com:1883` | 物模型数据上报 + 接收指令 |
| EMQX 公共 | `broker.emqx.io:1883` | Web端直读(可选) |

**非阻塞重连**: 每 5 秒检查一次, 断开自动重连, 不阻塞 loop。

#### 5.2.3 JSON 转换 + 状态上报

STM32 发来 `{"V":12.50, "I":1.23, "F":100000, "S":2}` → ESP8266 转为 OneNET 格式:

```json
{
  "id": "123",
  "version": "1.0",
  "params": {
    "V": {"value": 12.50},
    "I": {"value": 1.23},
    "F": {"value": 100000},
    "Switch": {"value": true},
    "SetFreq": {"value": 100000}
  }
}
```

`S=1` 或 `S=2` → `Switch=true` (运行中)。`S=0` 或 `S=3` → `Switch=false` (停止)。

> #### V5.1 关键修复: 遥测覆盖命令问题
>
> 旧版固件有两个严重的 bug:
>
> **SetFreq 覆盖**: 每次遥测把 `SetFreq = 当前F`, 导致云端下发 108kHz 后下一秒就被覆盖成 100kHz:
> ```cpp
> // ❌ 旧代码
> txDoc["params"]["SetFreq"]["value"] = f;   // 用当前F覆盖了指令值!
> // ✅ 新代码
> txDoc["params"]["SetFreq"]["value"] = s_lastSetFreq;  // 只上报最后收到的指令值
> ```
>
> **Switch 覆盖**: 收到云端 Switch=false 命令, ESP8266 同一轮 loop 里发 CMD:OFF 给 STM32 后立即处理旧串口数据 — STM32 还没执行, 旧 S=2 又把 Switch 覆盖成 true:
> ```cpp
> // ✅ 新代码: 收到 ON/OFF 命令后跳过下一次遥测的 Switch 字段
> if (s_skipSwitch) {
>     s_skipSwitch = 0;  // 跳过, 等 STM32 更新状态再上报
> } else {
>     txDoc["params"]["Switch"]["value"] = running;
> }
> ```
>
> 另外 `#define DEBUG` 已注释 — 旧版开了调试输出, ESP8266 往串口打 `[WiFi]` 等日志, 会和 JSON 帧混在一起污染通信。

**STATUS:ONLINE 机制**: ESP8266 在 WiFi+OneNET MQTT 双通后, 向 STM32 串口发 `STATUS:ONLINE\n`。STM32 收到后才知道网络真的通了——这是解决"硬件初始化完了但WiFi没连"这个坑的关键。

> ---
> ### 🛠️ 调试避坑与排错指南 #4: ESP8266 反复重启 / 连不上 WiFi
>
> **现象分类**:
> - **串口循环输出 `Booting...`**: 供电不足, 或 WiFiManager 没有已存凭据又没有 startConfigPortal 兜底
> - **WiFi 连上了但 OneNET 离线**: MQTT 地址错误 (检查 `mqtts.heclouds.com:1883`) 或 Token 错误
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

[📍此处需配图：ESP8266烧录时的接线特写，红色箭头指向GPIO0-GND短接]

---

## 6. 网页控制台

### 6.1 本地开发 (V1, 调试保留)

在电脑上直接运行，浏览器 `http://localhost:4567` 打开：

```bash
cd ONENETapp
node -e "const h=require('http');const f=require('fs');const p=require('path');
h.createServer((q,r)=>{let fp=q.url.split('?')[0];if(fp==='/')fp='/login.html';
if(!p.extname(fp))fp+='.html';
f.readFile(p.join(__dirname,fp),(e,d)=>{if(e){f.readFile(p.join(__dirname,
'login.html'),(e2,d2)=>{r.writeHead(200);r.end(d2)});return}
r.writeHead(200);r.end(d)})}).listen(4567,()=>console.log('http://localhost:4567'))"
```

本地模式适合调试——改了代码立刻刷新看效果。缺点是无法外网访问，手机和其他设备打不开。

### 6.2 方案演进 (我们踩过的全记录)

| 阶段 | 用了什么 | 为什么换了 |
|:---|:---|:---|
| V1 | **本地 Node.js 服务** | 只能自己电脑用, 无法外网访问 |
| V2 | **Netlify** 拖拽部署 | 刚上线时正常, 后来免费带宽耗尽被暂停 ☠️ |
| V3 | **GitHub Pages** | 部署成功但路由有 bug——`/monitoring` 无法映射到 `monitoring.html` → 404 |
| **V4** | **Cloudflare Pages** ✅ | 免费不限量, 原生支持 SPA 路由, **当前方案** |

### 6.2 Cloudflare Pages 部署 (当前稳定方案)

1. 确保代码在 GitHub: `https://github.com/Ran-sh/WPT_Onenet_IoT`
2. [pages.cloudflare.com](https://pages.cloudflare.com) → GitHub 登录
3. Create project → 选仓库 → **不设构建命令** (静态站点) → Deploy
4. 得到 `wptonenet.483763727.workers.dev` 公网地址

[📍此处需配图：Cloudflare Pages部署完成页面截图，红框标出域名]

### 6.3 配置 OneNet

打开网页 → 登录 (`admin` / `123456789`) → **设置** → OneNet 平台配置 → 填 Product ID / Device Name / Token → 保存并重启。

[📍此处需配图：网页设置页截图，标注三个必填字段]

### 6.4 各页面功能

| 页面 | 功能 | 特别说明 |
|:---|:---|:---|
| 首页 | V/I/F 实时数据横排 + 连接状态 | 数据来自 OneNET API |
| 控制 | 启停开关 + 频率设置(kHz) + 操作记录 | 1分钟自动同步实际状态 |
| 监测 | 实时折线图 | Y轴根据数据动态缩放 |
| 历史 | 历史数据表 + 长期变化曲线 | 手机端两行堆叠显示 |
| 设置 | OneNet配置 / 数据模型管理 | 支持传感器/控制器动态增删 |

---

## 7. 微信小程序

### 7.1 当前架构 (V5.1)

V5.0 起小程序**直连 OneNET HTTP API**，与网页端完全相同的后端逻辑：

```
小程序 ──HTTPS── OneNET API (iot-api.heclouds.com) ──MQTT── ESP8266 ── STM32
```

不再依赖 Railway、EMQX 或任何中间桥接服务器。小程序和网页端共用同一套：
- `GET /thingmodel/query-device-property` 读取数据
- `POST /thingmodel/set-device-property` 下发指令 (Switch / SetFreq)
- 同一套 Token 鉴权

### 7.2 数据流与轮询

| 数据类型 | 刷新间隔 | 说明 |
|:---|:---|:---|
| 全部数据 (V/I/F/Switch/SetFreq) | **2 秒** (单次请求) | V5.1合并为 fetchAll |
| 在线检测 | 10 秒超时 | 数据时间戳 > 10s → 离线 |
| Switch 命令验证 | 3 秒后验证 + 重发 | 防 MQTT 丢包 |
| Swiper 同步 | 仅首次连接 | 之后永不被云端覆盖 |

> #### V5.1 关键变更
>
> - **合并轮询**: 旧版 `fetchData`(5s) + `fetchControlState`(60s) 两次独立请求 → 新版 `fetchAll` 单次请求 2s 间隔, 减少 50% 的 API 调用同时刷新更快
> - **在线检测**: 之前只判断 HTTP 请求成功与否 → V5.1 检查数据时间戳, 超过 10s 无新数据判定设备离线. 配合 STM32 固件 V5.1 的遥测门控 (UI>=READY 才发), 实现"设备在线=可操作"的准确判断
> - **Switch 命令验证重发**: `onSwitch` 发送命令后 3 秒验证 OneNET Switch 是否真的变化, 若未生效自动重发一次. 解决 ESP8266 MQTT 断连导致命令丢失的偶发问题
> - **Swiper 永不自动跳**: Swiper 只在首次拿到在线数据时同步一次实际频率, 之后锁死仅受用户手指控制 (轮询/乐观锁过期/网络抖动都不会改变 swiper 位置)

### 7.3 功能特性

- **双主题**: 深色/浅色 (CSS 变量, localStorage 持久化)
- **启停开关**: `switch` 组件 toggle, 带 3s 后验证重发
- **频率设置**: swiper 滑动选频 (PWM 量化可达值) + 确认按钮, 发送 `kHz × 1000` Hz
- **实时数据卡**: 电压/电流/频率, 2s 刷新
- **手动刷新**: 标题栏 ⟳ 按钮，旋转动画，立即拉取最新状态

### 7.4 方案演进 (历史参考)

V5.0 之前经历了两代架构：

| 阶段 | 方案 | 结果 | 放弃原因 |
|:---|:---|:---|:---|
| V1 | WebSocket 直连 EMQX | ❌ | 微信真机拒 EMQX TLS 证书 |
| V2 | HTTP 轮询 + ngrok 隧道 | ❌ | DNS 污染 + 免费版限流 |
| V3 | Railway 桥接 (bridge.cjs) | ✅→废弃 | 多一层中转, 免费额度有限 |
| **V4** | **OneNET API 直连** | **✅ 当前** | 与网页端统一, 零额外依赖 |

### 7.5 Railway 桥接方案 (备选, 仅作参考)

如果未来 OneNET API 有变化导致小程序无法直连，可以回退到 Railway 桥接方案：

```
小程序 ──HTTPS── Railway ──MQTT── EMQX ── ESP8266 ── STM32
```

桥接代码位于 `安卓app/server/bridge.cjs`（本地）和 `Railway_Deploy/bridge.mjs`（云端），部署参考仓库 `Ran-sh/WPT_Railway`。当前版本已不推荐此方案。

---

## 8. 联调指南

### 8.1 上电顺序

> **⚠️ 关键**: **先给 ESP8266 上电, 等 5 秒, 再给 STM32 上电**。顺序反了会导致 STM32 在 ESP8266 还没连上网的时候就尝试硬件复位, 浪费时间。

1. ESP8266 独立 3.3V 上电 → 等 ~5s WiFi+MQTT 连好
2. STM32 上电 → 按 KEY0 → 硬件初始化 ESP8266 (~3s)
3. OLED 显示 `HW: READY` → 等 ESP8266 发 `STATUS:ONLINE`
4. OLED 切正常状态 → 网页/小程序应该能看到数据

### 8.2 逐环节验证

[📍此处需配图：联调流程图，每个环节标注验证方法和通过标准]

| 步骤 | 检查什么 | 怎么算通过 | 如果没过 |
|:---|:---|:---|:---|
| 1 | STM32 串口 | 按 KEY0 后串口助手 115200 收到 `{"V":...}` | 检查 PA2/PA3 接线 + USART2 初始化 |
| 2 | ESP8266 WiFi | 串口监视器 `Connected! IP:` | 检查供电 + WiFiManager 配网 |
| 3 | ESP8266 MQTT | 串口监视器 `OneNET Connected!` | 检查 Token + 产品ID |
| 4 | OneNET 在线 | 控制台设备状态 "在线" | 回头查步骤 2-3 |
| 5 | OneNET 数据 | 控制台数据流有 V/I/F | 检查串口 JSON 格式 |
| 6 | 网页数据 | 首页显示实时数据 | 检查设置页 OneNet 配置 |
| 7 | 网页控制 | 点开关, OLED 响应 | 检查 CMD 指令链路 |

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
| OLED 显示 "No WiFi" | ESP8266 是否发了 STATUS:ONLINE | ESP8266 固件版本太旧 |
| 网页"未连接" | F12 Console 看 API 响应 | Token 或设备名填错 |
| 网页"正在连接中"不消 | 设置页配置了没 | localStorage 没保存 |
| 设了频率 OLED 不对 | 正常现象 | 频率量化, 网页端已自动修正 |
| 长按 KEY0 没反应 | STM32 固件版本 | 旧版 KEY FSM 没长按检测 |
| 网页端 404 | Cloudflare 分支 | 推送到了 gh-pages 但 master 没同步 |
| 控制页开关状态不对 | ESP8266 是否上报了 S 字段 | 需烧录最新 ESP8266 + STM32 固件 |
| 网页/小程序设频率无效 | config.js toCloud 崩溃 | V5.1: FREQ_HZ/FREQ_LIST 未定义, 已修复 |
| 小程序 Swiper 自动跳 | 云端 SetFreq 覆盖 | V5.1: swiper 锁定仅首连同步, 之后不受云影响 |
| 设 Stop 硬件没反应 | ESP8266 MQTT 丢命令 | V5.1: Switch 带 3s 验证重发 |

---

## 10. 踩坑全记录 (避坑指南精华)

### 10.1 频率控制方案的四次迭代

| 版本 | 怎么做 | 为什么不好 |
|:---|:---|:---|
| V1 CMD:F_UP/DOWN | 每次 ±1kHz | 小程序点一下触发两次 (双重点击) |
| V2 OneNET FreqAdd/FreqSub | 虚拟按键点动 | 物模型复杂, 两个布尔键 |
| V3 CMD:SETFREQ (跳变) | 瞬间设频率 | 跳频太暴力, 不安全 |
| **V4 SETFREQ + 渐变** ✅ | 500Hz/10ms 斜坡到达 | 平滑, 可中断, **当前方案** |

### 10.2 `device` vs `devices` — 一个字母的惨案

Token 里是 `devices` (复数), 不是 `device`。`%2F` 是 `/` 的 URL 编码。写成单数会让 OneNET 返回 `authentication failed: invalid res`, 让你怀疑人生半小时。

### 10.3 WiFi 连接状态追踪

`ESP8266_Init()` 只做硬件复位 → `ESP8266_IsReady()` 只表示"硬件就绪", 不代表"WiFi已连"。必须等 ESP8266 发来 `STATUS:ONLINE` 才能确认网络通了。

### 10.4 ngrok DNS 污染

教育网 DNS 把 `connect.ngrok-agent.com` 解析到 `127.0.0.1`, ngrok 彻底废了。修过一次 (改 hosts), 后来直接换 Railway。

### 10.5 WiFiManager v2.x `autoConnect` 不自动开 AP

加 `startConfigPortal()` 兜底, 否则没凭据就不开热点, ESP8266 反复重启。

### 10.6 CORS 不是问题的"问题"

一直以为是跨域问题, F12 查了半天 CORS 头。最后发现 OneNET API 本来就返回 `Access-Control-Allow-Origin: *`, 真正的问题是 Token 里 `device` 少了一个 `s`。

### 10.7 网页端组件选型备忘录

| 组件 | 试试过的 | 最终用 | 为什么 |
|:---|:---|:---|:---|
| 频率选取 | Slider→齿轮滚轮→自写触摸尺→Swiper | Swiper | 滑动检测最可靠 |
| 部署平台 | Netlify→GitHub Pages→Cloudflare | Cloudflare | 免费不限量+SPA路由 |
| 图表Y轴 | 固定 0~60V | 动态 data±25% | 10V 数据不再压成平线 |
| 状态标识 | 正常/异常→✓/✗→圆底徽章 | 圆底 ✓/✗ | 视觉最直观 |

---

> **全文完**。这份文档从开始写到收工用了近一个月, 踩过的坑比跑通的代码还多。如果你跟着做遇到问题, 回头翻第 9 章故障速查表, 大概率能找到答案。祝你的全桥不发烫, ESP8266 不掉线, OneNET 不报 401。
