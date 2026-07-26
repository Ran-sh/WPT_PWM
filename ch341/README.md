# CH341A 字库烧录操作指南

> 适用项目: WPT_PWM V5.1.2 (`5.0`分支) | 目标芯片: W25Q128 (16MB SPI NOR Flash) | 更新: 2026-07-26

> **V5.1.2 字库工具说明**: CRC32覆盖头部元数据和全部有效负载；烧录前强制生成新备份，仅写前2MB字库分区，读回时校验完整2MB。

本指南用于将 GB2312 全字库 (20897 汉字 + 95 ASCII + 图标动画) 通过 CH341A USB-SPI 编程器烧录到板载 W25Q128 Flash 芯片。

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
| **CS** | 白 | `/CS` | **PB12** | 片选, 低电平有效；固件上电钳位为高 |
| **CLK** (SCK) | 黄 | `SCK` | **PA5** | SPI 时钟 |
| **MOSI** (SI/MOSI) | 蓝 | `MOSI` (DI) | **PA7** | 主出从入, 数据写入 |
| **MISO** (SO/MISO) | 绿 | `MISO` (DO) | **PA6** | 主入从出, 数据读取 |
| **GND** | 黑 | `GND` | 板端 GND | 共地 |
| **3.3V** (VCC) | 红 | `VCC` | 板端 3.3V | 供电 (见下方警告) |

### 2.2 接线示意图

```
CH341A 编程器                          目标板 (W25Q128 已焊接)
┌─────────────────┐                   ┌──────────────────────┐
│ CS    ───── 白 ──────────────────── PB12  (FLASH_CS)      │
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

V5.1.2运行时由 `Spi1_Shared` 管理TFT和W25Q128的共享总线，但它不能解决外部CH341A与已上电STM32同时驱动的问题，因此烧录时仍必须让目标板完全断电。

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

### 4.1 Zadig (已打包)

项目 `ch341/flashrom-1.4/` 目录下已附带 `zadig-2.8.exe`, 直接双击运行:

```bash
ch341\flashrom-1.4\zadig-2.8.exe
```

**注意:** 网上流传的独立 `zadig-2.9.exe` 版本存在兼容性问题, 推荐使用附带版本。

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

## 5. flashrom — 无需额外下载 (已打包)

### 5.1 flashrom.exe 位置

`ch341/flashrom-1.4/flashrom.exe` — 已随项目分发, 无需单独下载。

```
ch341/
├── flashrom-1.4/
│   ├── flashrom.exe          ← 64位 (推荐)
│   ├── libwinpthread-1.dll   ← 运行时依赖
│   ├── zadig-2.8.exe         ← WinUSB 驱动替换工具
│   └── x32_flashrom/         ← 32位备选
│       └── flashrom.exe
├── generate_font.py
├── burn_flash.py
├── requirements.txt
└── README.md
```

`burn_flash.py` 启动时自动检测本地 `flashrom-1.4/flashrom.exe`, 无需配置 PATH。

### 5.2 验证

```bash
ch341\flashrom-1.4\flashrom.exe --version
```

输出: `flashrom 1.4.0 on Windows 10.0 (x86_64) — Dreg comp`

支持 `ch341a_spi` 编程器, 可直接与 W25Q128 通信。

### 5.3 Zadig (驱动替换)

同一目录下已附带 `zadig-2.8.exe`, 直接运行即可:

```bash
ch341\flashrom-1.4\zadig-2.8.exe
```

操作步骤: Options → List All Devices → 选 CH341A → 选 WinUSB → Replace Driver

---

## 6. Python 环境配置 (VS Code 从零搭建)

### 6.1 确认工具链已就绪

| 工具 | 状态 | 路径/版本 |
|:---|:---|:---|
| Python 3.10+ | ✅ 已安装 | `C:\Users\48376\.conda\envs\skill-opt310\python.exe` (3.10.20) |
| pip | ✅ 已安装 | v26.1.1 |
| Pillow | ✅ 已安装 | v12.2.0 (字模渲染库) |
| VS Code | ✅ 已安装 | v1.125.1 |

### 6.2 导入项目配置

项目 `.vscode/` 目录已预先配置好，VS Code 打开项目后自动生效:

```
项目根目录/
├── .vscode/
│   ├── settings.json     ← Python 解释器路径 + 工作区设置
│   └── launch.json       ← 调试运行配置 (generate_font / burn_flash)
├── ch341/
│   ├── generate_font.py
│   ├── burn_flash.py
│   └── requirements.txt
└── ...
```

**settings.json** 内容说明:

```json
{
    "python.defaultInterpreterPath": "C:/Users/48376/.conda/envs/skill-opt310/python.exe",
    "python.terminal.activateEnvironment": false,
    "[python]": {
        "editor.tabSize": 4,
        "editor.insertSpaces": true
    },
    "files.exclude": {
        "Keil_Project/Objects": true,
        "Keil_Project/Listings": true,
        "__pycache__": true,
        ".superpowers": true
    },
    "terminal.integrated.env.windows": {
        "PATH": "C:\\Users\\48376\\.conda\\envs\\skill-opt310;${env:PATH}"
    }
}
```

> 说明: 通过 `terminal.integrated.env.windows.PATH` 向 VS Code 终端注入 Python 路径。打开新终端后 `python` 自动指向 skill-opt310，无需 conda activate。

**launch.json** 内容说明:

| 配置名称 | 快捷键 | 作用 |
|:---|:---|:---|
| `generate_font.py — 生成字库` | F5 | 运行字库生成 (仅测试，不烧录) |
| `burn_flash.py — 烧录字库` | F5 | 运行完整烧录流程 |

### 6.3 操作步骤

**第 1 步: 打开项目**

```
1. 启动 VS Code
2. 文件 → 打开文件夹 → 选择 D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT
3. VS Code 自动读取 .vscode/settings.json
   → 终端 PATH 自动注入 Python 3.10 (skill-opt310) 到最前面
   → 无需 conda activate
