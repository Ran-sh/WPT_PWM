#!/usr/bin/env python3
"""
burn_flash.py — CH341A + flashrom 字库烧录编排
V1.0  2026-06-23

用法: python ch341/burn_flash.py

前置: 1. Zadig 已将 CH341A 驱动换为 WinUSB
      2. flashrom.exe 在 PATH 中 (1.4-devel 社区编译版)
      3. CH341A 已夹到 W25Q128 排针 (CS/CLK/MOSI/MISO/GND/3.3V)
      4. STM32 必须完全断电 (防止 SPI 总线冲突导致 flashrom 无法识别芯片)

工作流: CRC32 自测 → 检测 flashrom → 调用 generate_font.py → 备份全片 16MB
        → 叠加 font_data.bin → 写入全片 → 读回逐字节校验 → 清理临时文件
"""

import subprocess, sys, os, struct, zlib

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
FONT_BIN     = os.path.join(SCRIPT_DIR, "font_data.bin")
BACKUP_BIN   = os.path.join(SCRIPT_DIR, "backup_16MB.bin")
VERIFY_BIN   = os.path.join(SCRIPT_DIR, "verify_readback.bin")
MERGED_BIN   = os.path.join(SCRIPT_DIR, "merged_flash.bin")
FONT_SIZE    = 2 * 1024 * 1024       # 字库分区 2MB
CHIP_SIZE    = 16 * 1024 * 1024      # W25Q128 全片 16MB
VERIFY_LEN   = 248 * 1024            # 校验范围: 前 248KB (字库有效数据区)
ERASE_BLOCKS = 62                    # 248KB = 62 * 4KB sectors
FLASHROM     = "flashrom"

# Windows 子进程窗口抑制标志
_NO_WIN = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def compute_crc32(data: bytes) -> int:
    """zlib.crc32 → uint32, 与 STM32 CRC32_Compute 多项式/初值/final XOR 一致"""
    return zlib.crc32(data) & 0xFFFFFFFF


def run_flashrom(args: list, desc: str):
    """调用 flashrom, 失败时打印 stderr/stdout 并退出"""
    cmd = [FLASHROM, "-p", "ch341a_spi"] + args
    print(f"[..] {desc}")
    print(f"     {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=300, creationflags=_NO_WIN)
    if result.returncode != 0:
        print(f"[FAIL] {desc}")
        if result.stdout.strip():
            print(result.stdout)
        if result.stderr.strip():
            print(result.stderr)
        sys.exit(1)
    print(f"[OK]  {desc}")


def main():
    """烧录编排主函数"""

    # ═══════════════════════════════════════════════════════════════
    # Step 0: CRC32 自测 (fail-fast 防算法不一致)
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 0/5: CRC32 自测 ====]")
    print("[..] CRC32('1234') 期望 0x9BE3E0A3")
    assert compute_crc32(b"1234") == 0x9BE3E0A3, \
        "CRC32 自测失败! Python zlib.crc32 与 STM32 CRC32_Compute 算法参数不一致"
    print("[OK]  CRC32 自测通过: CRC32('1234') = 0x9BE3E0A3")

    # ═══════════════════════════════════════════════════════════════
    # Step 1: 检测 flashrom
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 1/5: 检测 flashrom ====]")
    result = subprocess.run([FLASHROM, "--version"], capture_output=True,
                            text=True, timeout=30, creationflags=_NO_WIN)
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
    if os.path.exists(BACKUP_BIN):
        backup_size = os.path.getsize(BACKUP_BIN)
        if backup_size == CHIP_SIZE:
            print(f"[WARN] 已有备份文件 {BACKUP_BIN} ({backup_size} 字节), 跳过读取")
            print(f"      如需强制重新备份, 请删除该文件后重试")
        else:
            print(f"[WARN] 已有备份文件但大小异常 ({backup_size} ≠ {CHIP_SIZE}), 重新读取")
            os.remove(BACKUP_BIN)
    if not os.path.exists(BACKUP_BIN):
        run_flashrom(["-r", BACKUP_BIN], f"读取全片 Flash → {BACKUP_BIN}")
        backup_size = os.path.getsize(BACKUP_BIN)
        if backup_size != CHIP_SIZE:
            print(f"[FAIL] 备份大小 {backup_size} 字节 ≠ {CHIP_SIZE} (16MB), 可能接线不良或芯片故障")
            print(f"      请检查: 1. CH341A 跳线帽在 3.3V 位置  2. 排针接触良好  3. STM32 已断电")
            sys.exit(1)
    print(f"[OK]  备份就绪: {BACKUP_BIN} ({CHIP_SIZE} 字节 = 16MB)")

    # ═══════════════════════════════════════════════════════════════
    # Step 4: 叠加字库 → 烧写全片 16MB
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 4/5: 叠加字库 → 写入全片 Flash ====]")

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
        font_data = bytearray(f.read())

    # 将 font_data.bin 覆盖到备份的前 2MB (保护配置分区 + 黑匣子日志)
    print(f"[..] 覆盖字库到镜像前 2MB (地址 0x000000~0x{0x200000:06X})...")
    backup[0:FONT_SIZE] = font_data

    # 计算字库区 CRC32 (写入后读回比对用)
    font_crc = compute_crc32(bytes(backup[0:ERASE_BLOCKS * 4096]))
    print(f"     字库区 CRC32: 0x{font_crc:08X} (覆盖 0x000000~0x{ERASE_BLOCKS * 4096:06X})")

    # 写入合并镜像
    with open(MERGED_BIN, "wb") as f:
        f.write(backup)
    print(f"[..] 合并镜像已生成: {MERGED_BIN} ({len(backup)} 字节)")

    run_flashrom(["-w", MERGED_BIN], f"烧写全片 Flash ← {MERGED_BIN}")

    # ═══════════════════════════════════════════════════════════════
    # Step 5: 读回校验 (逐字节比对前 248KB)
    # ═══════════════════════════════════════════════════════════════
    print("\n[==== Step 5/5: 读回逐字节校验 ====]")
    run_flashrom(["-r", VERIFY_BIN], f"读回校验 → {VERIFY_BIN}")

    print("[..] 逐字节比对字库分区 (0x000000~0x{:06X}, {:,} 字节)...".format(
        VERIFY_LEN, VERIFY_LEN))

    with open(VERIFY_BIN, "rb") as f:
        verify_data = f.read()

    mismatch_count = 0
    first_mismatch = -1

    # 仅校验字库有效数据区 (前 248KB), 配置区/黑匣子不参与比对
    cmp_len = min(VERIFY_LEN, len(verify_data), len(backup))
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
        print("  3. 屏幕应显示 GB2312 全字库 (6763 汉字)")
        print("  4. 若 Magic 不匹配, 自动回退到片内 ROM 76 字")
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
    # backup_16MB.bin 保留 (安全备份, 是用户的最后防线)

    if mismatch_count != 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
