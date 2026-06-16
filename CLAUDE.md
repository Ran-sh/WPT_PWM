# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `4.0TFT` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT` |
| **版本** | V14 |
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

## 模块分层与文件结构

```
Keil_Project/
├── Hardware/
│   ├── Tft_Driver        ← ST7735 SPI 彩屏, 160x128 横屏 RGB565
│   ├── TFT_CN_Font       ← 78汉字 16x16 宋体字库 (LSB-first)
│   ├── TFT_Font          ← 95 ASCII 8x16 字库 (LSB-first)
│   ├── TFT_Img           ← 图标字模: WIFI(4+6帧), MQTT(3态+6帧动画), ICON_STAR, STAR_ANIM[16]
│   ├── Pwm_Driver        ← TIM1 全桥 PWM (95-150kHz, 1000ns死区)
│   ├── Inverter_Control  ← 软启动状态机 + 频率斜坡 + Cancel
│   ├── Adc_Driver        ← ADC1 双通道 + 64样本滑动窗口 + EMA
│   ├── Key_Driver        ← 4键 FSM, 10ms去抖, 单击/双击/长按
│   ├── Led_Driver        ← 6 LED 驱动 (快闪/慢闪/常亮/灭)
│   ├── Buzzer_Driver     ← 蜂鸣器驱动
│   ├── Esp8266_Driver    ← USART2 115200, CH_PD+RST 非阻塞初始化
│   ├── Ui_Controller     ← V13 两级菜单架构 (9页面)
│   └── Energy_Bar        ← 像素级动态能量条 (绿→红渐变)
├── System/
│   └── Sys_Timer         ← SysTick 1ms + DWT 周期计数器
├── User/
│   ├── main.c            ← main() 入口, 4阶段启动 + 非阻塞主循环
│   ├── App_Network       ← 联网管理 + 重试 + 指令接收 + 遥测发送
│   └── stm32f10x_it.c    ← ISR (SysTick + USART2)
└── Start/
    └── system_stm32f10x.c ← 时钟配置 (SYSCLK_FREQ_72MHz)
```

依赖方向: Hardware → System → Application，严格单向。

## 主循环调度

```c
while (1) {
    Key_Driver_Task();                  // 10ms  4键独立FSM轮询
    Adc_Driver_Filter_Task();           // ~2ms  ADC DMA + 滑动窗口滤波
    Ui_Controller_Task();               // 200ms UI状态机 + 页面绘制 + 按键分发 + PB10 + 过流
    App_Network_Task();                 //       ESP初始化 + 重试 + 指令 + 遥测
    Inverter_Control_Soft_Start_Task(); // 10ms  扫频 150k→100kHz, 200Hz/步
    Inverter_Control_Freq_Ramp_Task();  // 10ms  频率微调 1kHz/步
    Led_Driver_Task();                  //       LED 闪烁 + 状态输出
    Buzzer_Driver_Task();               //       蜂鸣器调度
    IWDG_ReloadCounter();               //       1.6s 看门狗
    __WFI();                            //       休眠
}
```

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
- **EMA**: 指数移动平均 (alpha=0.25, τ≈800ms), 在 `Update_EMA()` 中调用, 用于 UI 显示
- **过流保护**: 使用 EMA 平滑值 `s_ema_i > 5.0A`, 每 200ms 检查
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
- 每个 tick 轮询驱动所有引脚

## Esp8266_Driver (V6.4)

- **硬件**: CH_PD=PB11 (EN), RST=PA1 (独立复位), USART2 115200 8N1
- **供电**: 独立 3.3V LDO, 不可从 STM32 板载 LDO 取电
- **初始化**: GPIO+USART2 仅配一次 (`Config_GPIO_Once` + `Config_USART_Once`), `s_hw_configured` 保护
- **状态机**: IDLE → RST_PULSE(100ms) → BOOT_WAIT(4s) → READY
- **启动时序**: `Start_Init()` → CH_PD=1 → RST=0 100ms脉冲 → RST=1 释放 → 等4s固件加载
- **接收**: ISR 逐字节, `\r`/`\n` 触发帧完成, `Copy_Rx_Frame` 消费后清空, RX 256字节
- **main.c 阶段0**: 最早钳位 PA1=0, PB11=0 防止上电浮空

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

