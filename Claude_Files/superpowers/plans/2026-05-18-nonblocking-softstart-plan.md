# 非阻塞软启动扫频 + 主程序重构 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将阻塞式 Inverter_SoftStart 重构为非阻塞状态机，集成到主循环，支持 KEY0/PC指令触发，OLED 实时显示扫频进度。

**Architecture:** PWM 模块内新增 3 态状态机 (IDLE/SWEEP/DONE)，通过时间戳差值法 2ms/步 扫频 150kHz→100kHz，共 250 步 ≈ 500ms。main.c 主循环调用 Task 函数推进状态。UI 模块和 App_Net 模块通过 Trigger/Stop 接口控制。

**Tech Stack:** STM32F103 SPL V3.5.0, ARMCC V5.06, Keil MDK-ARM V5

---

## 文件结构

| 文件 | 操作 | 职责 |
|:---|:---|:---|
| `Hardware/PWM.h` | 修改 | 新增状态枚举 + 5 个公开函数声明，移除旧 Inverter_SoftStart |
| `Hardware/PWM.c` | 修改 | 状态机实现，PWM_Init 改 MOE OFF + 150kHz 默认，频率下限 95kHz |
| `User/main.c` | 修改 | 清理裸 TIM1 操作，加入 Inverter_SoftStart_Task |
| `Hardware/UI.c` | 修改 | 按键映射 Trigger/Stop，扫频进度 OLED 显示，切页清屏 |
| `User/App_Net.c` | 修改 | ON→Trigger，OFF→Stop，配合状态检查 |

---

### Task 1: PWM.h — 新增非阻塞软启动 API

**Files:**
- Modify: `Hardware/PWM.h`

- [ ] **Step 1: 替换文件内容**

```c
/**
 ******************************************************************************
 * @file    Hardware/PWM.h
 * @brief   全桥 PWM 驱动 —— 公开接口
 * @note    存放路径: 项目根目录\Hardware\
 *          硬件接口: TIM1 高级定时器 (部分重映射)
 *            CH1 : PA8 (上管左)   CH1N: PA7 (下管左)
 *            CH2 : PA9 (上管右)   CH2N: PB0 (下管右)
 *          特性: 互补输出 + 死区插入 (DEADTIME_NS 宏可调) + 50% 固定占空比
 *          控制方式: PFM (脉冲频率调制), 通过改变频率调节谐振功率
 *          安全红线: 频率 >= 95kHz 绝对硬限幅, 低于此值钳位拒绝
 *          驱动芯片: IR2103S (3.3V 逻辑兼容)
 ******************************************************************************
 */

#ifndef __PWM_H
#define __PWM_H

#include "stm32f10x.h"

/* ── 死区时间宏定义 (ns), 修改此值即可调整硬件死区 ── */
#define DEADTIME_NS  1000

/* ── 软启动扫频参数 (与 PWM.c 内部实现一致, 供 UI 等外部模块使用) ── */
#define SOFTSTART_START_FREQ_HZ   150000UL
#define SOFTSTART_TARGET_FREQ_HZ  100000UL

/* ── 软启动状态 ── */
typedef enum {
    SS_IDLE  = 0,   /* 待机: MOE 关, 等待触发 */
    SS_SWEEP = 1,   /* 扫频中: 150kHz → 100kHz */
    SS_DONE  = 2    /* 完成: 100kHz 谐振稳态运行 */
} SoftStart_State_t;

void     PWM_Init(void);
uint32_t PWM_SetFrequency(uint32_t freq_Hz);
uint32_t PWM_GetFrequency(void);
void     PWM_Enable(void);
void     PWM_Disable(void);

/*
 * 非阻塞软启动扫频 (150kHz → 100kHz, 200Hz/步, 2ms/步, 共 250 步 ≈ 500ms)
 *   Trigger: KEY0 或 PC "ON" 触发, 仅 SS_IDLE 时有效
 *   Task:    主循环每轮调用, 内部 2ms 时间戳节拍
 *   Stop:    KEY1 或 PC "OFF" 关断, 回复 SS_IDLE
 *   每次 Trigger 必定从 150kHz 重新开始, 不复用上次状态
 */
void               Inverter_SoftStart_Trigger(void);
void               Inverter_SoftStart_Task(void);
void               Inverter_SoftStart_Stop(void);
SoftStart_State_t  Inverter_SoftStart_GetState(void);
uint32_t           Inverter_SoftStart_GetCurrentFreq(void);

#endif
```

