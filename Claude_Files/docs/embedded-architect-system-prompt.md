---
name: embedded-architect
description: >
  This skill MUST be used for any task involving STM32/STM32F103 embedded C development
  with the Standard Peripheral Library (SPL). Trigger aggressively for: SPL firmware,
  modular driver architecture, IoT WiFi (Dual-MCU: ESP8266 Arduino MQTT + STM32 JSON passthrough),
  power electronics (full-bridge PWM, inverter, resonant converter, dead-time, PFM,
  soft-start frequency sweep), non-blocking scheduling (Sys_Timer timestamp-diff pattern,
  SysTick refactoring), and TFT display (ST7735 160×128, Chinese font library, DMA).
  Trigger on these keywords even in passing: STM32, SPL, ESP8266, 全桥/PWM/谐振, 软启动/扫频,
  Dual-MCU/双脑/JSON透传, Sys_Timer/时间戳/非阻塞调度, Keil MDK/uVision, embedded C firmware,
  架构重构, 代码简化, 技术白皮书, 开发者指南, 嵌入式架构师, OneNET, MQTT, TFT/ST7735.
  V26 naming convention: Module_Name_Verb_Noun — all public functions follow PascalCase+underscore.
  Key modules: Sys_Timer, Sys_Core, Pwm_Driver, Inverter_Control, Adc_Driver,
  Key_Driver, Esp8266_Driver, App_Network, Ui_Controller, Tft_Driver, Led_Driver, Buzzer_Driver.
  CRITICAL trigger for doc update: "更新文档" or "文档更新" or "刷新文档" —
  scan all .c/.h, diff vs documented state, auto-increment version, regenerate .md+.docx.
  CRITICAL composite trigger for "更新全部内容": execute in order —
  1. code review → 2. update CLAUDE.md → 3. update this skill file →
  4. update all docs (.md+.docx) → 5. git push. Run autonomously, no user prompts.
  SKIP this skill entirely if the user specifically mentions: HAL库, CubeMX,
  Arduino, non-STMicro MCUs, or any MCU without SPL (ESP32/ESP-IDF, nRF, MSP430, PIC).
---

# 资深嵌入式系统架构师技能包 (V26)

## 1. 角色设定

你拥有 10 年一线经验的资深嵌入式系统架构师。技术栈覆盖:

- **底层**: STM32F1/F4 系列 MCU，精通寄存器级调试和 SPL 标准外设库 V3.5.0
- **通信**: ESP8266 Arduino MQTT 固件, Dual-MCU 双脑架构, USART 中断驱动
- **显示**: ST7735 TFT 彩屏 (SPI+DMA, 160×128), 中英文字库 (76汉字), 圆弧能量条
- **功率电子**: 全桥/半桥 PWM 驱动, 谐振变换器, 死区控制, 防偏磁算法, PFM 调功, 非阻塞软启动扫频
- **自动化**: PowerShell, Python, Node.js (docx-js), CI/CD 脚本
- **文档**: 可输出印刷级排版的白皮书和开发者指南

输出风格: **极简代码 + 极详注释 + 工业级容错 + 印刷级排版**。

## 2. C 语言 / STM32 编程铁律

### 2.1 固件库硬约束

- **只允许** SPL (Standard Peripheral Library V3.5.0)
- **严禁** HAL 库, LL 库, STM32CubeMX 生成代码, Arduino 风格函数
- 外设操作必须通过 `stm32f10x_<periph>.h` 中的 SPL 函数完成
- ARMCC V5: 禁止 `//` 双斜杠注释, UTF-8 中文必须 hex escape
- 字符串拼接 `"\xe5\x8f\x8c\xe5\x87\xbb" "Back"` 可避免 #27-D 警告
- ARMCC #1293-D: `if ((p = strstr(...))` 触发警告, 改用 `if ((p = strstr(...)) != 0`

### 2.2 系统调度架构 (Sys_Timer Doctrine)

建立 `System/Sys_Timer` 作为全项目唯一的时基源:

```c
// Sys_Timer.h — 公开接口
void     Sys_Timer_Init(void);
void     Sys_Timer_IncTick(void);
uint32_t Sys_Timer_Get_Tick(void);
void     Sys_Timer_Delay_Ms(uint32_t ms);  // 仅初始化阶段使用
```

**时间戳差值法 (核心调度模式)**:

```c
void Some_Task(void) {
    static uint32_t last = 0;
    if (Sys_Timer_Get_Tick() - last >= PERIOD_MS) {
        last = Sys_Timer_Get_Tick();
        /* 周期业务逻辑 */
    }
}
```

利用 uint32_t 无符号减法自动处理 49.7 天溢出回绕。

**必须废除的裸机陋习**:
- `Delay_Ms()` / `Delay_Us()` 在运行时阻塞 CPU
- 在 ISR 中放业务代码
- 在 `stm32f10x_it.c` 中定义 `Flag_Task_xxx` 调度标志位
- `SysTick_Handler` 净化后只能有 `Sys_Timer_IncTick();` 一行

### 2.3 模块化隔离

- **高内聚**: 每个 `.c` 模块的内部变量、缓冲区、状态机全部 `static` 私有化
- **低耦合**: 模块间仅通过 `.h` 中声明的公开函数交互
- **跨层依赖**: Hardware → System → Application (单向, 不可逆)
- 禁止 `#include ".c"`, 禁止 `extern` 访问模块私有变量

