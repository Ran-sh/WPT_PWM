import glob, os

base = r"D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT"

# ═══════════════════════════════════════════════════════════════
# Fix all public functions in Pwm_Driver.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Pwm_Driver.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Pwm_Driver_Init(void)\n{",
    "/** @brief 初始化 TIM1 全桥 PWM: 4通道+死区+预载, 初始全关零输出 */\nvoid Pwm_Driver_Init(void)\n{"
)
c = c.replace(
    "void Pwm_Driver_Enable(void)  { TIM_Cmd(TIM1, ENABLE); TIM_CtrlPWMOutputs(TIM1, ENABLE); }",
    "/** @brief 开启 PWM 输出: 启动计数器 + MOE 使能 */\nvoid Pwm_Driver_Enable(void)  { TIM_Cmd(TIM1, ENABLE); TIM_CtrlPWMOutputs(TIM1, ENABLE); }"
)
c = c.replace(
    "void Pwm_Driver_Disable(void) { TIM_CtrlPWMOutputs(TIM1, DISABLE); TIM_Cmd(TIM1, DISABLE); }",
    "/** @brief 关闭 PWM 输出: MOE 关断 + 计数器停止, 全桥归零 */\nvoid Pwm_Driver_Disable(void) { TIM_CtrlPWMOutputs(TIM1, DISABLE); TIM_Cmd(TIM1, DISABLE); }"
)
c = c.replace(
    "uint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)\n{",
    "/** @brief 设置 PWM 频率并原子更新寄存器 (钳位 95~150kHz, 强制偶数 ticks 防偏磁)\n *  @param freq_hz 目标频率 (Hz)\n *  @retval 实际设定频率 (Hz) */\nuint32_t Pwm_Driver_Set_Frequency(uint32_t freq_hz)\n{"
)
c = c.replace(
    "uint32_t Pwm_Driver_Get_Frequency(void)\n{",
    "/** @brief 获取当前 PWM 频率 (Hz), 从 TIM1->ARR 实时计算 */\nuint32_t Pwm_Driver_Get_Frequency(void)\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Pwm_Driver.c")

# ═══════════════════════════════════════════════════════════════
# Fix Key_Driver.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Key_Driver.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Key_Driver_Init(void)\n{",
    "/** @brief 初始化 4 键 GPIO: PB5/PB7/PB8/PB9 全部 IPU 上拉 */\nvoid Key_Driver_Init(void)\n{"
)
c = c.replace(
    "void Key_Driver_Task(void)\n{",
    "/** @brief 周期扫描 4 键 FSM (每 10ms), 自动去抖+单击/双击/长按判定 */\nvoid Key_Driver_Task(void)\n{"
)
c = c.replace(
    "void Key_Driver_Get_All_Events(Key_Driver_Event out[4])\n{",
    "/** @brief 批量读取 4 键事件 (单次临界区, 阅后即焚, 减少 IRQ 抖动) */\nvoid Key_Driver_Get_All_Events(Key_Driver_Event out[4])\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Key_Driver.c")

# ═══════════════════════════════════════════════════════════════
# Fix Led_Driver.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Led_Driver.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Led_Driver_Init(void)\n{",
    "/** @brief 初始化 5 LED GPIO + 禁用 JTAG 释放 PB3/PB4 (PA12 已让给 Flash CS) */\nvoid Led_Driver_Init(void)\n{"
)
c = c.replace(
    "void Led_Driver_Task(void)\n{",
    "/** @brief 周期驱动所有 LED: 根据状态自动 ON/OFF/SLOW/FAST 闪烁 */\nvoid Led_Driver_Task(void)\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Led_Driver.c")

# ═══════════════════════════════════════════════════════════════
# Fix Sys_Timer.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "System", "Sys_Timer.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Sys_Timer_Init(void)\n{",
    "/** @brief 初始化 SysTick 1ms + DWT 72MHz 周期计数器 (全局时基) */\nvoid Sys_Timer_Init(void)\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Sys_Timer.c")

# ═══════════════════════════════════════════════════════════════
# Fix Inverter_Control.c — add comments to all public functions
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Inverter_Control.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Inverter_Control_Soft_Start_Trigger(void)\n{",
    "/** @brief 触发软启动: IDLE -> SWEEP, 频率从 150kHz 向下斜坡 */\nvoid Inverter_Control_Soft_Start_Trigger(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Soft_Start_Stop(void)\n{",
    "/** @brief 停止软启动: 关 PWM -> 回 IDLE */\nvoid Inverter_Control_Soft_Start_Stop(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Soft_Start_Reset(void)\n{",
    "/** @brief 复位软启动状态机 (FAULT 恢复后调用) */\nvoid Inverter_Control_Soft_Start_Reset(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Soft_Start_Task(void)\n{",
    "/** @brief 软启动周期任务: 驱动频率斜坡 150k->100kHz, 非阻塞 */\nvoid Inverter_Control_Soft_Start_Task(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz)\n{",
    "/** @brief 触发频率斜坡到目标值 (1kHz/步, 非阻塞)\n *  @param target_hz 目标频率 (Hz) */\nvoid Inverter_Control_Freq_Ramp_Trigger(uint32_t target_hz)\n{"
)
c = c.replace(
    "void Inverter_Control_Freq_Ramp_Task(void)\n{",
    "/** @brief 频率斜坡周期任务: 逐级调到目标频率 */\nvoid Inverter_Control_Freq_Ramp_Task(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Freq_Ramp_Cancel(void)\n{",
    "/** @brief 取消当前频率斜坡 (FAULT 或手动停止时调用) */\nvoid Inverter_Control_Freq_Ramp_Cancel(void)\n{"
)
c = c.replace(
    "void Inverter_Control_Soft_Start_Fault(void)\n{",
    "/** @brief 故障刹车: 立即关断 PWM, 进入 FAULT 状态 */\nvoid Inverter_Control_Soft_Start_Fault(void)\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Inverter_Control.c")

# ═══════════════════════════════════════════════════════════════
# Fix Esp8266_Driver.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Esp8266_Driver.c")
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

c = c.replace(
    "void Esp8266_Driver_Start_Init(void)\n{",
    "/** @brief 启动 ESP8266 硬件初始化: RST 脉冲 + BOOT_WAIT */\nvoid Esp8266_Driver_Start_Init(void)\n{"
)
c = c.replace(
    "void Esp8266_Driver_Init_Task(void)\n{",
    "/** @brief 周期驱动 ESP8266 初始化状态机 */\nvoid Esp8266_Driver_Init_Task(void)\n{"
)
c = c.replace(
    "void Esp8266_Driver_Send_String(const char* str)\n{",
    "/** @brief 发送字符串到 ESP8266 (轮询 TXE+TC, 阻塞)\n *  @param str 以 \\0 结尾的字符串 */\nvoid Esp8266_Driver_Send_String(const char* str)\n{"
)
c = c.replace(
    "void Esp8266_Driver_Rx_Char(uint8_t ch)\n{",
    "/** @brief ISR 回调: 接收 1 字节到环形缓冲 */\nvoid Esp8266_Driver_Rx_Char(uint8_t ch)\n{"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(c)
print("OK: Esp8266_Driver.c")

print("\nAll done!")
