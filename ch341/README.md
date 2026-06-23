# CH341A 字库烧录操作指南

> 适用项目: WPT_PWM V4.3.0 | 目标芯片: W25Q128 (16MB SPI NOR Flash) | 日期: 2026-06-23

本指南用于将 GB2312 全字库 (6763 汉字 + 95 ASCII + 图标动画) 通过 CH341A USB-SPI 编程器烧录到板载 W25Q128 Flash 芯片。

---

## 1. 硬件清单

| 序号 | 物品 | 数量 | 说明 |
|:---|:---|:---|:---|
| 1 | CH341A 编程器 | 1 个 | USB-SPI 转换器, 黑色/金色外壳均可 |
| 2 | 杜邦线 (母对母) | 6 根 | 连接 CH341A 排针到 W25Q128 板端排针 |
| 3 | W25Q128 芯片 | 1 片 | **已焊接在目标板上, 无需拆卸** |
| 4 | USB 数据线 | 1 根 | 连接 CH341A 到电脑 (通常随编程器附赠) |

---

## 2. 接线表

> **关键原则: 直接连接 W25Q128 板端排针, STM32 必须完全断电。**

### 2.1 CH341A 编程器 → W25Q128 板端

CH341A 编程器排针位置丝印清晰, 对照下表接线:

| CH341A 引脚 | 杜邦线颜色 (建议) | W25Q128 板端信号 | 目标板触点 | 备注 |
|:---|:---|:---|:---|:---|
| **CS** | 白 | `/CS` | **PA12** | 片选, 低电平有效 |
| **CLK** (SCK) | 黄 | `SCK` | **PA5** | SPI 时钟 |
| **MOSI** (SI/MOSI) | 蓝 | `MOSI` (DI) | **PA7** | 主出从入, 数据写入 |
| **MISO** (SO/MISO) | 绿 | `MISO` (DO) | **PA6** | 主入从出, 数据读取 |
| **GND** | 黑 | `GND` | 板端 GND | 共地 |
| **3.3V** (VCC) | 红 | `VCC` | 板端 3.3V | 供电 (见下方警告) |

### 2.2 接线示意图

```
CH341A 编程器                          目标板 (W25Q128 已焊接)
┌─────────────────┐                   ┌──────────────────────┐
│ CS    ───── 白 ──────────────────── PA12  (FLASH_CS)      │
│ CLK   ───── 黄 ──────────────────── PA5   (SPI1_SCK)      │
│ MOSI  ───── 蓝 ──────────────────── PA7   (SPI1_MOSI)     │
│ MISO  ───── 绿 ──────────────────── PA6   (SPI1_MISO)     │
│ GND   ───── 黑 ──────────────────── GND                   │
│ 3.3V  ───── 红 ──────────────────── 3.3V                  │
└─────────────────┘                   └──────────────────────┘
```

---

## 3. 关键警告

### 3.1 电压选择 (生死攸关)

CH341A 编程器上有 **3.3V / 5V 跳线帽**, 必须插在 **3.3V** 位置。

W25Q128 工作电压为 2.7V ~ 3.6V, 5V 供电会**永久损坏芯片**。烧录前请反复确认跳线帽位置。

### 3.2 STM32 必须完全断电

烧录期间 SPI 总线仅由 CH341A 独占。如果 STM32 也上电, 其 GPIO 引脚会驱动总线, 导致:

- flashrom 无法识别 W25Q128 (报 "No EEPROM/flash device found")
- 数据写入错误
- 可能的硬件损坏

**操作顺序**:
1. 断开 STM32 板所有电源 (USB 线 + 外部电源)
2. 等待 5 秒确保电容放电
3. 连接 CH341A 排针
4. 插入 CH341A USB 线

### 3.3 排针接触

杜邦线务必插紧, 虚接会导致校验失败。烧录后如果屏幕无变化, 首先要排查接线。

---

## 4. 驱动安装 (Zadig, 一次性)

Windows 默认将 CH341A 识别为串口设备, flashrom 需要 **WinUSB** 驱动才能通过 SPI 协议直接访问芯片。

### 4.1 下载 Zadig

官方下载: https://zadig.akeo.ie/

选择 `Zadig-2.9.exe` (或最新版)。

### 4.2 替换驱动

