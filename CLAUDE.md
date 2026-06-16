# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **版本** | V16 |
| **语言** | 中文交流，代码注释中英混合 |

## Git 推送前置钩子

**每次 `git push` 之前，必须按顺序执行：**

1. 运行 `Keil_Project/keilkill.bat` 清理全部 Keil 编译中间产物
2. `git add -A && git commit -m "..." && git push origin 4.0TFT`
3. **铁律**: 禁止将 `.obj` `.lst` `.axf` 等编译产物上传到 GitHub

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only
- **Project**: `Keil_Project/Project.uvprojx`, F7 编译 → F8 下载, 无 CLI 编译
- ARMCC V5 不支持 `--multibyte_chars`, UTF-8 中文必须 hex escape (`\xE6\x97\xA0...`)
- 字符串拼接 `"\xe5\x8f\x8c\xe5\x87\xbb" "Back"` 可避免 ARMCC #27-D 警告
- ARMCC #1293-D: `if ((p = strstr(...))` 触发警告, 改用 `if ((p = strstr(...)) != 0`

### ESP8266 (Arduino IDE)

- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU, `Arduino_Project/...ino`
- **烧录**: GPIO0 接 GND → 上电 → 上传 → 断开 GPIO0-GND → 重新上电
- **配网**: 首次上电开热点 `STM32_WPT_Config` → 手机连上输 WiFi 密码 → 自动连 OneNET

## Architecture: Dual-MCU

```
STM32 (物理脑)               ESP8266 (联网脑)
TIM1 PWM 95~150kHz          WiFi + MQTT 自动联网
ADC 双通道 + 64样本滑动窗口   STATUS:ONLINE 心跳
TFT/KEY/LED 人机交互        CMD:ON/OFF/SETFREQ 控制
Sys_Safety 独立安全监测      USART2 115200 纯文本 JSON
        │                          │
        └────── USART2 JSON ───────┘
```

**Iron rule**: STM32 不发 AT 指令, ESP 不碰 PWM/ADC。开机自动联网。

## 系统全局状态机 (V14)

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

| 状态 | PWM | Task 子集 |
|:---|:---|:---|
| INIT | 关 | 初始化阶段 |
| IDLE | 关 | Key+ADC+Network+Safety+UI |
| SWEEP | 开 | + Soft_Start (150k→100kHz) |
| RUNNING | 开 | + Freq_Ramp |
| FAULT | 关 | + FAULT UI, 取消所有斜坡 |

## 文件结构 (V16)

### 完整目录树

```
WPT_PWM_V4.0_ONENET_TFT/
├── Keil_Project/                    ← STM32 固件 (Keil MDK)
│   ├── Project.uvprojx              ← 工程入口, F7编译→F8下载
│   ├── keilkill.bat                 ← 清理编译产物 (push前必执行)
│   ├── Hardware/ (12源文件, ~3600行)
│   │   ├── Tft_Driver.c/h         ← ST7735 SPI+DMA 彩屏 (606+76行)
│   │   ├── TFT_Font_Data.h        ← ASCII 95字 + 中文 78字 + WIFI/MQTT/STAR 图标 (358行)
│   │   ├── Ui_Controller.c/h      ← 9页面 UI 状态机 + 圆弧能量条仪表盘 (1697+37行)
│   │   ├── Pwm_Driver.c/h         ← TIM1 全桥 PWM 95-150kHz 1000ns死区 (113+33行)
│   │   ├── Inverter_Control.c/h   ← 软启动 150k→100kHz + 频率斜坡 (146+67行)
│   │   ├── Adc_Driver.c/h         ← ADC1 双通道 + 64样本滑动窗口 (162+20行)
│   │   ├── Esp8266_Driver.c/h     ← USART2 115200 + Try_Copy_Rx_Frame 原子接收 (246+49行)
│   │   ├── Key_Driver.c/h         ← 4键 FSM, 单击/双击/长按 (137+38行)
│   │   ├── Led_Driver.c/h         ← 6 LED 闪烁 (135+42行)
│   │   └── Buzzer_Driver.c/h      ← 蜂鸣器 (68+30行)
│   ├── User/ (8文件, ~750行)
│   │   ├── main.c                 ← 程序入口 50行
│   │   ├── Sys_Core.c/h           ← 状态枚举+初始化+安全(Sys_Safety_Reset_EMA)+运行调度 (203+44行)
│   │   ├── App_Network.c/h        ← WiFi+V16指数退避+8s心跳+帧快照TOCTOU防竞态+遥测 (253+41行)
│   │   ├── stm32f10x_it.c/h       ← ISR (SysTick + USART2 ORE防锁死) (68+42行)
│   │   └── stm32f10x_conf.h       ← SPL 配置 (87行)
│   ├── System/ → Sys_Timer.c/h    ← SysTick 1ms + DWT (48+36行)
│   ├── Start/  → system_stm32f10x.c + CMSIS
│   └── Library/ → SPL V3.5.0 (只读, 不可修改)
├── Arduino_Project/                ← ESP8266 固件 (Arduino IDE)
│   └── ESP8266_MQTT_Firmware.ino  ← WiFiManager+双MQTT Broker+指令去抖+Mqtt_Task_Publish_Telemetry (486行)
├── ONENETapp/                      ← 网页控制台 (Cloudflare Pages, 纯JS)
│   ├── index.html                 ← 主页+连接指示 (404行)
│   ├── monitoring.html            ← 实时监测+趋势图 (394行)
│   ├── control.html               ← 设备控制+5s同步 (450行)
│   ├── history.html               ← 历史查询 (531行)
│   ├── alerts.html                ← 报警记录 (325行)
│   ├── settings.html              ← 系统设置 (803行)
│   ├── login.html                 ← 登录页 (150行)
│   ├── js/onenet.js               ← OneNET API+乐观更新+setProperty重试3次 (303行)
│   ├── js/config.js               ← 数据模型+颜色映射 (67行)
│   ├── js/mobile-nav.js           ← 移动端底部导航栏 (30行)
│   └── service-worker.js          ← PWA Service Worker (16行)
├── 安卓app/                        ← 微信小程序 (WeChat MiniProgram)
│   ├── app.js/json/wxss           ← 全局配置+双主题 (18行)
│   ├── pages/index/index.js       ← OneNET直连+频率映射+指令验证重发重试 (216行)
│   ├── pages/index/index.wxml     ← UI 模板 (79行)
│   └── pages/index/index.wxss     ← 双主题样式系统 (345行)
└── Claude_Files/                   ← AI 生成文档 (tools/docs)
    ├── tools/generate_docx.js      ← .docx 生成器
    └── docs/                       ← 技术文档 (.md + .docx)
```

