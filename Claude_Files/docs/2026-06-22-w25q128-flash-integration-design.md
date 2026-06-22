# W25Q128 SPI NOR Flash 集成设计方案

> 版本: V4.3.0-DRAFT | 日期: 2026-06-22 | 状态: 设计完成，待审批

## 1. 项目背景与目标

STM32F103C8T6 当前 64KB Flash / 20KB SRAM 已接近极限。本项目引入 Winbond W25Q128 (128Mbit / 16MB SPI NOR Flash)，通过 SPI1 分时复用，实现三大核心能力：

| 目标 | 描述 | 优先级 |
|:---|:---|:---|
| 📚 全字库 UI | GB2312 全字库外挂 + 开机全彩动画，释放片内 ROM | P0-P2 |
| 📊 黑匣子日志 | 离线断网时循环记录运行参数，故障时锁存现场 | P4 |
| ⚙️ 参数持久化 | WiFi 配网凭证 + 硬件校准值掉电保存，免配网冷启动 | P3 |

## 2. 硬件接线

### 2.1 SPI1 分时复用引脚

```
STM32F103C8                    W25Q128 (SOIC-8)
─────────────────────────────────────────────────
PA5  (SPI1_SCK)   ──────────── CLK    (Pin 6)
PA7  (SPI1_MOSI)  ──────────── DI     (Pin 5)
PA6  (SPI1_MISO)  ──────────── DO     (Pin 2)
PA12 (GPIO)       ──────────── /CS    (Pin 1)
─────────────────────────────────────────────────
VCC  (3.3V)       ──────────── /WP    (Pin 3)   上拉, 不写保护
VCC  (3.3V)       ──────────── /HOLD  (Pin 7)   上拉, 不暂停
VCC  (3.3V)       ──────────── VCC    (Pin 8)
GND               ──────────── GND    (Pin 4)
```

### 2.2 PA6 引脚角色切换

| 访问目标 | PA6 模式 | 选中 | 说明 |
|:---|:---|:---|:---|
| TFT 写命令 | GPIO_Out_PP (DC=0) | PA4=L | TFT 命令模式 |
| TFT 写数据 | GPIO_Out_PP (DC=1) | PA4=L | TFT 数据/DMA 模式 |
| W25Q128 通信 | GPIO_Input (MISO) | PA12=L | Flash 读取 |
| 空闲 | GPIO_Out_PP (保持) | PA4=H, PA12=H | 默认 TFT 就绪 |

### 2.3 引脚冲突释放

| 释放引脚 | 原功能 | 新功能 | 代价 |
|:---|:---|:---|:---|
| PA12 | LED_TEMP (温度指示灯) | W25Q128_CS | 释放 1 个 LED (原无实际传感器) |

### 2.4 总线拓扑

```
                     ┌──────────────────────────┐
                     │     STM32F103C8T6        │
                     │                          │
                     │  PA5 ── SCK  ──┬──── TFT_SCK
                     │  PA7 ── MOSI ──┼──── TFT_SDA
                     │                │    ┌─ W25Q128_DI
                     │  PA6 ── GPIO ──┤    │
                     │         (动态切)└────┼─ W25Q128_DO (读时)
                     │                     │
                     │  PA4 ── GPIO ─────── TFT_CS
                     │  PA12 ─ GPIO ─────── W25Q128_CS
                     └──────────────────────────┘
```

**CS 门控是核心安全机制**: PA4 和 PA12 绝不同时为低，同一时刻仅一个从设备驱动总线。

## 3. Flash 内存分区 (16MB)

```
W25Q128  16MB (0x000000 ~ 0xFFFFFF)
┌─────────────────────────────────────────────────────────────────┐
│  分区            │  起始地址    │  大小       │  擦除粒度        │
├─────────────────────────────────────────────────────────────────┤
│ 📚 全字库区      │ 0x000000    │   2 MB     │  4KB 扇区         │
│ 🖼️ 开机画面区    │ 0x200000    │   1 MB     │  4KB 扇区         │
│ ⚙️ 参数配置 A     │ 0x300000    │   4 KB     │  4KB 扇区 (单)    │
│ ⚙️ 参数配置 B     │ 0x301000    │   4 KB     │  4KB 扇区 (双副本) │
│ 📊 黑匣子日志    │ 0x310000    │   4 MB     │  64KB 块擦除       │
│ ── 预留 ──      │ 0x710000    │   9 MB     │  —                │
└─────────────────────────────────────────────────────────────────┘
```