**V10 新增**: `Freq_Ramp_Cancel()` — 手动按键时取消远程斜坡，避免竞态。F+/F- 用 `s_user_target_hz` 按 1kHz 粒度追踪逻辑目标，避免整数分频偏差累积导致频率非单调跳变。

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
  - `CMD:SETFREQ:` 偏移用 `strstr` 返回值计算, 不硬编码 `+12`
  - 所有远程指令检查 `Ui_Controller_Is_No_WiFi_Mode()`
- **遥测**: 每 500ms 发送 `{"V":x,"I":x,"F":x}`, 仅 UI≥READY 且 ONLINE
- **心跳**: 已移除 (ESP 无心跳帧, 30s 超时误判离线)
- **`Soft_Reset`**: 仅重置网络状态为 IDLE (用于进入无WIFI模式), 不重启硬件
- **`Start_Connect`**: 启动 ESP 硬件初始化 + 联网

## Energy_Bar (动态能量条)

- 替代 `#` 字符刻度条, 像素级绘制, 绿→红 8段渐变
- `Energy_Bar_Draw(x, y, max_w, h, value, min_val, max_val, metric, bg_color)`
- 宽度 = `(value - min) / (max - min) * max_w`, 钳位 0~1
- 段数自适应每段 ≥4px (1~8段), range≤0 擦除后返回
- 刻度范围标签 (95/150, 0/48, 0/3) 保持不变

## PB10 PowerContrl

- **逻辑**: 高使能/低关断 12V (与 PWM 开关**完全独立**)
  - `Adc_Driver_Get_Voltage() > 12V` → 拉低使能
  - `Adc_Driver_Get_Voltage() ≤ 12V` → 拉高关断
- **POWER LED**: 与 PB10 同步, >12V 亮
- **上电初始**: PB10 拉低 (关断 12V, 安全态)

## UI 状态机 (V13 — 两级菜单 + 环形仪表盘)

**9 页面** (扁平枚举, 栈式导航):

**MENU pages**: MAIN_MENU, MONITOR_SUB_MENU, SWEEP, MONITOR_SUMMARY, WIFI_SETUP, FAULT — 增量刷新, 光标=ICON_STAR, 200ms 节流.

**GAUGE pages**: MONITOR_FREQ(), MONITOR_VOLT(), MONITOR_CURR() — **全屏 160×128 无 Header/Divider/Bottom**, sin 查表极坐标, 3线锥形指针, 5×10 微数字标注.

### 环形仪表盘引擎 (V13 精简版 — 无连线弧, 无火箭)

| 组件 | 实现 |
|:---|:---|
| sin 查表 | `GAUGE_SIN[]` 180+ 元素 int16_t, 自动计数, ~724B ROM |
| 极坐标 | `Gauge_Polar(a, r, &x, &y)` — a=0°=左, 90°=顶, 180°=右 |
| 圆心 | (80, 100), 刻度外端 R_TICK=64, 大/中/细内端 R=54/58/61 |
| 刻度 | 三层径向线 (主10px/中6px/细3px), 仅 `Bres_Line` 1px, 无连线弧, 不加粗 |
| 数字 | 5×10 微数字, `Dynamic Anchor Alignment` — sin/cos 自适应偏移, 锚点 R_TICK+6 |
| 指针 | `Draw_Pointer(a, color)` — 中心线全长 + 侧线 2/3 长 → 锥形渐尖, PTR_LEN=46 (R_BIG-8, 不触刻度) |
| 尾部 | 单线 1px (Hub 中心→反向 1/3 处) |
| Hub | `Draw_Hub()` — 逐行 Fill_Rect 填充 4 层 (黑底圆+外圈+中圈+红核+高光) |
| 右上角区 | `Draw_TopRight_Icons()` — **WIFI@x=128 + MQTT@x=144** (两区, 所有页面共用) |
| 左上角区 | **Badge@x=0,col=0** (左对齐): V/C 页 OK/WRN/HI, F 页 SWP/DON/IDL |
| 底行值 | 8×16 左对齐 `F 125.0` / `V 12.50` / `C 1.250` — 位于 Hub 下方 y=112 (行7) |
| PWM 停止 | Freq 页灰字 `F ---.-`, 指针不画, `s_user_target_synced=0` |

### GaugeConfig 三表参数 (V13 整十对齐)

| 参数 | 电压 V | 电流 C | 频率 F |
|:---|:---|:---|:---|
| range | 0→**50** | 0→**5** | **90**→150 |
| big_step | **10** | **1** | 10 |
| mid_step | **5** | **0.5** | **5** |
| fine_step | **1** | **0.1** | 1 |
| red_start | 42 | **4.0** | **140** |