## 主循环 (V14 状态机)

```c
int main(void) {
    Sys_Clamp_ESP(); Sys_Hardware_Init(); Sys_Startup_Screen(); Sys_Post_Init();
    g_sys_state = SYS_STATE_IDLE;
    while (1) {
        Key_Driver_Task(); Adc_Driver_Filter_Task(); App_Network_Task(); Sys_Safety_Task();
        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;
        }
        IWDG_ReloadCounter(); __WFI();
    }
}
```

## Sys_Safety (安全监测, 独立于 UI)

- **EMA 滤波**: α=0.25, τ≈800ms, 每圈主循环更新 V/I
- **PB10 电源**: 电压 >12V → 拉高使能, ≤12V → 拉低关断
- **过流检测**: `s_safety_ema_i > 5.0A` → `Inverter_Control_Soft_Start_Fault()` + `Buzzer BEEP` + `g_sys_state = SYS_FAULT`

## EMA 双级滤波链

| 层级 | 模块 | 滤波对象 | 用途 |
|:---|:---|:---|:---|
| 安全级 | `Sys_Safety_Update_EMA()` | ADC 原始 V/I | 过流保护, PB10 阈值 |
| 显示级 | `Ui_Controller_Update_EMA()` | Sys_Safety 输出 | UI 仪表盘 + 综合监测页 |
| 数字量 | `Pwm_Driver_Get_Frequency()` | 无滤波 | 频率（零迟滞, 保证调频跟手） |

## App_Network WiFi 重试 (V16)

- **指数退避**: 0-2次 3s → 3-7次 15s → 8-13次 30s → 14-21次 60s → 22-31次 2min → 32-46次 5min → 47+次 30min，永不 FAILED
- **心跳超时**: 8s 无 ESP 帧 → 判定离线 → 自动重连 (`s_last_esp_ms` + `Esp8266_Driver_Start_Init()`)
- **远程指令**: CMD:ON/OFF 同步更新 `g_sys_state` + `Ui_Controller_Force_Page()`
- **帧处理安全**: `Try_Copy_Rx_Frame` 消除 check-then-act 丢帧窗口; `ss_cmd`/`conn_cs` 帧内快照防 ELSE-IF 链间 TOCTOU
- **热点加速**: RSSI ≥ -35 → 直接重置退避级别为 3s 级快速直连
- **MQTT 超时修复**: 计时器仅在首次进入 MQTT 状态时重置一次 (非每圈)

## ESP8266 固件 (V16)