- [ ] **Step 2: 提交**

```bash
git add Hardware/PWM.h
git commit -m "feat: add non-blocking soft-start API to PWM.h"
```

---

### Task 2: PWM.c — 宏定义 + 状态变量 + PWM_Init 改造

**Files:**
- Modify: `Hardware/PWM.c`

- [ ] **Step 1: 重写文件头、include、宏定义区域 (替换 PWM.c 第 1 行至第 84 行)**

```c
/**
 ******************************************************************************
 * @file    Hardware/PWM.c
 * @brief   全桥 PWM 驱动 (TIM1 四通道互补输出 + 死区 + 非阻塞软启动)
 * @note    存放路径: 项目根目录\Hardware\
 *
 * 硬件接口:
 * TIM1 高级定时器 (GPIO_PartialRemap_TIM1)
 *   CH1  : PA8 → 上半桥左臂上管
 *   CH1N : PA7 → 下半桥左臂下管
 *   CH2  : PA9 → 上半桥右臂上管
 *   CH2N : PB0 → 下半桥右臂下管
 *
 * 驱动策略:
 *   CH1 = PWM1, CH2 = PWM2 → 对角线交替导通
 *   占空比恒 50% (防偏磁), PFM 调功
 *   死区由 DEADTIME_NS 宏控制, 寄存器值编译期自动换算
 *
 * 安全红线:
 *   频率硬下限 95kHz (PWM_SetFrequency 钳位)
 *   软启动 150kHz 感性区 → 100kHz 谐振点, 非阻塞状态机
 *
 * 栅极驱动: IR2103S, 内部 ~100ns 防直通
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  可调参数
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * 以下宏 DEADTIME_NS、SOFTSTART_START_FREQ_HZ、SOFTSTART_TARGET_FREQ_HZ
 * 已定义于 Hardware/PWM.h, 此处通过 include 获取, 不重复定义。
 */

#define SOFTSTART_STEP_HZ         200UL      /* 每次降频步长 (Hz)         */
#define SOFTSTART_STEP_DELAY_MS   2U         /* 每步延时 (ms)             */
/* 总步数: (150k-100k)/200 = 250 步, 总耗时 250×2ms = 500ms */

#define PWM_FREQ_HARD_MIN_HZ      95000UL    /* 频率安全硬下限            */
#define PWM_FREQ_HARD_MAX_HZ      150000UL   /* 频率安全硬上限            */

#define TIM1_CLK_HZ               72000000UL /* APB2 定时器时钟            */

/*
 * ═══════════════════════════════════════════════════════════════════════
 * DeadTime 寄存器值换算 (编译期常量, 不占运行时开销)
 * ═══════════════════════════════════════════════════════════════════════
 *
 *   TDTS = 1/(72MHz) × CKD_DIV1 ≈ 13.889 ns
 *   TIM_DeadTime = round( DEADTIME_NS / TDTS )
 *                = ( DEADTIME_NS × 72 + 500 ) / 1000    [四舍五入]
 *
 *   代入 DEADTIME_NS = 1000: DeadTime = 72, 实际 ≈ 1000ns
 *
 *   速查表 (72MHz):
 *     200ns→14(~194ns)  300ns→22(~306ns)  500ns→36(~500ns)  1000ns→72(~1000ns)
 *
 *   DeadTime 8-bit 寄存器 (0~255), 最大死区 ≈ 3.54μs
 *   IR2103S ~100ns 防直通 + MCU 死区 = 总死区 ~1100ns
 */
#define DEADTIME_REG_VAL          (((DEADTIME_NS) * 72 + 500) / 1000)

/* 编译期静态断言: 若报错请减小 DEADTIME_NS */
typedef char __deadtime_range_check[(DEADTIME_REG_VAL <= 255) ? 1 : -1];

/* ── 软启动状态机私有变量 ── */
static SoftStart_State_t s_ss_state       = SS_IDLE;
static uint32_t          s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
static uint32_t          s_ss_last_step    = 0;
```