1. 插上 CH341A USB 线 (不必连接目标板)
2. 运行 Zadig.exe
3. 菜单 → `Options` → 勾选 `List All Devices`
4. 下拉列表中选择 `CH341A` (或 `USB CH341A`)
5. 驱动框中确认显示 **WinUSB** (如不是, 点击上下箭头切换)
6. 点击 `Replace Driver` 按钮
7. 等待进度条完成 (约 30 秒)

成功后, 设备管理器中 CH341A 将从 "端口 (COM & LPT)" 移到 "Universal Serial Bus devices"。

**注意:** 如果后续想用 CH341A 的串口功能, 可以再次运行 Zadig 换回 `usbser` 驱动。两种驱动随时可换, 不影响硬件。

---

## 5. flashrom 下载与安装

### 5.1 下载

W25Q128 支持需要较新版本的 flashrom (1.4-devel 社区编译版)。推荐下载地址:

- **WinRAID 论坛**: https://winraid.level1techs.com/ (搜索 `flashrom-1.4`)
- **GitHub**: https://github.com/nocomp/flashrom-ch341a/releases

下载 `flashrom-windows-x64.zip` 解压后得到 `flashrom.exe`。

### 5.2 加入 PATH

**方法 A (推荐):** 将 `flashrom.exe` 放到一个固定目录 (如 `C:\Tools\flashrom\`), 然后将该目录加入系统 PATH:

1. Win+R → `sysdm.cpl` → 高级 → 环境变量
2. 在 "系统变量" 中找到 `Path` → 编辑 → 新建 → `C:\Tools\flashrom`
3. 确定保存

**方法 B:** 直接将 `flashrom.exe` 复制到 `ch341\` 目录下 (脚本会搜索当前目录)。

### 5.3 验证安装

打开新的命令行窗口, 输入:

```bash
flashrom --version
```

应该输出类似 `flashrom v1.4-devel ... on Windows ...` 的信息。如果提示 "不是内部或外部命令", 请检查 PATH 设置或重启命令行。

---

## 6. Python 环境

### 6.1 Python 版本

需要 **Python 3.10** 或更高版本: https://www.python.org/downloads/

安装时勾选 "Add Python to PATH"。

### 6.2 安装依赖

```bash
pip install -r ch341/requirements.txt
```

当前依赖: `Pillow >= 10.0.0` (用于生成字库位图, Windows 自动加载系统宋体 simsun.ttc)。

---

## 7. 烧录步骤

### 7.1 烧录前检查清单

- [ ] CH341A 跳线帽在 **3.3V** 位置
- [ ] 6 根杜邦线按接线表正确连接
- [ ] STM32 目标板 **完全断电** (USB 线已拔)
- [ ] Zadig WinUSB 驱动已安装 (设备管理器确认)
- [ ] `flashrom --version` 正常输出
- [ ] `python --version` ≥ 3.10

### 7.2 步骤一 (可选): 仅测试字库生成

此命令验证 Python 环境和字体渲染是否正常, 生成 `font_data.bin` 但不烧录:

```bash
python ch341/generate_font.py
```

输出应包含:
- `[OK] CRC32 自测通过`
- `[OK] 加载字体: C:\Windows\Fonts\simsun.ttc`
- `[OK] 生成 font_data.bin (2097152 字节 = 2MB)`

### 7.3 步骤二: 完整烧录

```bash
python ch341/burn_flash.py
```

脚本自动执行以下 5 步:

| 步骤 | 操作 | 耗时 | 说明 |
|:---|:---|:---|:---|
| Step 0 | CRC32 自测 | <1s | 验证 Python 与 STM32 CRC32 算法一致 |
| Step 1 | 检测 flashrom | <1s | 确认 flashrom 可用 |
| Step 2 | 生成字库镜像 | ~3min | 渲染 6763 汉字 + 95 ASCII + 图标 |
| Step 3 | 备份全片 16MB | ~3min | 读取 → `backup_16MB.bin` (安全防线) |
| Step 4 | 写入全片 Flash | ~5min | 字库覆盖前 2MB, 其余保持原样 |
| Step 5 | 读回逐字节校验 | ~3min | 逐字节比对前 248KB, 0 差异才算通过 |

**总耗时: 约 12~15 分钟** (主要取决于 USB 速度和字库渲染)。

### 7.4 烧录成功标志

```
================================================================
   [PASS] 字库烧录成功! 所有 253,952 字节完全一致
================================================================

  后续操作:
  1. 断开 CH341A USB
  2. STM32 重新上电
  3. 屏幕应显示 GB2312 全字库 (6763 汉字)
  4. 若 Magic 不匹配, 自动回退到片内 ROM 76 字
```

### 7.5 烧录后操作

1. 拔掉 CH341A USB 线
2. 拔掉 6 根杜邦线 (或保持连接, 但不影响正常使用)
3. 给 STM32 目标板上电
4. 观察 TFT 屏幕: 正常显示全字库中文, 不再出现缺字方块

---

## 8. 故障排除

| 现象 | 可能原因 | 解决方法 |
|:---|:---|:---|
| `flashrom: No EEPROM/flash device found` | CH341A 驱动不是 WinUSB | 重新运行 Zadig, 确认选择 WinUSB 后点击 Replace Driver |
| 同上 | CH341A 跳线帽在 5V 位置 | 立即断电, 将跳线帽改到 3.3V 位置 |
| 同上 | STM32 未断电, SPI 总线冲突 | 拔掉 STM32 所有电源, 等待 5 秒后重试 |
| 同上 | 杜邦线接触不良或接错 | 对照接线表逐根检查, 重新插紧 |
| `flashrom: No such file or directory` | flashrom.exe 未加入 PATH | 按 §5.2 配置 PATH, 或重启命令行 |
| `generate_font.py` 报错 `未找到 simsun.ttc` | 系统缺少宋体 | Win10+ 默认自带; 如精简版缺失可手动下载 simsun.ttc |
| 校验失败 (>0 字节不一致) | 杜邦线虚接 | 重新插紧所有排针, 尤其 GND 和 3.3V |
| 同上 | 供电不足 (USB 延长线过长) | CH341A 直插电脑 USB 口, 不要用无源 HUB |
| 同上 | 跳线帽误插 5V (芯片内部已受损) | 断电, 换到 3.3V 重试; 如反复失败芯片可能已损坏 |
| 烧录成功但屏幕无变化 | STM32 固件未启用 Flash 字库 | 确认固件版本 ≥ V4.3.0, 已编译 App_Storage Flash 字库模块 |
| 屏幕白屏 (无任何显示) | 字库 Magic 校验失败, Flash 初始化卡住 | 检查 backup_16MB.bin 是否被误擦除, 可用 flashrom 恢复备份 |
| 同上 | SPI1 引脚 PA6 被 W25Q128 占用, TFT 无法通信 | 烧录后务必拔掉 CH341A 排针 (尤其 MISO/PA6) 再给 STM32 上电 |

### 8.1 从备份恢复

`burn_flash.py` 在 Step 3 会自动备份全片 16MB 到 `ch341/backup_16MB.bin`。如果烧录后出现异常, 可用以下命令恢复:

```bash
flashrom -p ch341a_spi -w ch341/backup_16MB.bin
```

此命令将芯片完整恢复到烧录前的状态。

---

## 9. 文件说明

| 文件 | 用途 |
|:---|:---|
| `ch341/generate_font.py` | 字库生成: PIL 渲染 GB2312 一级汉字 6763 字 + ASCII 95 字 + 图标 54 帧 → `font_data.bin` (2MB) |
| `ch341/burn_flash.py` | 烧录编排: CRC32 自测 → 调用字库生成 → 备份全片 → 写入 → 逐字节校验 |
| `ch341/font_data.bin` | 生成产物: 2MB 字库镜像 (格式对齐 W25Q128 Flash 布局) |
| `ch341/backup_16MB.bin` | 安全备份: 烧录前全片 16MB 读取, **请勿删除**, 是恢复的最后防线 |
| `ch341/requirements.txt` | Python 依赖: Pillow ≥ 10.0.0 |

临时文件 (`merged_flash.bin`, `verify_readback.bin`) 在烧录成功后自动清理。

---

## 10. 参考

- CH341A 编程器数据手册: https://www.wch.cn/products/CH341.html
- flashrom 官方文档: https://www.flashrom.org/
- W25Q128 数据手册: https://www.winbond.com/
- WPT 项目设计文档: `Claude_Files/docs/2026-06-22-w25q128-flash-integration-design.md`
