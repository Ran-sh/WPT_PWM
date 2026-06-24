#!/usr/bin/env python3
"""burn_splash.py — 烧录开机动画到 W25Q128 SPLASH 分区 (0x200000, 1MB)"""

import subprocess, os, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SPLASH_BIN = os.path.join(SCRIPT_DIR, "splash.bin")
FLASHROM   = os.path.join(SCRIPT_DIR, "flashrom-1.4", "flashrom.exe")
if not os.path.exists(FLASHROM):
    FLASHROM = "flashrom"
NO_WIN = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
SPLASH_ADDR = 0x200000

def run(cmd, desc):
    print(f"[..] {desc}")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120, creationflags=NO_WIN)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        sys.exit(1)
    print(f"[OK] {desc}")

def main():
    if not os.path.exists(SPLASH_BIN):
        print("请先运行 generate_splash.py")
        sys.exit(1)
    size = os.path.getsize(SPLASH_BIN)
    if size > 1 * 1024 * 1024:
        print(f"SPLASH 大小 {size} > 1MB!")
        sys.exit(1)

    backup = os.path.join(SCRIPT_DIR, "splash_backup.bin")
    print("[Step 1/3] 备份全片...")
    run([FLASHROM, "-p", "ch341a_spi", "-r", backup, "-c", "W25Q128.V"], "备份")

    fb = bytearray(open(backup, "rb").read())
    spl = open(SPLASH_BIN, "rb").read()
    fb[SPLASH_ADDR : SPLASH_ADDR + len(spl)] = spl
    merged = os.path.join(SCRIPT_DIR, "splash_merged.bin")
    open(merged, "wb").write(fb)

    print("[Step 2/3] 写入 SPLASH 分区...")
    run([FLASHROM, "-p", "ch341a_spi", "-w", merged, "-c", "W25Q128.V"], "写入")

    print("[Step 3/3] 读回校验...")
    run([FLASHROM, "-p", "ch341a_spi", "-r", backup, "-c", "W25Q128.V"], "读回")
    vb = open(backup, "rb").read()
    err = sum(1 for i in range(SPLASH_ADDR, SPLASH_ADDR + len(spl)) if vb[i] != fb[i])
    if err:
        print(f"[FAIL] {err} 字节不一致!")
        sys.exit(1)
    print("[PASS] SPLASH 烧录成功!")

    os.remove(merged)
    os.remove(backup)

if __name__ == "__main__":
    main()
