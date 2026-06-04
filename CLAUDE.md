# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Branch Identity

| 项目 | 内容 |
|:---|:---|
| **仓库** | https://github.com/Ran-sh/WPT_PWM |
| **分支** | `3.0ONENET` |
| **本地目录** | `D:\Claude Code Project\WPT_PWM_ONENET_V3.0` |
| **协议** | OneNET MQTT 物模型 (Dual-MCU 架构) |
| **版本** | V6.1 |

其他分支: `master` (V0.0 基版) → `WPT_PWM_V0.0`, `1.0LAN` (NetAssist 局域网 V3.4) → `WPT_PWM_NetAssistant_LAN_V1.0`, `2.0WAN` (巴法云 TCP V3.5) → `WPT_PWM_Bemfa_WAN_V2.0`, `4.0TFT` (OneNET MQTT V6.2 TFT彩屏) → `WPT_PWM_V4.0_ONENET_TFT`

### 复合指令触发规则

**当用户说"更新全部内容"时，按顺序自动执行：**

1. `/simplify` — 三路代码审查 (复用/质量/效率)，修复发现的问题
2. `/init` — 重新生成 CLAUDE.md
3. 更新 `embedded-architect` skill (`Claude_Files/docs/embedded-architect-system-prompt.md` + `~/.claude/skills/embedded-architect/SKILL.md`)
4. 更新全部文档 (`.md` + `.docx` 配对生成)
5. 美化 GitHub README.md
6. `git push` 推送当前分支 (3.0ONENET)

**执行期间**: 全部权限自动通过，不中断等待用户确认。

## Naming Convention (V6.0)

全部模块统一采用 `Module_Name_Action_Object()` 帕斯卡+下划线命名:

- 公开函数: `Module_Name_Verb_Noun()` — 如 `Oled_Driver_Show_String()`, `Adc_Driver_Get_Voltage()`
- 静态变量: `s_module_description` — 如 `s_ui_state`, `s_rx_frame_flag`
- 类型/枚举: `Module_Name_Type` — 如 `Ui_Controller_State`, `Inverter_Control_Soft_Start_State`
- 枚举值: `MODULE_NAME_ENUM_VALUE` — 全大写+下划线+模块前缀, 如 `LED_DRIVER_STATE_ON`, `INVERTER_CONTROL_SS_STATE_DONE`, `UI_CONTROLLER_STATE_READY`
- 宏常量: `MODULE_NAME_VALUE` — 全大写+下划线+模块前缀, 如 `ADC_DRIVER_VREF_MCU`, `APP_NETWORK_MAX_RETRIES`
- 静态函数: 建议加模块前缀, 如 `Ui_Controller_Draw_Running()`
- 头文件保护: `MODULE_NAME_H` (无前导下划线, 避免 C 保留标识符)

### 编码规范 (Code Design Rules)

**状态机**
- 禁止用隐式 bool/int 标志组合表达状态 — 必须用显式 `typedef enum`
- 每个状态机有命名的枚举类型 + 单一状态变量, 不准用 `s_flag1`+`s_flag2` 拼凑
- 状态转换集中在一个 Task 函数内, 用 `switch` 分发
- 示例: `App_Network_Conn_State` 替代 `s_network_online`+`s_connecting` 双 bool

**调度**
- 所有周期任务用 `Sys_Timer_Get_Tick() - last >= PERIOD` 时间戳差值模式
- `Sys_Timer_Delay_Ms()` 仅限初始化阶段, 运行时绝对禁止阻塞延时
- 初始化序列也优先用非阻塞状态机 (参考 `Esp8266_Driver_Init_Task` 的 CH_PD 时序)
- 主循环末尾 `__WFI()` 休眠, SysTick 唤醒, 不空转