### 3.1 📚 全字库区 (0x000000 ~ 0x200000, 2MB)

```
0x000000 ┌──────────────────────────────────┐
         │ 字库头部 (32B)                     │
         │  Magic:    0x574B ("WK")      2B  │
         │  Version:  uint16_t            2B  │
         │  CRC32:    uint32_t            4B  │
         │  ASCII_Size:    uint32_t       4B  │
         │  CJK_Base:      uint32_t (Unicode起始) │
         │  CJK_Count:     uint32_t (码点数) │
         │  Reserved:      12B               │
0x000020 ├──────────────────────────────────┤
         │ ASCII 8×16 字模 (95字符)  ~1.5KB │
         │ ASCII 5×10 微数字 (12字符) ~0.1KB│
0x000700 ├──────────────────────────────────┤
         │ CJK 基础区 (U+4E00~U+9FFF)       │
         │  20902 码位 × 32B = 668KB        │
         │  GB2312 全部 6763 汉字分布其中    │
         │  空白码位存 0x00 填充             │
0x0A7800 ├──────────────────────────────────┤
         │ 图标动画包                        │
         │  WiFi ×10帧 + MQTT ×6帧 + Star   │
         │  预留扩展图标 ~32KB               │
0x0B0000 ├──────────────────────────────────┤
         │ 基础中文 76 字 (兼容现有)          │
         │  16×16 兼容原有 CN_INDEX 格式     │
         ├──────────────────────────────────┤
         │ ... 预留空间                      │
0x200000 └──────────────────────────────────┘
```

**CJK 字模布局策略**: 按 Unicode 码点连续排列 (U+4E00 → 偏移0, U+9FFF → 偏移 668KB)。GB2312 的 6763 字分散其中，中间有大量空白码位(日韩/生僻字)。选择存全 20902 码位(668KB) 而非仅存 6763 字(216KB)，因为连续索引公式极简，无需查表。

**查字公式** (UTF-8 → Flash 物理地址, MCU 侧代码):

```c
/* UTF-8 3字节 → Unicode 码点 */
uint16_t cp = ((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);

if (cp >= 0x4E00 && cp <= 0x9FFF) {
    /* 直接计算 Flash 偏移, 零 RAM 查表 */
    uint32_t flash_addr = FONT_CJK_BASE + (cp - 0x4E00) * 32;
    W25Q_Driver_Read(flash_addr, buf, 32);
    /* buf 直接送入现有 Decode_CN_Row() — PC 端已做 bit_reverse, MCU 零改动 */
}

if (cp < 0x80) {
    /* ASCII: 同样直算 */
    flash_addr = FONT_ASCII_BASE + cp * 16;
}
```

**关键**: 所有旋转重标定（HZK16 MSB→LSB bit_reverse + GB2312→Unicode 区位映射）均在 **PC 端 Python 脚本** 完成。MCU 侧代码与现有 `Decode_CN_Row()` 完全兼容，零适配改动。

**性能**: 4 条位运算 + 1 次乘法 + 1 次加法 → Flash 地址。零 RAM 占用，零循环。

### 3.2 🖼️ 开机画面区 (0x200000 ~ 0x300000, 1MB)

```
一帧全彩 RGB565 = 160 × 128 × 2 = 40,960 字节 ≈ 40KB
1MB = 可存最多 25 帧全彩画面

0x200000 ┌──────────────────────────────────┐
         │ Frame 0: 品牌 Logo        40KB   │
         │ Frame 1: 启动动画-帧1     40KB   │
         │ Frame 2: 启动动画-帧2     40KB   │
         │ Frame 3: 启动动画-帧3     40KB   │
         │ ...                               │
         │ Frame N: 仪表盘静态背景   40KB   │
0x300000 └──────────────────────────────────┘
```

泵送方式: DMA 从 W25Q128 读 → 经 SPI1 → STM32 缓冲 → SPI1 → TFT。一帧 40KB 在 18MHz 下约 18ms。

