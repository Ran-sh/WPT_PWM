# CH341 + Python 字库烧录 — 设计文档

> **状态**: 已确认 | **日期**: 2026-06-23 | **版本**: V1.0

## 1. 目标

将 GB2312 一级汉字 (6763字) + ASCII (95字) + 全部图标从 STM32 片内 ROM (`TFT_Font_Data.h`) 迁移到 W25Q128 Flash (2MB 字库分区), 通过 CH341A USB-SPI 编程器 + Python + flashrom 一次性烧录。

## 2. Flash 字库分区精确布局

```
┌────────────────┐ 0x000000
│ Header (32B)   │  魔数 0x574B / 版本 / total_size / CRC32 / 各区偏移
├────────────────┤ 0x000020
│ ASCII (1520B)  │  95字 × 16B, 8×16 LSB-first (与 TFT_Font_Data.h 格式一致)
├────────────────┤ 0x000620 (256B 对齐)
│ Icon Table     │  元数据表 (16B header + N×8B entry) + 位图数据
│                │  WIFI_ICON(4帧) + WIFI_ANIM(6帧) + MQTT_ICON(3态+6帧)
│                │  + ICON_STAR + STAR_CURSOR_ANIM(16帧) + ROCKET_ANIM(16帧)
├────────────────┤ 下一个 256B 对齐边界
│ CJK Index      │  [Unicode LE 2B][Data_Offset LE 2B] × 6763 = 27052B
│                │  按 Unicode 码点升序排列 (保证二分查找正确性)
├────────────────┤ CJK_Data_BASE = 对齐到 256B 边界
│ CJK Data       │  6763 字 × 32B = 216416B 紧凑排列, 无空洞
│                │  每字 32B: 16行 × 2B, LSB-first, bit_reverse 预处理
└────────────────┘
```

| 区段 | 起始地址 | 大小 | 说明 |
|:---|:---|:---|:---|
| Header | 0x000000 | 32B | 魔数+版本+CRC32+各区偏移量 |
| ASCII | 0x000020 | 1520B | 95字×16B, LSB-first (与现有解码器兼容) |
| Icon Table | 0x000620 (对齐) | ~8KB | 元数据表 + 位图数据 |
| CJK Index | 下一对齐边界 | 27052B | [U16][U16]×6763, Unicode 升序 |
| CJK Data | 下一对齐边界 | 216416B | 6763字×32B 紧凑排列 |
| **擦除范围** | **0x000000~0x0003E000** | **62扇区 (248KB)** | 精准覆盖, 不碰配置/黑匣子 |

## 3. Font Header 结构 (32字节)

| 偏移 | 大小 | 字段 | 说明 |
|:---|:---|:---|:---|
| 0x00 | 2B | magic | 0x574B ("WK") |
| 0x02 | 1B | version | 1 (递增的格式版本号) |
| 0x03 | 1B | reserved | 保留 |
| 0x04 | 4B | total_size | 字库分区数据总字节数 (含 Header) |
| 0x08 | 4B | crc32 | CRC32 覆盖范围: 0x0C→0x1F (Header 尾部 20B), LE |
| 0x0C | 2B | ascii_offset | ASCII 起始偏移 (固定=0x0020) |
| 0x0E | 2B | ascii_count | ASCII 字符数 (95) |
| 0x10 | 2B | ascii_bytes | 每 ASCII 字模字节数 (16) |
| 0x12 | 2B | reserved2 | 保留 |
| 0x14 | 4B | cjk_index_offset | CJK Index 起始偏移 (Python 动态计算) |
| 0x18 | 2B | cjk_index_count | CJK 索引条数 (6763) |
| 0x1A | 2B | cjk_data_bytes | 每 CJK 字模字节数 (32) |
| 0x1C | 4B | cjk_data_offset | CJK Data 起始偏移 (Python 动态计算) |

> 32B 精准对齐。CRC32 仅校验 Header 自身 (快速启动检查), 全量数据一致性由 Python burn_flash.py 烧后逐字节比对保证。

**CJK Index 条目 (每条 4B)**:
- `[2B] Unicode` — 码点值, LE (如 0x4E00 = "一")
- `[2B] Data_Offset` — 相对 CJK_Data_BASE 的字模偏移

**Icon Table Header (16B)**:
- `[2B] n_entries` — 图标条目数
- `[2B] n_frames` — 总帧数
- `[4B] data_area_size` — 图标数据区总字节数

