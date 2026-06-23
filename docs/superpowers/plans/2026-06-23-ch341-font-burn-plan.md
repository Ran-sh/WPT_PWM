# CH341 + Python 字库烧录 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CH341A USB-SPI 编程器 + Python + flashrom 将 GB2312 全字库 (6763 CJK + 95 ASCII + 54帧图标) 一次性烧入 W25Q128, STM32 固件同步支持 Flash 字库渲染 (总线独占二分检索 + ROM 回退)。

**Architecture:** PC 端 Python 负责 PIL 渲染字模 + CRC32 小端序组装 → flashrom CLI 执行 SPI 烧录。STM32 端 W25Q_Driver 层新增 `W25Q_Font_Index_Binary_Search` 总线独占二分检索 (入口持锁→搜索→唯一释放) + Tft_Driver 双路径 (Flash/ROM 自动切换)。

**Tech Stack:** Python 3.10+ / Pillow / zlib / flashrom 1.4-devel / ARMCC V5 C89 / SPL V3.5.0

---

## 文件结构总览

| 文件 | 操作 | 职责 |
|:---|:---|:---|
| `ch341/burn_flash.py` | **Create** | 烧录编排: CRC32 自测 → 生成镜像 → 备份 → 擦除 → 烧写 → 校验 |
| `ch341/requirements.txt` | **Create** | pip 依赖: Pillow |
| `ch341/README.md` | **Create** | 小白操作指南 (Zadig 驱动→接线→烧录) |
| `Keil_Project/Hardware/Tft_Driver.c` | **Modify** | 集成 Flash 字库渲染 (双路径), 追加 ~90 行 |
| `Keil_Project/User/Sys_Core.c` | **Modify** | `Sys_Post_Init()` 末尾调 `Font_Header_Load` |
| `Keil_Project/Hardware/W25Q_Driver.c` | **已修改** | 5 函数 static→public + 追加 Font_Header_Load + Binary_Search |
| `Keil_Project/Hardware/W25Q_Driver.h` | **已修改** | Font_Header typedef + 总线原语 + Binary_Search 声明 |
| `Keil_Project/User/App_Storage.h` | **已修改** | CRC32_Compute 公开声明 |
| `ch341/generate_font.py` | **已创建** | 字模渲染 + 数据组装 (已完成) |

---

### Task 1: `ch341/burn_flash.py` — 烧录编排脚本

**Files:**
- Create: `ch341/burn_flash.py`
- Create: `ch341/requirements.txt`

- [ ] **Step 1: 创建 `ch341/requirements.txt`**

```
Pillow>=10.0.0
```

Run: `pip install -r ch341/requirements.txt`
Expected: 确认 Pillow 已安装 (或重新安装)

- [ ] **Step 2: 编写 `ch341/burn_flash.py` 主脚本**