### 增量刷新 (V13)

- **入场**: `Draw_Gauge_Full()` — Tft_Driver_Clear + 刻度+数字+底行值+指针+Hub+Badge+WIFI/MQTT
- **200ms**: `Gauge_Dynamic_Update()` — Z轴图层: 擦旧针→擦旧字绘新字(`s_gauge_val_str` diff)→绘新针→Hub; 仅 `oa!=na` 或 `text_changed` 时重绘
- **每帧 (Phase 0)**: WIFI/MQTT 帧差检测, 仅帧号变化时重绘行0; 无全局擦除 (底层的 `Draw_Single_Icon` 原生含背景覆盖)
- **PWM 停止时**: Freq_Dynamic_Update 跳过指针绘制, 仅刷新灰字

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
| F_UP | 单击 | RUNNING | 频率 +1kHz (基于逻辑目标, 单调递增) |
| F_DOWN | 单击 | 菜单 | 光标下移 (循环) |
| F_DOWN | 单击 | RUNNING | 频率 -1kHz (基于逻辑目标, 单调递减) |

### 频率步进 (V10 修复)

**问题**: F+/F- 用 `Get_Frequency()` 实际值 (整数分频偏离) +1kHz → `Set_Frequency` 导致非单调跳变 (例如 101.1→101.0→102.9).

**修复**: 引入 `s_user_target_hz` 追踪逻辑 kHz 目标。首次按键从实际频率四舍五入到最近 kHz 同步, 之后严格 ±1000Hz 步进, 不受整数分频累积偏差影响。同时 `Freq_Ramp_Cancel()` 防止手动步进与远程斜坡竞态。

### 完整画面布局 (代码 line 0-7 = 显示行 1-8)

**画面1: MAIN_MENU (主菜单)**
```
行0: [WPT-PWM 标题 左对齐 黄]           WIFI|MQTT
行1: --------------------
行2: 1.启动/停止PWM         ← 动态文字(运行中=停止PWM)
行3: 2.状态监测
行4: 3.无线配网             ← cursor 0-2 (非FAULT)
行5: 4.故障清除             ← 仅FAULT时启用, 否则灰色
行6: --------------------
行7: [ON:确认 PAGE:返回 右对齐 白]
```

**画面2: MONITOR_SUB_MENU (监测子菜单)** — 4行滚动窗口
```
行0: [状态监测 标题 左对齐 黄]         WIFI|MQTT
行1: --------------------
行2-5: 1.综合监测 / 2.监测频率 / 3.监测电压 / 4.监测电流 / 5.返回主菜单
       (可见窗口4行, cursor≥3时内容上滚)
行6: --------------------
行7: [返回 右对齐 白]
```

**画面3: WIFI_SETUP (无线配网)**
```
行0: [启动页 左对齐 黄]               WIFI|MQTT
行1: --------------------
行2: 无线状态: 连接成功/连接失败/连接中/未连接  [白]
行3: 重试 1/3  [仅连接中显示, 其余擦除]
行5: [ON:连接/断开WIFI 右对齐]
行6: [长按ON: 清除WIFI 右对齐 红]
行7: --------------------
```

**画面4: FAULT (过流故障)**
```
行0: [!!!故障!!! 左对齐 红]           WIFI|MQTT
行3: 过流保护 [居中 红]
行5: PWM已关断 [居中 白]
行7: [ON:复位重启 右对齐 白]
```

**画面5: SWEEP (扫频)** — 仅子页0
```
行0: [扫频页 左对齐 黄]               WIFI|MQTT
行1: --------------------
行2: 频率F:xxx.xkHz [白 左对齐]
行3-4: 能量条(像素级) + 百分比
行5: 电压V:xx.xxV [居中 蓝]
行6: 电流I:+x.xxA [居中 蓝]
行7: [OFF:停止扫频 右对齐 白]
```

**画面6: MONITOR_SUMMARY (综合监测)**
```
行0: [监测模式 左对齐 黄]             WIFI|MQTT
行1: --------------------
行3: 频率F:xxx.xkHz [居中 白]
行4: 电压V:xx.xxV [居中 白]
行5: 电流I:+x.xxA [居中 白]
行7: [OFF:停止 右对齐 白]
```