**Icon Entry (每条 8B)**:
- `[2B] icon_id` — 标识 (0=WIFI_ICON, 1=WIFI_ANIM, 2=MQTT_ICON, 3=MQTT_YES, 4=MQTT_NO, 5=MQTT_ANIM, 6=ICON_STAR, 7=STAR_CURSOR, 8=ROCKET_ANIM)
- `[2B] n_frames` — 该图标帧数
- `[2B] data_offset` — 相对 Icon Data 区起始的偏移
- `[2B] reserved` — 保留

## 4. CRC32 一致性铁律

### 4.1 算法参数

| 参数 | STM32 CRC32_Compute | Python zlib.crc32 |
|:---|:---|:---|
| 多项式 | 0x04C11DB7 | 0x04C11DB7 (Ethernet CRC32) |
| 初始值 | 0xFFFFFFFF | 0xFFFFFFFF |
| Final XOR | ^ 0xFFFFFFFF | ^ 0xFFFFFFFF |
| 输入 | uint8_t array | bytes |
| 输出类型 | **uint32_t** (无符号) | **int32** (有符号!) |

### 4.2 Python 侧强制规则

```python
import zlib
import struct

def compute_crc32(data: bytes) -> int:
    """与 STM32 CRC32_Compute 一致, 返回 uint32"""
    return zlib.crc32(data) & 0xFFFFFFFF  # 强制无符号

# 写入 Flash Header (小端序, 与 Cortex-M3 LE 原生字节序一致)
# Header 结构: [0x00 magic/version/reserved/total_size][0x08 crc32][0x0C... 其他字段]
crc = compute_crc32(header_bytes[0x0C:0x20])  # 校验 0x0C→0x1F 共 20B
header_bytes = header_bytes[:0x08] + struct.pack('<I', crc) + header_bytes[0x0C:]
```

### 4.3 自测断言 (烧录前必须通过)

```python
# 已知输入 → 已知输出, 两边同时验证
assert compute_crc32(b"1234") == 0x9BE3E0A3
# STM32 侧: CRC32_Compute("1234", 4) → 0x9BE3E0A3
```

## 5. 字模位图格式

### 5.1 解码路径 (STM32 Tft_Driver.c, 不变)

```c
/* ASCII: LSB-first, 每字节 bit0=左端 */
static void Decode_Char_Row(uint8_t byte_val, uint16_t fg, uint16_t bg, uint16_t* out) {
    for (b = 0; b < 8; b++)
        out[b] = (byte_val & (0x01 << b)) ? fg : bg;
}

/* CJK: LSB-first, lo=行左8px, hi=行右8px */
static void Decode_CN_Row(uint8_t lo, uint8_t hi, uint16_t fg, uint16_t bg, uint16_t* out) {
    for (b = 0; b < 8; b++) {
        out[b]     = (lo & (0x01 << b)) ? fg : bg;
        out[b + 8] = (hi & (0x01 << b)) ? fg : bg;
    }
}
```

### 5.2 Python 生成规则

PIL 渲染 → 1-bit 位图 → LSB-first 排列 → **bit_reverse 每字节** (可选, 见下)。

**关键确认**: 检查现有 `TFT_Font_Data.h` 中字模是否需要 bit_reverse。

验证方法: 取 "一" (U+4E00) 像素 → PIL 渲染 → 看第0行是否与 CN_FONT_16X16[0] 头两字节一致。如果不一致, 添加 `bit_reverse` 步骤。

```
PIL 渲染流程:
  simsun.ttc 16px → ImageDraw.text("一") → 1-bit bitmap → LSB-first → byte[] → 32B
```

## 6. STM32 固件改动

### 6.1 Tft_Driver.c — 字模来源切换

新增 Flash 读取路径, 替代片内 ROM 数组直接访问:

