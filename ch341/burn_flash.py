#!/usr/bin/env python3
"""
burn_flash.py — CH341A + flashrom 字库烧录编排
V5.1.3  2026-07-26  统一发布版本和目录约束
V5.1.2  2026-07-26  全字库校验、强制新备份、分区写入

用法: python ch341/burn_flash.py

前置: 1. Zadig 已将 CH341A 驱动换为 WinUSB
      2. flashrom.exe 在 PATH 中 (1.4-devel 社区编译版)
      3. CH341A 已夹到 W25Q128 排针 (CS/CLK/MOSI/MISO/GND/3.3V)
      4. STM32 必须完全断电 (防止 SPI 总线冲突导致 flashrom 无法识别芯片)

工作流: CRC32 自测 → 检测 flashrom → 调用 generate_font.py → 备份全片 16MB
        → 合并 font_data.bin → 仅写字库分区 → 读回完整分区校验 → 清理临时文件
"""

import os
import subprocess
import sys
from datetime import datetime

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
FONT_BIN     = os.path.join(SCRIPT_DIR, "font_data.bin")
RUN_TIMESTAMP = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
BACKUP_BIN   = os.path.join(SCRIPT_DIR, f"backup_{RUN_TIMESTAMP}_16MB.bin")
VERIFY_BIN   = os.path.join(SCRIPT_DIR, "verify_readback.bin")
MERGED_BIN   = os.path.join(SCRIPT_DIR, "merged_flash.bin")
FONT_SIZE    = 2 * 1024 * 1024       # 字库分区 2MB
CHIP_SIZE    = 16 * 1024 * 1024      # W25Q128 全片 16MB
VERIFY_LEN   = FONT_SIZE             # 校验整个 2MB 字库分区
LAYOUT_FILE  = os.path.join(SCRIPT_DIR, "layout.txt")
WRITE_TIMEOUT = 3600                 # 2MB 写入超时 1 小时 (CH341A SPI 约 0.5KB/s)
# 优先级: 本地 flashrom 目录 > PATH
_FLASHROM_LOCAL = os.path.join(SCRIPT_DIR, "flashrom-1.4", "flashrom.exe")
FLASHROM = _FLASHROM_LOCAL if os.path.exists(_FLASHROM_LOCAL) else "flashrom"

# Windows 子进程窗口抑制标志
_NO_WIN = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def compute_crc32(data: bytes) -> int:
    """STM32 CRC32_Compute 同款: poly=0x04C11DB7, init=0xFFFFFFFF, refin=false, xorout=0xFFFFFFFF"""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b << 24)
        for _ in range(8):
            crc = (crc << 1) ^ 0x04C11DB7 if crc & 0x80000000 else crc << 1
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


def run_flashrom(args: list, desc: str, timeout: int = 300):
    """调用 flashrom, 实时输出进度条, 失败时退出"""
    cmd = [FLASHROM, "-p", "ch341a_spi"] + args
    print(f"[..] {desc}")
    print(f"     {' '.join(cmd)}")
    # 不捕获输出 — 让 flashrom 进度条直接刷终端
    try:
        result = subprocess.run(cmd, timeout=timeout, creationflags=_NO_WIN)
    except FileNotFoundError:
        print(f"[FAIL] 未找到 flashrom: {FLASHROM}")
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print(f"[FAIL] {desc} (超时 {timeout}s)")
        sys.exit(1)
    if result.returncode != 0:
        print(f"[FAIL] {desc} (exit={result.returncode})")
        sys.exit(1)
    print(f"[OK]  {desc}")