**画面7-9: MONITOR_FREQ/VOLT/CURR (V13 环形仪表盘)**
```
全屏 160×128 无 Header/Divider/Bottom
  圆心(80,100), 刻度外端 R_TICK=64, 大/中/细 54/58/61
  悬空刻度线 (无连线弧), 1px 锐利 Bres_Line
  5×10 微数字 Dynamic Anchor Alignment (sin/cos 自适应)
  3线锥形红色指针 + 金属 Hub
  底行值(行7,y=112) 左对齐, col=0
  左上角 Badge@col=0: F页 SWP/DON/IDL, V/C页 OK/WRN/HI
  右上角: WIFI@x=128 + MQTT@x=144 (Draw_TopRight_Icons)
  PWM 停止 → Freq 页灰字 "F ---.-", 无指针
```

### 行0顶部区布局 (V13 解耦)

```
行0 (y=0, h=16):  ┌ 标题/Badge 左对齐 ─── ─── ─── ─── ─── WIFI ┬ MQTT ┐
  菜单页:          [WPT-PWM...] 黄                            x=128  x=144
  仪表盘:          [SWP/DON/IDL]@col=0                        x=128  x=144
                   ← Draw_Header + Badge ──────────────→  ← Draw_TopRight_Icons ─→
```

**V13 解耦**: `Draw_Header(title)` 擦除行0 + 左对齐标题。仪表盘页不调 Draw_Header, Badge 在左上角 col=0。`Draw_TopRight_Icons()` 仅负责 WIFI@128 + MQTT@144, 无全局擦除 (底层覆盖)。所有 9 个页面通过 Phase 0 帧差检测统一调度。

**WIFI角标颜色规则:**

| 状态 | 颜色 | 动画 |
|:---|:---|:---|
| WiFi在线 | 绿色 | 静态 WIFI_ICON (RSSI分级 0-3格) |
| 连接中 | 蓝色渐变 | WIFI_CONNECT_ANIM 6帧 (150ms/帧) |
| 连接失败/离线 | 红色 | WIFI_OFF_ICON 静态 |
| 无WiFi模式 | 红色 | WIFI_REMOVE_ICON 静态 |

**MQTT云颜色规则:**

| 状态 | 颜色 | 动画 |
|:---|:---|:---|
| 在线 | 绿色 | MQTT_YES_ICON 静态 |
| 连接中 | 彩虹渐变(6色) | MQTT_ANIM 6帧 (200ms/帧) |
| 离线/失败 | 红色 | MQTT_NO_ICON 静态 |

## 编码规范

### 命名规范 (V14 强制执行)

全部模块统一采用 `Module_Name_Action_Object()` 帕斯卡+下划线命名。

**零容忍铁律**：即使是用户直接指定的命名, 不符合规范也必须更正。AI 生成代码时首次就按规范命名, 不允许使用临时占位名或通用短名。

| 层次 | 规则 | 正确示例 | 违规示例 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` (模块前缀强制) | `Sys_Run_Led_Tick()` | `Led_Tick()`, `polar()` |
| 静态变量 | `s_module_description` 小写+下划线 | `s_ui_state`, `s_gauge_val_str` | `uiState`, `gauge_str` |
| 全局变量 | `g_description` 小写+下划线 | `g_sys_state` | `Sys_State_Global` |
| 类型/枚举名 | `Module_Name_Type` | `Sys_State`, `GaugeConfig` | `sysState_t`, `gauge_config_t` |
| 枚举值 | `MODULE_NAME_ENUM_VALUE` 全大写+下划线+模块前缀 | `SYS_STATE_IDLE`, `UI_PAGE_FAULT` | `State_Idle`, `pgFault` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写+下划线+模块前缀 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |
| 头文件保护 | `MODULE_NAME_H` 无前导下划线 | `SYS_STATE_H` | `_SYS_STATE_H` |
| 文件名 | PascalCase+下划线, `.c`/`.h` 配对 | `Sys_State.h` / `Sys_State.c` | `sysstate.h` |

### 注释 (V14 强化)

```c
/**
 ******************************************************************************
 * @file    User/Sys_State.h
 * @brief   系统全局状态机 — 公开接口       ← 一句话
 * @note    V14: 定义系统级运行模式...       ← 关键设计约束/踩坑记录
 ******************************************************************************
 */

/** @brief  公开函数: 一行说明功能 */
/** @param  param_name  参数说明 */
/** @retval 返回值说明 */