**模块架构**
- 每个模块 `.h` 只放公开接口, `.c` 放全部实现 + 静态变量
- `.c` 内部函数一律 `static`, 不准跨模块 `extern` 访问私有变量
- 不允许 `#include ".c"` 文件
- 模块内部辅助函数建议加模块前缀避免与 SPL 库函数冲突 (如 `Oled_I2C_Init` 而非 `I2C_Init`)
- 头文件保护: `MODULE_NAME_H`, 禁止双下划线前缀 (`__NAME_H` 是 C 保留字)

**OOP 在 C 中的实践**
- 相关变量封装到 struct 中, 避免多个分散的 static 变量
- 状态机用 struct 打包 (状态 + 定时器 + 上下文)
- 一个 `.c` 只管理自己定义的结构体, 外部通过函数接口访问

**分层依赖**
- Hardware → System → Application, 严格单向
- 应用层不直接操作寄存器, 不绕过硬件抽象层
- `Ui_Controller` 可通过 `Inverter_Control` 间接访问 PWM, 但不直接调 `Pwm_Driver`

**临界区**
- 统一 PRIMASK 保存/恢复模式, 禁止裸 `__disable_irq()`:
  ```c
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  /* critical section */
  __set_PRIMASK(primask);
  ```

**注释**
- 公开函数: `@brief` 一行说明功能, `@param`/`@retval` 标注参数和返回值
- 模块 `.h` 顶部: `@brief` 一句话 + `@note` 关键设计约束
- 不写 HOW (代码本身说明), 只写 WHY (为什么这样做, 踩过什么坑)

**安全**
- 故障处理器进入死循环前必须关断 PWM (`TIM_CtrlPWMOutputs(TIM1, DISABLE)`)
- IWDG 看门狗在 main() 初始化, 主循环喂狗
- 编译期可验证的约束用 `typedef char assertion[(condition)?1:-1]` 检查

**全桥 PWM 基线 (重构不改, 关系到全桥是否输出波形)**
- `GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE)` — PA7=CH1N, PB0=CH2N, 缺失则无输出
- `TIM_CounterMode_Up` — 不可改为 CenterAligned (频率公式不同, 两路 CH1=PWM1+CH2=PWM2 配合 Up 计数实现对角线交替导通)
- CH1=`TIM_OCMode_PWM1`, CH2=`TIM_OCMode_PWM2` — 两路不同模式, 桥间产生差分电压; 同模式则桥间电压为零
- `TIM_OCNPolarity_Low` — IR2103S LIN 为低有效, 不可改为 High
- `TIM_OCNIdleState_Set` — MOE 关断时下管必须关断 (LIN=HIGH), 不可改为 Reset
- 死区 1000ns, 由 `PWM_DRIVER_DEADTIME_NS` 宏统一定义
- 频率范围 95kHz~150kHz (`PWM_DRIVER_FREQ_MIN_HZ`/`MAX_HZ`), 软启动从 150k 扫到 100k
- 以上参数源自 V0.0 已验证硬件, 重构时逐行对照, 不准擅自改动

**可维护性**
- 魔法数字命名常量, 不准裸值散落代码中
- 显示字符串集中为 `#define STR_*` 宏, 方便多语言替换
- 频率/电压/电流限制单一定义, 全项目引用同一处
- 不保留废弃代码和旧文件, 删干净避免维护陷阱
- 生成的文件放到指定目录, 不准散落在桌面或其他无关位置; 不确定存放路径时先询问

## Architecture: Dual-MCU

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│         STM32 (物理脑)        │    │      ESP8266 (联网脑)         │
│  ─────────────────────────── │    │  ─────────────────────────── │
│  • PWM 发波 + PFM 调功        │    │  • WiFiManager 网页配网       │
│  • ADC 双通道采集 + 滤波       │    │  • OneNET MQTT 物模型连云     │
│  • KEY/OLED/LED 人机交互      │    │  • 串口 JSON ↔ STM32 透传    │
│  • 纯 JSON 串口透传           │    │  • Conn_State 连接状态机      │
│  • 软启动扫频 + 过流保护       │    │  • 前缀匹配防协议误触发       │
└──────────┬───────────────────┘    └──────────┬───────────────────┘
           │           USART2 115200           │
           │   纯文本 JSON (零 AT 指令)          │
           ├──────────────────────────────────►│
           │  {"V":12.50,"I":1.23,"F":100000}  │
           │◄──────────────────────────────────┤
           │  CMD:ON\n  或  CMD:OFF\n           │
           │  CMD:SETFREQ:100000\n              │
