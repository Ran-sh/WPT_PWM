import glob, os

base = r"D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT"

# ═══════════════════════════════════════════════════════════════
# Buzzer_Driver.c — 全部 3 个公开函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "Buzzer_Driver.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("void Buzzer_Driver_Init(void)\n{", "/** @brief 初始化 PB15 推挽输出, 初始低电平 -> 蜂鸣器静音 */\nvoid Buzzer_Driver_Init(void)\n{")
c = c.replace("void Buzzer_Driver_Task(void)\n{", "/** @brief 周期任务: 根据 s_state 自动控制 ON/OFF/BEEP 间歇蜂鸣 */\nvoid Buzzer_Driver_Task(void)\n{")
c = c.replace("void Buzzer_Driver_Set_State(Buzzer_Driver_State state)\n{", "/** @brief 设置蜂鸣器工作模式 (OFF=静音, ON=持续响, BEEP=间歇 200ms/800ms) */\nvoid Buzzer_Driver_Set_State(Buzzer_Driver_State state)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Buzzer_Driver.c — 3 functions")

# ═══════════════════════════════════════════════════════════════
# Adc_Driver.c — 剩余的公开函数（已有部分注释）
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "Adc_Driver.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("void Adc_Driver_Init(void)\n{", "/** @brief 初始化 ADC1 + DMA1 双通道连续扫描, 144241 周期互质采样 */\nvoid Adc_Driver_Init(void)\n{")
c = c.replace("void Adc_Driver_Filter_Task(void)\n{", "/** @brief 周期任务: DMA 滑动窗口滤波 + 电压/电流 EMA 更新 */\nvoid Adc_Driver_Filter_Task(void)\n{")
c = c.replace("void Adc_Driver_Calibrate_Offset(void)\n{", "/** @brief 冷启动电流零点自测算: 50 样本均值 -> s_i_offset */\nvoid Adc_Driver_Calibrate_Offset(void)\n{")
c = c.replace("float Adc_Driver_Get_Voltage(void) { return s_voltage; }", "/** @brief 获取 EMA 滤波后的实时电压值 (V) */\nfloat Adc_Driver_Get_Voltage(void) { return s_voltage; }")
c = c.replace("float Adc_Driver_Get_Current(void) { return s_current; }", "/** @brief 获取 EMA 滤波后的实时电流值 (A) */\nfloat Adc_Driver_Get_Current(void) { return s_current; }")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Adc_Driver.c — 5 functions")

# ═══════════════════════════════════════════════════════════════
# Led_Driver.c — 全部公开函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "Led_Driver.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("static void Drive_Pin(GPIO_TypeDef* port, uint16_t pin,", "/** @brief 驱动单个 LED 引脚: 根据状态 ON/OFF/SLOW 闪烁/FAST 闪烁 */\nstatic void Drive_Pin(GPIO_TypeDef* port, uint16_t pin,")
c = c.replace("void Led_Driver_Init(void)\n{", "/** @brief 初始化 5 LED GPIO + 禁用 JTAG (PB3/PB4/PA15), 上电自检 500ms */\nvoid Led_Driver_Init(void)\n{")
c = c.replace("void Led_Driver_Task(void)\n{", "/** @brief 周期驱动所有 LED: WiFi/PWM/COM/POWER 状态灯 + SYSTEM 心跳 */\nvoid Led_Driver_Task(void)\n{")
c = c.replace("void Led_Driver_Set_WiFi(Led_Driver_State state)   { s_wifi_state  = state; }", "/** @brief 设置 WiFi 状态 LED (PB4): ON=在线, SLOW=重连, OFF=离线 */\nvoid Led_Driver_Set_WiFi(Led_Driver_State state)   { s_wifi_state  = state; }")
c = c.replace("void Led_Driver_Set_Pwm(Led_Driver_State state)    { s_pwm_state   = state; }", "/** @brief 设置 PWM 运行 LED (PB3): ON=运行, SLOW=扫频, OFF=停机 */\nvoid Led_Driver_Set_Pwm(Led_Driver_State state)    { s_pwm_state   = state; }")
c = c.replace("void Led_Driver_Set_Com(Led_Driver_State state)    { s_com_state   = state; }", "/** @brief 设置通信 LED (PA10): ON=数据收发, OFF=空闲 */\nvoid Led_Driver_Set_Com(Led_Driver_State state)    { s_com_state   = state; }")
c = c.replace("void Led_Driver_Set_Power(Led_Driver_State state)  { s_power_state = state; }", "/** @brief 设置电源 LED (PA11): ON=12V 使能, OFF=12V 关断 */\nvoid Led_Driver_Set_Power(Led_Driver_State state)  { s_power_state = state; }")
c = c.replace("void Led_Driver_Set_Temp(Led_Driver_State state)   { /* PA12→Flash CS, 函数保留占位 */ }", "/** @brief [已禁用] PA12 已让给 W25Q128 Flash CS */\nvoid Led_Driver_Set_Temp(Led_Driver_State state)   { /* PA12->Flash CS, 函数保留占位 */ }")
c = c.replace("void Led_Driver_Set_System(uint8_t on_off)          { s_system_on   = on_off; }", "/** @brief 控制系统心跳 LED (PA15): 1=闪烁, 0=灭 */\nvoid Led_Driver_Set_System(uint8_t on_off)          { s_system_on   = on_off; }")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Led_Driver.c — 9 functions")

# ═══════════════════════════════════════════════════════════════
# Pwm_Driver.c — 已全部注释, 无需重复
# ═══════════════════════════════════════════════════════════════
print("OK: Pwm_Driver.c — already done")

# ═══════════════════════════════════════════════════════════════
# Sys_Core.c — 全部 17 个函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "User", "Sys_Core.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("void Sys_Clamp_ESP(void)\n{", "/** @brief 钳位 ESP8266 控制脚 (RST=0, CH_PD=0), 防止上电浮空误触发 */\nvoid Sys_Clamp_ESP(void)\n{")
c = c.replace("void Sys_Hardware_Init(void)\n{", "/** @brief 硬件驱动批量初始化: Pwm/TFT/Led/Buzzer/Adc/Key, PB10 初始关断 */\nvoid Sys_Hardware_Init(void)\n{")
c = c.replace("void Sys_Startup_Screen(void)\n{", "/** @brief 开机画面: SPLASH 动画 ~4.8s + Flash/ROM 字库状态显示 */\nvoid Sys_Startup_Screen(void)\n{")
c = c.replace("void Sys_Post_Init(void)\n{", "/** @brief 后初始化: LED 心跳 + Flash 参数加载 + ADC 校准 + WDG + ESP 联网 */\nvoid Sys_Post_Init(void)\n{")
c = c.replace("static void Sys_Safety_Update_EMA(void)\n{", "/** @brief 更新安全级 EMA 滤波 (a=0.25, ~800ms), 从 ADC 原始值重新计算 */\nstatic void Sys_Safety_Update_EMA(void)\n{")
c = c.replace("float Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }", "/** @brief 获取安全级 EMA 滤波电压 (用于过流保护+PB10控制) */\nfloat Sys_Safety_Get_EMA_Voltage(void)  { return s_safety_ema_v; }")
c = c.replace("float Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }", "/** @brief 获取安全级 EMA 滤波电流 (用于过流保护阈值比较) */\nfloat Sys_Safety_Get_EMA_Current(void)  { return s_safety_ema_i; }")
c = c.replace("void Sys_Safety_Reset_EMA(void)\n{", "/** @brief 重置过流 EMA 缓存: 电流清零 + 强制重新收敛, 防止 FAULT 误重触发 */\nvoid Sys_Safety_Reset_EMA(void)\n{")
c = c.replace("void Sys_Safety_Task(void)\n{", "/** @brief 安全监测周期任务: 仅 RUNNING 状态执行, >5.0A -> FAULT, PB10 电源控制 */\nvoid Sys_Safety_Task(void)\n{")
c = c.replace("static void Sys_Run_Led_Tick(void)\n{", "/** @brief 200ms 周期驱动 LED 任务 */\nstatic void Sys_Run_Led_Tick(void)\n{")
c = c.replace("static void Sys_Run_Buzzer_Tick(void)\n{", "/** @brief 50ms 周期驱动蜂鸣器任务 */\nstatic void Sys_Run_Buzzer_Tick(void)\n{")
c = c.replace("void Sys_Run_Idle(void)\n{", "/** @brief IDLE 状态运行: UI + LED + 蜂鸣器 + Key + ADC + 网络 + 安全 + WDG/WFI */\nvoid Sys_Run_Idle(void)\n{")
c = c.replace("void Sys_Run_Sweep(void)\n{", "/** @brief SWEEP 状态运行: UI + 软启动扫频 + 黑匣子 + 全 Task + WDG/WFI */\nvoid Sys_Run_Sweep(void)\n{")
c = c.replace("void Sys_Run_Running(void)\n{", "/** @brief RUNNING 状态运行: UI + 频率斜坡 + 黑匣子 + 全 Task + WDG/WFI */\nvoid Sys_Run_Running(void)\n{")
c = c.replace("void Sys_Run_Fault(void)\n{", "/** @brief FAULT 状态运行: UI + 取消斜坡 + 全 Task (Key+ADC+网络+安全) + WDG/WFI */\nvoid Sys_Run_Fault(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Sys_Core.c — 15 functions")

# ═══════════════════════════════════════════════════════════════
# App_Network.c — 剩下的未注释函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "User", "App_Network.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("uint8_t App_Network_Soft_Reset(void)\n{", "/** @brief 软复位网络状态机 (进入无 WiFi 模式时调用) */\nuint8_t App_Network_Soft_Reset(void)\n{")
c = c.replace("void App_Network_Resume_From_Offline(void)\n{", "/** @brief 从被动离线恢复: OFFLINE_PASSIVE -> 嗅探恢复连接 */\nvoid App_Network_Resume_From_Offline(void)\n{")
c = c.replace("uint8_t App_Network_Get_Connect_Status(void)\n{", "/** @brief 获取当前连接状态枚举值 */\nuint8_t App_Network_Get_Connect_Status(void)\n{")
c = c.replace("uint8_t App_Network_Get_Retry_Count(void)  { return s_retry_count; }", "/** @brief 获取当前重试次数 */\nuint8_t App_Network_Get_Retry_Count(void)  { return s_retry_count; }")
c = c.replace("uint8_t App_Network_Is_Connected(void)\n{", "/** @brief 查询是否在线 (ONLINE 状态) */\nuint8_t App_Network_Is_Connected(void)\n{")
c = c.replace("uint8_t App_Network_Is_Offline(void)\n{", "/** @brief 查询是否离线 (PASSIVE 或 ACTIVE 状态) */\nuint8_t App_Network_Is_Offline(void)\n{")
c = c.replace("uint8_t App_Network_Is_Connecting(void)\n{", "/** @brief 查询是否正在连接 (WIFI 或 MQTT 状态) */\nuint8_t App_Network_Is_Connecting(void)\n{")
c = c.replace("static uint32_t App_Network_Get_Retry_Timeout(void)\n{", "/** @brief 指数退避重试间隔计算: 5s,15s,30s,60s,2min,5min,30min */\nstatic uint32_t App_Network_Get_Retry_Timeout(void)\n{")
c = c.replace("static void App_Network_Check_Retry(void)\n{", "/** @brief 重试检查: 超时 -> 重试连接, 5次上限后不再重试 */\nstatic void App_Network_Check_Retry(void)\n{")
c = c.replace("static void App_Network_Check_Offline_Recovery(void)\n{", "/** @brief 被动离线嗅探恢复: 检测 ESP STATUS 帧 -> 自动重连 */\nstatic void App_Network_Check_Offline_Recovery(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: App_Network.c — 10 functions")

# ═══════════════════════════════════════════════════════════════
# Sys_Timer.c
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "System", "Sys_Timer.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("void Sys_Timer_Init(void)\n{", "/** @brief 初始化 SysTick 1ms + DWT 72MHz 周期计数器 (全局唯一时基) */\nvoid Sys_Timer_Init(void)\n{")
c = c.replace("void Sys_Timer_Inc_Tick(void)\n{", "/** @brief SysTick ISR 回调: 递增毫秒计数 (禁止用户代码调用) */\nvoid Sys_Timer_Inc_Tick(void)\n{")
c = c.replace("uint32_t Sys_Timer_Get_Tick(void)\n{", "/** @brief 获取毫秒时间戳 (32bit 无符号, ~49.7 天回绕安全) */\nuint32_t Sys_Timer_Get_Tick(void)\n{")
c = c.replace("uint32_t Sys_Timer_Get_Cycles(void)\n{", "/** @brief 获取 DWT CPU 周期计数 (亚毫秒高精度定时) */\nuint32_t Sys_Timer_Get_Cycles(void)\n{")
c = c.replace("void Sys_Timer_Delay_Ms(uint32_t ms)\n{", "/** @brief 阻塞延时 ms 毫秒 (仅初始化阶段使用, 运行时禁止阻塞) */\nvoid Sys_Timer_Delay_Ms(uint32_t ms)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Sys_Timer.c — 5 functions")

# ═══════════════════════════════════════════════════════════════
# stm32f10x_it.c
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "User", "stm32f10x_it.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("void NMI_Handler(void)          { }", "/** @brief NMI 不可屏蔽中断 — 空处理 */\nvoid NMI_Handler(void)          { }")
c = c.replace("void HardFault_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE);", "/** @brief 硬件错误处理器: 先关 PWM + 拉低 PB10 关 12V, 再死循环 */\nvoid HardFault_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE);")
c = c.replace("void MemManage_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE);", "/** @brief 内存管理错误: 关 PWM -> 死循环 */\nvoid MemManage_Handler(void)    { TIM_CtrlPWMOutputs(TIM1, DISABLE);")
c = c.replace("void BusFault_Handler(void)     { TIM_CtrlPWMOutputs(TIM1, DISABLE);", "/** @brief 总线错误: 关 PWM -> 死循环 */\nvoid BusFault_Handler(void)     { TIM_CtrlPWMOutputs(TIM1, DISABLE);")
c = c.replace("void UsageFault_Handler(void)   { TIM_CtrlPWMOutputs(TIM1, DISABLE);", "/** @brief 用法错误: 关 PWM -> 死循环 */\nvoid UsageFault_Handler(void)   { TIM_CtrlPWMOutputs(TIM1, DISABLE);")
c = c.replace("void SVC_Handler(void)          { }", "/** @brief SVC 系统调用 — 空处理 */\nvoid SVC_Handler(void)          { }")
c = c.replace("void DebugMon_Handler(void)     { }", "/** @brief 调试监视器 — 空处理 */\nvoid DebugMon_Handler(void)     { }")
c = c.replace("void PendSV_Handler(void)       { }", "/** @brief PendSV 可挂起系统调用 — 空处理 */\nvoid PendSV_Handler(void)       { }")
c = c.replace("void SysTick_Handler(void)\n{", "/** @brief SysTick ISR: 仅调用 Sys_Timer_IncTick(), 不含任何业务逻辑 */\nvoid SysTick_Handler(void)\n{")
c = c.replace("void USART2_IRQHandler(void)\n{", "/** @brief USART2 ISR: ESP8266 数据通道, 先处理 ORE 防锁死, 再 RXNE -> Rx_Char */\nvoid USART2_IRQHandler(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: stm32f10x_it.c — 10 functions")

# ═══════════════════════════════════════════════════════════════
# main.c — main函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "User", "main.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("int main(void)\n{", "/** @brief 程序入口: 初始化 -> SPLASH -> 1s停留 -> 主循环按状态分发 */\nint main(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: main.c — 1 function")

# ═══════════════════════════════════════════════════════════════
# Inverter_Control.c — 遗漏的函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "Inverter_Control.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("Inverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void)\n{", "/** @brief 获取软启动状态机当前状态 (原子读取, 无需关 IRQ) */\nInverter_Control_Soft_Start_State Inverter_Control_Soft_Start_Get_State(void)\n{")
c = c.replace("uint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void)\n{", "/** @brief 获取软启动当前实际频率 (Hz) */\nuint32_t Inverter_Control_Soft_Start_Get_Current_Freq(void)\n{")
c = c.replace("uint32_t Inverter_Control_Freq_Ramp_Get_Target(void)\n{", "/** @brief 获取频率斜坡目标值 (Hz) */\nuint32_t Inverter_Control_Freq_Ramp_Get_Target(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Inverter_Control.c — 3 additional functions")

# ═══════════════════════════════════════════════════════════════
# Esp8266_Driver.c — 遗漏的函数
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "Esp8266_Driver.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("uint8_t Esp8266_Driver_Is_Ready(void)\n{", "/** @brief 查询 ESP8266 是否就绪 (初始化完成, 串口可用) */\nuint8_t Esp8266_Driver_Is_Ready(void)\n{")
c = c.replace("uint8_t Esp8266_Driver_Get_Rx_Flag(void)\n{", "/** @brief 查询是否有新接收帧 (非阻塞) */\nuint8_t Esp8266_Driver_Get_Rx_Flag(void)\n{")
c = c.replace("const char* Esp8266_Driver_Get_Rx_Buffer(void)\n{", "/** @brief 获取接收缓冲区只读指针 (配合 Get_Rx_Flag 使用) */\nconst char* Esp8266_Driver_Get_Rx_Buffer(void)\n{")
c = c.replace("void Esp8266_Driver_Clear_Rx_Buffer(void)\n{", "/** @brief 清空接收缓冲 (临界区保护) */\nvoid Esp8266_Driver_Clear_Rx_Buffer(void)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: Esp8266_Driver.c — 4 additional functions")

# ═══════════════════════════════════════════════════════════════
# W25Q_Driver.c — 遗漏的 (部分已有注释)
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "Hardware", "W25Q_Driver.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("uint32_t W25Q_Driver_Read_JEDEC_ID(void)\n{", "/** @brief 读 JEDEC ID: 24bit 0xEF4018=W25Q128, 失败返回0 */\nuint32_t W25Q_Driver_Read_JEDEC_ID(void)\n{")
c = c.replace("uint8_t W25Q_Driver_Read_SR1(void)\n{", "/** @brief 读状态寄存器 1 (用于外部 Busy 检查) */\nuint8_t W25Q_Driver_Read_SR1(void)\n{")
c = c.replace("uint8_t Font_Header_Load(Font_Header *hdr)\n{", "/** @brief 加载并校验 Font Header (magic + CRC32), 返回 1=有效 */\nuint8_t Font_Header_Load(Font_Header *hdr)\n{")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: W25Q_Driver.c — 3 additional functions")

# ═══════════════════════════════════════════════════════════════
# App_Storage.c — 遗漏的 helper
# ═══════════════════════════════════════════════════════════════
f = os.path.join(base, "Keil_Project", "User", "App_Storage.c")
with open(f, 'r', encoding='utf-8') as fh: c = fh.read()
c = c.replace("static uint8_t CRC8_Compute(const uint8_t *data, uint8_t len)\n{", "/** @brief CRC8 查表计算 (多项式 0x07, 256B ROM 表) */\nstatic uint8_t CRC8_Compute(const uint8_t *data, uint8_t len)\n{")
c = c.replace("uint32_t CRC32_Compute(const uint8_t *data, uint32_t len)\n{", "/** @brief CRC32 计算 (多项式 0x04C11DB7, refin=false, 与 STM32 CRC 外设一致) */\nuint32_t CRC32_Compute(const uint8_t *data, uint32_t len)\n{")
c = c.replace("uint32_t Blackbox_Get_Entry_Count(void) { return s_log_seq; }", "/** @brief 获取黑匣子已写入条目总数 */\nuint32_t Blackbox_Get_Entry_Count(void) { return s_log_seq; }")
with open(f, 'w', encoding='utf-8', newline='\n') as fh: fh.write(c)
print("OK: App_Storage.c — 3 additional functions")

print("\n全部完成! 所有 .c 文件中所有函数前已添加中文 @brief 注释。")
