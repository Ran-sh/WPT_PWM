"""
V4.3.2 注释规范化脚本
将所有 .c/.h/.py 中的注释统一为: 中文为主, 关键术语英文, 统一风格
"""
import glob, os, re

base = r"D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT"

# ═══════════════════════════════════════════════════════════════
# 1. Buzzer_Driver.h + Buzzer_Driver.c
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Buzzer_Driver.h")
with open(f, 'r', encoding='utf-8') as fh:
    content = fh.read()

new_header = """/**
 ******************************************************************************
 * @file    Hardware/Buzzer_Driver.h
 * @brief   有源蜂鸣器驱动 — V4.3.2
 *
 *  接线: PB15 -> R(1k) -> S8050 基极(NPN), 集电极->蜂鸣器->VCC, 发射极->GND
 *        高电平驱动, HIGH=S8050导通=蜂鸣器响, 2.7kHz 有源电磁式
 *        BEEP 模式: 200ms 响 / 800ms 停 (20% 占空比, 足够引起注意但避免持续刺耳)
 ******************************************************************************
 */"""

idx = content.find('/**')
idx2 = content.find('*/', idx)
content = new_header + content[idx2+3:]

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(content)
print("OK: Buzzer_Driver.h")


# ═══════════════════════════════════════════════════════════════
# 2. stm32f10x_it.h
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "User", "stm32f10x_it.h")
with open(f, 'r', encoding='utf-8') as fh:
    content = fh.read()

new_header = """/**
  ******************************************************************************
  * @file    User/stm32f10x_it.h
  * @brief   中断服务函数头文件 — V4.3.2
  *
  *  Cortex-M3 异常向量 + STM32 外设中断向量声明
  *  所有定时调度已迁移至 Sys_Timer 时间戳差值法, 不再依赖 ISR 标志位
  ******************************************************************************
  */"""

idx = content.find('/**')
idx2 = content.find('*/', idx)
content = new_header + content[idx2+3:]

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(content)
print("OK: stm32f10x_it.h")


# ═══════════════════════════════════════════════════════════════
# 3. stm32f10x_it.c — header box already fine, fix function comments
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "User", "stm32f10x_it.c")
with open(f, 'r', encoding='utf-8') as fh:
    content = fh.read()

# Fix HardFault line
content = content.replace(
    "/* 故障处理器: 进入死循环前强制关断 PWM 输出 + 拉低 PB10 关 12V, 防止桥臂直通烧毁 MOSFET */",
    "/* Fault handlers: 先关 PWM + 拉低 PB10 关 12V, 防止桥臂直通烧毁 MOSFET */"
)

# Fix SysTick comment
content = content.replace(
    "/**\n  * @brief  SysTick 中断服务函数 (每 1ms)\n  * @note   唯一操作: 递增系统时基计数器\n  *         不再包含任何任务调度逻辑 (KEY扫描/OLED刷新/LED闪烁 均已迁移至各模块 Task 函数)\n  */",
    "/** @brief  SysTick ISR — 仅 Sys_Timer_IncTick(), 1ms 时基, 不含任何业务逻辑 */"
)

# Fix USART2 IRQ comment
content = content.replace(
    "/**\n  * @brief  USART2 接收中断 (ESP8266 数据通道)\n  * @note   先处理溢出错误 (ORE) 防止中断锁死, 再处理正常接收。\n  *         收到字节注入 ESP8266_RxChar(), 由该函数负责帧拼接和缓冲区管理。\n  */",
    "/** @brief  USART2 ISR — ESP8266 数据通道, 先处理 ORE 防中断锁死, 再 RXNE -> Rx_Char */"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(content)
print("OK: stm32f10x_it.c")


# ═══════════════════════════════════════════════════════════════
# 4. Tft_Driver.h — update header and function comments
# ═══════════════════════════════════════════════════════════════

f = os.path.join(base, "Keil_Project", "Hardware", "Tft_Driver.h")
with open(f, 'r', encoding='utf-8') as fh:
    content = fh.read()

# Fix header
old_hdr = """/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.h
 * @brief   ST7735 128x160 TFT 彩屏显示驱动 — V4.3.2
 * @note    SPI1 分时复用 (PA5=SCK, PA7=MOSI, PA6=DC/MISO 动态, PA4=TFT_CS,
 *          PA12=W25Q128_CS, PA0=RST, PB6=BL)
 *          SPI Mode3, 全双工 (TFT 只写, Flash 读写)
 *          字库: Flash 20897 字 (CRC32) -> ROM 76 字回退
 *          横屏 160x128, RGB565, MADCTL=0xA0
 ******************************************************************************
 */"""

new_hdr = """/**
 ******************************************************************************
 * @file    Hardware/Tft_Driver.h
 * @brief   ST7735 128x160 TFT 彩屏驱动 — V4.3.2
 *
 *  连接: SPI1 分时复用 (PA5=SCK, PA7=MOSI, PA6=DC/MISO 动态切换, PA4=TFT_CS,
 *        PA12=W25Q128_CS 双片选门控, PA0=TFT_RST, PB6=TIM4_CH1 背光 PWM)
 *        SPI Mode3 CPOL=H CPHA=2Edge, 全双工 (TFT 只写不读, Flash 读写)
 *        字库: Flash 20897 字 (CRC32 STM32 refin=false) -> ROM 76 字自动回退
 *        横屏 160x128 RGB565, MADCTL=0xA0, SetWin 偏移 X+1/Y+2
 *        DMA1_Channel3 像素泵送, WrCmd/WrDat 8bit 轮询, PA6 动态 DC/MISO
 ******************************************************************************
 */"""

content = content.replace(old_hdr, new_hdr)

# Fix SPLASH comment
content = content.replace(
    "/** @brief 显示 SPLASH 开机动画 (纯代码: 标题脉冲 + 图标 + 进度条, ~2.85s, ROM 76 字)\n *  @note  Delay_Ms 步进, 不依赖 W25Q Flash, ~4.8s */",
    "/** @brief SPLASH 开机动画 — 背光渐亮 + 两行逐字点亮 ~4.8s, ROM 76 字, 不依赖 W25Q */"
)

with open(f, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write(content)
print("OK: Tft_Driver.h")


print("\nDone! All files updated.")