- **指令去抖**: Mqtt_Task_Parse_Command 2s 窗口内相同 payload 直接丢弃
- **Switch 状态**: 仅 `s==2` (SS_DONE) 上报 true, `s==1` (SWEEP) 为过渡态不上报运行
- **遥测频率**: 仅在 running 时上报真实 F 值, 否则上报 0
- **SetFreq 量化**: `(val/1000)*1000`, 与 STM32 PMW 1kHz 步进一致
- **公共 Broker 透传**: `wpt/20260001/data` 接收原始 STM32 JSON, `wpt/20260001/cmd` 透传 CMD 指令

## 网页端 (Cloudflare Pages) 关键设计

- **乐观更新**: `setProperty` 成功后立即写 localStorage 缓存 + 3s 乐观锁, 轮询同步时忽略云端旧值
- **重试机制**: `setProperty` 网络/业务错误各重试 3 次 (500ms/800ms 间隔)
- **连接指示**: 同步失败 → 红色"连接失败"; 设备离线 → 黄色"离线"; 在线 → 绿色"在线"
- **控制页同步**: 5s 间隔 (与首页一致), 非 60s
- **数据模型**: `config.js` DEFAULT_DATA_MODEL 定义 sensors(V/I/F) + controls(Switch/SetFreq)
- **频率映射**: `fromCloud: v => Math.floor(v/1000)` / `toCloud: v => v*1000`, Web 显示 kHz

## 微信小程序 关键设计

- **频率查表**: `buildFreqMap()` 预计算 STM32 硬件分频后的有效 kHz 值 (匹配 PMW 偶数 ticks 约束)
- **停机显示**: F=0 时频率显示 0 (灰色), 保持最近已知频率供用户选频 (`s_last_display_freq`)
- **指令重试**: `_sendCmd` 内置 3 次重试 (600ms/800ms), onSwitch 额外 3s 后验证重发
- **双主题**: CSS 变量 `theme-dark` / `theme-light` 完整调色板

## 全链路数据一致性 (V16 铁律)

| 状态 | STM32 遥测 | ESP 上报 OneNET | Web/小程序显示 |
|:---|:---|:---|:---|
| IDLE | V=0,I=0,F=0,S=0 | Switch=false, F=0 | 停机/0 |
| SWEEP | 不发送遥测 | (无数据) | (上一帧缓存) |
| RUNNING | V=EMA,I=EMA,F=真实Hz,S=2 | Switch=true, F=真实Hz | 运行中/实时值 |
| FAULT | V=0,I=0,F=0,S=3 | Switch=false, F=0 | 故障/0 |

Telemetry JSON 全链路格式不变: `{"V":xx,"I":xx,"F":xx,"S":x}\n`

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | 功能 | Pin | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | ADC_CH8 (电流, CC6920BSO) |
| PA1 | ESP8266 RST | PB1 | ADC_CH9 (电压) |
| PA2 | USART2_TX | PB3 | LED_PWM |
| PA3 | USART2_RX | PB4 | LED_WIFI |
| PA4 | TFT_CS | PB5 | PAGE 按键 (IPU) |
| PA5 | SPI1_SCK | PB6 | TFT 背光 TIM4_CH1 |
| PA6 | TFT_DC | PB7 | F_DOWN 按键 (IPU) |
| PA7 | SPI1_MOSI | PB8 | F_UP 按键 (IPU) |
| PA8 | TIM1_CH1 | PB9 | ON/OFF 按键 (IPU) |
| PA9 | TIM1_CH2 | PB10 | PowerContrl (高=使能12V) |
| PA10 | LED_COM | PB11 | ESP8266 CH_PD (EN) |
| PA11 | LED_POWER | PB13 | TIM1_CH1N |
| PA12 | LED_TEMP | PB14 | TIM1_CH2N |
| PA15 | LED_SYSTEM | PB15 | 蜂鸣器 |

JTAG 禁用释放 PB3/PB4/PB5/PA15。

## 编码规范

### 命名 (V14 零容忍)

| 层次 | 规则 | 正确 | 违规 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` 强制前缀 | `Sys_Run_Led_Tick()` | `Led_Tick()` |
| 静态变量 | `s_description` | `s_gauge_val_str` | `uiState` |
| 全局变量 | `g_description` | `g_sys_state` | `Sys_State_Global` |
| 枚举值 | `MODULE_NAME_VALUE` 全大写 | `SYS_STATE_IDLE` | `State_Idle` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |
| 头文件保护 | `MODULE_NAME_H` 无前导下划线 | `SYS_CORE_H` | `_SYS_CORE_H` |

### 注释

- 公开函数必须带 `@brief` + `@param`/`@retval`
- `.h` 顶部必须带 `@file` + `@brief` + `@note`
- 禁止 `//` 双斜杠 (ARMCC V5), 统一用 `/** */` 或 `/* */`
- 只写 WHY, 不写 HOW

### 调度 + 架构铁律