```c
/* ── 字体读取: Flash → 局部缓冲 ── */

/** @brief 从 W25Q128 Flash 读取一个 ASCII 字模行 (1B) */
static uint8_t Tft_Flash_Read_ASCII_Byte(uint8_t char_idx, uint8_t row) {
    uint8_t b;
    uint32_t addr = g_font_header.ascii_offset + (uint32_t)char_idx * 16 + row;
    W25Q_Driver_Read(addr, &b, 1);
    return b;
}

/** @brief 从 W25Q128 Flash 读取一个 CJK 字模行 (2B, lo+hi) */
static void Tft_Flash_Read_CJK_Row(uint16_t unicode, uint8_t row, uint8_t *lo, uint8_t *hi) {
    /* 二分查找 CJK Index → 获取 data_offset → 读字模数据 */
    uint16_t data_offset = Font_Index_Binary_Search(unicode);
    if (data_offset == 0xFFFF) { *lo = 0; *hi = 0; return; }
    uint32_t addr = g_font_header.cjk_data_offset + data_offset + (uint32_t)row * 2;
    W25Q_Driver_Read(addr, lo, 1);
    W25Q_Driver_Read(addr + 1, hi, 1);
}

/** @brief 二分搜索 CJK Index (6763条升序, 13次比较) */
static uint16_t Font_Index_Binary_Search(uint16_t unicode) {
    uint16_t lo = 0, hi = g_font_header.cjk_index_count;
    while (lo < hi) {
        uint16_t mid = (lo + hi) >> 1;
        /* 读 Index[mid] → 获取 Unicode 值比对 */
        uint16_t mid_uc;
        uint32_t index_addr = g_font_header.cjk_index_offset + (uint32_t)mid * 4;
        W25Q_Driver_Read(index_addr, (uint8_t*)&mid_uc, 2);
        if (mid_uc < unicode) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_font_header.cjk_index_count) {
        /* 读取并验证完全匹配 */
        uint16_t found_uc; uint16_t found_off;
        uint32_t addr = g_font_header.cjk_index_offset + (uint32_t)lo * 4;
        W25Q_Driver_Read(addr, (uint8_t*)&found_uc, 2);
        W25Q_Driver_Read(addr + 2, (uint8_t*)&found_off, 2);
        if (found_uc == unicode) return found_off;
    }
    return 0xFFFF;  /* 未找到 → 显示 □ */
}
```

### 6.2 初始化时加载 Font Header

```c
/* Sys_Core.c 启动阶段 */
static Font_Header g_font_header;  /* 缓存 Header 到 RAM (32B) */

void Font_Header_Load(void) {
    uint32_t crc_stored, crc_computed;
    W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)&g_font_header, sizeof(Font_Header));

    /* 魔数校验 */
    if (g_font_header.magic != FONT_MAGIC) {
        /* 字库未烧录, 回退到片内 ROM 76字 */
        g_font_flash_valid = 0;
        return;
    }

    /* CRC32 仅校验 Header 自身 (快速启动检查), 全量数据由 Python 烧后比对 */
    crc_stored = g_font_header.crc32;
    g_font_header.crc32 = 0;  /* 临时清零用于计算 */
    crc_computed = CRC32_Compute((uint8_t*)&g_font_header + 0x0C,  /* 从 0x0C 开始 */
                                  32 - 0x0C);  /* 20 字节 */
    g_font_header.crc32 = crc_stored;

    g_font_flash_valid = (crc_stored == crc_computed);
}
```

### 6.3 SPI1 DFF 模式切换处理

核心问题: TFT DMA 渲染需要 SPI1 16-bit 模式, Flash 读取需要 8-bit 模式。

**策略**: 在每次 Flash 读取字模数据时临时切 8-bit → 读完立即恢复 16-bit。字模读取只在页面重绘时触发 (非热路径), 不会与 TFT DMA 传输竞态。

```c
/* W25Q_Driver_Read 内部已包含: Enter_Mode(8-bit) → 读 → Leave_Mode(恢复 PA6 为 DC) */
/* TFT DMA 发送前确保 SPI1 处于 16-bit 模式 (由 Tft_DMA_Transfer 入口统一设置) */
```

### 6.4 文件改动清单

| 文件 | 改动 | 行数估计 |
|:---|:---|:---|
| `Tft_Driver.c` | ASCII/CJK 渲染改用 Flash 读取 + 二分搜索 | +80 |
| `Tft_Driver.h` | Font_Header 类型声明 | +30 |
| `App_Storage.c` | Font_Header_Load() 初始化 | +40 |
| `App_Storage.h` | Font_Header + FONT_CRC32_BYPASS 门槛宏 | +20 |
| `TFT_Font_Data.h` | 保留作为回退 (Flash 无效时使用) | 无改动 |

## 7. Python 工具

