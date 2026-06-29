#!/usr/bin/env python3
"""burn_splash.py — 烧录开机动画到 W25Q128 SPLASH 分区 (0x200000, 1MB)
V1.2 2026-06-28  全片合并写入 (layout 模式不可靠, 改用已验证的全片读→叠加→写)

用法: python burn_splash.py
前置: generate_splash.py 已生成 splash.bin, STM32 已断电
"""

import subprocess, os, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SPLASH_BIN = os.path.join(SCRIPT_DIR, "splash.bin")
BACKUP_BIN = os.path.join(SCRIPT_DIR, "splash_backup_16MB.bin")
MERGED_BIN = os.path.join(SCRIPT_DIR, "splash_merged.bin")
FLASHROM   = os.path.join(SCRIPT_DIR, "flashrom-1.4", "flashrom.exe")
if not os.path.exists(FLASHROM):
    FLASHROM = "flashrom"
NO_WIN = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
SPLASH_ADDR  = 0x200000
SPLASH_SIZE  = 1 * 1024 * 1024
CHIP_SIZE    = 16 * 1024 * 1024
READ_TIMEOUT = 600   # 全片读取 10 分钟
WRITE_TIMEOUT = 3600  # 全片写入 1 小时


def run_flashrom(args, desc, timeout=300):
    cmd = [FLASHROM, "-p", "ch341a_spi"] + args
    print(f"[..] {desc}")
    print(f"     {' '.join(cmd)}")
    r = subprocess.run(cmd, timeout=timeout, creationflags=NO_WIN)
    if r.returncode != 0:
        print(f"[FAIL] {desc} (exit={r.returncode})")
        sys.exit(1)
    print(f"[OK]  {desc}")


def main():
    if not os.path.exists(SPLASH_BIN):
        print("请先运行 generate_splash.py")
        sys.exit(1)
    size = os.path.getsize(SPLASH_BIN)
    if size > SPLASH_SIZE:
        print(f"SPLASH 大小 {size} > 1MB!")
        sys.exit(1)
    print(f"[OK]  splash.bin: {size} 字节")

    # Step 1: 备份全片 (有则跳过)
    print("\n[Step 1/3] 备份全片 Flash 16MB...")
    if os.path.exists(BACKUP_BIN) and os.path.getsize(BACKUP_BIN) == CHIP_SIZE:
        print(f"[WARN] 已有备份, 跳过读取")
    else:
        run_flashrom(["-r", BACKUP_BIN], "备份全片 Flash", timeout=READ_TIMEOUT)
    print(f"[OK]  备份就绪: {BACKUP_BIN} ({CHIP_SIZE} 字节)")

    # Step 2: 叠加 SPLASH → 写入全片
    print("\n[Step 2/3] 叠加 SPLASH → 写入全片...")
    with open(BACKUP_BIN, "rb") as f:
        chip = bytearray(f.read())
    with open(SPLASH_BIN, "rb") as f:
        splash = f.read()
    chip[SPLASH_ADDR : SPLASH_ADDR + len(splash)] = splash
    with open(MERGED_BIN, "wb") as f:
        f.write(chip)
    print(f"[..] 合并镜像: {MERGED_BIN}")
    print("[..] 预计耗时 ~30-60 分钟, 请耐心等待...")
    run_flashrom(["-w", MERGED_BIN], "写入全片 Flash", timeout=WRITE_TIMEOUT)

    # Step 3: 读回校验 SPLASH 分区
    print("\n[Step 3/3] 读回校验 SPLASH 分区...")
    verify_bin = os.path.join(SCRIPT_DIR, "splash_verify.bin")
    run_flashrom(["-r", verify_bin], "读回全片", timeout=READ_TIMEOUT)

    with open(SPLASH_BIN, "rb") as f:
        original = f.read()
    with open(verify_bin, "rb") as f:
        full_verify = f.read()

    err = 0
    for i in range(len(original)):
        vi = SPLASH_ADDR + i
        if vi < len(full_verify) and full_verify[vi] != chip[vi]:
            err += 1
            if err <= 10:
                print(f"  [DIFF] addr=0x{vi:06X} wrote=0x{chip[vi]:02X} read=0x{full_verify[vi]:02X}")

    if err:
        print(f"\n[FAIL] SPLASH 校验失败: {err} 字节不一致!")
        sys.exit(1)

    print(f"\n{'=' * 50}")
    print("   [PASS] SPLASH 开机动画烧录成功!")
    print(f"{'=' * 50}")

    # 清理
    for tmp in [MERGED_BIN, verify_bin]:
        if os.path.exists(tmp):
            os.remove(tmp)
    print("[..] 已清理临时文件")
    print("\n后续: 断开 CH341A → STM32 上电 → 应看到 5 帧 fade-in 开机动画")


if __name__ == "__main__":
    main()