### 3.3 ⚙️ 参数配置区 (0x300000 ~ 0x302000, 8KB 双副本)

```
Sector A (0x300000)                    Sector B (0x301000)  ← 偏移 4KB, 不同物理页
┌────────────────────────┐            ┌────────────────────────┐
│ Magic: 0x57434647  (4B)│            │ Magic: 0x57434647  (4B)│
│ Version: uint32    (4B)│            │ Version: uint32    (4B)│
├────────────────────────┤            ├────────────────────────┤
│ WiFi 配网凭证:          │            │        同 A             │
│  SSID        [32B]     │            │                        │
│  Password    [64B]     │            │                        │
│  MQTT_Key    [64B]     │            │                        │
├────────────────────────┤            ├────────────────────────┤
│ 硬件校准:              │            │        同 A             │
│  ADC_I_Offset  float   │            │                        │
│  ADC_V_Gain    float   │            │                        │
│  Freq_Trim_Hz  int32   │            │                        │
├────────────────────────┤            ├────────────────────────┤
│ 系统偏好:              │            │        同 A             │
│  DefaultFreq   uint16  │            │                        │
│  Backlight     uint8   │            │                        │
│  Language      uint8   │            │                        │
├────────────────────────┤            ├────────────────────────┤
│ CRC32           (4B)   │            │ CRC32           (4B)   │
└────────────────────────┘            └────────────────────────┘
```

**双副本写入策略**: 写 A → 验证 A CRC → 写 B → 验证 B CRC。任何一步失败可恢复。

**上电加载策略**:

```c
if (验A)     → 用A
else if (验B) → 用B
else          → 恢复出厂安全默认值 (150kHz待机, 拒绝加载任何未知数据)
```

**安全默认值**: 频率 = 150kHz (安全高起点), Switch = OFF, PWM = DISABLED。

### 3.4 📊 黑匣子循环日志区 (0x310000 ~ 0x710000, 4MB)

#### 单条日志结构 (14字节 紧凑二进制)

```
┌────────┬────────┬────────┬────────┬──────────┬────────┐
│ 时间戳  │ V_EMA  │ I_EMA  │ Freq_Hz│ SysState │  CRC8  │
│ uint32 │ uint16 │ uint16 │ uint16 │  uint8   │ uint8  │
│ 4B     │ 2B     │ 2B     │ 2B     │  1B      │ 1B     │
└────────┴────────┴────────┴────────┴──────────┴────────┘
  [0-3]    [4-5]    [6-7]    [8-9]     [10]      [11]
```

#### 存储策略

```
200ms 间隔 → 5条/秒 → 420B/秒 → 18KB/小时
4MB = 可存 ~299,000 条 ≈ 16.6 小时连续记录

0x310000 ┌──────────────────────────────────┐
         │ Block 0: 日志区头部                │
         │  写指针 (uint32_t)                 │
         │  故障锁存地址 (uint32_t)           │
         │  循环次数 (uint32_t)               │
0x310100 ├──────────────────────────────────┤
         │ Block 1-63: 循环日志体            │
         │  每块 64KB (约 4680条/块)         │
         │  写满 → 回到 Block 1 覆盖最旧    │
         │                                   │
         │  ...                              │
0x708000 ├──────────────────────────────────┤
         │ 故障锁存保护区 (最后 4 块, 256KB)  │
         │  过流触发时冻结, 永不覆盖          │
         │  预触发5s(25条) + 后5s(25条)      │
0x710000 └──────────────────────────────────┘
```

#### 触发条件

仅在 `SYS_STATE_SWEEP` 或 `SYS_STATE_RUNNING` 时启用日志。IDLE/FAULT 时暂停。

#### Page Program 跨页保护

W25Q128 页写不自动跨页: 起始地址 0x3100FA → 写入 14B → 0x310108 超出页尾 0x3100FF，硬件绕回页头 0x310000 洗掉头部数据。

**解法规约**: 每次写前检测本页剩余空间，不足 14B 时跳至下页头。

```c
if ((current_addr & 0xFF) + 14 > 256) {
    current_addr = (current_addr & ~0xFF) + 256;  /* 跳到下页 */
}
if (current_addr >= BLACKBOX_END) current_addr = BLACKBOX_START;
```