### 7.1 文件结构

```
ch341/
├── generate_font.py     # 字模生成 + 数据组装 → font_data.bin
├── burn_flash.py        # 烧录编排 + CRC32 自测 + flashrom 调用
├── requirements.txt     # Pillow
└── README.md            # 操作指南
```

### 7.2 generate_font.py 流程

```
1. PIL ImageFont.truetype("simsun.ttc", 16) 加载宋体
2. 渲染 ASCII 0x20~0x7E → 95字 × 16B, LSB-first
3. 渲染 GB2312 一级 0x4E00~0x9FA0 → 6763字 × 32B, LSB-first
4. 验证: "一"(U+4E00) 与 CN_FONT_16X16[0] 逐字节比对, 确认 bit_reverse
5. 从 Keil_Project/Hardware/TFT_Font_Data.h 提取全部图标字节数组
6. 组装 Header → ASCII → Icons → CJK Index → CJK Data
7. 计算 CRC32, 写入 Header
8. 写入 font_data.bin (~250KB)
```

### 7.3 burn_flash.py 流程

```
1. 自测: CRC32("1234") == 0x9BE3E0A3
2. 检查 flashrom 可用: subprocess.run(["flashrom", "--version"])
3. 调用 generate_font 生成 font_data.bin
4. 擦除: flashrom -p ch341a_spi -E -i 0x000000:0x0003E000
5. 写入: flashrom -p ch341a_spi -w font_data.bin --ifd --noverify-all
   (--ifd: 只写 font_data.bin 覆盖的区域, 保护配置/黑匣子)
6. 读回校验: flashrom -p ch341a_spi -r verify.bin
7. 逐字节比对 [0x000000, 0x0003E000) → 报告结果
```

### 7.4 flashrom 分区烧录策略

由于 flashrom 的 `--layout` / `--ifd` 支持有限, 实际策略:

```
方案 A (推荐): 生成 16MB 完整镜像
  1. flashrom -p ch341a_spi -r full_backup.bin  # 备份全片
  2. Python 将 font_data.bin 覆盖到 full_backup.bin 前 248KB
  3. flashrom -p ch341a_spi -w full_backup.bin   # 全片写入
  → 保护配置+黑匣子: 从备份读回的 0x300000+ 区域未修改, 写入后一致

方案 B (精确): 仅写字库分区
  1. flashrom -p ch341a_spi --layout layout.txt -w font_data.bin
  layout.txt:
    000000:03dfff font
  → 如果 flashrom 版本支持 partial write, 更安全

默认使用方案 A, 在文档中说明备份重要性。
```

## 8. 前置条件 (一次性)

1. **CH341A 驱动**: Zadig 替换为 WinUSB (非默认串口驱动)
2. **flashrom Windows 版**: 下载 1.4-devel 社区编译版
3. **Python 3.10+**: `pip install Pillow`
4. **接线**: CH341A → W25Q128 (CS/CLK/MOSI/MISO/GND/3.3V), 确保 3.3V 跳线

## 9. 操作流程 (小白版)

```
步骤 1: 接线 → 把 CH341A 夹到 W25Q128 上 (或通过板载排针)
步骤 2: 插 USB → 设备管理器确认 CH341A 在 "Universal Serial Bus devices" 下
步骤 3: python ch341/generate_font.py → 生成 font_data.bin
步骤 4: python ch341/burn_flash.py → 自动备份+擦除+烧写+校验
步骤 5: STM32 重新上电 → 屏幕显示 GB2312 全字库
```

## 10. 风险与缓解

| 风险 | 缓解 |
|:---|:---|
| 烧录中断 (USB 松动) | 烧前全片备份 → 重烧 |
| SPI1 DFF 模式竞态 | W25Q_Driver_Read 原子进入/退出, Tft_DMA_Transfer 入口确保 16-bit |
| 二分搜索性能 | 13次比较 + Flash 读取, 每字 ~300μs, 整屏 8行×10字 = 24ms (可接受) |
| 字库烧录后 CRC 不匹配 | burn_flash.py 烧后自动读回逐字节比对 |
| simsun.ttc 不可用 | Windows 自带, 回退到 PIL default font 或开源字体 |
| flashrom 不识别 W25Q128 | 降级用 `-c "SFDP-capable chip"` 或 `-c W25Q128.V` 强制指定 |