```

**第 2 步: 确认 Python 解释器**

```
1. 按 Ctrl+Shift+P → 输入 "Python: Select Interpreter"
2. 选择 "Python 3.10.20 ('skill-opt310') — C:\Users\48376\.conda\envs\skill-opt310\python.exe"
   (.vscode/settings.json 已通过 "python.defaultInterpreterPath" 预指定, 通常自动选中)
```

**第 3 步: 确认环境正常**

```
1. 按 Ctrl+` 打开 VS Code 内置终端
2. 输入 python --version
   预期输出: Python 3.10.20
3. 输入 python -c "import PIL; print('Pillow OK')"
   预期输出: Pillow OK
```

如果步骤 3 报错 `No module named 'PIL'`:
```bash
C:\Users\48376\.conda\envs\skill-opt310\python.exe -m pip install Pillow
```

**第 4 步: 运行脚本**

> 当前工具链状态（已就绪）:
> - Python 3.10.20 + Pillow 12.2.0 ✅
> - flashrom 1.4.0 (`ch341/flashrom-1.4/flashrom.exe`) ✅
> - `font_data.bin` 可预生成（非必需） ✅
> - `generate_font.py` + `burn_flash.py` ✅
> - 待完成: CH341A 接线 + WinUSB 驱动

方式 A — F5 一键运行 (推荐):
```
1. 左侧点击 "运行和调试" 图标 (Ctrl+Shift+D)
2. 顶部下拉选择 "burn_flash.py — 烧录字库"
3. 按 F5 启动
```

方式 B — 终端运行:
```
cd ch341
python generate_font.py    # [可选] 仅测试字库生成
python burn_flash.py       # 完整烧录 (需 CH341A 已接线 + WinUSB 驱动)
```

### 6.4 故障排除

| 症状 | 解决 |
|:---|:---|
| 终端 `python --version` 不是 3.10 | 关闭终端 → Ctrl+` 重新打开 (settings.json 通过 PATH 注入自动生效, 无需 conda activate) |
| `No module named 'PIL'` | `C:\Users\48376\.conda\envs\skill-opt310\python.exe -m pip install Pillow` |
| `import` 下有红色波浪线 | Ctrl+Shift+P → Python: Select Interpreter → 选 `skill-opt310` (路径: `C:\Users\48376\.conda\envs\skill-opt310\python.exe`) |
| 终端报 `conda activate` 失败 | **不需要 conda activate** — 配置已通过 PATH 注入绕过 conda 初始化 |
| 文件保存后中文乱码 | 文件 → 首选项 → 设置 → 搜索 "files.encoding" → 设为 `utf8` |

---

## 7. 烧录步骤

### 7.1 烧录前检查清单

- [ ] CH341A 跳线帽在 **3.3V** 位置
- [ ] 6 根杜邦线按接线表正确连接
- [ ] STM32 目标板 **完全断电** (USB 线已拔)
- [ ] Zadig WinUSB 驱动已安装 (设备管理器 → Universal Serial Bus devices → CH341A)
- [ ] VS Code 终端内 `python --version` ≥ 3.10
- [ ] `ch341\flashrom-1.4\flashrom.exe --version` 正常输出

### 7.2 步骤一 (可选): 仅测试字库生成

```bash
cd ch341
python generate_font.py
```

输出应包含:
- `[OK] CRC32 自测通过: 固件 Checksum_CRC32('1234') = 0x596A3B55`
- `[OK] 加载字体: C:\WINDOWS\Fonts\simsun.ttc`
- `[OK] 生成 font_data.bin (2097152 字节 = 2MB)`
- `擦除扇区: 512 个 (0x000000~0x200000)`

### 7.3 步骤二: 完整烧录

```bash
cd ch341
python burn_flash.py
```

脚本自动执行以下 5 步:

| 步骤 | 操作 | 耗时 | 说明 |
|:---|:---|:---|:---|
| Step 0 | CRC32 自测 | <1s | 验证 Python 与 STM32 CRC32 算法一致 (refin=false) |
| Step 1 | 检测 flashrom | <1s | 确认 flashrom 可用 |
| Step 2 | 生成字库镜像 | ~8min | 渲染 20897 汉字 + 95 ASCII + 图标 |
| Step 3 | 备份全片 16MB | ~3min | 每次都新建 `backup_<时间戳>_16MB.bin`，绝不复用旧备份 |
| Step 4 | 写入字库分区 | ~5min | 只擦写前2MB，配置和黑匣子日志分区不受影响 |
| Step 5 | 读回逐字节校验 | ~3min | 逐字节比对前 2MB, 0 差异才算通过 |

**总耗时: 约 20 分钟** (主要取决于 USB 速度和字库渲染)。

### 7.4 烧录成功标志

```
================================================================
   [PASS] 字库烧录成功! 所有 2,097,152 字节完全一致