```python
#!/usr/bin/env python3
"""
burn_flash.py — CH341A + flashrom 字库烧录编排
V1.0  2026-06-23

用法: python ch341/burn_flash.py

前置: 1. Zadig 已将 CH341A 驱动换为 WinUSB
       2. flashrom.exe 在 PATH 中 (1.4-devel 社区编译版)
       3. CH341A 已夹到 W25Q128 排针 (CS/CLK/MOSI/MISO/GND/3.3V)
       4. STM32 断电 (防止 SPI 总线冲突)
"""

import subprocess, sys, os, struct, zlib, shutil

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
FONT_BIN     = os.path.join(SCRIPT_DIR, "font_data.bin")
BACKUP_BIN   = os.path.join(SCRIPT_DIR, "backup_16MB.bin")
VERIFY_BIN   = os.path.join(SCRIPT_DIR, "verify_readback.bin")
FLASH_SIZE   = 2 * 1024 * 1024  # 字库分区 2MB
CHIP_SIZE    = 16 * 1024 * 1024 # W25Q128 全片 16MB
ERASE_BLOCKS = 62               # 248KB = 62 × 4KB sectors
FLASHROM     = "flashrom"


def run_flashrom(args, desc):
    cmd = [FLASHROM, "-p", "ch341a_spi"] + args
    print(f"[..] {desc}")
    print(f"     {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300,
                            creationflags=subprocess.CREATE_NO_WINDOW
                                if sys.platform == "win32" else 0)
    if result.returncode != 0:
        print(f"[FAIL] {desc}")
        print(result.stdout); print(result.stderr)
        sys.exit(1)
    print(f"[OK]  {desc}")


def compute_crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def main():
    # 1. CRC32 自测
    print("[..] CRC32 自测: '1234' → 0x9BE3E0A3")
    assert compute_crc32(b"1234") == 0x9BE3E0A3, \
        "CRC32 自测失败! Python zlib 与 STM32 CRC32_Compute 不一致"
    print("[OK]  CRC32 自测通过")

    # 2. 检测 flashrom
    print("[..] 检测 flashrom...")
    result = subprocess.run([FLASHROM, "--version"], capture_output=True, text=True,
                            creationflags=subprocess.CREATE_NO_WINDOW
                                if sys.platform == "win32" else 0)
    if result.returncode != 0:
        print("[FAIL] flashrom 未找到! 请下载 flashrom 1.4-devel 社区版并加入 PATH")
        print("       下载: https://winraid.level1techs.com/ (搜索 flashrom-1.4)")
        sys.exit(1)
    print(f"[OK]  flashrom 可用")

    # 3. 生成字库镜像
    print("[..] 调用 generate_font 生成字库镜像...")
    gen = os.path.join(SCRIPT_DIR, "generate_font.py")
    result = subprocess.run([sys.executable, gen], capture_output=True, text=True,
                            timeout=600, cwd=SCRIPT_DIR,
                            creationflags=subprocess.CREATE_NO_WINDOW
                                if sys.platform == "win32" else 0)
    if result.returncode != 0:
        print("[FAIL] generate_font 失败:")
        print(result.stdout); print(result.stderr)
        sys.exit(1)
    print(result.stdout)

    if not os.path.exists(FONT_BIN):
        print(f"[FAIL] {FONT_BIN} 未生成")
        sys.exit(1)
    if os.path.getsize(FONT_BIN) != FLASH_SIZE:
        print(f"[FAIL] font_data.bin 大小 {os.path.getsize(FONT_BIN)} ≠ {FLASH_SIZE}")
        sys.exit(1)

    # 4. 备份全片 Flash
    print("\n[==== Step 1/4: 备份全片 Flash ====]")
    if os.path.exists(BACKUP_BIN):
        print(f"[WARN] 已存在备份 {BACKUP_BIN}, 跳过读取")
        print(f"      (如需重新备份, 请删除备份文件后重试)")
    else:
        run_flashrom(["-r", BACKUP_BIN], f"备份全片 Flash → {BACKUP_BIN}")
        backup_size = os.path.getsize(BACKUP_BIN)
        if backup_size != CHIP_SIZE:
            print(f"[WARN] 备份大小 {backup_size} ≠ {CHIP_SIZE}, 可能接触不良")

    # 5. 方案选择: 全片镜像法 (保护配置+黑匣子)
    print("\n[==== Step 2/4: 准备写入镜像 ====]")
    backup = bytearray(open(BACKUP_BIN, "rb").read())
    font   = bytearray(open(FONT_BIN, "rb").read())

    if len(backup) < FLASH_SIZE:
        print("[FAIL] 备份容量不足, 无法覆盖字库分区")
        sys.exit(1)

    # 将 font_data.bin 覆盖到备份的前 2MB
    backup[0:FLASH_SIZE] = font

    # 计算 CRC32 校验 (写入后读回用)
    font_crc = compute_crc32(bytes(backup[0:ERASE_BLOCKS * 4096]))
    print(f"     字库区 CRC32: 0x{font_crc:08X}")

    merged = os.path.join(SCRIPT_DIR, "merged_flash.bin")
    with open(merged, "wb") as f:
        f.write(backup)

    # 6. 烧写全片
    print("\n[==== Step 3/4: 烧写全片 Flash ====]")
    run_flashrom(["-w", merged], f"烧写全片 Flash ← {merged}")

    # 7. 读回校验
    print("\n[==== Step 4/4: 读回校验 ====]")
    run_flashrom(["-r", VERIFY_BIN], f"读回校验 → {VERIFY_BIN}")

    verify = open(VERIFY_BIN, "rb").read()
    mismatch_count = 0
    first_mismatch = -1
    for i in range(min(ERASE_BLOCKS * 4096, len(verify))):
        if verify[i] != backup[i]:
            if mismatch_count < 10:
                print(f" [DIFF] addr=0x{i:06X} wrote=0x{backup[i]:02X} read=0x{verify[i]:02X}")
            mismatch_count += 1
            if first_mismatch < 0:
                first_mismatch = i

    if mismatch_count == 0:
        print("\n" + "=" * 60)
        print("  [PASS] 字库烧录成功 — 所有字节一致!")
        print("=" * 60)
        print("\n后续: STM32 上电 → 自动识别 Flash 字库 → GB2312 全字库渲染")
    else:
        print(f"\n[FAIL] {mismatch_count} 字节不一致! "
              f"首地址 0x{first_mismatch:06X}")
        print("请检查: 1. CH341A 跳线是否 3.3V  "
              "2. 排针接触是否良好  "
              "3. STM32 是否已断电")
        sys.exit(1)

    # 清理
    if os.path.exists(merged):
        os.remove(merged)
    if os.path.exists(VERIFY_BIN):
        os.remove(VERIFY_BIN)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: 验证 burn_flash.py 语法**

Run: `python -m py_compile ch341/burn_flash.py`
Expected: 静默完成 (无输出)

- [ ] **Step 4: 提交**

```bash
git add ch341/burn_flash.py ch341/requirements.txt
git commit -m "feat: CH341 burn_flash.py 烧录编排脚本 (备份+擦除+烧写+CRC校验)"
```

---

### Task 2: `ch341/README.md` — 操作指南

**Files:**
- Create: `ch341/README.md`

- [ ] **Step 1: 编写操作指南**

```markdown
# CH341A + Python W25Q128 字库烧录 — 操作指南