/* ── 行内注释: 只写 WHY, 不写 HOW ── */    /* 代码本身说明 HOW */
```

**注释铁律**:
- 公开函数必须带 `@brief` + `@param`/`@retval` (如有参数/返回值)
- 模块 `.h` 顶部必须带 `@file` + `@brief` + `@note`
- 禁止 `//` 双斜杠注释 (ARMCC V5 兼容, 项目规范强制), 统一用 `/** */` 或 `/* */`
- 不写 HOW (代码本身说明), 只写 WHY (为什么这样做, 踩过什么坑)
- 修改任何函数逻辑时, 同步更新注释

### 状态机
- 禁止隐式 bool/int 拼凑 → 必须用 `typedef enum` + 单一状态变量
- 状态转换集中在 Task 函数内, 用 `switch` 分发
- 示例: `App_Network_Conn_State` 替代 `s_online + s_connecting` 双 bool

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

### 编译期检查
```c
typedef char assertion[(condition) ? 1 : -1];
```

### OOP 在 C 中的实践
- 相关变量封装到 struct 中, 避免多个分散的 static 变量
- 状态机用 struct 打包 (状态 + 定时器 + 上下文)
- 一个 `.c` 只管理自己定义的结构体, 外部通过函数接口访问

### 注释
- 公开函数: `@brief` 一行说明功能, `@param`/`@retval` 标注参数和返回值
- 模块 `.h` 顶部: `@brief` 一句话 + `@note` 关键设计约束
- 不写 HOW (代码本身说明), 只写 WHY (为什么这样做, 踩过什么坑)

### 文件大小
≤800行/文件, ≤50行/函数 (当前 `Ui_Controller.c` ~1650行, 因其包含全部9页面+环形仪表盘引擎)

## 跨子项目命名规范 (V14 强制)

本仓库含 3 个独立子项目, 各自遵循其语言生态的标准命名约定。

| 层级 | STM32 (C/SPL) | ESP8266 (Arduino C++) | ONENETapp (JavaScript) |
|:---|:---|:---|:---|
| 函数/方法 | `Module_Name_Verb_Noun()` | `Module_Task_Verb_Noun()` | `camelCase()` / `PascalCase` class |
| 静态函数 | `Module_Name_Verb_Noun()` (强制前缀) | 同左, 推荐前缀 | N/A (模块作用域) |
| 静态变量 | `s_module_description` | `s_module_description` | N/A (模块作用域) |
| 全局变量 | `g_description` | 禁止 (用 getter/setter) | 禁止 |
| 枚举/类名 | `MODULE_NAME_TYPE` | `Module_Name` PascalCase | `PascalCase` class |
| 枚举值 | `MODULE_NAME_VALUE` | `MODULE_NAME_VALUE` (全大写推荐) | `UPPER_SNAKE_CASE` const |
| 宏常量 | `MODULE_NAME_VALUE` | `#define MODULE_NAME_VALUE` | `UPPER_SNAKE_CASE` |
| 头文件/保护 | `MODULE_NAME_H` | `#pragma once` | N/A |
| 注释格式 | `/** @brief */` `/* ── */` | `/** @brief */` `/* */` | `/** JSDoc */` `// inline` |
| 禁止注释 | `//` 双斜杠 (ARMCC V5 兼容) | `//` 允许 (Arduino 标准) | `//` 允许 (JS 标准) |

**跨子项目零容忍**:
- 任何子项目的代码都必须遵循其所属语言的标准命名, 即使是用户指定的名称, 不符合规范也必须更正
- AI 生成代码时首次就按规范命名, 不允许使用 `temp`/`test`/`foo`/`xxx` 占位名
- 每次 `/init` 或提交前审查时, 优先检查命名合规, 发现违规自动修复

**ESP8266 特殊约束** (Arduino C++):
- 静态变量在 `.ino` 顶层声明, 加 `s_` 前缀标记模块内私有
- 禁止使用 C++ `new`/`delete` (ESP8266 堆仅 ~40KB)
- 避免 `String` 类动态分配, 优先用 `char[]` 栈缓冲区

**ONENETapp 特殊约束** (Vanilla JS, 无菌构建):
- 不使用 Node.js `require`/`import` (纯浏览器环境)
- localStorage key 统一 `iot_` 前缀
- 网络请求统一加 3s 超时 `AbortController`