================================================================

  后续操作:
  1. 断开 CH341A USB
  2. STM32 重新上电
  3. 屏幕应显示 GB2312 全字库 (20897 汉字)
  4. 若Magic、结构边界或CRC32不匹配, 自动回退到片内ASCII/图标及4个必要汉字
```

### 7.5 烧录后操作

1. 拔掉 CH341A USB 线
2. 拔掉 6 根杜邦线 (或保持连接, 但不影响正常使用)
3. 给 STM32 目标板上电
4. 观察 TFT 屏幕: 正常显示全字库中文, 不再出现缺字方块

### 7.6 开机动画

> **V4.3.2 变更**: 开机动画改为 STM32 纯代码实现 (8帧背光渐亮), 不依赖 W25Q SPLASH 分区。
> 无需额外烧录, 固件已内置。删除 `burn_splash.py` 和 `generate_splash.py`。

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
| 烧录成功但屏幕无变化 | STM32 固件未启用 Flash 字库 | 确认使用V5.1.2固件，并检查启动时Flash字库CRC状态 |
| 屏幕白屏 (无任何显示) | PB12虚焊或共享SPI接线冲突 | 检查PB12/PA5/PA6/PA7，确认CH341A排针已拔下；V5.1.2会在超时后恢复总线 |
| 同上 | SPI1 引脚 PA6 被 W25Q128 占用, TFT 无法通信 | 烧录后务必拔掉 CH341A 排针 (尤其 MISO/PA6) 再给 STM32 上电 |

### 8.1 从备份恢复

如果烧录后出现异常，用项目自带的 flashrom 恢复:

```bash
ch341\flashrom-1.4\flashrom.exe -p ch341a_spi -w ch341\backup_<时间戳>_16MB.bin -c W25Q128.V
```

此命令将芯片完整恢复到烧录前的状态。

---

## 9. 文件说明

| 文件 | 用途 |
|:---|:---|
| `ch341/generate_font.py` | 字库生成器: PIL 渲染 GB2312 全字库 20897 字 + ASCII 95 字 + 35组图标78帧 → 字库格式V2 `font_data.bin` (2MB) |
| `ch341/burn_flash.py` | 烧录编排: CRC32自测 → 生成字库 → 新备份全片 → 仅写入2MB字库分区 → 完整2MB逐字节校验 |
| `ch341/font_data.bin` | 生成产物: 2MB 字库镜像 (格式对齐 W25Q128 Flash 布局, LSB-first 无位序反转) |

| `ch341/requirements.txt` | Python 依赖: Pillow ≥ 10.0.0 |

> **V4.3.2 变更**: 删除 `generate_splash.py`/`burn_splash.py` — SPLASH 改为 STM32 纯代码实现, 开机动画存 ROM 不占 W25Q。

临时文件 (`merged_flash.bin`, `verify_readback.bin`) 在烧录成功后自动清理。

---

## 10. 参考

- CH341A 编程器数据手册: https://www.wch.cn/products/CH341.html
- flashrom 官方文档: https://www.flashrom.org/
- W25Q128 数据手册: https://www.winbond.com/
- WPT 项目设计文档: `Claude_Files/docs/2026-06-22-w25q128-flash-integration-design.md`