- [ ] **Step 2: 重写 PWM_Init — MOE 关闭, 默认 150kHz (替换当前 PWM_Init 函数, 约第 87-172 行)**

```c
/**
 * @brief  TIM1 全桥 PWM 初始化 (配置完成但 MOE 关, 上电安全态)
 * @note   72MHz / 不分频 / Up 计数 / 默认 150kHz (感性安全起点)
 *         计数器运行但 MOE 禁止 → 全桥无输出
 *         软启动由 Inverter_SoftStart_Trigger() 开启 MOE 并扫频
 *
 *   150kHz 周期: 72MHz / 150k = 480 ticks → ARR = 479, CCR = 240 (50%)
 */
void PWM_Init(void)
{
    /* ================= 1. 时钟与重映射 ================= */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM1, ENABLE);

    /* ================= 2. GPIO (复用推挽, 50MHz) ================= */
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);

    /* ================= 3. 时基单元 (默认 150kHz) ================= */
    TIM_InternalClockConfig(TIM1);

    TIM_TimeBaseInitTypeDef tbase;
    tbase.TIM_ClockDivision     = TIM_CKD_DIV1;
    tbase.TIM_CounterMode       = TIM_CounterMode_Up;
    tbase.TIM_Period            = 480 - 1;     /* 150kHz */
    tbase.TIM_Prescaler         = 1 - 1;
    tbase.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tbase);

    TIM_ARRPreloadConfig(TIM1, ENABLE);

    /* ================= 4. 输出比较 (全桥对角导通) ================= */
    TIM_OCInitTypeDef oc;
    TIM_OCStructInit(&oc);

    /* IR2103S: 上管高有效, 下管低有效, 空闲态全部关断 */
    oc.TIM_OCIdleState  = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Set;
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity  = TIM_OCNPolarity_Low;
    oc.TIM_OutputState  = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_Pulse        = 240;  /* CCR = 50% @ 150kHz */

    oc.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OC1Init(TIM1, &oc);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);

    oc.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OC2Init(TIM1, &oc);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);

    /* ================= 5. 死区与刹车 ================= */
    TIM_BDTRInitTypeDef bdtr;
    bdtr.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    bdtr.TIM_Break            = TIM_Break_Disable;
    bdtr.TIM_BreakPolarity    = TIM_BreakPolarity_Low;
    bdtr.TIM_DeadTime         = DEADTIME_REG_VAL;
    bdtr.TIM_LOCKLevel        = TIM_LOCKLevel_OFF;
    bdtr.TIM_OSSIState        = TIM_OSSIState_Disable;
    bdtr.TIM_OSSRState        = TIM_OSSRState_Disable;
    TIM_BDTRConfig(TIM1, &bdtr);

    /* ================= 6. 使能计数器但禁止 MOE (上电安全态) ================= */
    TIM_Cmd(TIM1, ENABLE);              /* 计数器运行, 准备就绪 */
    TIM_CtrlPWMOutputs(TIM1, DISABLE);  /* MOE 关 → 全桥无输出 */
}
```

- [ ] **Step 3: 提交**

```bash
git add Hardware/PWM.c
git commit -m "feat: PWM_Init MOE off, 150kHz default, 95kHz hard limit"
```

---

### Task 3: PWM.c — PWM_SetFrequency 限幅改 95kHz + 状态机函数

**Files:**
- Modify: `Hardware/PWM.c`

- [ ] **Step 1: 修改 PWM_SetFrequency 硬限幅 (替换约第 188-192 行)**