### 全桥 PWM 基线 (重构不改, 关系到全桥是否输出波形)
- `TIM_CounterMode_Up` — 不可改为 CenterAligned (频率公式不同, 两路 CH1=PWM1+CH2=PWM2 配合 Up 计数实现对角线交替导通)
- CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` — 两路不同模式, 桥间产生差分电压; 同模式则桥间电压为零
- `TIM_OCNPolarity_Low` — IR2103S LIN 为低有效, 不可改为 High
- `TIM_OCNIdleState_Reset` — MOE 关断时上下管全关
- 死区 1000ns, 由 `PWM_DRIVER_DEADTIME_NS` 宏统一定义
- 频率范围 95kHz~150kHz (`PWM_DRIVER_FREQ_MIN_HZ`/`MAX_HZ`), 软启动从 150k 扫到 100k
- 以上参数源自 V0.0 已验证硬件, 重构时逐行对照, 不准擅自改动

### 可维护性
- 魔法数字命名常量, 不准裸值散落代码中
- 显示字符串集中为 `#define` 宏, 方便多语言替换
- 频率/电压/电流限制单一定义, 全项目引用同一处
- 不保留废弃代码和旧文件, 删干净避免维护陷阱
- 生成的文件放到指定目录, 不准散落在桌面或其他无关位置; 不确定存放路径时先询问

## Safety

- **故障处理**: Fault → 关 PWM (MOE+计数器) → 锁存 FAULT 状态 → 停止扫频任务 → 蜂鸣器BEEP
- **FAULT 恢复**: 单击 ON/OFF → `Soft_Start_Reset()` → 回到 MAIN_MENU
- **PB10**: 高使能/低关断 12V。仅受电压阈值(>12V)控制, 与PWM独立
- **过流保护**: 每 200ms 用 EMA 平滑值检查 > 5.0A → Fault + Buzzer BEEP
- **上电安全**: TIM1 全关(CEN+MOE), PB10 拉低关12V, 开机自动联网
- **看门狗**: IWDG, LSI 40kHz/64, reload=1000 → 1.6s, `DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP` 调试时暂停
- **HardFault**: 所有故障 ISR 先关 PWM 再死循环
- **低功耗**: 主循环末尾 `__WFI()` 休眠, SysTick 唤醒, 空闲电流 ~5mA
- **Library Doctrine**: SPL V3.5.0 ONLY. No HAL/LL functions. 内部函数加模块前缀避免命名冲突
- **显示平滑 (EMA)**: V/I/F 显示使用指数移动平均 (α=0.25, τ≈800ms)。`Ui_Controller.c` 中 `Update_EMA()` 实现, 状态转移时 `Reset_EMA()` 重置消除收敛滞后

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

**要点**: 硬件整数分频 → 并非所有 kHz 值可达 → 频率斜坡使用容差收敛 (`|diff| ≤ 1000Hz`)。

## ADC 互质相位采样 (Anti-Aliasing)

```
DWT 采样周期 = 144241 CPU 周期
PWM 周期 = 720 CPU 周期 (72MHz / 100kHz)

互质性: gcd(144241, 720) → ... → gcd = 1 ✓

采样在 720 个不同 PWM 相位均匀分布
→ 64 样本滑动窗口 (128ms) 收敛至 DC 分量
→ 有效抑制 100kHz 开关纹波
```

**编译期保护**: `typedef char Adc_Driver_Assert_HSE_72MHz[(SystemCoreClock == 72000000) ? 1 : -1]`

## 中断服务

### NVIC 配置
- **优先级分组**: `NVIC_PriorityGroup_2` (2 位抢占 + 2 位子优先级)
- **USART2**: 抢占 1, 子 0

### SysTick_Handler (每 1ms)
```c
void SysTick_Handler(void) {
    Sys_Timer_Inc_Tick();  // 仅递增 s_sys_tick, 无任何业务逻辑
}
```

### USART2_IRQHandler
```
USART2_IRQHandler → ORE溢出检查 (读DR清标志, 防ISR死锁)
                  → RXNE → Esp8266_Driver_Rx_Char(ch)
                  → 拼接行缓冲, \r/\n 触发帧标志
```

### HardFault 保护
所有故障 Handler (`HardFault/MemManage/BusFault/UsageFault`) 先关 PWM 再死循环:
```c
void HardFault_Handler(void) {
    TIM_CtrlPWMOutputs(TIM1, DISABLE);  // ← 先关桥臂!
    while(1);  // 等 IWDG 复位
}
```

## V9→V10 主要变更