```

**Iron rule**: STM32 never sends AT commands. ESP8266 never touches PWM/ADC. Communication is pure text JSON over USART2 at 115200bps.

## 多仓库推送规则 (Multi-Repo Push Doctrine)

| 本地文件夹 | 远程仓库 | 分支 | 说明 |
|:---|:---|:---|:---|
| `Keil_Project/`、`Arduino_Project/`、`安卓app/`、`Claude_Files/`、根目录文件 | `Ran-sh/WPT_PWM` | `3.0ONENET` | 主仓库 |
| `ONENETapp/` | `Ran-sh/WPT_Onenet_IoT` | `master` | 网页控制台 (Cloudflare Pages) |
| `Railway_Deploy/` | `Ran-sh/WPT_Railway` | `main` | Railway 桥接服务器 |

```bash
# 主仓库
git add -A && git commit -m "..." && git push origin 3.0ONENET

# ONENETapp (需同时推到 gh-pages)
cd ONENETapp && git add -A && git commit -m "..." && git push && git push origin gh-pages:master && cd ..

# Railway
cd Railway_Deploy && git add -A && git commit -m "..." && git push && cd ..
```

## Build System

### STM32 (Keil MDK)
- **IDE**: Keil MDK-ARM V5 (uVision), ARMCC V5.06 update 5 (build 528)
- **Target MCU**: STM32F103C8 (Cortex-M3, 64KB Flash, 20KB SRAM)
- **Project File**: `Keil_Project/Project.uvprojx`
- **Output**: `Keil_Project/Objects/Project.hex` (HEX-80)
- **Library**: SPL V3.5.0 in `Keil_Project/Library/` — read-only, never modified
- No CLI build — compilation through Keil IDE GUI.

### ESP8266 (Arduino IDE)
- **Board**: Generic ESP8266 Module, Flash 1M, 80MHz CPU
- **File**: `Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino`
- **Libraries**: ESP8266WiFi, PubSubClient, ArduinoJson v7, WiFiManager

## STM32 Module Map (V6.0)

```
Keil_Project/
├── Hardware/
│   ├── Oled_Driver      ← SSD1315 I2C OLED, bit-banged (PA11/PA12)
│   ├── Led_Driver        ← 4 LED 驱动 (PC13心跳, PB3/PB4/PB5 状态灯)
│   ├── Pwm_Driver        ← TIM1 全桥 PWM 硬件抽象 (95-150kHz, 1000ns死区)
│   ├── Inverter_Control  ← 软启动状态机 + 频率斜坡 (应用层, 依赖 Pwm_Driver)
│   ├── Adc_Driver        ← ADC1+DMA1 双通道 + 64样本滤波 + 自动零点校准
│   ├── Key_Driver        ← 双按键 FSM (PB12/PB13), 10ms去抖, 单击/双击/长按
│   ├── Esp8266_Driver    ← USART2 115200 异步收发, CH_PD 非阻塞初始化状态机
│   └── Ui_Controller     ← 7界面状态机 + OLED绘制 + 按键分发 + LED联动
├── System/
│   └── Sys_Timer         ← SysTick 1ms + DWT 周期计数器 (亚毫秒定时)
└── User/
    ├── Main.c            ← main() 入口, 4阶段启动 + 非阻塞主循环
    ├── App_Network       ← 联网管理 + 遥测门控 + 指令接收
    └── stm32f10x_it.c    ← ISR (SysTick→Sys_Timer_Inc_Tick, USART2→Esp8266_Driver_Rx_Char)