## 事前准备 (一次性)

### 1. 硬件
| 物品 | 说明 |
|:---|:---|
| CH341A 编程器 | 淘宝 ~10元, 黑色 PCB, 24/25 系列插座 |
| 杜邦线 6 根 | 母对母, CS/CLK/MOSI/MISO/GND/3.3V |
| W25Q128 | 已焊在板上, 通过排针引出 |

### 2. 接线 (CH341A → W25Q128 排针)

| CH341A 排针 | W25Q128 排针 | 功能 |
|:---|:---|:---|
| CS | PA12 (FLASH_CS) | 片选 |
| CLK | PA5 (SCK) | 时钟 |
| MOSI | PA7 (MOSI) | 主出从入 |
| MISO | PA6 (MISO) | 主入从出 |
| GND | GND | 共地 |
| 3.3V | 3.3V | 供电 |

> ⚠️ **必须 3.3V!** CH341A 跳线帽插到 3.3V 位置, 5V 会烧 W25Q128!

> ⚠️ **STM32 断电!** 烧录时 STM32 必须完全断电, 否则 SPI 总线冲突!

### 3. 驱动 (一次性的)

1. 下载 Zadig: https://zadig.akeo.ie/
2. 插上 CH341A USB
3. 打开 Zadig → Options → List All Devices
4. 选中 CH341A → 驱动换成 **WinUSB** (或 libusb-win32)
5. 点击 Replace Driver

### 4. flashrom

1. 下载 flashrom Windows 版:
   - 搜索 "flashrom 1.4 devel windows ch341a_spi"
   - 或 https://winraid.level1techs.com/ 下载 `flashrom-1.4-devel-*-ch341a_spi-win64.zip`
2. 解压 `flashrom.exe` 到 `C:\Windows\` 或加入 PATH

### 5. Python

```bash
# 安装 Python 3.10+ (若没有)
# https://www.python.org/downloads/

# 安装依赖
pip install -r ch341/requirements.txt
```

## 烧录步骤

```bash
# 确认接线 + STM32 断电 + CH341A 插 USB