### 3.5 访问频率与主循环影响分析

| 操作 | 触发时机 | 数据量 | 耗时(18MHz) | 频率 |
|:---|:---|:---|:---|:---|
| 字模读取 | 页面绘制时 | ~32B/字 | ~20μs/字 | 换页瞬间 |
| 黑匣子写 | SYS_RUNNING/SWEEP 200ms | 14B | ~30μs | 5Hz |
| 参数读写 | 上电/配网保存 | ~256B | ~150μs | 极少 |
| 开机画面 | 仅上电一次 | 40KB | ~18ms | 1次/开机 |
| 字库 CRC 自检 | 仅上电一次 | 2MB | ~200ms | 1次/开机 |

**结论**: 最频繁的黑匣子写入仅占 200ms 周期的 0.015%。对 144241 周期互质采样和 TIM1 原子频率斜坡 **无时序影响**。

## 4. 三级校验防御体系

> 核心理念: 加密不需要 (算力内耗), 校验是生死线 (EMI 毛刺可能翻转 SPI 总线比特位, 导致频率误读→逆变桥炸管)。

### 4.1 配置参数区 — 双副本 CRC32 闭锁

| 威胁 | 防御 |
|:---|:---|
| 擦写中途断电 → 数据损坏 | 双副本 A→B 顺序写入, 始终有一个完好 |
| EMI 干扰读操作 → 比特翻转 | 上电 CRC32 校验, 坏副本自动切备用 |
| 双副本全坏 (极少) | 恢复出厂安全默认 (150kHz, OFF, PWM禁用) |

### 4.2 黑匣子日志 — 行内 CRC8 动态校验

多项式: `0x07` (x^8 + x^2 + x + 1), 查表法 256B 表, 3 条机器指令/字节。

EEPROM 仿真后备: 若 STM32 内部 Flash 有空余页, 在故障锁存的同时将 50 条关键日志 (700B) 备份到 STM32 片内 Flash 的备份页 (BKP + Flash 模拟 EEPROM)。

### 4.3 字库区 — 上电一次性 CRC32 自检

仅 `SYS_STATE_INIT` 阶段执行一次。运行时读字模零额外校验开销。Magic Number `0x574B` 做前置快速判断, CRC32 做完整验证。

## 5. 软件驱动层架构

### 5.1 新增/修改文件

```
Keil_Project/Hardware/
├── W25Q_Driver.c/h          ← 新增: SPI Flash 底层驱动
├── Tft_Driver.c/h           ← 修改: 字模来源切换为 W25Q_Driver_Read()

Keil_Project/User/
├── App_Storage.c/h           ← 新增: 分区管理 (字库/参数/黑匣子)
├── Sys_Core.c                ← 修改: Sys_Post_Init() 读 Flash 参数
├── App_Network.c             ← 修改: 断网时写入黑匣子
├── main.c                    ← 修改: 初始化调用 W25Q_Driver_Init()

TFT_Font_Data.h               ← 精简: 可全部删除, 数据迁移至 Flash
```

### 5.2 W25Q_Driver 公开接口

```c
/* 初始化: 配置 SPI1/GPIO, 读 JEDEC ID 校验芯片存在 */
void     W25Q_Driver_Init(void);

/* 通用读: 任意地址, 任意长度 (0x03 Read Data, 轮询模式) */
void     W25Q_Driver_Read(uint32_t addr, uint8_t *buf, uint16_t len);

/* 页写: ≤256B, 调用方已保证不跨页 (0x02 Page Program) */
void     W25Q_Driver_Write_Page(uint32_t addr, const uint8_t *buf, uint16_t len);

/* 扇区擦除: 4KB (0x20), 阻塞 ~45ms, 仅配网/校准场景使用 */
void     W25Q_Driver_Erase_Sector(uint32_t addr);

/* PA6 模式切换 (内部调用, 不公开) */
static inline void W25Q_Enter_Mode(void);   /* PA6 → Input, PA12=L */
static inline void W25Q_Leave_Mode(void);   /* PA12=H, PA6 → GPIO_Out */
```

### 5.3 App_Storage 公开接口