| 领域 | 变更 |
|:---|:---|
| UI 架构 | V9 6态7画面 → V10 两级菜单 9页面 (MAIN_MENU + MONITOR_SUB_MENU) |
| 菜单渲染 | 纯色反白高亮 + ICON_STAR 菱形图标 (选中行col=0, 文字col=2对齐) |
| 子菜单 | 5项4行滚动窗口, 光标循环, 增量重绘免闪烁 |
| 频率步进 | F+/F- 改用 `s_user_target_hz` 逻辑目标, 消除整数分频累积偏差 |
| 斜坡控制 | 新增 `Freq_Ramp_Cancel()`, 手动步进时取消远程斜坡 |
| WiFi页面 | buf扩容 21→42字节, 修复"连接成功"显示截断 |
| 字库 | 78汉字 (曾补"连接成功""失败""清除WIFI"等) |
| 图标系统 | `TFT_Img.h`: WIFI_ICON[4], WIFI_CONNECT_ANIM[6], WIFI_OFF/REMOVE, MQTT 3态+6帧, ICON_STAR |
| 自动联网 | 开机自动 `App_Network_Start_Connect()`, 不再默认无WIFI |
| 绘制策略 | 页面切换全清, 光标移动增量, 200ms节流, Header每帧刷新动画 |

## V10→V11 主要变更

| 领域 | 变更 |
|:---|:---|
| UI 架构 | 新增环形仪表盘 (MONITOR_FREQ/VOLT/CURR, 共3页) |
| 仪表盘引擎 | sin 查表极坐标 + Bresenham 3线锥形指针 + 金属Hub + 5×10微数字 |
| 弧线 | 已移除 (V13 无连线弧, 纯悬空刻度) |
| 刻度 | 三层(大/中/细, 10px/6px/3px), 1px Bres_Line 不加粗, 红区刻度红色, 白区刻度白色/灰色 |
| 增量刷新 | 入场全绘 + 200ms增量(Z轴图层:擦针→刷新字→绘针→Hub) |
| 底行值 | V13: 8×16 左对齐 Hub 下方 y=112 (行7) |
| Badge位置 | V13: 左上角 col=0 (左对齐) |
| WIFI位置 | WIFI@128, MQTT@144 (V11 最终确定, 之后不变) |

## V12→V13 主要变更

| 领域 | 变更 |
|:---|:---|
| 表盘视觉 | 移除连线弧(白弧+红弧), 悬空刻度线, 大幅减少 SPI Fill_Rect(~540次→0) |
| 刻度参数 | R_TICK=68→64, R_BIG=50→54, R_FINE=62→61, 更紧凑 |
| 加粗移除 | 刻度线仅1px Bres_Line, 删除 lw>1 偏移加粗逻辑 |
| 数字对齐 | 废除 R_LBL 固定居中 → Dynamic Anchor Alignment (sin/cos 自适应) |
| 指针重构 | 5线剑形→3线锥形 (中心全长+侧线2/3长渐尖), 尾部3线→1线 |
| 图标闪烁修复 | Draw_TopRight_Icons 删除全局擦除, 底层 Draw_Single_Icon 原生含背景覆盖 |
| 火箭移除 | 删除 Draw_TopRight_Icons 中 Rocket block + Phase 0 rocket_frame 追踪 |
| 底行值左对齐 | y=112 从居中 Center() → col=0 左对齐 |
| Badge 左移 | x=96→x=0 col=0, 左上角左对齐 |
| 量程整十对齐 | V:0→50(10/5/1), C:0→5(1/0.5/0.1), F:90→150(10/5/1) |
| PWM 停止灰字 | Freq 页 PWM 未运行时显示 "F ---.-" 灰字, 指针不画 |
| 擦除函数迁移 | Erase_Pixel_Area → Tft_Driver_Erase_Pixel_Area, 全文件替换 Fill_Rect(BG)→此函数 |
| s_gauge_val_str | 新增值缓存变量, 增量刷新时精确 diff 避免无效重绘 |

## ESP8266 Firmware (参考 3.0 基版)

单文件 `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`, 注释分段架构:

| 段 | 命名空间 | 职责 |
|:---|:---|:---|
| 配置区 | `#define` | 所有可调参数 |
| 连接状态机 | `MQTT_CONN_STATE_*` 枚举 | IDLE→WIFI→MQTT→ONLINE→FAILED 显式状态 |
| MQTT 模块 | `Mqtt_Task_*` | 双 Broker 连接 + OneNET 物模型收发 |
| 串口模块 | `Serial_Parse_*` | 非阻塞行读取 + 前缀匹配防协议误触发 |

关键改进: `Str_Starts_With()` 前缀匹配替代 `strstr()` 子串搜索, 防止 `STATUS:ONLINE` 嵌入 JSON 字符串时误触发。