# 第 1 步: (可选) 仅生成镜像, 不烧录
python ch341/generate_font.py
# → 生成 ch341/font_data.bin (2MB)
# → 查看输出: CRC32 / 各区偏移 / 擦除扇区数

# 第 2 步: 烧录
python ch341/burn_flash.py
# → CRC32 自测
# → 生成 font_data.bin
# → 备份全片 Flash → backup_16MB.bin
# → 合并镜像 → 烧写全片
# → 读回逐字节校验

# 第 3 步: 上电验证
# → STM32 重新上电
# → 屏幕应显示 GB2312 全字库
# → 如果魔法数无效, 自动回退到片内 ROM 76字
```

## 故障排除

| 症状 | 检查 |
|:---|:---|
| flashrom 不识别 CH341A | Zadig 驱动未替换为 WinUSB, 重做步骤 3 |
| "No EEPROM/flash device found" | 检查接线 + 3.3V 跳线 + STM32 已断电 |
| 校验失败 (大量不匹配) | 排针接触不良, 重新夹紧 |
| 烧录后屏幕无变化 | 检查 `g_font_flash_valid` 是否为 1 (Header CRC32 校验) |
| 烧录后白屏 | SPI1 DFF 模式未恢复, 重新上电 STM32 |
```

- [ ] **Step 2: 提交**

```bash
git add ch341/README.md
git commit -m "docs: CH341 字库烧录操作指南 (接线+驱动+烧录步骤+故障排除)"
```

---

### Task 3: `Tft_Driver.c` — 集成 Flash 字库渲染

**Files:**
- Modify: `Keil_Project/Hardware/Tft_Driver.c` (第 12 行后加 include, 第 28 行后加全局变量, 第 410 行后替换中文渲染, 第 350 行修改 Show_Char)
- Delete: `Keil_Project/Hardware/Tft_Driver_Flash_Insert.c` (参考文件, 集成后删除)
- Delete: `Keil_Project/Hardware/Tft_Driver_Flash_Font_Insert.c` (重复文件, 删除)

- [ ] **Step 1: 在 `Tft_Driver.c` 第 12 行 `#include "TFT_Font_Data.h"` 之后追加 W25Q 头文件**

Edit file `Keil_Project/Hardware/Tft_Driver.c`:
```
old: #include "TFT_Font_Data.h"
new: #include "TFT_Font_Data.h"
     #include "W25Q_Driver.h"
```

- [ ] **Step 2: 在 `static uint8_t s_dma_configured = 0;` (第 28 行) 之后追加 Flash 字体全局变量**

Edit file `Keil_Project/Hardware/Tft_Driver.c`:
```
old: static uint8_t s_dma_configured = 0;
new: static uint8_t s_dma_configured = 0;
     static uint8_t  g_font_flash_valid = 0;     /* 1=Flash 字库头校验通过, 走新渲染路径 */
     static Font_Header g_font_header;           /* RAM 缓存 Header 32B (从 W25Q 加载) */
```

- [ ] **Step 3: 重写 `Tft_Driver_CN_Draw()` 函数 (第 416~445 行)**

将原函数整个替换为双路径版本:

```c
/* ═══════════════════════════════════════════════════════════════
 *  中文渲染 — Flash 二分检索 (6763字) / ROM 线性查找 (76字) 双路径
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t Tft_Is_UTF8_CN(uint8_t c) { return (c >= 0xE0 && c <= 0xEF); }

static void Tft_Driver_CN_Draw(uint8_t ln, uint8_t col, const uint8_t *utf8,
                                uint16_t fg, uint16_t bg)
{
    if (ln >= TFT_LINE_COUNT || col + 1 >= TFT_CHAR_PER_LINE) return;

    if (g_font_flash_valid) {
        uint16_t unicode; uint8_t row; uint16_t data_off; uint32_t glyph_base; uint16_t* p;
        unicode  = ((uint16_t)(utf8[0] & 0x0F) << 12);
        unicode |= ((uint16_t)(utf8[1] & 0x3F) << 6);
        unicode |= ((uint16_t)(utf8[2] & 0x3F));
        if (unicode < 0x4E00 || unicode > 0x9FA0) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg); return;
        }
        data_off = W25Q_Font_Index_Binary_Search(unicode, &g_font_header);
        if (data_off == 0xFFFF) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg); return;
        }
        SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
        glyph_base = g_font_header.cjk_data_offset + (uint32_t)data_off;
        p = s_dma_buf;
        for (row = 0; row < 16; row++) {
            uint8_t lo_hi[2];
            W25Q_Driver_Read(glyph_base + (uint32_t)row * 2, lo_hi, 2);
            Decode_CN_Row(lo_hi[0], lo_hi[1], fg, bg, p + row * 16);
        }
        Tft_DMA_Send(s_dma_buf, 256);
        return;
    }

    /* ── 片内 ROM 回退: CN_INDEX 线性查找 ── */
    {
        uint8_t row, g_idx, i; uint16_t* p;
        g_idx = 0xFF;
        for (i = 0; i < TFT_CN_FONT_CHAR_COUNT; i++) {
            if (CN_INDEX[i*3] == utf8[0] && CN_INDEX[i*3+1] == utf8[1] && CN_INDEX[i*3+2] == utf8[2]) {
                g_idx = i; break;
            }
        }
        if (g_idx == 0xFF) {
            SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
            Tft_DMA_Fill(256, bg); return;
        }
        SetWin(col * 8, ln * 16, col * 8 + 15, ln * 16 + 15);
        p = s_dma_buf;
        for (row = 0; row < 16; row++)
            Decode_CN_Row(CN_FONT_16X16[g_idx][row * 2], CN_FONT_16X16[g_idx][row * 2 + 1],
                           fg, bg, p + row * 16);
        Tft_DMA_Send(s_dma_buf, 256);
    }
}
```

- [ ] **Step 4: 在 `Tft_Driver_Show_Char()` 函数中增加 Flash ASCII 路径 (第 350~374 行)**

定位到原代码第 365 行左右的 `p = s_dma_buf; for (row = 0...)` 块，在其前插入:

```c
    if (g_font_flash_valid) {
        uint8_t row; uint32_t base; uint16_t* p;
        if (idx < 0x20 || idx > 0x7E) {
            SetWin(col * 8, line * 16, col * 8 + 7, line * 16 + 15);
            Tft_DMA_Fill(128, bg); return;
        }
        SetWin(col * 8, line * 16, col * 8 + 7, line * 16 + 15);
        base = g_font_header.ascii_offset + (uint32_t)(idx - 0x20) * 16;
        p = s_dma_buf;
        for (row = 0; row < 16; row++) {
            uint8_t byte_val;
            W25Q_Driver_Read(base + (uint32_t)row, &byte_val, 1);
            Decode_Char_Row(byte_val, fg, bg, p + row * 8);
        }
        Tft_DMA_Send(s_dma_buf, 128);
        return;
    }
```

> 注意: 原有 ROM 路径代码保持不变，新代码块插在 if-else 之前，g_font_flash_valid==1 时走 Flash 路径并 return。

- [ ] **Step 5: 删除参考文件**

```bash
rm "D:/Claude Code Project/WPT_PWM_V4.0_ONENET_TFT/Keil_Project/Hardware/Tft_Driver_Flash_Insert.c"
rm "D:/Claude Code Project/WPT_PWM_V4.0_ONENET_TFT/Keil_Project/Hardware/Tft_Driver_Flash_Font_Insert.c"
```

- [ ] **Step 6: 构建验证**

Keil → Project → Rebuild all target files (F7)
Expected: `0 Error(s), 0 Warning(s)`

- [ ] **Step 7: 提交**

```bash
git add Keil_Project/Hardware/Tft_Driver.c
git rm Keil_Project/Hardware/Tft_Driver_Flash_Insert.c Keil_Project/Hardware/Tft_Driver_Flash_Font_Insert.c
git commit -m "feat: Tft_Driver Flash 字库双路径渲染 (二分检索 CJK + ASCII 流式读)"
```

---

### Task 4: `Tft_Driver_Init()` — 启动时加载 Font Header