```c
/* 分区管理 */
void     App_Storage_Init(void);            /* 上电校验所有分区 */
uint8_t  App_Storage_Load_Config(sys_config_t *cfg);
void     App_Storage_Save_Config(const sys_config_t *cfg);

/* 字库 */
uint8_t  App_Storage_Check_Font(void);      /* 返回: 0=缺失 1=完好 2=损坏 */
void     App_Storage_Read_Glyph(uint16_t unicode, uint8_t *buf_32b);

/* 黑匣子 */
void     Blackbox_Log_Tick(float v, float i, uint16_t freq, uint8_t state);
void     Blackbox_Lock_Fault_Snapshot(void); /* 过流时锁存前后各5秒 */
```

### 5.4 SPI1 通信安全约束

**读操作**: 使用 SPI 轮询模式 (非 DMA)。SPI1 的 DMA 通道与 TFT 共用, 读方向 MISO 与 TFT 的 MOSI-only 模式不兼容。

**写/擦除操作**: 写操作前检查 `g_sys_state != SYS_STATE_SWEEP && g_sys_state != SYS_STATE_RUNNING`。运行态禁止擦除 (45ms 阻塞会打断 PWM 实时控制)。

**中断安全**: 读 Flash 期间 USART2 RX ISR 仍正常运行 (NVIC 优先级未变)。W25Q_Driver_Read() 可被 ISR 打断, 但 SPI 总线本身由 CS 硬件门控, 不会有数据冲突。

## 6. 字模旋转重标定与 PC 端工具

### 6.1 取模格式差异分析

当前 TFT 驱动（`Tft_Driver.c`）与标准 HZK16 字库的取模方式存在关键差异：

```
当前 CN_FONT_16X16 格式 (PCtoLCD2002 行主序 LSB-first):
  每行 2字节: lo(左8列) + hi(右8列)
  解码: out[b] = (lo & (0x01 << b)) ? fg : bg;
        bit0 → 该组最左像素, bit7 → 该组最右像素

标准 HZK16 格式 (MSB-first):
  每行 2字节: left_byte + right_byte
  解码: bit7 → 该组最左像素, bit0 → 该组最右像素
```

**对比图示 (单行, 以 lo 字节为例)**:

```
当前 LSB-first (PCtoLCD2002):
  lo byte:  [b0][b1][b2][b3][b4][b5][b6][b7]
  像素映射:  左←──────────────────────────→右
  即 b0=最左列, b7=第8列

HZK16 MSB-first:
  byte:     [b7][b6][b5][b4][b3][b2][b1][b0]
  像素映射:  左←──────────────────────────→右
  即 b7=最左列, b0=第8列
```

**结论**: HZK16 每个字节需要做 **8-bit 位反转** 才能与现有 Decode_CN_Row() 兼容。

```
HZK16 byte 0b11000001 → bit_reverse → 0b10000011 → 现有解码器可正确渲染
```

### 6.2 开机画面帧方向标定

MADCTL=0xA0 (横屏 160×128) 下 RGB565 数据的扫描顺序:

```
TFT 显示坐标系 (MADCTL=0xA0):
  X: 0→159 (左→右)
  Y: 0→127 (上→下)
  
  DMA 泵送顺序: (Y=0,X=0) (Y=0,X=1) ... (Y=0,X=159)
                 (Y=1,X=0) (Y=1,X=1) ... (Y=1,X=159)
                 ...
                 (Y=127,X=0) ... (Y=127,X=159)
  
  = 行主序, 从左到右, 从上到下 = 20480 个 RGB565 半字
```

**PC 端生成要求**: 源 PNG 设计为 160×128 横屏画面 → Image2Cpp 选择"水平扫描"→ 生成 20480 个 RGB565 值 → 按顺序写入 Flash，运行时 DMA 直接泵送，无需软件旋转。

### 6.3 工具链与生成流程

```bash
Claude_Files/tools/
├── flash_font_builder.py       ← 新增: 字库转换 + 镜像打包
│   输入: HZK16 (238KB 原始文件)
│   处理: 区位码→Unicode→20902码位网格展开 → 逐字节 bit_reverse → 插入 Magic+CRC32头部
│   输出: flash_font.bin (668KB)
│
└── flash_splash_builder.py     ← 新增: 开机画面帧打包
    输入: splash_*.png (160×128 RGB)
    处理: PNG→RGB565 raw (Image2Cpp 或 Python Pillow)
    输出: flash_splash.bin (5帧=200KB)
```