将:
```c
    /* ── 安全红线: 绝对不许低于 100kHz (容性区 = 炸机) ── */
    if (freq_Hz < 100000UL)
        freq_Hz = 100000UL;
    else if (freq_Hz > 150000UL)
        freq_Hz = 150000UL;
```

改为:
```c
    /* ── 安全硬限幅: 95kHz 绝对底线 (容性区 = 炸机) ── */
    if (freq_Hz < PWM_FREQ_HARD_MIN_HZ)
        freq_Hz = PWM_FREQ_HARD_MIN_HZ;
    else if (freq_Hz > PWM_FREQ_HARD_MAX_HZ)
        freq_Hz = PWM_FREQ_HARD_MAX_HZ;
```

- [ ] **Step 2: 删除旧阻塞式 Inverter_SoftStart 函数 (第 237-275 行)，替换为以下 5 个函数**

```c
/**
 * @brief  触发软启动扫频 (仅 SS_IDLE 时有效)
 * @note   设置起始频率 150kHz, 使能 MOE, 进入 SS_SWEEP 状态
 *         重复触发被忽略 (状态检查防抖)
 */
void Inverter_SoftStart_Trigger(void)
{
    if (s_ss_state != SS_IDLE) return;   /* 已在运行, 忽略 */

    s_ss_state        = SS_SWEEP;
    s_ss_current_freq = SOFTSTART_START_FREQ_HZ;

    PWM_SetFrequency(s_ss_current_freq); /* 确保从 150kHz 开始 */
    PWM_Enable();                        /* 开启 MOE → 全桥有输出 */
    s_ss_last_step = SysTimer_GetTick();
}

/**
 * @brief  软启动扫频任务 (主循环每轮调用, 非阻塞)
 * @note   时间戳差值法, 每 2ms 降频 200Hz
 *         到达 100kHz 后自动转入 SS_DONE
 */
void Inverter_SoftStart_Task(void)
{
    if (s_ss_state != SS_SWEEP) return;

    if (SysTimer_GetTick() - s_ss_last_step < SOFTSTART_STEP_DELAY_MS)
        return;

    s_ss_last_step = SysTimer_GetTick();

    if (s_ss_current_freq > SOFTSTART_TARGET_FREQ_HZ + SOFTSTART_STEP_HZ) {
        s_ss_current_freq -= SOFTSTART_STEP_HZ;
    } else {
        s_ss_current_freq = SOFTSTART_TARGET_FREQ_HZ;
        s_ss_state = SS_DONE;
    }

    PWM_SetFrequency(s_ss_current_freq);
}

/**
 * @brief  紧急关断全桥输出
 * @note   关 MOE → 回 SS_IDLE, 下次 Trigger 重新从 150kHz 扫频
 */
void Inverter_SoftStart_Stop(void)
{
    PWM_Disable();
    s_ss_state = SS_IDLE;
}

/**
 * @brief  查询当前软启动状态
 */
SoftStart_State_t Inverter_SoftStart_GetState(void)
{
    return s_ss_state;
}

/**
 * @brief  查询当前扫频实时频率 (供 UI 显示)
 */
uint32_t Inverter_SoftStart_GetCurrentFreq(void)
{
    return s_ss_current_freq;
}
```

- [ ] **Step 3: 提交**

```bash
git add Hardware/PWM.c
git commit -m "feat: add non-blocking soft-start state machine"
```

---

### Task 4: main.c — 清理裸 TIM1 操作 + 加入软启动 Task

**Files:**
- Modify: `User/main.c`

- [ ] **Step 1: 替换 main.c 全部内容**