- 周期任务: `Sys_Timer_Get_Tick() - last >= PERIOD` (uint32_t 回绕安全)
- `Sys_Timer_Delay_Ms()` 仅初始化阶段, 运行时禁止阻塞
- `.h` 只公开接口, `.c` 全 static 内部实现
- 禁止 `extern` 访问模块私有变量, 禁止 `#include ".c"`
- 分层单向: Hardware → System → Application

## TFT 驱动 (ST7735 Green Tab, 不可改)

| 参数 | 值 |
|:---|:---|
| SPI | Mode 3, 18MHz, DMA1_Channel3, 只写不读 |
| 分辨率 | 160×128 横屏, MADCTL=0xA0 |
| SetWin 偏移 | X+1, Y+2 |
| 字库 | 8×16 ASCII (95) + 16×16 中文 (78) + 5×10 微数字 (12) |
| 字库位序 | 全部 LSB-first, 统一在 `TFT_Font_Data.h` |
| 图标 | WIFI(4+动画6帧), MQTT(3态+动画6帧), ICON_STAR |

## PWM 基线 (不可改)

- TIM1 CH1=PWM1 + CH2=PWM2, Up 模式, 50% 占空, `TIM_OCNPolarity_Low`
- 死区 1000ns, 95-150kHz, UDIS 原子更新
- 开机: TIM_Cmd(DISABLE) + MOE(DISABLE), 零输出

## 圆弧能量条仪表盘 (V15)

| 参数 | 值 |
|:---|:---|
| 圆心 | G_CX=80, G_CY=84 |
| 半径 | R_TICK=56 (外), R_BIG=50 (主6px), R_FINE=53 (细3px) |
| 绘制 | 1px Bres_Line, 高亮色/暗灰槽(0x18C3), 无弧无Fill加粗 |
| **信息舱** | Row 4(Y=64): 状态 OK/WRN/HI/SWP/DON/IDL 居中 |
| | Row 5(Y=80): 纯数值 12.45 居中黄色 (电流 3位小数) |
| | Row 6(Y=96): 标签 电压 V/电流 A/频率 kHz 居中青色 |
| Phase 0 | WIFI@128 + MQTT@144, cs/frame/page 四因子触发 |

### GaugeConfig 三表

| 参数 | 电压 V | 电流 C | 频率 F |
|:---|:---|:---|:---|
| range | 0→50 | 0→2 | 90→150 |
| big_step | 10 | 0.5 | 10 |
| fine_step | 1 | 0.1 | 1 |
| red_start | 42 | 1.8 | 140 |
| 停机 | — | — | val=0 能量条归零, 灰"0" |

### UI Phase 架构

| Phase | 职责 |
|:---|:---|
| 0 | Global Top-Right Icons Manager |
| 1 | System Fault detection (监听 g_sys_state) |
| 2 | Sweep complete → auto-jump SUMMARY |
| 3 | Key scan + dispatch → 可改 s_page/g_sys_state |
| 4 | Page change → tracking invalidated |
| 5 | 200ms tick → dynamic incremental update |
| 6 | Cursor boundary clamp |
| 7 | Draw — full page only when s_page_drawn==0 |

安全逻辑 (PB10/过流) 已从 UI 剥离到 Sys_Safety。

## Safety

- **过流**: Sys_Safety 每圈检测 >5.0A → SYS_FAULT + Buzzer BEEP
- **FAULT 恢复**: ON/OFF 单击 → `Soft_Start_Reset()` → MAIN_MENU
- **上电**: TIM1 全关, PB10 拉低关 12V
- **看门狗**: IWDG 1.6s, 调试自动暂停
- **HardFault**: 先关 PWM 再死循环

## 环形仪表盘速查

```c
#define G_CX   80  /* 圆心 */    #define R_TICK 56  /* 外圈 */
#define G_CY   84                #define R_BIG  50  /* 主刻度 (6px) */
                                 #define R_FINE 53  /* 细刻度 (3px) */

Bres_Line(x0,y0,x1,y1,color);              /* 1px 能量条刻度 */
Gauge_Polar(angle_deg, radius, &x, &y);     /* sin 查表极坐标 */
Draw_Gauge_Full(cfg, val);                   /* 入场全绘 */  
Gauge_Dynamic_Update(cfg, val, old_val);     /* 200ms 增量差分 */  
Draw_TopRight_Icons();                       /* WIFI@128 MQTT@144 */

/* 三表配置 */
GAUGE_V = { 0, 50, 10, 5,    1,  42, 'V'};
GAUGE_C = { 0,  2,0.5,0.25,0.1, 1.8, 'C'};
GAUGE_F = {90,150, 10, 5,    1, 140, 'F'};

/* 信息舱: 状态(Row4) → 数值(Row5,黄) → 标签(Row6,青) */
```