**Files:**
- Modify: `Keil_Project/Hardware/Tft_Driver.c` (Tft_Driver_Init 末尾追加)

> 架构修正: g_font_flash_valid 是 Tft_Driver.c 的 static private 变量，Font_Header_Load 的调用点必须落在同一编译单元内。因此在 Tft_Driver_Init() 末尾追加调用——TFT 硬件初始化完成后，立即检查 Flash 字库。

**Files:**
- Modify: `Keil_Project/Hardware/Tft_Driver.c` (Tft_Driver_Init 末尾追加)

- [ ] **Step 1: 在 `Tft_Driver_Init()` 末尾 (return 之前) 追加 Font Header 加载**

定位 `Tft_Driver_Init()` 函数末尾 (约第 310 行)，在 `return;` 或最后一个函数调用之后追加:

```c
    /* ── Flash 字库头部校验: 魔数+CRC32, 失败则 g_font_flash_valid=0 走 ROM 回退 */
    {
        uint32_t crc_stored; uint32_t crc_computed;
        W25Q_Driver_Read(W25Q_ADDR_FONT, (uint8_t*)&g_font_header, sizeof(Font_Header));
        if (g_font_header.magic == FONT_MAGIC) {
            crc_stored = g_font_header.crc32; g_font_header.crc32 = 0;
            crc_computed = CRC32_Compute((uint8_t*)&g_font_header + 0x0C, 20);
            g_font_header.crc32 = crc_stored;
            g_font_flash_valid = (crc_stored == crc_computed) ? 1 : 0;
        }
    }
```

> 需要 extern 声明 CRC32_Compute: 在 Tft_Driver.c 顶部 #include 区域追加:
> ```c
> extern uint32_t CRC32_Compute(const uint8_t *data, uint32_t len);  /* App_Storage.c */
> ```

- [ ] **Step 2: 构建验证**

Keil → F7 Rebuild
Expected: `0 Error(s), 0 Warning(s)`

- [ ] **Step 3: 提交**

```bash
git add Keil_Project/Hardware/Tft_Driver.c
git commit -m "feat: Tft_Driver_Init 启动加载 Flash 字库 Header (CRC32 校验)"
```

---

### Task 5: 最终构建验证 + 整机集成测试

**Files:**
- (无新建文件, 全量 Rebuild)

- [ ] **Step 1: 清理 Keil 编译产物**

Run: `cmd.exe /c Keil_Project\keilkill.bat`

- [ ] **Step 2: 全量 Rebuild**

Keil → Project → Rebuild all target files (F7)
Expected: `0 Error(s), 0 Warning(s)`

核对 Code/RO-data/RW-data/ZI-data 对比:
- RO-data 预期减少: 原 `TFT_Font_Data.h` 字库 ~13KB 仍保留 (ROM 回退), +新代码 ~200B
- Total 预期微增 ~200B

- [ ] **Step 3: 记录构建日志**

```bash
git add Keil_Project/Objects/Project.build_log.htm
git commit -m "build: V4.3.1 Flash 字库集成 — 编译通过 0E0W"
```

---

### Task 6: 版本号更新 + CLAUDE.md 对齐

**Files:**
- Modify: `CLAUDE.md`
- Modify: 所有涉及 V4.3.0→V4.3.1 的文件头注释

- [ ] **Step 1: 更新 CLAUDE.md 版本号**

版本号: V4.3.0 → V4.3.1
审查历史: 追加 V4.3.1 条目

```markdown
| V4.3.1 | CH341+Python Flash 字库烧录: generate_font.py(GB2312 6763字+图标 2MB镜像) + burn_flash.py(flashrom 备份+擦除+烧写+逐字节校验) + W25Q_Font_Index_Binary_Search(总线独占二分检索 5.85μs/字) + Tft_Driver Flash/ROM 双路径(单字单检索 16×提速) + Font_Header CRC32 小端序铁律 |
```

- [ ] **Step 2: 提交**

```bash
git add CLAUDE.md
git commit -m "docs: V4.3.1 CLAUDE.md 更新 — CH341 Flash 字库集成"
```