**字库转换核心逻辑**:

```python
def bit_reverse(byte):
    """HZK16 MSB-first → TFT LSB-first"""
    result = 0
    for i in range(8):
        if byte & (1 << i):
            result |= (1 << (7 - i))
    return result

def build_font_image(hzk16_path, output_path):
    with open(hzk16_path, 'rb') as f:
        hzk16 = f.read()  # 238KB, 6763+682=7445 字符, 每字32B

    # 头部: 32B
    header = struct.pack('<HHI III',
        0x574B,     # Magic "WK"
        1,           # Version
        crc32_placeholder,  # 后续回填
        1520,        # ASCII_Size
        0x4E00,      # CJK_Base (Unicode 一)
        20902        # CJK_Count (U+4E00~U+9FFF)
    )

    # CJK 字模: Unicode 码点连续展开
    # GB2312区位码 → Unicode 映射表 (查标准转换表)
    cjk_buf = bytearray(20902 * 32)  # 668KB, 初始全0
    for gb_idx, char_32b in enumerate(hzk16_chars):
        unicode_cp = gb2312_to_unicode(gb_idx)  # 查表
        offset = (unicode_cp - 0x4E00) * 32
        # 逐字节 bit_reverse
        for j in range(32):
            cjk_buf[offset + j] = bit_reverse(char_32b[j])

    # 回填 CRC32
    crc = crc32(header[8:] + cjk_buf)
    header[4:8] = struct.pack('<I', crc)

    with open(output_path, 'wb') as f:
        f.write(header)      # 32B
        f.write(ascii_data)  # ~1.5KB
        f.write(micro_data)  # ~0.1KB
        f.write(cjk_buf)     # 668KB
        f.write(icon_data)   # ~32KB (WiFi/MQTT/Star)
```

### 6.4 GB2312 区位码 → Unicode 映射

由于每个汉字要从 HZK16 的 GB2312 区位索引映射到 Unicode 码点，需要一个 7445 项的转换表。该表在 **PC 端 Python 生成脚本中使用**（不在 MCU 上），可从标准 GB2312→Unicode 对照表导入。

**MCU 侧无需此映射表** — MCU 直接用 UTF-8 码点计算 Flash 偏移，映射已经在生成镜像时通过 Unicode 连续排列消解了。

### 6.5 烧录命令

```bash
# 生成完整镜像 (字库 + 开机画面 + 空白区)
python flash_font_builder.py   --hzk16 HZK16 --out flash_font.bin
python flash_splash_builder.py --frames splash_f0.png splash_f1.png ... --out flash_splash.bin

# 合并为单一镜像
cat flash_font.bin flash_splash.bin > w25q128_image.bin
# 剩余空间填充 0xFF

# 烧录 (CH341A)
# ch341a v1.45: 检测芯片 → 擦除 → 打开 w25q128_image.bin → 编程 → 校验
```

## 7. 实现分阶段计划

| Phase | 内容 | 依赖 | 预期工时 | 风险 |
|:---|:---|:---|:---|:---|
| **P0** | W25Q_Driver 基础驱动 + PA6 切换 + JEDEC 验证 | 硬件接线完成 | 核心基础 | SPI1 分时复用稳定性 |
| **P1** | 将现有字库/图标迁移到 Flash + Tft_Driver 改造 | P0 | 中等 | 现有 UI 行为不变 (回归) |
| **P2** | GB2312 全字库烧录 + Unicode 查字算法 | P1 + PC工具 | 中等 | 首次需烧录 668KB |
| **P3** | 参数配置区双副本 + Sys_Post_Init 改造 | P0 | 较小 | 配网流程兼容性 |
| **P4** | 黑匣子循环日志 + 故障锁存 + CRC8 | P0 | 中等 | Page Program 跨页保护 |
| **P5** | 开机全彩动画 | P1 | 较小 | DMA 链式传输调试 |

## 8. 风险与回退