## Docs Directory (参考)

| Document | Purpose |
|:---|:---|
| `CLAUDE.md` | AI 辅助开发规范 (架构/编码/安全/画面布局) |
| `README.md` | GitHub 项目主页 (特性/架构/快速开始) |
| `Claude_Files/docs/` | 可选: 详细开发指南 MD+DOCX 配对 |

## 多仓库结构 (参考)

| 本地文件夹 | 远程仓库 | 分支 | 说明 |
|:---|:---|:---|:---|
| `Keil_Project/` 等 | `Ran-sh/WPT_PWM` | `4.0TFT` | 主仓库 |
| `ONENETapp/` | `Ran-sh/WPT_Onenet_IoT` | `master` | 网页控制台 (Cloudflare Pages) |
| `Railway_Deploy/` | `Ran-sh/WPT_Railway` | `main` | Railway 桥接服务器 (历史) |

## Git

```bash
git add -A && git commit -m "..." && git push origin 4.0TFT
```

## 环形仪表盘速查

```c
/* 核心渲染函数 (Ui_Controller.c) */
Tft_Driver_Fill_Rect(x, y, w, h, color);            // DMA 单色填充 (已开 SetWin)
Tft_Driver_Erase_Pixel_Area(x, y, w, h);             // 黑底擦除 (等价 Fill_Rect BLACK)
Tft_Driver_Show_5x10_String_Pixel(x, y, s, fg, bg);  // 5×10 微数字标注
Bres_Line(x0, y0, x1, y1, color);                    // 1px Bresenham 线
Draw_Pointer(a, color);                               // 3线锥形指针 (中心全长+侧2/3, PTR_LEN=46)
Draw_Hub();                                           // 逐行填充金属 Hub (~80 SPI)
Gauge_Polar(angle_deg, radius, &x, &y);              // sin 查表极坐标
Draw_Gauge_Full(cfg, val);                            // 入场全绘 (刻度+数字+值+指针+Hub+Badge+WIFI/MQTT)
Gauge_Dynamic_Update(cfg, val, old_val);              // 200ms 增量 (Z轴图层:擦针→刷新字→绘针→Hub)
Draw_TopRight_Icons();                                // V13: WIFI@128 + MQTT@144 两区 (无火箭)

/* 三表配置 (V13 整十对齐) */
GAUGE_V = {0,50,10,5,1,42,'V'};       GAUGE_C = {0,5,1,0.5,0.1,4.0,'C'};
GAUGE_F = {90,150,10,5,1,140,'F'};    // {min,max,big,mid,fine,red,label}

/* V13 布局 (解耦) */
// 左上 Badge@col=0 (左对齐): SWP/DON/IDL, OK/WRN/HI
// 右上 TopRight: WIFI@x=128 | MQTT@x=144 (无全局擦除)
// 底行值: y=112 col=0 左对齐, PWM 停止→灰字 "F ---.-"
// 圆心(80,100), R_TICK=64, R_BIG/MID/FINE=54/58/61, PTR_LEN=46
// Dynamic Anchor Alignment: 数字锚点 R_TICK+6, sin/cos 自适应偏移
```

## TFT 注意事项

- **Fill_Rect(1,1) 陷阱**: 每像素一次 SetWin + SPI 模式切换 + DMA 等待, 高频调用引发 SPI 片选饱和导致横竖线条纹。Hub/指针应逐行 Fill_Rect 一次性填充, 非逐像素。
- **指针长度严格小于刻度内径**: PTR_LEN=46 < R_BIG=50, 黑线擦除不伤刻度, 实现图层物理隔离。
- **DMA 传输后必须等待 BSY**: `while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY))` 确保最后一帧发完再拉高 CS。
- **SPI 8→16→8 切换**: 每次 DMA 传输前后必须停 SPI, 切 DFF, 重开 SPI。
- **sin 表声明不用显式长度**: `static const int16_t GAUGE_SIN[]` 让编译器自动计数, 避免 #146 too many initializer values。
- **WIFI(x=128) + MQTT(x=144) 位置不变**: 所有页面共用, Phase 0 帧差检测调度, 无全局擦除。
- **PTR_LEN=46 < R_BIG=54**: 指针尖距主刻度内端 8px 安全间隙, 黑线擦除不伤刻度。
- **PCtoLCD2002 取模配置**: 字符模式, 行主序, LSB-first, 宽度对齐到字节。5×10 每字 10 字节。