```

**Layer dependency**: Hardware → System → Application. Never reverse.

## Key Design Rules

### Scheduling: Timestamp-Diff (Non-Blocking)
```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
        last = Sys_Timer_Get_Tick();
        // business logic
    }
}
```
`Sys_Timer_Delay_Ms()` is deprecated — use non-blocking state machines even for initialization sequences (see `Esp8266_Driver_Init_Task` for the CH_PD reset pattern). `System/Delay.c` is deleted, do not revive.

### IWDG Watchdog + Power Saving (V6.0)

IWDG: LSI 40kHz, prescaler 64, reload 1000 → ~1.6s timeout. `IWDG_ReloadCounter()` in main loop. Any task hang triggers auto-reset.
`__WFI()` at loop end: idle current ~30mA → ~5mA. SysTick ISR wakes CPU every 1ms.

### Display Smoothing (V6.0)

OLED V/I/F use EMA (α=0.25, τ≈800ms) separate from fast ADC filter used for telemetry/protection. Display jitter eliminated without affecting measurement accuracy.

### ADC Anti-Aliasing
采样周期 144241 CPU cycles 与 100kHz PWM (720 cycles) 互质 → 720 个不同相位均匀覆盖。64 样本滑动窗口 (128ms) 收敛至 DC 分量。自动零点校准: READY 状态首次采集 50 样本取平均, 后续 EMA 追踪。

### PWM Safety
- MOE off at boot, only enabled by `Inverter_Control_Soft_Start_Trigger()`
- Atomic ARR/CCR update via UDIS→write→UG→clear UDIS
- `Inverter_SetState()` saves/restores PRIMASK (never unconditionally enables IRQ)
- Fault handlers disable TIM1 outputs before infinite loop

### HardFault Protection (V6.0)
All fault handlers (`HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler`) call `TIM_CtrlPWMOutputs(TIM1, DISABLE)` before `while(1)` to prevent bridge shoot-through on CPU crash.

### Overcurrent Protection (V6.1)
`Ui_Controller_Task` 在 SWEEPING/RUNNING 状态每 200ms 检测电流 > `UI_CONTROLLER_OVERCURRENT_THRESHOLD_A` (5.0A), 触发 `Inverter_Control_Soft_Start_Fault()` → MOE 关断 + SS_FAULT 锁存。仅 KEY0/KEY1 可复位。

### V6.1 Key Fixes
| 级别 | 文件 | 修复内容 |
|:---|:---|:---|
| CRITICAL | `Inverter_Control.c` | 频率斜坡: `current==target` → `\|diff\|≤1000Hz` 容差收敛 |
| CRITICAL | `Pwm_Driver.c` | 恢复 V0.0 基线: Up 计数 + PartialRemap + PWM1/PWM2 + OCNPolarity_Low + OCNIdleState_Set |
| HIGH | `Esp8266_Driver.c` | Start_Init 清空 RX 缓冲 (防止 ESP 复位后残留帧) |
| HIGH | `Esp8266_Driver.c` `Key_Driver.c` | 裸 `__enable_irq()` → PRIMASK 保存/恢复 (3 处) |
| MEDIUM | `Adc_Driver.c` | 编译期断言 `SystemCoreClock == 72MHz` (DWT 互质依赖) |
| MEDIUM | `Oled_Driver.c/h` | `double` → `float` (Cortex-M3 无硬件 FPU) |
| MEDIUM | `Ui_Controller.c` | EMA 显示状态模块级 + `Reset_Display_EMA()` (消除重启收敛滞后) |
| LOW | `App_Network.c` | 遥测门控: 嵌套 `return` → 单 `if(allow_telemetry)` 模式 |

### Library Doctrine
- **SPL V3.5.0 ONLY**. No HAL/LL functions.
- Internal functions prefixed with module name to avoid SPL name clashes (e.g., `Oled_I2C_Init` not `I2C_Init`).

## Pin Mapping (STM32F103C8 LQFP-48)

| Pin | Function | Connected To |
|:---|:---|:---|
| PA0 | ADC_CH0 | CC6920-10A current sensor |
| PA1 | ADC_CH1 | Voltage divider (20:1) |
| PA2 | USART2_TX | ESP8266 RXD |
| PA3 | USART2_RX | ESP8266 TXD |
| PA7 | TIM1_CH1N | Half-bridge left low-side |
| PA8 | TIM1_CH1 | Half-bridge left high-side |
| PA9 | TIM1_CH2 | Half-bridge right high-side |
| PA11 | GPIO (OD) | OLED SCL |
| PA12 | GPIO (OD) | OLED SDA |
| PB0 | TIM1_CH2N | Half-bridge right low-side |
| PB1 | GPIO (PP) | ESP8266 CH_PD/EN |
| PB3 | GPIO (PP) | WiFi LED (JTAG disabled) |
| PB4 | GPIO (PP) | PWM LED (JTAG disabled) |
| PB5 | GPIO (PP) | Ready LED |
| PB12 | GPIO (IPU) | KEY0 |
| PB13 | GPIO (IPU) | KEY1 |
| PC13 | GPIO (PP) | Heartbeat LED (active-low) |

ESP8266 requires independent 3.3V LDO ≥500mA with 100μF+0.1μF decoupling. RST pin: 10kΩ pull-up to 3.3V. GPIO0: pull low during firmware upload.

## Startup Flow (V6.1)

```
上电 → Pwm_Driver_Init(MOE=OFF) → Oled_Driver_Init → Led_Driver_Init
     → Adc_Driver_Init → Key_Driver_Init
     → Sys_Timer_Init (SysTick + DWT) → IWDG_Init (1.6s 超时)
     → DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP (调试安全)
     → App_Network_Start_Connect (ESP 非阻塞初始化, 自动清空RX缓冲)
     → while(1):
         Key_Driver_Task | Adc_Driver_Filter_Task | Ui_Controller_Task
         | App_Network_Task | Inverter_Control_Soft_Start_Task
         | Inverter_Control_Freq_Ramp_Task | Led_Driver_Task
         | IWDG_ReloadCounter | __WFI