| 风险 | 缓解 |
|:---|:---|
| SPI1 分时复用 PA6 切换引入毛刺 | CS 门控确保仅一个从设备在线; PA6 先切模式再拉 CS |
| W25Q128 焊接不良/虚焊 | JEDEC ID 校验 (0xEF4018) 在 Init 时必过, 失败则 LED 故障指示 |
| 字库 CRC 校验不通过 | 回退 ASCII Only 模式, TFT 显示 "字库故障" 错误码 |
| 配置双副本全坏 | 加载出厂安全默认参数, 不加载任何未知值 |
| Flash 擦除途中断电 | 双副本交替写, 先写新再擦旧, 确保始终有 1 份有效 |

## 9. 决断记录

### 9.1 字库 CRC32 自检 200ms — 接受开机同步自检

**决断**: 开机同步自检，无需做异步懒加载。

**理由**: 系统在 `SYS_STATE_INIT` 阶段本身就要进行电容充电准备和 ESP8266 冷启动上电序列（~4s BOOT_WAIT），屏幕停留在"启动中..."欢迎页。开机多花 200ms 做字库自检，能把"运行中因读取损坏字库导致总线卡死"的隐患在开机前 100% 拦截，对工业安全极其划算。200ms 完全被 ESP8266 4s 等待窗口吸收。

### 9.2 开机动画帧数 — 5 帧循环极简动画

**决断**: 5 帧循环的极简轻量动画，不做长动画。

**理由**: 单帧全彩 RGB565 高达 40KB，在 18MHz 速率下 MCU 搬运和转发需要消耗总线开销。动画唯一目的是让用户知道系统没死机，5 帧循环配合 Sys_Timer 做步进淡入，在质感和效率之间取得平衡，同时腾出富余的 9MB 预留空间。

### 9.3 ADC 校准值 — 保留本地自测算为 B 方案

**决断**: Flash 固化校准值作为主方案，保留 `Adc_Driver_Calibrate_Offset()` 作为末级 B 方案。

**理由**: 当系统触发参数区全坏恢复出厂安全默认值时，Flash 内部的零点校准浮点数（ADC_I_Offset）会被一同清空。此时单片机必须能通过 `if-else` 分支立刻无缝退回到原有的本地上电零点自测算，保证系统在完全失去外部存储时依然具备安全防线。

```c
/* 加载 ADC 校准值: Flash 优先 → 本地自测算降级 */
if (cfg_valid && cfg.adc_i_offset != 0.0f) {
    s_i_offset = cfg.adc_i_offset;           /* Flash 固化值 */
} else {
    Adc_Driver_Calibrate_Offset();           /* 降级: 上电自测算 */
    cfg.adc_i_offset = s_i_offset;           /* 回写 Flash 供下次使用 */
    App_Storage_Save_Config(&cfg);
}
```

## 10. GitHub 适配资源清单

### 10.1 字库资源

| 资源 | 仓库/来源 | 用途 |
|:---|:---|:---|
| **zhangjirui/Dot-matrix-font** | <https://github.com/zhangjirui/Dot-matrix-font> | HZK16（宋体）、HZK16F（繁体）、HZK16S（美术体）多风格 16×16 点阵字库文件 |
| **evildao/GB2312Font** | <https://github.com/evildao/GB2312Font> | GB2312 汉字字符集 + Unicode 映射表，可直接构建软字库 |
| **HZK16 标准字库** | 标准文件 `HZK16` (~238KB) | 6763 汉字 + 682 符号，GB2312-80 区位码索引，每字 32B |
| **ASCII 字库** | 同上仓库 ASC16 (~4KB) | 95 可打印 ASCII，8×16 点阵，兼容现有 TFT_Font_Data.h 格式 |

### 10.2 Flash 驱动参考

| 资源 | 仓库/来源 | Stars | 用途 |
|:---|:---|:---|:---|
| **libdriver/w25qxx** | <https://github.com/libdriver/w25qxx> | 1.2k+ | ⭐ 推荐 — MISRA 合规全系列驱动，4 层分层架构，指令表+状态机逻辑可直接复用 |
| **nimaltd/spif** | <https://github.com/nimaltd/spif> | 630+ | W25Qxx/N25Qxx 全系列 HAL 驱动，社区最活跃，API 简洁 |
| **nimaltd/w25qxx** | <https://github.com/netube99/w25qxx> | — | spif 的前身/镜像版本 |
| **maudeve-it/W25Qxxx_SPI_FLASH_STM32-CompactEL** | <https://github.com/maudeve-it/W25Qxxx_SPI_FLASH_STM32-CompactEL> | — | 精简版 11-13KB RAM，支持 TouchGFX 集成 + External Loader |
| **FT9R/w25qxx** | <https://github.com/FT9R/w25qxx> | — | 轻量交叉编译版本，接口简洁 |
| **sie-foss/w25qxx** | <https://codeberg.org/sie-foss/w25qxx> | — | Codeberg 社区优化版：mutex 线程安全 + smart overwrite + 坏块检测 |