```c
/**
 ******************************************************************************
 * @file    User/main.c
 * @brief   无线充电 PWM 系统 —— 主程序入口
 * @note    存放路径: 项目根目录\User\
 *
 *          架构 (V3.0):
 *          - 上电: 硬件配置 → 时基 → 联网 → 等待触发
 *          - 全桥默认关断 (MOE OFF), 由 Trigger 开启扫频
 *          - 非阻塞软启动状态机: 150kHz → 100kHz, ~500ms
 *          - 触发源: KEY0 单击 / PC "ON" 指令
 *
 *          依赖: STM32F10x 标准外设库 (SPL)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "SysTimer.h"
#include "HardLED.h"
#include "OLED.h"
#include "PWM.h"
#include "ADC.h"
#include "KEY.h"
#include "UI.h"
#include "App_Net.h"

int main(void)
{
    /* ═══════════════════════════════════════════════════════════
     *  阶段 1: 板载外设初始化 (硬件层, 全桥无输出)
     * ═══════════════════════════════════════════════════════════ */
    PWM_Init();           /* TIM1 配置完成, MOE 关, 安全态 */
    OLED_Init();
    HardLED_Init();
    ADC_DMA_Init();
    KEY_Init();

    OLED_Clear();
    OLED_ShowString(1, 1, "Wireless Charge");

    /* ═══════════════════════════════════════════════════════════
     *  阶段 2: 系统时基初始化 (SysTick 1ms)
     * ═══════════════════════════════════════════════════════════ */
    SysTimer_Init();

    /* ═══════════════════════════════════════════════════════════
     *  阶段 3: 网络应用层初始化 (阻塞联网, 约 20~30s)
     * ═══════════════════════════════════════════════════════════ */
    App_Net_Init();       /* 联网成功后内部已 OLED_Clear */

    /* ═══════════════════════════════════════════════════════════
     *  阶段 4: 主循环 (纯非阻塞)
     * ═══════════════════════════════════════════════════════════ */
    while (1)
    {
        KEY_Task();
        UI_Task();
        App_Net_Task();
        Inverter_SoftStart_Task();  /* 非阻塞扫频步进 (内部 2ms 节拍) */
        HardLED_Task();
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add User/main.c
git commit -m "feat: integrate non-blocking soft-start into main loop"
```

---

### Task 5: UI.c — 按键映射 + 扫频显示 + 切页清屏

**Files:**
- Modify: `Hardware/UI.c`

- [ ] **Step 1: 替换 UI.c 全部内容**