```

## ESP8266 Firmware (V5.1)

单文件 `ESP8266_MQTT_Firmware.ino`, 注释分段架构:

| 段 | 命名空间 | 职责 |
|:---|:---|:---|
| 配置区 | `#define` | 所有可调参数 |
| 连接状态机 | `MQTT_CONN_STATE_*` 枚举 | IDLE→WIFI→MQTT→ONLINE→FAILED 显式状态 |
| MQTT 模块 | `Mqtt_Task_*` | 双 Broker 连接 + OneNET 物模型收发 |
| 串口模块 | `Serial_Parse_*` | 非阻塞行读取 + 前缀匹配防协议误触发 |

关键改进: `Str_Starts_With()` 前缀匹配替代 `strstr()` 子串搜索, 防止 `STATUS:ONLINE` 嵌入 JSON 字符串时误触发。

## Documentation Output

- 每个 `.md` 文档在 `Claude_Files/docs/` 有配对的 `.docx`
- `.docx` 生成: `cd Claude_Files && npm install && node Claude_Files/tools/generate_docx.js "Claude_Files/docs/<name>.md"`
- 代码变更后版本号自增 (逻辑改动 +0.1, 新模块 +1.0, 纯格式日期刷新)
- "更新文档"时先 diff 再决定是否重写; 无变更则输出 "没有任何文件变化，无需更新"

### Docs Directory

| Document | Purpose |
|:---|:---|
| `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md` | 完整全指南 |
| `Claude_Files/docs/embedded-architect-system-prompt.md` | Skill 定义 (同步至 `~/.claude/skills/embedded-architect/SKILL.md`) |