def main():
    """烧录编排主函数"""

    # ═══════════════════════════════════════════════════════════════
    # Step 0: CRC32 自测 (fail-fast 防算法不一致)
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 0/5: CRC32 自测 ====]")
    print("[..] CRC32('1234') 期望 0x596A3B55 (STM32 refin=false)")
    if compute_crc32(b"1234") != 0x596A3B55:
        print("[FAIL] CRC32 自测失败，与 STM32 CRC32_Compute 算法参数不一致")
        sys.exit(1)
    print("[OK]  CRC32 自测通过: STM32 CRC32('1234') = 0x596A3B55")

    # ═══════════════════════════════════════════════════════════════
    # Step 1: 检测 flashrom
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 1/5: 检测 flashrom ====]")
    try:
        result = subprocess.run([FLASHROM, "--version"], capture_output=True,
                                text=True, timeout=30, creationflags=_NO_WIN)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        print("[FAIL] flashrom 未找到或响应超时")
        sys.exit(1)
    if result.returncode != 0:
        print("[FAIL] flashrom 未找到!")
        print("       请下载 flashrom 1.4-devel 社区编译版并加入 PATH")
        print("       下载: https://winraid.level1techs.com/ (搜索 flashrom-1.4)")
        print("       或: https://github.com/nocomp/flashrom-ch341a/releases")
        sys.exit(1)
    # 提取版本行
    ver_line = result.stdout.strip().split('\n')[0] if result.stdout.strip() else "unknown"
    print(f"[OK]  flashrom 可用: {ver_line}")

    # ═══════════════════════════════════════════════════════════════
    # Step 2: 生成字库镜像 (调用 generate_font.py)
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 2/5: 生成字库镜像 ====]")
    gen_script = os.path.join(SCRIPT_DIR, "generate_font.py")
    if not os.path.exists(gen_script):
        print(f"[FAIL] 未找到 generate_font.py: {gen_script}")
        sys.exit(1)

    result = subprocess.run([sys.executable, gen_script],
                            capture_output=True, text=True,
                            timeout=600, cwd=SCRIPT_DIR,
                            creationflags=_NO_WIN)
    if result.returncode != 0:
        print("[FAIL] generate_font.py 执行失败:")
        if result.stdout.strip():
            print(result.stdout)
        if result.stderr.strip():
            print(result.stderr)
        sys.exit(1)
    print(result.stdout.strip())

    # 验证产物
    if not os.path.exists(FONT_BIN):
        print(f"[FAIL] font_data.bin 未生成, 请检查 generate_font.py 输出")
        sys.exit(1)
    font_size = os.path.getsize(FONT_BIN)
    if font_size != FONT_SIZE:
        print(f"[FAIL] font_data.bin 大小异常: {font_size} 字节 (期望 {FONT_SIZE} = 2MB)")
        sys.exit(1)
    print(f"[OK]  font_data.bin 大小正确: {font_size} 字节 = 2MB")

    # ═══════════════════════════════════════════════════════════════
    # Step 3: 备份全片 Flash 16MB
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 3/5: 备份全片 Flash 16MB ====]")
    # 每次烧录前都重新读取, 绝不使用可能过期的旧备份
    run_flashrom(["-r", BACKUP_BIN], f"读取全片 Flash → {BACKUP_BIN}")
    backup_size = os.path.getsize(BACKUP_BIN)
    if backup_size != CHIP_SIZE:
        print(f"[FAIL] 备份大小 {backup_size} 字节 ≠ {CHIP_SIZE} (16MB), 可能接线不良或芯片故障")
        print("      请检查: 1. CH341A 跳线帽在 3.3V 位置  2. 排针接触良好  3. STM32 已断电")
        sys.exit(1)
    print(f"[OK]  备份就绪: {BACKUP_BIN} ({CHIP_SIZE} 字节 = 16MB)")

    # ═══════════════════════════════════════════════════════════════
    # Step 4: 合并字库镜像，仅写入 2MB 字库分区
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 4/5: 合并字库 → 写入 2MB 字库分区 ====]")

    # 读取备份
    print("[..] 读取备份镜像...")
    with open(BACKUP_BIN, "rb") as f:
        backup = bytearray(f.read())

    if len(backup) != CHIP_SIZE:
        print(f"[FAIL] 备份数据长度异常: {len(backup)} ≠ {CHIP_SIZE}")
        sys.exit(1)

    # 读取字库
    print("[..] 读取字库镜像...")
    with open(FONT_BIN, "rb") as f:
        font_data_verify = f.read()

    # 将 font_data.bin 覆盖到备份的前 2MB (保护配置分区 + 黑匣子日志)
    print(f"[..] 覆盖字库到镜像前 2MB (地址 0x000000~0x{FONT_SIZE - 1:06X})...")
    backup[0:FONT_SIZE] = font_data_verify

    # 计算字库区 CRC32 (写入后读回比对用)
    font_crc = compute_crc32(bytes(backup[0:FONT_SIZE]))
    print(f"     字库区 CRC32: 0x{font_crc:08X} (覆盖 0x000000~0x{FONT_SIZE - 1:06X})")

    # 合并镜像保留备份中的配置与日志, 实际只写入字库分区
    with open(MERGED_BIN, "wb") as f:
        f.write(backup)
    print(f"[..] 合并镜像已生成: {MERGED_BIN} ({len(backup)} 字节)")
    print("[..] 仅写入前 2MB 字库分区, 不擦除后续配置与日志...")

    run_flashrom(["-l", LAYOUT_FILE, "-i", "font", "-w", MERGED_BIN],
                 f"烧写字库分区 ← {MERGED_BIN}",
                 timeout=WRITE_TIMEOUT)

    # ═══════════════════════════════════════════════════════════════
    # Step 5: 读回校验 (逐字节比对字库分区)
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 5/5: 读回逐字节校验 (字库分区) ====]")
    run_flashrom(["-r", VERIFY_BIN], f"读回全片 → {VERIFY_BIN}",
                 timeout=600)

    print("[..] 逐字节比对字库分区 (0x000000~0x{:06X}, {:,} 字节)...".format(
        VERIFY_LEN - 1, VERIFY_LEN))

    with open(VERIFY_BIN, "rb") as f:
        verify_data = f.read()

    if len(verify_data) != CHIP_SIZE:
        print(f"[FAIL] 读回镜像长度异常: {len(verify_data)} ≠ {CHIP_SIZE}")
        sys.exit(1)

    mismatch_count = 0
    first_mismatch = -1

    # 校验整个字库分区, 配置区和黑匣子日志不参与比对
    cmp_len = VERIFY_LEN
    for i in range(cmp_len):
        if verify_data[i] != backup[i]:
            if mismatch_count < 20:  # 最多打印 20 条差异
                print(f"  [DIFF] addr=0x{i:06X}  wrote=0x{backup[i]:02X}  read=0x{verify_data[i]:02X}")
            mismatch_count += 1
            if first_mismatch < 0:
                first_mismatch = i

    if mismatch_count == 0:
        print("\n" + "=" * 64)
        print("   [PASS] 字库烧录成功! 所有 {:,} 字节完全一致".format(cmp_len))
        print("=" * 64)
        print("\n  后续操作:")
        print("  1. 断开 CH341A USB")
        print("  2. STM32 重新上电")
        print("  3. 屏幕应显示全字库 (20897 汉字)")
        print("  4. 若 Magic 或 CRC 不匹配，固件自动回退到片内 ROM 必要字库")
    else:
        fail_pct = mismatch_count * 100.0 / cmp_len
        print("\n" + "=" * 64)
        print(f"   [FAIL] 校验失败: {mismatch_count:,} 字节不一致 ({fail_pct:.2f}%)")
        print(f"          首个差异地址: 0x{first_mismatch:06X}")
        print("=" * 64)
        print("\n  请检查:")
        print("  1. CH341A 跳线帽是否在 3.3V 位置 (5V 会损坏 W25Q128)")
        print("  2. 排针接触是否良好 (重新夹紧再试)")
        print("  3. STM32 是否已完全断电 (USB/电源全部断开)")
        print("  4. 备份文件仍保留: {} (可重新烧录)".format(BACKUP_BIN))

    # ═══════════════════════════════════════════════════════════════
    # 清理临时文件
    # ═══════════════════════════════════════════════════════════════
    for tmp_file in [MERGED_BIN, VERIFY_BIN]:
        if os.path.exists(tmp_file):
            os.remove(tmp_file)
            print(f"[..] 已清理临时文件: {tmp_file}")

    # font_data.bin 保留 (本地生成产物, 可复用)
    # 时间戳备份永久保留, 用于失败后恢复原始芯片内容

    if mismatch_count != 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