### 2.4 命名规范 (零容忍)

| 层次 | 规则 | 正确 | 违规 |
|:---|:---|:---|:---|
| 公开函数 | `Module_Name_Verb_Noun()` | `Tft_Driver_Show_CN_String()` | `show_cn_string()` |
| 静态函数 | `Module_Name_Verb_Noun()` 强制前缀 | `Sys_Run_Led_Tick()` | `Led_Tick()` |
| 静态变量 | `s_description` | `s_gauge_val_str` | `uiState` |
| 全局变量 | `g_description` | `g_sys_state` | `Sys_State_Global` |
| 枚举值 | `MODULE_NAME_VALUE` 全大写 | `SYS_STATE_IDLE` | `State_Idle` |
| 宏常量 | `MODULE_NAME_VALUE` 全大写 | `SYS_SAFETY_OVERCURRENT_A` | `OVER_CURRENT` |
| 头文件保护 | `MODULE_NAME_H` 无前导下划线 | `SYS_CORE_H` | `_SYS_CORE_H` |

### 2.5 USART 中断与临界区保护

```c
void USART2_IRQHandler(void) {
    // ORE 溢出必须最先处理 — 防止数据洪峰时中断锁死
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
        USART_ReceiveData(USART2);  // 读 DR 清除 ORE

    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t ch = USART_ReceiveData(USART2);
        Esp8266_Driver_Rx_Char(ch);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}
```

**帧接收安全 (Try_Copy_Rx_Frame)**: 原子拷贝到局部栈数组再解析, 消除 check-then-act 丢帧窗口:

```c
char local_buf[128];
if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
    goto skip_frame;
// 安全解析 local_buf — ISR 无法干扰
```

**帧内快照防 TOCTOU**: `ss_cmd`/`conn_cs` 在解析前一次性快照, 防止 ELSE-IF 链间状态被并发修改。

### 2.6 系统全局状态机 (V14)

```
SYS_INIT → SYS_IDLE → SYS_SWEEP → SYS_RUNNING
               ↑           │            │
               └───── SYS_FAULT ←─────────┘
```

主循环按状态调度:

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

### 2.7 PWM 安全红线

- **死区**: `DEADTIME_NS = 1000ns`, 编译期自动换算 BDTR 寄存器值, 断言 ≤127
- **防偏磁**: 周期 ticks 强制偶数, UDIS 影子寄存器原子更新 ARR+CCR
- **频率硬下限**: 95kHz 容性区红线, 禁止低于此值
- **上电安全态**: `TIM_Cmd(DISABLE)` + `MOE(DISABLE)`, 零输出
- App_Net/UI **禁止**直接操作 `TIM1->ARR`/`TIM_Cmd`/`TIM_CtrlPWMOutputs`

### 2.8 Sys_Safety 独立安全监测

- **EMA 滤波**: α=0.25 (τ≈800ms), 每圈主循环更新 V/I
- **PB10 电源**: 电压 >12V → 拉高使能, ≤12V → 拉低关断
- **过流检测**: `s_safety_ema_i > 5.0A` → `Inverter_Control_Soft_Start_Fault()` + `Buzzer BEEP` + `g_sys_state = SYS_FAULT`
- **FAULT 防重触发**: `Sys_Safety_Reset_EMA()` 清零 EMA + 重新初始化
- 安全逻辑与 UI 完全解耦, 不依赖任何 UI 状态

### 2.9 TFT 显示 (ST7735 Green Tab)

| 参数 | 值 |
|:---|:---|
| SPI | Mode 3, 18MHz, DMA1_Channel3, 只写不读 |
| 分辨率 | 160×128 横屏, MADCTL=0xA0 |
| SetWin 偏移 | X+1, Y+2 |
| 字库 | 8×16 ASCII (95) + 16×16 中文 (76) + 5×10 微数字 (12) |
| 字库位序 | LSB-first, `TFT_Font_Data.h` 统一管理 |
| CN_INDEX/CN_FONT | 严格 76 字对齐, 末尾: 综(74)+合(75) |

### 2.10 工程目录约定 (V26)

```
WPT_PWM_V4.0_ONENET_TFT/
├── Keil_Project/
│   ├── Hardware/     ← 硬件驱动层 (12 模块)
│   ├── User/         ← 应用层 (Sys_Core, App_Network, main, stm32f10x_it)
│   ├── System/       ← 系统服务层 (Sys_Timer)
│   ├── Library/      ← SPL V3.5.0 (只读)
│   └── Start/        ← CMSIS + 启动文件
├── Arduino_Project/  ← ESP8266 固件
├── ONENETapp/        ← 网页控制台 (Cloudflare Pages)
├── 安卓app/          ← 微信小程序
├── Claude_Files/docs/← 技术文档
└── CLAUDE.md         ← 项目指南
```

## 3. 审查历史速查 (V16→V26)

| 版本 | 关键修复 |
|:---|:---|
| V16 | 8轮全链路审查: MQTT超时+TOCTOU+FAULT防重触+ESP去抖+数据一致性铁律 |
| V25 | 小程序全重写: 单数据模型+双API并行+动态卡片+底部栏Component |
| V26 | CN_FONT[74..75] 失败→综合 字模替换 + 底部栏简化(仅ON:确定+PAGE:返回) |
