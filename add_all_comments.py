import glob, os, re

base = r"D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT"

# ────────────────────────────────────────────────────────────────
# 工具函数: 给 .c 中所有未注释函数前添加中文 @brief 注释
# ────────────────────────────────────────────────────────────────

def patch_file(filepath, replacements):
    """批量替换: {函数签名: 中文注释}"""
    with open(filepath, 'r', encoding='utf-8') as fh:
        c = fh.read()
    for sig, comment in replacements.items():
        if sig in c and comment not in c:
            c = c.replace(sig, comment + '\n' + sig)
    with open(filepath, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(c)
    print(f"OK: {os.path.basename(filepath)} — {len(replacements)} 个函数注释")

# ═══════════════════════════════════════════════════════════════
# Tft_Driver.c — 所有公开函数 + 关键静态函数
# ═══════════════════════════════════════════════════════════════
patch_file(os.path.join(base, "Keil_Project", "Hardware", "Tft_Driver.c"), {
    "void Tft_Driver_Init(void)":       "/** @brief 初始化 ST7735 TFT: 硬件复位+寄存器序列+背光 PWM, 不访问 W25Q */",
    "void Tft_Driver_Font_Init(void)":   "/** @brief 初始化 Flash 字库: 读 Font_Header + CRC32 校验 -> 有效则启用 Flash 全字库路径 */",
    "uint8_t Tft_Driver_Is_Font_Flash_Valid(void)": "/** @brief 查询 Flash 字库是否有效 (1=Flash 20897字, 0=ROM 76字回退) */",
    "void Tft_Driver_Clear(uint16_t color)": "/** @brief 全屏填充单色 (DMA 填充) */",
    "void Tft_Driver_Set_Backlight(uint8_t v)": "/** @brief 设置背光 PWM 占空比 (0=灭, 255=最亮, TIM4_CH1) */",
    "void Tft_Driver_Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)": "/** @brief 像素级填充矩形 (坐标+宽高, 含边界裁剪) */",
    "void Tft_Driver_Erase_Pixel_Area(uint16_t x, uint16_t y, uint16_t w, uint16_t h)": "/** @brief 黑底擦除指定像素区域 (等价 Fill_Rect 黑色) */",
    "static void Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg, uint16_t* out)": "/** @brief 解码 1 字节 ASCII 字模行 -> 8 像素 (LSB-first) */",
    "static void Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg, uint16_t* out)": "/** @brief 解码 2 字节 CJK 字模行 -> 16 像素 (LSB-first) */",
    "void Tft_Driver_Show_Char(uint8_t line, uint8_t col, char ch,": "/** @brief 在指定行列绘制 ASCII 字符 (双路径: Flash 流式 / ROM 字模回退) */",
    "void Tft_Driver_Show_String(uint8_t line, uint8_t col, const char* s,": "/** @brief 绘制 ASCII 字符串 (自动逐字, 超行截断) */",
    "void Tft_Driver_Show_Num(uint8_t ln, uint8_t col, uint32_t v,": "/** @brief 绘制无符号整数 (右对齐, 前导空格) */",
    "void Tft_Driver_Show_Float(uint8_t ln, uint8_t col, float v,": "/** @brief 绘制浮点数 (右对齐, 指定整数+小数位数) */",
    "void Tft_Driver_Show_CN_String(uint8_t ln, uint8_t col, const char* s,": "/** @brief 绘制中英文混合字符串 (自动识别 UTF-8 中文 + ASCII) */",
    "void Tft_Driver_Draw_WiFi_Icon(uint16_t x, uint16_t y, uint8_t frame, uint16_t fg, uint16_t bg)": "/** @brief 绘制 16x16 WiFi 信号动画图标 (frame: 0~3 逐帧扩散) */",
    "void Tft_Driver_Draw_Single_Icon(uint16_t x, uint16_t y, const uint8_t data[32],": "/** @brief 绘制 16x16 单帧图标 (32 字节 LSB-first 位图) */",
    "void Tft_Driver_Show_5x10_String_Pixel(uint16_t x, uint16_t y,": "/** @brief 绘制 5x10 微型数字字符串 (像素坐标, DMA 发送) */",
    "void Tft_Driver_Draw_Icon_By_Id(uint16_t x, uint16_t y, uint8_t icon_id,": "/** @brief 按 icon_id 绘制 16x16 图标 (11=BATTERY ~ 30=CLOCK) */",
    "void Tft_Driver_Show_Splash(void)": "/** @brief SPLASH 开机动画: 背光渐亮 + 无 线 充 电 / WPT 逐字点亮 ~2.0s */",
})

# ═══════════════════════════════════════════════════════════════
# App_Storage.c
# ═══════════════════════════════════════════════════════════════
patch_file(os.path.join(base, "Keil_Project", "User", "App_Storage.c"), {
    "static void App_Storage_Defaults(App_Storage_Config *cfg)": "/** @brief 写入出厂安全默认值 (SSID/密码清零, 100kHz 默认频率) */",
    "uint8_t App_Storage_Load_Config(App_Storage_Config *cfg)": "/** @brief 从 Flash 双副本加载参数配置 (A 优先, A坏读取B, 全坏回默认) */",
    "void App_Storage_Save_Config(const App_Storage_Config *cfg)": "/** @brief 保存参数配置到 Flash 双副本 (先写 A 再写 B, 保持至少一份有效) */",
    "void App_Storage_Write_Factory_Defaults(void)": "/** @brief 恢复出厂设置并写入 Flash */",
    "static void Blackbox_Pack(float v, float i, uint16_t freq, uint8_t state,": "/** @brief 封装黑匣子条目: 浮点参数 -> 14B 紧凑二进制 + CRC8 校验 */",
    "void Blackbox_Log_Tick(float v, float i, uint16_t freq, uint8_t state)": "/** @brief 黑匣子定时记录 (1s/条, SWEEP+RUNNING 状态, 循环写入) */",
    "void Blackbox_Lock_Fault_Snapshot(void)": "/** @brief 故障锁存: 保留故障前后 5s 窗口数据, 禁止循环覆盖 */",
    "void App_Storage_Init(void)": "/** @brief 初始化存储层: 恢复黑匣子写指针 (从 Flash Block 0 头部) */",
    "uint8_t Blackbox_Read_Entry(uint32_t index, Blackbox_Entry_Packed *out)": "/** @brief 按索引读取黑匣子条目 (0=最旧) */",
})

# ═══════════════════════════════════════════════════════════════
# App_Network.c
# ═══════════════════════════════════════════════════════════════
patch_file(os.path.join(base, "Keil_Project", "User", "App_Network.c"), {
    "uint8_t App_Network_Start_Connect(void)": "/** @brief 启动联网流程: IDLE -> WIFI -> MQTT -> ONLINE (非阻塞) */",
    "void App_Network_Manual_Connect(void)": "/** @brief 手动连接: 复位 OFFLINE 标志 -> 重启联网状态机 */",
    "void App_Network_Manual_Disconnect(void)": "/** @brief 手动断开: 进入 OFFLINE_ACTIVE 模式, 需手动恢复 */",
    "void App_Network_Task(void)": "/** @brief 网络周期任务: 驱动 WiFi 状态机 + MQTT 心跳 + 离线恢复嗅探 */",
    "uint8_t App_Network_Is_Connected(void)": "/** @brief 查询是否在线 (ONLINE 状态) */",
    "uint8_t App_Network_Is_Offline(void)": "/** @brief 查询是否离线 (PASSIVE 或 ACTIVE) */",
    "uint8_t App_Network_Is_Connecting(void)": "/** @brief 查询是否正在连接 (WIFI 或 MQTT 状态) */",
    "uint8_t App_Network_Get_Connect_Status(void)": "/** @brief 获取当前连接状态枚举值 */",
})

# ═══════════════════════════════════════════════════════════════
# W25Q_Driver.c — 补充未注释的公开函数
# ═══════════════════════════════════════════════════════════════
patch_file(os.path.join(base, "Keil_Project", "Hardware", "W25Q_Driver.c"), {
    "void W25Q_Enter_Mode(void)": "/** @brief 进入 Flash 独占模式: PA6->MISO + CS=L, 独占 SPI 总线 */",
    "void W25Q_Leave_Mode(void)": "/** @brief 退出 Flash 独占模式: CS=H + PA6->DC, 释放总线归还 TFT */",
    "void W25Q_SPI_8bit(void)": "/** @brief SPI1 -> 8bit 帧模式 (原子闪切 DFF 位) */",
    "void W25Q_Wait_Busy_Timeout(void)": "/** @brief 阻塞等待 W25Q128 Busy 位清零 (超时护底) */",
    "void W25Q_Driver_Read(uint32_t addr, uint8_t *buf, uint16_t len)": "/** @brief 通用 SPI 读: 任意地址任意长度 (0x03 命令, 轮询) */",
    "void W25Q_Driver_Write_Page(uint32_t addr, const uint8_t *buf, uint16_t len)": "/** @brief 页写入: <=256B, 自动发写使能+等 Busy (调用方保证不跨页) */",
    "void W25Q_Driver_Erase_Sector(uint32_t addr)": "/** @brief 扇区擦除: 4KB (0x20), 发波态硬件拦截, 阻塞 ~45ms */",
    "uint32_t W25Q_Font_Index_Binary_Search(uint16_t unicode, const Font_Header *hdr)": "/** @brief 总线独占二分搜索 CJK 字模索引 (6763 条 Unicode 升序, 5.85us/字) */",
})

print("\n全部完成!")