```c
/**
 ******************************************************************************
 * @file    Hardware/UI.c
 * @brief   人机交互界面 —— OLED 显示 + 按键事件分发
 * @note    存放路径: 项目根目录\Hardware\
 *
 *          双页面架构:
 *            页面 0: 控制面板 (电压/电流/频率/软启动状态, 按键操作)
 *            页面 1: 锁屏监控 (只读, 屏蔽按键)
 *
 *          按键 (V3.0):
 *            KEY0 单击 : 触发软启动 (仅 SS_IDLE 时)
 *            KEY0 双击 : 切页 (控制面板 ↔ 锁屏监控)
 *            KEY1 单击 : 关断全桥
 *
 *          软启动显示:
 *            SS_SWEEP: 实时频率 + 进度条
 *            SS_DONE:  监控页 (电压/电流/频率)
 *            SS_IDLE:  待机 "Ready"
 *
 *          每次切页调用 OLED_Clear() 防残影
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "UI.h"
#include "OLED.h"
#include "KEY.h"
#include "PWM.h"
#include "ADC.h"
#include "SysTimer.h"

static uint8_t  UI_Page            = 0;  /* 0: 控制面板, 1: 锁屏监控 */
static uint8_t  is_Bridge_Running  = 0;  /* 供外部同步 */

void UI_Task(void)
{
    static uint32_t   last_oled       = 0;
    static uint8_t    last_ss_state   = 0xFF; /* 上轮状态, 用于检测切页 */
    uint8_t           need_refresh    = 0;

    if (SysTimer_GetTick() - last_oled >= 200) {
        last_oled = SysTimer_GetTick();
        need_refresh = 1;
    }

    SoftStart_State_t ss = Inverter_SoftStart_GetState();
    uint8_t key0_event = KEY_Get_Event(0);  /* KEY0 (PB12) */
    uint8_t key1_event = KEY_Get_Event(1);  /* KEY1 (PB13) */

    /*
     * 状态迁移检测: 每当状态变化时强制清屏 + 立即刷新
     * 覆盖 SS_IDLE→SWEEP, SWEEP→DONE, DONE→IDLE, IDLE→DONE (远程)
     */
    if (ss != last_ss_state) {
        last_ss_state = ss;
        OLED_Clear();
        need_refresh = 1;
    }

    /* ── KEY0 双击: 全局切页 (最高优先级) ── */
    if (key0_event == 2) {
        UI_Page = !UI_Page;
        OLED_Clear();
        return;
    }

    /* ── 页面路由 ── */
    switch (UI_Page)
    {
        case 0: /* 控制面板 */
            /* KEY0 单击: 触发软启动 (仅待机状态有效) */
            if (key0_event == 1) {
                Inverter_SoftStart_Trigger();
                is_Bridge_Running = 1;
            }
            /* KEY1 单击: 关断 */
            if (key1_event == 1) {
                Inverter_SoftStart_Stop();
                is_Bridge_Running = 0;
            }

            if (need_refresh) {
                switch (ss) {
                    case SS_IDLE:
                        OLED_ShowString(1, 1, "[Control Mode] ");
                        OLED_ShowString(2, 1, "State: IDLE   ");
                        OLED_ShowString(3, 1, "Press KEY0 start");
                        OLED_ShowString(4, 1, "F:  --.- kHz    ");
                        break;

                    case SS_SWEEP:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        /* 进度: (150k - current) / (150k - 100k) × 10 */
                        uint32_t progress = (SOFTSTART_START_FREQ_HZ - f) * 10
                                          / (SOFTSTART_START_FREQ_HZ - 100000UL);
                        OLED_ShowString(1, 1, "[Sweeping...]  ");
                        OLED_ShowString(2, 1, "Freq: ");
                        OLED_ShowNum(2, 7, f / 1000, 3);
                        OLED_ShowString(2, 10, ".");
                        OLED_ShowNum(2, 11, (f % 1000) / 100, 1);
                        OLED_ShowString(2, 12, "kHz  ");
                        OLED_ShowString(3, 1, "[");
                        for (int i = 0; i < 10; i++)
                            OLED_ShowString(3, 2 + i, (i < (int)progress) ? "#" : " ");
                        OLED_ShowString(3, 12, "]");
                        break;
                    }

                    case SS_DONE:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        OLED_ShowString(1, 1, "[Resonant Mode]");
                        OLED_ShowString(2, 1, "F:");
                        OLED_ShowNum(2, 3, f / 1000, 3);
                        OLED_ShowString(2, 6, "kHz  ");
                        OLED_ShowString(3, 1, "V:");
                        OLED_ShowFloatNum(3, 3, Get_Real_Voltage(), 2, 1);
                        OLED_ShowString(3, 9, "I:");
                        OLED_ShowFloatNum(3, 11, Get_Real_Current(), 1, 2);
                        OLED_ShowString(4, 1, "KEY1: Stop     ");
                        break;
                    }
                }
            }
            break;

        case 1: /* 锁屏监控 (只读, 屏蔽按键) */
            if (need_refresh) {
                switch (ss) {
                    case SS_IDLE:
                        OLED_ShowString(1, 1, "- Monitor Only -");
                        OLED_ShowString(2, 1, "State: IDLE    ");
                        OLED_ShowString(3, 1, "Waiting trigger ");
                        break;
                    case SS_SWEEP:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        OLED_ShowString(1, 1, "- Monitor Only -");
                        OLED_ShowString(2, 1, "Sweeping...    ");
                        OLED_ShowString(3, 1, "F:");
                        OLED_ShowNum(3, 3, f / 1000, 3);
                        OLED_ShowString(3, 6, "kHz");
                        break;
                    }
                    case SS_DONE:
                    {
                        uint32_t f = Inverter_SoftStart_GetCurrentFreq();
                        OLED_ShowString(1, 1, "- Monitor Only -");
                        OLED_ShowString(2, 1, "Freq: ");
                        OLED_ShowNum(2, 7, f / 1000, 3);
                        OLED_ShowString(2, 10, "kHz");
                        OLED_ShowString(3, 1, "Volt: ");
                        OLED_ShowFloatNum(3, 7, Get_Real_Voltage(), 2, 2);
                        OLED_ShowString(4, 1, "Curr: ");
                        OLED_ShowFloatNum(4, 7, Get_Real_Current(), 2, 2);
                        break;
                    }
                }
            }
            break;
    }
}

void UI_SetBridgeState(uint8_t on_off)
{
    is_Bridge_Running = on_off;
}

uint8_t UI_GetBridgeState(void)
{
    return is_Bridge_Running;
}
```

