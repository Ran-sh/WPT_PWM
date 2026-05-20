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
#include "PWM.h"
#include "SysTimer.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  可调参数
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * 以下宏 DEADTIME_NS、SOFTSTART_START_FREQ_HZ、SOFTSTART_TARGET_FREQ_HZ
 * 已定义于 Hardware/PWM.h, 此处通过 include 获取, 不重复定义。
 */

#define SOFTSTART_STEP_HZ         200UL      /* 每次降频步长 (Hz)         */
#define SOFTSTART_STEP_DELAY_MS   10U        /* 每步延时 (ms)             */
/* 总步数: (150k-100k)/200 = 250 步, 总耗时 250×10ms = 2500ms */

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

/*
 * 编译期安全断言: BDTR 线性段 DTG[7:0] 范围 0~127 (DTG[7:5]=0xx)
 * DTG >= 128 进入非线性分段编码, 死区时间不可预期, 必须阻止
 * 若编译报错, 说明 DEADTIME_NS 太大, 需减小或改用更大 CKD 分频
 */
typedef char __deadtime_linear_check[(DEADTIME_REG_VAL <= 127) ? 1 : -1];

/* ── 软启动状态机私有变量 ── */
static SoftStart_State_t s_ss_state       = SS_IDLE;
static uint32_t          s_ss_current_freq = SOFTSTART_START_FREQ_HZ;
static uint32_t          s_ss_last_step    = 0;

/*
 * 原子状态切换: 关全局中断 → 写状态 → 恢复
 * 防止按键和 WiFi 指令同时对 s_ss_state 抢占写入导致状态机错乱
 */
static void Inverter_SetState(SoftStart_State_t new_state)
{
    __disable_irq();
    s_ss_state = new_state;
    __enable_irq();
}


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

    /*
     * OIS (Output Idle State) 防直通配置:
     *   MOE=0 时 CH1/CH2 强制输出 OCIdleState=Reset (低电平)
     *   CH1N/CH2N 强制输出 OCNIdleState=Set (高电平)
     *   IR2103S: HIN 高有效 → CH1/CH2 低 = 上管关
     *            LIN 低有效 → CH1N/CH2N 高 = 下管关
     *   结论: MOE 关断时 4 个 MOSFET 全部可靠关断, 绝无桥臂直通
     */
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


/**
 * @brief  PFM 调频函数 (50% 占空比锁定 + 95kHz 安全红线)
 * @param  freq_Hz: 目标频率 (Hz)
 * @retval 实际设定频率 (Hz)
 * @note   同步修改 ARR 和 CCR 保证占空比绝对 50%
 *         强制偶数周期防止左右半桥不对称偏磁
 *         低于 95kHz 的请求被钳位拒绝
 */
uint32_t PWM_SetFrequency(uint32_t freq_Hz)
{
    uint32_t ticks;
    uint16_t arr, ccr;

    /* ── 安全硬限幅: 95kHz 绝对底线 (容性区 = 炸机) ── */
    if (freq_Hz < PWM_FREQ_HARD_MIN_HZ)
        freq_Hz = PWM_FREQ_HARD_MIN_HZ;
    else if (freq_Hz > PWM_FREQ_HARD_MAX_HZ)
        freq_Hz = PWM_FREQ_HARD_MAX_HZ;

    /* ── 周期 ticks 计算 ── */
    ticks = TIM1_CLK_HZ / freq_Hz;

    /* ── 强制偶数 (防偏磁) ── */
    if (ticks % 2 != 0)
        ticks += 1;

    arr = (uint16_t)(ticks - 1);
    ccr = (uint16_t)(ticks / 2);      /* 50% 绝对居中 */

    /*
     * 原子更新: 暂停影子寄存器传输, 写入 ARR+CCR1+CCR2,
     * 软件触发 UE 事件一次性加载, 再恢复硬件更新。
     * 防止 Update Event 在 ARR 和 CCR 写入之间触发,
     * 导致新周期配旧占空比 → 偏磁 → 炸机。
     */
    TIM1->CR1 |= TIM_CR1_UDIS;           /* 暂停影子寄存器加载 */
    TIM_SetAutoreload(TIM1, arr);
    TIM_SetCompare1(TIM1, ccr);
    TIM_SetCompare2(TIM1, ccr);
    TIM1->EGR |= TIM_EGR_UG;            /* 软件触发更新, 原子加载全部影子寄存器 */
    TIM1->CR1 &= (uint16_t)(~TIM_CR1_UDIS); /* 恢复硬件更新 */

    return TIM1_CLK_HZ / ticks;
}


uint32_t PWM_GetFrequency(void)
{
    uint16_t arr = TIM1->ARR;
    return arr ? TIM1_CLK_HZ / (arr + 1) : 100000UL;
}


void PWM_Enable(void)
{
    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

/*
 * 安全关断时序: 先断 MOE (硬件刹车, 输出立即高阻),
 * 再停计数器, 防止关断过程中出现窄脉冲直通。
 */
void PWM_Disable(void)
{
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
    TIM_Cmd(TIM1, DISABLE);
}


/**
 * @brief  触发软启动扫频 (仅 SS_IDLE 时有效)
 * @note   设置起始频率 150kHz, 使能 MOE, 进入 SS_SWEEP 状态
 *         重复触发被忽略 (状态检查防抖)
 */
void Inverter_SoftStart_Trigger(void)
{
    if (s_ss_state != SS_IDLE) return;   /* 已在运行, 忽略 */

    Inverter_SetState(SS_SWEEP);
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
        Inverter_SetState(SS_DONE);
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
    Inverter_SetState(SS_IDLE);
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