**libdriver/w25qxx 架构要点**:

```c
/* 接口抽象层 — 只需实现 5 个平台函数 */
typedef struct w25qxx_handle_s {
    uint8_t (*spi_qspi_init)(void);
    uint8_t (*spi_qspi_deinit)(void);
    uint8_t (*spi_qspi_write_read)(/* ... */);  /* 统一 SPI/QSPI 收发 */
    void (*delay_ms)(uint32_t ms);
    void (*delay_us)(uint32_t us);
    void (*debug_print)(const char *const fmt, ...);
} w25qxx_handle_t;
```

**适配策略**: 取 libdriver 的指令编码表和状态机逻辑（JEDEC ID 识别、擦除算法、Page Program 边界检查），自行实现 SPI 层（因为 SPI1 分时复用 + PA6 动态切换是该项目的特有需求，不适合直接套用通用接口抽象层）。接口抽象层增加额外函数指针开销，对 64KB Flash MCU 不划算。

### 10.3 图标/位图素材

| 资源 | 仓库/来源 | Stars | 用途 |
|:---|:---|:---|:---|
| **gavinlyonsrepo/displaylib_16bit_PICO** | <https://github.com/gavinlyonsrepo/displaylib_16bit_PICO> | — | RGB565 位图/sprites 渲染 API（drawBitmap16Data、drawSpriteData 含透明色），多驱动支持 |
| **sumotoy/TFT_ST7735** | <https://github.com/sumotoy/TFT_ST7735> | — | drawIcon() 图标系统（缩放、透明背景），含**在线图像转 C 数组工具** |
| **Bodmer/TFT_eSPI** | <https://github.com/Bodmer/TFT_eSPI> | 3.5k+ | ⭐ 最流行 Arduino TFT 库，SPIFFS BMP RGB565 示例，可直接适配 Flash 读取 |
| **ammash97/dotmatrix** | <https://github.com/ammash97/dotmatrix> | — | Web 端位图/字符 C 数组生成器，OLED/LCD 通用 |
| **Adafruit GFX** | <https://github.com/adafruit/Adafruit-GFX-Library> | 2.4k+ | 标准位图 API 参考（drawBitmap 多色深） |

**图标选用策略**: WiFi ×10 帧 + MQTT ×6 帧 + Star 已有，当前不引入外部图标包。未来扩展时从上述仓库取设计参考，用 **ammash97/dotmatrix**（Web 工具）或 **sumotoy/TFT_ST7735**（在线图标转换器）转成 16×16 LSB-first 二进制数组后写入 Flash 图标区。

### 10.4 字库 PC 端工具

| 工具 | 来源 | 用途 |
|:---|:---|:---|
| **HZK16 标准字库文件** | 网上直接下载 (238KB) | 6763 汉字原始数据 |
| **GB2312 → Unicode 映射** | Python `gb2312` 库 或手动映射表 | 区位码 → Unicode 码点转换 |
| **PCtoLCD2002** | 取模软件 (Windows) | 自定义汉字/图标取模，16×16 LSB-first |
| **Image2Cpp (在线)** | <https://javl.github.io/image2cpp/> | PNG → RGB565 C 数组，用于开机画面帧 |
| **CH341A 编程器** | 硬件工具 (~¥10) | W25Q128 首次烧录 .bin 镜像 |

**字库镜像打包流程**:
```
HZK16 (6763字×32B) → Python 按 Unicode 重排 → 6763→20902码位展开
→ 植入 Magic + CRC32 头部 → 生成 flash_font.bin (668KB)
→ CH341A 烧录到 W25Q128 0x000020 偏移
```
