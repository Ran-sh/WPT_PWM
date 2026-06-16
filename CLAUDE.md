# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **版本** | V15 |
| **语言** | 中文交流，代码注释中英混合 |

## 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. 全面代码审查，修复发现的问题 (CRITICAL/HIGH 必须修)
2. `/init` — 重新生成 CLAUDE.md（包含完整画面布局 + 编码规范 + 架构 + 安全基线）
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` → `~/.claude/skills/embedded-architect/SKILL.md`)
4. 优化全部代码注释（所有源文件检查注释质量，补充 @brief/@param/@note，移除无效注释）
5. 更新全部文档 (`Claude_Files/docs/` 下 `.md` + `.docx` 配对生成)
6. 美化 GitHub README.md
7. `git push` 推送当前分支 (4.0TFT)

**执行期间**: 全部权限自动通过，不中断等待用户确认。

## Git 推送前置钩子

**每次 `git push` 之前，必须按顺序执行以下步骤：**

1. 运行 `Keil_Project/keilkill.bat` 清理全部 Keil 编译中间产物
2. `git add -A` 暂存所有变更
3. `git commit -m "..."`
4. `git push origin 4.0TFT`

**铁律**: 推送之前必须先 keilkill，禁止将 `.obj` `.lst` `.axf` 等编译产物上传到 GitHub。

## Build System

- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library**: SPL V3.5.0 (`Keil_Project/Library/`) — read-only, never modified
- **Project File**: `Keil_Project/Project.uvprojx`
- **Output**: `Keil_Project/Objects/Project.hex` (HEX-80)
- 无 CLI 编译 — Keil IDE 中 F7 编译 → F8 下载
- ARMCC V5 不支持 `--multibyte_chars`，UTF-8 中文必须用 hex escape (`\xE6\x97\xA0...`)
- 字符串拼接如 `"\xe5\x8f\x8c\xe5\x87\xbb" "Back"` 可避免 ARMCC #27-D 警告
- ARMCC #1293-D: `if ((p = strstr(...))` 赋值在条件中会触发警告，改用 `if ((p = strstr(...)) != 0`

### ESP8266 (Arduino IDE)

- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU
- **File**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- **Libraries**: ESP8266WiFi, PubSubClient, ArduinoJson v7, WiFiManager
- **烧录**: GPIO0 接 GND → 上电 → Arduino IDE 上传 → 断开 GPIO0-GND → 重新上电
- **配网**: 首次上电开热点 `STM32_WPT_Config` → 手机连上输 WiFi 密码 → ESP 重启 → 自动连 OneNET

## Architecture: Dual-MCU

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • TIM1 PWM 发波 95~150kHz    │    │  • 自动连接WiFi+MQTT          │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET/小程序/网页 指令    │
│  • TFT/KEY/LED 人机交互       │    │  • 串口 JSON ↔ STM32 透传    │
│  • 软启动扫频 + 过流保护       │    │  • STATUS:ONLINE 心跳        │
│  • 纯 JSON 串口透传           │    │  • CMD:ON/OFF/SETFREQ 控制   │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)           │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000}  │
           │◄──────────────────────────────────┤
           │  CMD:ON\n / CMD:OFF\n              │
           │  CMD:SETFREQ:100000\n              │
           │  STATUS:ONLINE                     │
```

**Iron rule**: STM32 never sends AT commands. ESP8266 never touches PWM/ADC. Communication is pure text JSON over USART2 at 115200bps.

开机默认自动联网，ESP 上电即初始化连接。

## 系统全局状态机 (V14)

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

| 状态 | 含义 | PWM | Task 子集 |
|:---|:---|:---|:---|
| `SYS_INIT` | 上电初始化中 | 关 | 无 |
| `SYS_IDLE` | 待机（主菜单/配网/故障页）| 关 | Key, ADC, Network, Safety, UI |
| `SYS_SWEEP` | 软启动扫频 150k→100kHz | 开 | + Soft_Start |
| `SYS_RUNNING` | 正常运行（仪表盘）| 开 | + Freq_Ramp |
| `SYS_FAULT` | 过流锁存 | 关 | + FAULT UI, 取消所有斜坡 |

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | 功能 | Pin | 功能 |
|:---|:---|:---|:---|
| PA0 | TFT_RES | PB0 | ADC_CH8 (电流, CC6920BSO) |
| PA1 | ESP8266 RST | PB1 | ADC_CH9 (电压) |
| PA2 | USART2_TX | PB3 | LED_PWM |
| PA3 | USART2_RX | PB4 | LED_WIFI |
| PA4 | TFT_CS | PB5 | PAGE 按键 (IPU, 低有效) |
| PA5 | SPI1_SCK | PB6 | TFT 背光 TIM4_CH1 |
| PA6 | TFT_DC | PB7 | F_DOWN 按键 (IPU, 低有效) |
| PA7 | SPI1_MOSI | PB8 | F_UP 按键 (IPU, 低有效) |
| PA8 | TIM1_CH1 | PB9 | ON/OFF 按键 (IPU, 低有效) |
| PA9 | TIM1_CH2 | PB10 | PowerContrl (高=使能12V, 低=关断) |
| PA10 | LED_COM | PB11 | ESP8266 CH_PD (EN) |
| PA11 | LED_POWER | PB13 | TIM1_CH1N |
| PA12 | LED_TEMP | PB14 | TIM1_CH2N |
| PA15 | LED_SYSTEM | PB15 | 蜂鸣器 |

TIM1 默认映射（不执行重映射），SPI1 默认映射。JTAG 禁用释放 PB3/PB4/PB5/PA15。

## 模块分层与文件结构 (V14 新模块)

```
Keil_Project/
├── Hardware/
│   ├── Tft_Driver        ← ST7735 SPI 彩屏, 160x128 横屏 RGB565
│   ├── TFT_CN_Font       ← 78汉字 16x16 宋体字库 (LSB-first)
│   ├── TFT_Font          ← 95 ASCII 8x16 字库 (LSB-first)
│   ├── TFT_Img           ← 图标字模: WIFI(4+6帧), MQTT(3态+6帧动画), ICON_STAR, STAR_ANIM[16]
│   ├── Pwm_Driver        ← TIM1 全桥 PWM (95-150kHz, 1000ns死区)
│   ├── Inverter_Control  ← 软启动状态机 + 频率斜坡 + Cancel
│   ├── Adc_Driver        ← ADC1 双通道 + 64样本滑动窗口
│   ├── Key_Driver        ← 4键 FSM, 10ms去抖, 单击/双击/长按
│   ├── Led_Driver        ← 6 LED 驱动 (快闪/慢闪/常亮/灭)
│   ├── Buzzer_Driver     ← 蜂鸣器驱动
│   ├── Esp8266_Driver    ← USART2 115200, CH_PD+RST 非阻塞初始化
│   ├── Ui_Controller     ← V15 UI状态机 + 圆弧能量条仪表盘
│   └── Energy_Bar        ← 像素级动态能量条 (绿→红渐变)
├── System/
│   └── Sys_Timer         ← SysTick 1ms + DWT 周期计数器
├── User/
│   ├── main.c            ← main() 入口 (V14 极简状态机, 28行)
│   ├── App_Network       ← 联网管理 + 重试 + 指令接收 + 遥测发送
│   ├── Sys_State.h       ← 全局系统状态枚举 + extern g_sys_state
│   ├── Sys_Init          ← 阶段0-4 上电初始化
│   ├── Sys_Safety        ← PB10电源控制 + 过流检测 + EMA滤波 (从UI剥离)
│   ├── Sys_Run           ← 4模式 Task 子集调度 (IDLE/SWEEP/RUNNING/FAULT)
│   └── stm32f10x_it.c    ← ISR (SysTick + USART2)
└── Start/
    └── system_stm32f10x.c ← 时钟配置 (SYSCLK_FREQ_72MHz)
```

依赖方向: Hardware → System → Application，严格单向。

## 主循环调度 (V14)

```c
int main(void) {
    /* 初始化阶段 */
    Sys_Clamp_ESP();  Sys_Hardware_Init();  Sys_Startup_Screen();  Sys_Post_Init();
    g_sys_state = SYS_STATE_IDLE;

    while (1) {
        /* 共性任务: 按键 + ADC + 网络 + PB10 + 过流检测 */
        Key_Driver_Task();
        Adc_Driver_Filter_Task();
        App_Network_Task();
        Sys_Safety_Task();

        /* 按系统状态分发业务 Task 子集 */
        switch (g_sys_state) {
            case SYS_STATE_IDLE:    Sys_Run_Idle();    break;
            case SYS_STATE_SWEEP:   Sys_Run_Sweep();   break;
            case SYS_STATE_RUNNING: Sys_Run_Running(); break;
            case SYS_STATE_FAULT:   Sys_Run_Fault();   break;
            default: break;
        }
        IWDG_ReloadCounter();
        __WFI();
    }
}
```

## Sys_Safety 安全监测 (V14 独立模块)

- **EMA 滤波**: α=0.25, τ≈800ms, 每圈主循环更新, 供过流检测 + UI 取值
- **PB10 电源控制**: 电压 >12V → 拉高使能, ≤12V → 拉低关断
- **过流检测**: `s_safety_ema_i > 5.0A` → `Inverter_Control_Soft_Start_Fault()` + `Buzzer BEEP` + `g_sys_state = SYS_FAULT`
- **频率**: 不在 Sys_Safety 中滤波，`s_ema_f` 直接取 `Pwm_Driver_Get_Frequency() / 1000.0f`（无 EMA 迟滞，保证调频跟手）

## EMA 架构说明 (V14 双源解耦)

| 层级 | 模块 | 滤波对象 | 用途 |
|:---|:---|:---|:---|
| 安全级 | `Sys_Safety_Update_EMA()` | V (电压), I (电流) | 过流保护, PB10 阈值判断 |
| 显示级 | `Ui_Controller_Update_EMA()` | `Sys_Safety_Get_EMA_*()` 的返回值 | UI 仪表盘 + 综合监测页数值 |
| 数字量 | `Pwm_Driver_Get_Frequency()` | 无滤波 | 频率显示（原子寄存器读数，零迟滞） |

**关键**: 显示级 EMA 重新滤波 Sys_Safety 的输出（非 ADC 原始值），形成"安全快→显示慢"的两级滤波链，避免高频闪烁。

## 启动流程 (V10)

```
上电 → 阶段0: 最早钳位 PA1=0, PB11=0 (ESP RST+CH_PD)
     → 阶段1: Pwm_Driver_Init(TIM1全关) + PB10拉低 + TFT/LED/Buzzer/ADC/Key 初始化
     → TFT 启动页画面 + 背光开启
     → 阶段2: Sys_Timer_Init (SysTick + DWT) + Led_Set_System(1)
     → 阶段3: IWDG_Init (1.6s), DBGMCU 停止 (调试安全)
     → 阶段4: 开机自动联网 (App_Network_Start_Connect, ESP WiFiManager 记忆配网)
     → while(1) 主循环
```

## TFT 驱动 (ST7735 Green Tab, 已验证不可改)

| 参数 | 值 |
|:---|:---|
| 芯片 | ST7735 (非S) 1.8" 128×160 Green Tab (中景园 ZJY180S0800TG01) |
| SPI | Mode 3 (CPOL=High, CPHA=2Edge), 18MHz, 只写不读, DMA1_Channel3 |
| 分辨率 | 160×128 横屏 |
| MADCTL | **0xA0** (MY=1,MX=0,MV=1) |
| SetWin 偏移 | X+1, Y+2 (横屏) |
| 背光 | PB6, TIM4_CH1 PWM 1kHz (**APB1**) |
| 颜色 | RGB565 |
| DMA架构 | V11: 缓冲→DMA一发完成 (MINC=1), Fill用MINC=0同色泵送, 零WrD16 |
| 字库 | 8×16 ASCII (95字符), 16×16 中文 (78汉字), **5×10 微型数字 (12字符)** |
| 字库位序 | 全部 LSB-first (bit0=最左像素) |
| s_dma_buf[256] | 512B 静态缓冲区, 通用像素解码 |
| 图标元素 | `TFT_Img.h`: WIFI_ICON[4], WIFI_CONNECT_ANIM[6], MQTT 3态+6帧, ICON_STAR, STAR_CURSOR_ANIM[16] |

## PWM 基线 (不可改)

- **引脚**: PA8=TIM1_CH1, PA9=TIM1_CH2, PB13=TIM1_CH1N, PB14=TIM1_CH2N
- **映射**: 默认映射 (不调用 `GPIO_PinRemapConfig`)
- **模式**: `TIM_CounterMode_Up`, CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2`
- **占空比**: 50% 固定 (`TIM_Pulse = ARR/2`)
- **极性**: `TIM_OCNPolarity_Low` (IR2103S LIN 低有效)
- **空闲态**: `TIM_OCIdleState_Reset`, `TIM_OCNIdleState_Reset` (MOE=0 时上下管全关)
- **死区**: 1000ns, 编译期计算 `DTG = 72*1000/1000 = 72`
- **频率**: 95kHz~150kHz (`PWM_DRIVER_FREQ_MIN_HZ`/`MAX_HZ`)
- **原子更新**: UDIS→写ARR+CCR→UG→清UDIS (防输出毛刺)
- **开机状态**: `TIM_Cmd(DISABLE)` + `TIM_CtrlPWMOutputs(DISABLE)` — 计数器+MOE全关, 零输出
- **启动**: `Pwm_Driver_Enable()` → `TIM_Cmd(ENABLE)` + `TIM_CtrlPWMOutputs(ENABLE)`
- **停止**: `Pwm_Driver_Disable()` → `TIM_CtrlPWMOutputs(DISABLE)` + `TIM_Cmd(DISABLE)`
- 以上参数源自 V0.0 已验证硬件, 不准擅自改动

## ADC 驱动

- **电压通道**: PB1=ADC_CH9, 电阻分压比 `(100k+4.7k)/4.7k ≈ 22.28`, VREF=3.30V
- **电流通道**: PB0=ADC_CH8, CC6920BSO 霍尔传感器, 零电流中位 1.65V, 灵敏度 132mV/A
- **滤波**: 64 样本滑动窗口, DMA 循环刷新, 每 ~2ms 一个样本
- **过流保护**: `Sys_Safety` 中每圈检测 `s_safety_ema_i > 5.0A` → SYS_FAULT
- **零点校准**: `Adc_Driver_Calibrate_Offset()` 存在但未调用, 偏移固定 1.65V

## Key_Driver

- **按键**: PB9=ON/OFF, PB8=F_UP, PB7=F_DOWN, PB5=PAGE
- **电气**: 全部 GPIO IPU (内部上拉), 按下=低电平
- **FSM**: IDLE→DEBOUNCE(10ms)→PRESS→WAIT_DOUBLE(200ms)→IDLE; PRESS→LONG(3000ms)
- **事件**: NONE / CLICK / DOUBLE_CLICK / LONG_PRESS (读后自动清空)
- **临界区**: `Key_Driver_Get_Event(id)` 内部 PRIMASK 保护

## Led_Driver

- **引脚**: SYSTEM(PA15), WiFi(PB4), PWM(PB3), COM(PA10), POWER(PA11), TEMP(PA12)
- **状态**: OFF / ON / SLOW(500ms) / FAST(200ms)
- **逻辑**:
  - SYSTEM: 500ms 心跳, 初始化后立即点亮
  - WiFi: 离线慢闪 / 连接中快闪 / 在线常亮
  - PWM: 扫频快闪 / 其余灭
  - COM: MQTT在线(ONLINE状态)常亮
  - POWER: 电压>12V 常亮 (与 PB10 同步)
  - TEMP: 暂未启用灭

## Esp8266_Driver (V6.4)

- **硬件**: CH_PD=PB11 (EN), RST=PA1 (独立复位), USART2 115200 8N1
- **供电**: 独立 3.3V LDO, 不可从 STM32 板载 LDO 取电
- **初始化**: GPIO+USART2 仅配一次 (`Esp8266_Driver_Config_GPIO_Once` + `Esp8266_Driver_Config_USART_Once`), `s_hw_configured` 保护
- **状态机**: IDLE → RST_PULSE(100ms) → BOOT_WAIT(4s) → READY
- **启动时序**: `Start_Init()` → CH_PD=1 → RST=0 100ms脉冲 → RST=1 释放 → 等4s固件加载
- **接收**: ISR 逐字节, `\r`/`\n` 触发帧完成, `Copy_Rx_Frame` 消费后清空, RX 256字节

**已知问题：整板冷启动时 ESP 通信乱码 (波特率偏), 单独给 ESP 上电后正常。根因待查。**

## Inverter_Control

### 软启动状态机
| 参数 | 值 |
|:---|:---|
| 起始频率 | 150kHz |
| 目标频率 | 100kHz |
| 步进 | 200Hz/步, 10ms/步 (~2.5s) |
| 状态 | IDLE → SWEEP → DONE / FAULT |

### 频率斜坡 (运行时微调)
| 参数 | 值 |
|:---|:---|
| 步进 | 1kHz/步, 10ms/步 |
| 范围 | 95kHz~150kHz |
| 来源 | F+/F- 按键 或 远程 CMD:SETFREQ: |

### 安全
- `Soft_Start_Reset()` 从 FAULT 恢复: 关PWM + 重置频率 + 清斜坡
- Stop/Fault/Trigger 均清理 `s_ramp_state`
- CMD:OFF 仅允许 SWEEP/DONE (禁止远程清除 FAULT)
- 所有远程指令检查 `Ui_Controller_Is_No_WiFi_Mode()`

## App_Network

- **连接状态**: IDLE→WIFI→MQTT→ONLINE, 失败→FAILED
- **重试**: 最多 3 次, 每次超时 8s, 超时后立即失败
- **指令解析**: STATUS:ONLINE / CMD:OFF / CMD:ON / CMD:SETFREQ:
  - `CMD:ON`/`CMD:OFF` 加分隔符检查 (防 `CMD:ON` 匹配 `CMD:ONLINE`)
  - `CMD:SETFREQ:` 偏移用 `strstr` 返回值计算
  - 所有远程指令检查 `Ui_Controller_Is_No_WiFi_Mode()`
- **遥测**: 每 500ms 发送 `{"V":x,"I":x,"F":x}`, 仅 RUNNING 状态且 ONLINE

**已知问题**: V14 中远程 CMD:ON/OFF 未同步更新 `g_sys_state`，远程控制可能假死。修复方案：App_Network 调用时设置 `g_sys_state` 而非仅操作底层。

## Energy_Bar (动态能量条)

- 替代 `#` 字符刻度条, 像素级绘制, 绿→红 8段渐变
- `Energy_Bar_Draw(x, y, max_w, h, value, min_val, max_val, metric, bg_color)`
- 宽度 = `(value - min) / (max - min) * max_w`, 钳位 0~1

## PB10 PowerContrl

- **逻辑**: 高使能/低关断 12V (与 PWM 开关**完全独立**)
  - `Adc_Driver_Get_Voltage() > 12V` → 拉低使能
  - `Adc_Driver_Get_Voltage() ≤ 12V` → 拉高关断
- **POWER LED**: 与 PB10 同步, >12V 亮
- **上电初始**: PB10 拉低 (关断 12V, 安全态)

## UI 状态机 (V15 — 圆弧能量条 + 信息舱)

**9 页面** (扁平枚举, 栈式导航):

**MENU pages**: MAIN_MENU, MONITOR_SUB_MENU, SWEEP, MONITOR_SUMMARY, WIFI_SETUP, FAULT — 增量刷新, 光标=ICON_STAR, 200ms 节流.

**GAUGE pages**: MONITOR_FREQ, MONITOR_VOLT, MONITOR_CURR — 全屏 160×128, 圆弧能量条仪表盘 + 底部三行信息舱.

### 圆弧能量条仪表盘 (V15)

| 组件 | 实现 |
|:---|:---|
| 圆心 | G_CX=80, G_CY=84 (下移18px 腾出底部信息舱) |
| 刻度半径 | R_TICK=56 (外圈), R_BIG=50 (主刻度6px), R_FINE=53 (细刻度3px) |
| 刻度绘制 | 1px Bres_Line, 无 Fill_Rect 加粗, 无连接弧 |
| 能量条 | `a <= na` 高亮色 / `a > na` 暗灰槽 (0x18C3), Bres_Line 增量差分回退 |
| 数字标注 | 5×10 微数字, Dynamic Anchor Alignment, 2px 边缘排斥力 |
| 信息舱 | Row 4(Y=64): 状态章 OK/WRN/HI/SWP/DON/IDL — 居中 |
| | Row 5(Y=80): 纯数值 12.45 — 居中黄色 |
| | Row 6(Y=96): 标签 电压 V / 电流 A / 频率 kHz — 居中青色 |
| 右上角 | WIFI@128 + MQTT@144 (全页面 Phase 0 统一管理) |
| PWM 停止 | Freq 页 val=0 能量条归零, 数值灰 "0" |

### GaugeConfig 三表参数 (V15)

| 参数 | 电压 V | 电流 C | 频率 F |
|:---|:---|:---|:---|
| range | 0→50 | 0→**2** | 90→150 |
| big_step | 10 | **0.5** | 10 |
| mid_step | 5 | 0.25 | 5 |
| fine_step | 1 | 0.1 | 1 |
| red_start | 42 | **1.8** | 140 |

### Phase 架构 (V14 精简)

| Phase | 职责 |
|:---|:---|
| Phase 0 | Global Top-Right Icons Manager (cs/wifi_frame/mqtt_frame/page diff) |
| Phase 1 | System Fault detection (监听 g_sys_state) |
| Phase 2 | Sweep complete → auto-jump SUMMARY |
| Phase 3 | Key scan + dispatch → may set s_page or g_sys_state |
| Phase 4 | Page change → s_page_drawn=0; all tracking invalidated |
| Phase 5 | 200ms tick → dynamic incremental update |
| Phase 6 | Cursor boundary clamp |
| Phase 7 | Draw — full page only when s_page_drawn==0 |

**安全逻辑 (PB10/过流) 已从 UI 完全剥离到 Sys_Safety 层。**

### 按键功能 (V10)

| 按键 | 事件 | 生效状态/页面 | 功能 |
|:---|:---|:---|:---|
| ON/OFF | 单击 | 菜单页 | 确认/进入 (启动PWM/切换启停/进入子项) |
| ON/OFF | 单击 | SWEEPING/RUNNING | 停止 PWM |
| ON/OFF | 单击 | FAULT | 复位逆变器 (`Soft_Start_Reset`) |
| ON/OFF | 双击 | 全部 | 智能WiFi: 在线→断开, 离线→连接 |
| ON/OFF | 长按 | 全部 | 清除WiFi配网 + 进入无WIFI |
| PAGE | 单击 | RUNNING仪表盘 | 切子页 (综合→频率→电压→电流→综合) |
| PAGE | 单击 | 菜单以外的非运行页 | 进入无WIFI调试 |
| PAGE | 双击 | RUNNING | 回到综合监测 |
| F_UP | 单击 | 菜单 | 光标上移 (循环) |
| F_UP | 单击 | RUNNING | 频率 +1kHz |
| F_DOWN | 单击 | 菜单 | 光标下移 (循环) |
| F_DOWN | 单击 | RUNNING | 频率 -1kHz |

### 完整画面布局 (V15)

#### 仪表盘页面 (MONITOR_FREQ/VOLT/CURR)
```
全屏 160×128, 无 Header, 无 Divider, 无 Bottom Bar
  圆弧 G_CX=80, G_CY=84, R_TICK=56
  能量条 + 5×10 微数字标注
  Row 4(Y=64): OK/WRN/HI 或 SWP/DON/IDL — 居中
  Row 5(Y=80): 12.45 — 居中黄色大字
  Row 6(Y=96): 电压 V / 电流 A / 频率 kHz — 居中青色
  右上角: WIFI@128 + MQTT@144
  PWM 停止: Freq 页 val=0 → 能量条全暗灰 + 数值灰色 "0"
```

#### MAIN_MENU
```
行0: [WPT-PWM 左对齐 黄]              WIFI|MQTT
行1: --------------------
行2: 1.启动/停止PWM
行3: 2.状态监测
行4: 3.无线配网
行5: 4.故障清除 (仅FAULT)
行6: --------------------
行7: [ON:确认 PAGE:返回 右对齐 白]
```

#### MONITOR_SUB_MENU
```
行0: [状态监测 左对齐 黄]            WIFI|MQTT
行1: --------------------
行2-5: 1.综合监测 / 2.监测频率 / 3.监测电压 / 4.监测电流 / 5.返回主菜单
行6: --------------------
行7: [返回 右对齐 白]
```

#### WIFI_SETUP
```
行0: [启动页 左对齐 黄]              WIFI|MQTT
行1: --------------------
行2: 无线状态: ...
行3: 重试 N/3
行5: [ON:连接/断开WIFI]
行6: [长按ON: 清除WIFI 红]
```

#### FAULT
```
行0: [!!!故障!!! 左对齐 红]          WIFI|MQTT
行3: 过流保护 [居中 红]
行5: PWM已关断 [居中 白]
行7: [ON:复位重启 右对齐 白]
```

#### SWEEP
```
行0: [扫频页 左对齐 黄]              WIFI|MQTT
行1: --------------------
行2: 频率F:xxx.xkHz [白]
行3-4: 能量条 + 百分比
行5: 电压 [居中 蓝]
行6: 电流 [居中 蓝]
行7: [OFF:停止扫频 右对齐 白]
```

#### MONITOR_SUMMARY
```
行0: [监测模式 左对齐 黄]            WIFI|MQTT
行1: --------------------
行3: 频率 [居中 白]
行4: 电压 [居中 白]
行5: 电流 [居中 白]
行7: [OFF:停止 右对齐 白]
```

### WIFI/MQTT 图标规则 (Phase 0)

| 状态 | WIFI | MQTT |
|:---|:---|:---|
| 在线 | 绿色, RSSI 分级 0-3 | 绿色 MQTT_YES_ICON |
| 连接中 | 蓝色渐变 6帧 150ms | 彩虹 6帧 200ms |
| 失败/离线 | 红色 WIFI_OFF_ICON | 红色 MQTT_NO_ICON |
| 无WiFi模式 | 红色 WIFI_REMOVE_ICON | 红色 MQTT_NO_ICON |

**Phase 0 触发条件**: cs != s_last_icon_cs / wifi_frame / mqtt_frame / s_page 四者任一变化即重绘。

## 编码规范

### 命名规范 (V14 强制执行)

全部模块统一采用 `Module_Name_Action_Object()` 帕斯卡+下划线命名。

**零容忍铁律**：即使是用户直接指定的命名, 不符合规范也必须更正。AI 生成代码时首次就按规范命名, 不允许使用临时占位名或通用短名。

| 层次 | 规则 | 正确示例 | 违规示例 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` (模块前缀强制) | `Sys_Run_Led_Tick()` | `Led_Tick()`, `polar()` |
| 静态变量 | `s_module_description` 小写+下划线 | `s_ui_state`, `s_gauge_val_str` | `uiState` |
| 全局变量 | `g_description` 小写+下划线 | `g_sys_state` | `Sys_State_Global` |
| 类型/枚举名 | `Module_Name_Type` | `Sys_State`, `GaugeConfig` | `sysState_t` |
| 枚举值 | `MODULE_NAME_ENUM_VALUE` 全大写+下划线+模块前缀 | `SYS_STATE_IDLE` | `State_Idle` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写+下划线+模块前缀 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |
| 头文件保护 | `MODULE_NAME_H` 无前导下划线 | `SYS_STATE_H` | `_SYS_STATE_H` |
| 文件名 | PascalCase+下划线, `.c`/`.h` 配对 | `Sys_State.h` | `sysstate.h` |

### 注释 (V14 强化)

- 公开函数必须带 `@brief` + `@param`/`@retval` (如有参数/返回值)
- 模块 `.h` 顶部必须带 `@file` + `@brief` + `@note`
- 禁止 `//` 双斜杠注释, 统一用 `/** */` 或 `/* */`
- 不写 HOW (代码本身说明), 只写 WHY (为什么这样做, 踩过什么坑)
- 修改任何函数逻辑时, 同步更新注释

### 状态机
- 禁止隐式 bool/int 拼凑 → 必须用 `typedef enum` + 单一状态变量
- 状态转换集中在 Task 函数内, 用 `switch` 分发

### 调度
- 所有周期任务: `Sys_Timer_Get_Tick() - last >= PERIOD` (uint32_t 回绕安全)
- `Sys_Timer_Delay_Ms()` 仅限初始化阶段, 运行时绝对禁止阻塞延时
- 主循环末尾 `__WFI()` 休眠, SysTick 唤醒

### 模块架构
- `.h` 只放公开接口, `.c` 放全部实现 + 静态变量
- `.c` 内部函数一律 `static`, 不准跨模块 `extern` 访问私有变量
- 不允许 `#include ".c"` 文件
- 分层依赖: Hardware → System → Application, 严格单向

### 临界区
```c
uint32_t primask = __get_PRIMASK();
__disable_irq();
/* critical section */
__set_PRIMASK(primask);
```
禁止裸 `__disable_irq()` + `__enable_irq()` (不安全)

## 跨子项目命名规范 (V14)

本仓库含 3 个独立子项目, 各自遵循其语言生态的标准命名约定。

| 层级 | STM32 (C/SPL) | ESP8266 (Arduino C++) | ONENETapp (JavaScript) |
|:---|:---|:---|:---|
| 函数/方法 | `Module_Name_Verb_Noun()` | `Module_Task_Verb_Noun()` | `camelCase()` / `PascalCase` class |
| 静态函数 | `Module_Name_Verb_Noun()` (强制前缀) | 同左, 推荐前缀 | N/A |
| 静态变量 | `s_module_description` | `s_module_description` | N/A |
| 全局变量 | `g_description` | 禁止 | 禁止 |
| 枚举值 | `MODULE_NAME_VALUE` | `MODULE_NAME_VALUE` (推荐) | `UPPER_SNAKE_CASE` const |
| 注释 | `/** @brief */` `/* ── */` | `/** @brief */` `/* */` | `/** JSDoc */` `// inline` |
| 禁止注释 | `//` 双斜杠 | `//` 允许 | `//` 允许 |

**ESP8266 约束**: 禁止 `new`/`delete`, 避免 `String` 类, 优先 `char[]`。
**ONENETapp 约束**: 无 `require`/`import`, localStorage key 统一 `iot_` 前缀, 网络请求 3s 超时。

## Safety

- **故障处理**: Fault → 关 PWM (MOE+计数器) → 锁存 FAULT 状态 → Buzzer BEEP
- **FAULT 恢复**: 单击 ON/OFF → `Soft_Start_Reset()` → 回到 MAIN_MENU
- **PB10**: 高使能/低关断 12V。仅受电压阈值(>12V)控制, 与PWM独立
- **过流保护**: `Sys_Safety` 每圈检查 `s_safety_ema_i > 5.0A` → SYS_FAULT
- **上电安全**: TIM1 全关(CEN+MOE), PB10 拉低关12V
- **看门狗**: IWDG 1.6s, `DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP` 调试暂停
- **HardFault**: 所有故障 ISR 先关 PWM 再死循环

## PWM 频率量化表

TIM1_CLK = 72MHz。Up 计数模式: `f_actual = 72MHz / ticks`, ticks 强制偶数 (防 DC 偏磁)。

| 目标 kHz | ticks | 实际 Hz | 实际 kHz | 误差 |
|:---|:---|:---|:---|---:|
| 95 | 758 | 94,987 | 94.99 | -0.01% |
| 100 | 720 | 100,000 | 100.00 | 0.00% |
| 105 | 684 | 105,263 | 105.26 | +0.25% |
| 110 | 654 | 110,092 | 110.09 | +0.08% |
| 115 | 626 | 115,016 | 115.02 | +0.01% |
| 120 | 600 | 120,000 | 120.00 | 0.00% |
| 125 | 576 | 125,000 | 125.00 | 0.00% |
| 130 | 554 | 129,963 | 129.96 | -0.03% |
| 135 | 534 | 134,831 | 134.83 | -0.13% |
| 140 | 514 | 140,078 | 140.08 | +0.06% |
| 145 | 496 | 145,161 | 145.16 | +0.11% |
| 150 | 480 | 150,000 | 150.00 | 0.00% |

## ADC 互质相位采样 (Anti-Aliasing)

```
DWT 采样周期 = 144241 CPU 周期, PWM 周期 = 720 CPU 周期 (72MHz / 100kHz)
互质性: gcd(144241, 720) = 1 ✓
采样在 720 个不同 PWM 相位均匀分布
→ 64 样本滑动窗口 (128ms) 收敛至 DC 分量
```

**编译期保护**: `typedef char Adc_Driver_Assert_HSE_72MHz[(SystemCoreClock == 72000000) ? 1 : -1]`

## 关键指标

| 参数 | 值 |
|:---|:---|
| Code | 32.8KB |
| RO-data | 9.0KB |
| RW-data | 336B |
| ZI-data | 2.8KB |
| Flash 占用 | ~64% (41.8KB/64KB) |

## Git

```bash
# 标准提交流程
cd Keil_Project && ./keilkill.bat && cd ..
git add -A && git commit -m "feat/fix/docs: ..." && git push origin 4.0TFT
```

## 环形仪表盘速查 (V15)

```c
#define G_CX   80    /* 圆心 X */
#define G_CY   84    /* 圆心 Y (下移, 腾出底部三行信息舱) */
#define R_TICK 56    /* 刻度外圈 */
#define R_BIG  50    /* 主刻度内圈 (6px) */
#define R_FINE 53    /* 细刻度内圈 (3px) */
#define CPS(x) ((uint8_t)(x))

/* 核心渲染函数 (Ui_Controller.c) */
Bres_Line(x0, y0, x1, y1, color);            /* 1px Bresenham 线 (能量条刻度) */
Gauge_Polar(angle_deg, radius, &x, &y);       /* sin查表极坐标 (圆心G_CX,G_CY) */
Draw_Gauge_Full(cfg, val);                     /* 入场全绘: 清屏+能量条+标注+信息舱+图标 */
Gauge_Dynamic_Update(cfg, val, old_val);       /* 200ms 增量: 能量条差分+数值/状态 diff */
Draw_TopRight_Icons();                         /* WIFI@128 + MQTT@144 */

/* 三表配置 (V15) */
GAUGE_V = { 0,  50, 10,  5,    1,   42,  'V'};  // {min,max,big,mid,fine,red,label}
GAUGE_C = { 0,   2, 0.5, 0.25, 0.1, 1.8, 'C'};  // 电流 0-2A, 大刻度 0.5A
GAUGE_F = {90, 150, 10,  5,    1, 140,  'F'};    

/* 信息舱布局 (Y 行坐标) */
Row 4 (Y=64): 状态章 OK/WRN/HI 或 SWP/DON/IDL — 居中, 对应颜色
Row 5 (Y=80): 纯数值 "12.45" — 居中, 黄色大字
Row 6 (Y=96): 标签 "电压 V" / "电流 A" / "频率 kHz" — 居中, 青色
PWM 停止: Freq 页 val=0 → 能量条全暗灰 + 数值灰 0
```

## TFT 注意事项

- **Fill_Rect(1,1) 陷阱**: 每像素一次 SetWin + SPI 模式切换 + DMA 等待, 高频调用引发 SPI 片选饱和导致横竖线条纹。改用 Bres_Line 逐像素或逐行 Fill_Rect。
- **DMA 传输后必须等待 BSY**: `while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY))` 确保最后一帧发完再拉高 CS。
- **SPI 8→16→8 切换**: 每次 DMA 传输前后必须停 SPI, 切 DFF, 重开 SPI。
- **WIFI(x=128) + MQTT(x=144) 位置不变**: 所有页面共用, Phase 0 帧差+切页检测调度。
- **PCtoLCD2002 取模配置**: 字符模式, 行主序, LSB-first, 宽度对齐到字节。5×10 每字 10 字节。