- [ ] **Step 2: 修复 C90 兼容性 (for 循环内声明变量)**

扫频进度条中的 `for (int i = 0; ...)` 在 ARMCC C90 模式下不通过。改为在函数开头声明:

在 `UI_Task` 函数顶部, `need_refresh` 之后增加:

```c
    uint8_t bar_i;  /* 进度条循环计数器 (C90 须在块开头声明) */
```

然后将进度条循环:
```c
                        for (int i = 0; i < 10; i++)
                            OLED_ShowString(3, 2 + i, (i < (int)progress) ? "#" : " ");
```

改为:
```c
                        for (bar_i = 0; bar_i < 10; bar_i++)
                            OLED_ShowString(3, 2 + bar_i, (bar_i < progress) ? "#" : " ");
```

- [ ] **Step 3: 提交**

```bash
git add Hardware/UI.c
git commit -m "feat: integrate soft-start state display and key mapping into UI"
```

---

### Task 6: App_Net.c — 远程 ON→Trigger / OFF→Stop

**Files:**
- Modify: `User/App_Net.c`

- [ ] **Step 1: 修改 Net_Remote_On (替换约第 51-55 行)**

```c
static void Net_Remote_On(void)
{
    if (Inverter_SoftStart_GetState() == SS_IDLE) {
        Inverter_SoftStart_Trigger();
        UI_SetBridgeState(1);
    }
}
```

- [ ] **Step 2: 修改 Net_Remote_Off (替换约第 60-64 行)**

```c
static void Net_Remote_Off(void)
{
    Inverter_SoftStart_Stop();
    UI_SetBridgeState(0);
}
```

- [ ] **Step 3: 提交**

```bash
git add User/App_Net.c
git commit -m "feat: wire remote ON/OFF to soft-start Trigger/Stop"
```

---

### Task 7: OLED.h — 确认 OLED_ShowFloatNum 接口

**Files:**
- Check: `Hardware/OLED.h`

- [ ] **Step 1: 确认函数签名**

```bash
grep -n "OLED_ShowFloatNum\|OLED_ShowNum\|OLED_ShowString\|OLED_Clear" Hardware/OLED.h
```

比较计划中使用的调用与 OLED.h 签名一致。特别注意 `OLED_ShowFloatNum` 的参数顺序。

- [ ] **Step 2: 如接口不匹配, 调整 UI.c 中的调用, 然后提交修正**

```bash
git add Hardware/UI.c
git commit -m "fix: match OLED function signatures in UI sweep display"
```

---

### Task 8: 编译验证

- [ ] **Step 1: 在 Keil uVision 中打开 Project.uvprojx 编译**

预期: 0 errors, 0 warnings (仅 `step_count` 的 unused 警告应消失, 因为旧的阻塞式函数已删除)

- [ ] **Step 2: 检查 HEX 输出**

确认 `Objects/Project.hex` 生成, 无链接错误。

- [ ] **Step 3: 如有编译错误, 修正后重新编译**

常见可能问题:
- `OLED_ShowFloatNum` 参数数量/类型不匹配 → 调整调用
- `for (int i = ...)` C90 模式报错 → 改为 `uint8_t i; for (i = 0; ...)`
- `#include` 缺失 → 补全

- [ ] **Step 4: 提交最终修正**

```bash
git add -A
git commit -m "fix: resolve compilation issues from soft-start integration"
```
