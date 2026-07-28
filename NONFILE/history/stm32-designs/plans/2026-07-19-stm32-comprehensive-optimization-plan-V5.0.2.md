# STM32 V5.0.2 Comprehensive Optimization Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Use `embedded-architect` for every production-code task and `superpowers:verification-before-completion` before claiming any phase complete.

**Goal:** 将V5.0.1 STM32固件按已批准设计升级为V5.0.2，依次修复功率联锁、过流保护、ADC时基、SPI共享、W25Q128、黑匣子、按键、网络、UI和维护性问题。

**Architecture:** `Sys_Core`成为唯一状态和功率控制入口；TIM3以500 Hz触发ADC双通道DMA；SPI1由共享总线模块统一切换TFT/W25Q128；W25Q底层只负责原始Flash协议，`App_Storage`负责配置和黑匣子V2；USART2使用中断TX队列；`main.c`只保留初始化与状态分发。

**Tech Stack:** STM32F103C8T6、SPL V3.5.0、ARMCC V5.06/C89、Keil MDK-ARM V5、ST7735、W25Q128JV、ESP8266 USART2 JSON。

**Approved design:** `../specs/2026-07-19-stm32-comprehensive-optimization-design-V5.0.2.md`

---

## 执行规则

1. 当前工作目录和分支固定为`D:\Claude Code Project\WPT_PWM_V5.0`、`5.0`，不创建其他业务分支。
2. 保留用户已有未提交内容：`.claude/settings.local.json`、`ONENETapp`、`.codex/`、根目录`AGENTS.md`。
3. 每次暂存必须列出明确文件，禁止`git add -A`，直到最终用户明确要求完整提交。
4. 不修改`Keil_Project/Library/`、`Keil_Project/Start/`。
5. 新代码遵守ARMCC V5/C89：代码块开头声明变量，不使用`//`注释，不使用动态内存。
6. 每个任务先增加可检查的失败条件，再做最小实现，再运行静态验证。
7. Keil没有可用命令行构建工具。每个阶段软件检查通过后，暂停并让用户在Keil执行Rebuild；读取新的`Objects/Project.build_log.htm`和`Listings/Project.map`验证。
8. 硬件测试前必须确认示波器/负载条件安全；过流测试优先使用限流电源或可控模拟信号，不直接短路功率级。
9. 各任务本地提交；未获用户明确指令前不push。

## Phase 0 — 基线与自动检查

### Task 1: 建立V5.0.2静态验收脚本

**Files:**

- Create: `Keil_Project/tests/verify_v5_0_2.ps1`
- Reference: `Keil_Project/Objects/Project.build_log.htm`
- Reference: `Keil_Project/Listings/Project.map`

**Step 1: 写最终态检查规则**

脚本至少检查：

- 目标33个STM32 `.c/.h` 文件存在。
- 最终文件头和Splash包含`V5.0.2`。
- `BLACKBOX_ENTRY_SIZE`等于12。
- W25Q驱动不包含`Sys_Core.h`、`App_Storage.h`。
- `Ui_Controller`和`App_Network`不直接调用`Inverter_Control_Soft_Start_Trigger()`。
- 除`Sys_Core.c`外没有代码直接写系统状态。
- KEY0/2/3不启用双击或长按，KEY1启用双击，KEY4启用长按。
- 不存在`UI_PAGE_SETTING_BL*`。
- 遥测显式映射S=0/1/2/3。
- `Project.uvprojx`包含后续新增模块。
- 构建日志存在时可解析0 Error、0 Warning。
- map存在时解析ROM/RAM并验证64 KB/20 KB边界。
- Git跟踪区没有`.obj/.lst/.axf/.hex/.map/.crf/.d`等编译产物。

脚本对每条输出`PASS/FAIL`，任一失败退出码为1。

**Step 2: 运行脚本确认当前版本失败**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project\tests\verify_v5_0_2.ps1
```

Expected: 因版本号、黑匣子长度、W25依赖、背光页面等规则失败，退出码1。

**Step 3: 记录基线资源**

在脚本输出中记录：

```text
Baseline ROM = 52272 bytes
Baseline RAM = 5560 bytes
Baseline build = 0 errors, 0 warnings
```

**Step 4: 检查脚本自身**

Run:

```powershell
git diff --check -- Keil_Project/tests/verify_v5_0_2.ps1
```

Expected: 无输出。

**Step 5: Commit**

```powershell
git add Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "test: 添加V5.0.2 STM32静态验收脚本"
```

### Task 2: 提取校验算法并固定Flash兼容格式

**Files:**

- Create: `Keil_Project/System/Checksum.h`
- Create: `Keil_Project/System/Checksum.c`
- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/Project.uvprojx`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 增加失败检查**

检查`Project.uvprojx`必须包含`Checksum.c/h`，且`CRC32_Compute`不再由`App_Storage`公开。

Run static script; Expected: FAIL。

**Step 2: 定义独立接口**

`Checksum.h`公开：

```c
uint8_t Checksum_CRC8(const uint8_t *data, uint16_t len);
uint32_t Checksum_CRC32(const uint8_t *data, uint32_t len);
uint8_t Checksum_Self_Test(void);
```

CRC32保持非反射格式：poly `0x04C11DB7`、init/final XOR `0xFFFFFFFF`。

**Step 3: 实现固定向量自检**

`"123456789"`期望：

```text
CRC8  = 0xF4
CRC32 = 0xFC891918
```

使用bitwise CRC8替代256 B表，节省ROM，保持poly `0x07`和初值0。

**Step 4: 替换调用点**

- `App_Storage.c`使用`Checksum_CRC8/CRC32`。
- `Tft_Driver.c`使用`Checksum_CRC32`。
- 删除`App_Storage.h`中的CRC32公开接口。
- 在`Project.uvprojx` System组加入新文件。

**Step 5: Verify**

Run static script; Expected: Checksum相关规则PASS。

执行Keil Rebuild检查点A：0 Error、0 Warning。

**Step 6: Commit**

```powershell
git add Keil_Project/System/Checksum.c Keil_Project/System/Checksum.h Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/Hardware/Tft_Driver.c Keil_Project/Project.uvprojx Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "refactor: 提取Flash兼容校验模块"
```

## Phase 1 — 功率联锁与状态安全

### Task 3: 修复TIM1影子寄存器更新顺序

**Files:**

- Modify: `Keil_Project/Hardware/Pwm_Driver.c`
- Modify: `Keil_Project/Hardware/Pwm_Driver.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 增加失败检查**

脚本拒绝`UDIS置位 -> EGR=UG -> UDIS清零`旧顺序。

**Step 2: 实现安全顺序**

在一次短临界区中：

1. 临时置UDIS。
2. 写ARR、CCR1、CCR2预装载值。
3. 清UDIS。
4. 写UG使三者同一更新边界生效。

保留频率钳位、偶数ticks和50%占空。

**Step 3: 增加状态查询**

如后续联锁需要，增加只读接口：

```c
uint8_t Pwm_Driver_Is_Enabled(void);
```

状态从TIM1 CEN与MOE实际寄存器读取，不维护重复软件布尔值。

**Step 4: Verify and Commit**

Run static script and Keil Rebuild。

```powershell
git add Keil_Project/Hardware/Pwm_Driver.c Keil_Project/Hardware/Pwm_Driver.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "fix: 修正TIM1频率原子更新时序"
```

### Task 4: 建立统一系统控制API

**Files:**

- Modify: `Keil_Project/User/Sys_Core.h`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/Hardware/Inverter_Control.c`
- Modify: `Keil_Project/Hardware/Inverter_Control.h`

**Step 1: 在头文件定义控制结果与故障原因**

```c
typedef enum {
    SYS_CONTROL_RESULT_OK = 0,
    SYS_CONTROL_RESULT_POWER_OFF,
    SYS_CONTROL_RESULT_FAULT_LATCHED,
    SYS_CONTROL_RESULT_ADC_NOT_READY,
    SYS_CONTROL_RESULT_INVALID_STATE
} Sys_Control_Result;

typedef enum {
    SYS_FAULT_NONE = 0,
    SYS_FAULT_OVERCURRENT,
    SYS_FAULT_ADC_STALE,
    SYS_FAULT_CONTROL_INVARIANT
} Sys_Fault_Code;
```

公开`Sys_Core_Request_Start/Stop/Reset_Fault/Trigger_Fault/Get_State/Get_Fault/Is_Power_Enabled`。

**Step 2: 编写状态不变量检查**

规则：

- PB10 LOW时PWM必须关闭。
- FAULT时PB10与PWM必须关闭。
- SWEEP/RUNNING时PB10必须HIGH。
- Reset Fault只清故障，不开12 V。

违反规则调用`Sys_Core_Trigger_Fault(SYS_FAULT_CONTROL_INVARIANT)`。

**Step 3: 实现电源动作顺序**

- 开电：只PB10 HIGH + POWER灯ON。
- 关电：先PWM Disable/Cancel，再PB10 LOW，再灯灭。
- 正常Stop：PWM关闭但PB10保持。
- Fault：PWM、PB10、POWER、STATUS全部关闭并锁存。

**Step 4: 保持临时兼容**

本任务先保留`g_sys_state`，但所有新动作经统一API。下一任务迁移所有调用点后再隐藏全局状态。

**Step 5: Verify and Commit**

Keil Rebuild，确认旧功能仍可编译。

```powershell
git add Keil_Project/User/Sys_Core.c Keil_Project/User/Sys_Core.h Keil_Project/Hardware/Inverter_Control.c Keil_Project/Hardware/Inverter_Control.h
git commit -m "feat: 建立统一功率与故障控制入口"
```

### Task 5: 迁移UI、网络与主程序状态调用

**Files:**

- Modify: `Keil_Project/Hardware/Ui_Controller.c`
- Modify: `Keil_Project/User/App_Network.c`
- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/main.c`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/User/Sys_Core.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 增加最终调用边界检查**

- `Ui_Controller.c`和`App_Network.c`不能调用Soft Start Trigger/Stop/Fault。
- 不能直接赋值系统状态。
- `main.c`通过getter分发状态。

**Step 2: 替换本地控制**

- KEY4启动调用`Sys_Core_Request_Start()`。
- KEY4停止调用`Sys_Core_Request_Stop()`。
- FAULT确认调用`Sys_Core_Reset_Fault()`。
- KEY0由Sys_Core处理，FAULT中不允许重新开电。

**Step 3: 替换远程控制**

- `CMD:ON`调用同一个Start接口。
- `CMD:OFF`调用同一个Stop接口。
- 拒绝启动时保持状态不变。

**Step 4: 隐藏系统状态**

- `g_sys_state`改为`Sys_Core.c`内部静态变量。
- 所有读者使用`Sys_Core_Get_State()`。
- `Blackbox_Log_Tick`接收显式状态参数，不读取全局。

**Step 5: Verify and Commit**

Run static script; direct-state/direct-start rules必须PASS。

Keil Rebuild检查点B。

```powershell
git add Keil_Project/Hardware/Ui_Controller.c Keil_Project/User/App_Network.c Keil_Project/User/App_Storage.c Keil_Project/User/main.c Keil_Project/User/Sys_Core.c Keil_Project/User/Sys_Core.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "refactor: 统一本地与远程启停联锁"
```

### Task 6: 将过流保护覆盖SWEEP和RUNNING

**Files:**

- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/Hardware/Ui_Controller.c`
- Modify: `Keil_Project/Hardware/Buzzer_Driver.c`

**Step 1: 调整Safety状态守卫**

过流检查条件改为：

```c
state == SYS_STATE_SWEEP || state == SYS_STATE_RUNNING
```

触发时只调用统一Fault接口，不在Safety中重复关断动作。

**Step 2: 修复FAULT显示与复位**

- Fault页根据`Sys_Fault_Code`显示过流、ADC失效或控制不变量。
- KEY4确认只清锁存并回IDLE。
- POWER和STATUS在FAULT保持OFF。

**Step 3: Hardware smoke test**

- PB10关闭时KEY4无PWM。
- KEY0开启不自动发波。
- KEY4正常Stop保留PB10 HIGH。
- KEY0关电先停PWM。

**Step 4: Commit**

```powershell
git add Keil_Project/User/Sys_Core.c Keil_Project/Hardware/Ui_Controller.c Keil_Project/Hardware/Buzzer_Driver.c
git commit -m "fix: 扩展扫频过流保护与故障锁存"
```

## Phase 2 — ADC固定时基与快速保护

### Task 7: 使用TIM3 500Hz触发ADC双通道DMA

**Files:**

- Modify: `Keil_Project/Hardware/Adc_Driver.c`
- Modify: `Keil_Project/Hardware/Adc_Driver.h`
- Modify: `Keil_Project/User/stm32f10x_it.c`
- Modify: `Keil_Project/User/stm32f10x_it.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 增加失败检查**

要求ADC使用`ADC_ExternalTrigConv_T3_TRGO`，不得继续使用Continuous模式和DWT 144241周期弃样逻辑。

**Step 2: 配置TIM3**

- TIM3计数频率1 MHz。
- ARR=1999，更新频率500 Hz。
- TRGO选择Update。

**Step 3: 配置ADC与DMA**

- ADC Scan=ENABLE、Continuous=DISABLE。
- 外部触发=T3_TRGO。
- DMA1 Channel1循环接收2个halfword。
- DMA TC中断每2 ms复制稳定的I/V原始对并递增序号。
- ISR禁止浮点计算和业务状态切换。

**Step 4: 暴露采样序号**

增加Task接口读取新快照并记录最后更新时间。

**Step 5: Verify and Commit**

Keil Rebuild；示波器/调试观察采样序号约500次/秒。

```powershell
git add Keil_Project/Hardware/Adc_Driver.c Keil_Project/Hardware/Adc_Driver.h Keil_Project/User/stm32f10x_it.c Keil_Project/User/stm32f10x_it.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "refactor: 使用TIM3固定触发ADC采样"
```

### Task 8: 分离显示滤波与安全滤波

**Files:**

- Modify: `Keil_Project/Hardware/Adc_Driver.c`
- Modify: `Keil_Project/Hardware/Adc_Driver.h`
- Modify: `Keil_Project/User/Sys_Core.c`

**Step 1: 增加双窗口**

- Display window=64。
- Safety window=8。
- 每个新DMA快照各推入一次。

**Step 2: 增加接口**

```c
float Adc_Driver_Get_Display_Voltage(void);
float Adc_Driver_Get_Display_Current(void);
float Adc_Driver_Get_Safety_Current(void);
uint8_t Adc_Driver_Is_Data_Fresh(void);
```

保留旧getter作为短期兼容或一次性迁移后删除。

**Step 3: 实现快速保护确认**

- 每个新安全样本检查一次。
- 连续3次`>5.0f`才触发。
- 回落后计数清零。
- SWEEP/RUNNING共用。

**Step 4: 数值守卫**

- 负电流钳位0。
- 电压增益参与计算。
- 非法增益回退1.0。

**Step 5: Verify and Commit**

```powershell
git add Keil_Project/Hardware/Adc_Driver.c Keil_Project/Hardware/Adc_Driver.h Keil_Project/User/Sys_Core.c
git commit -m "fix: 分离ADC显示与快速安全滤波"
```

### Task 9: 完成非阻塞校准与ADC失效保护

**Files:**

- Modify: `Keil_Project/Hardware/Adc_Driver.c`
- Modify: `Keil_Project/Hardware/Adc_Driver.h`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`

**Step 1: 将校准改为明确状态机**

状态：UNINITIALIZED、FILLING、CALIBRATING、READY、ERROR。

- 有效Flash校准直接READY。
- 无效校准等待64点窗口填满。
- 每10 ms累计一次，共50次。
- PB10必须LOW才允许校准。

**Step 2: 启动联锁**

`Sys_Core_Request_Start()`在ADC非READY或数据不新鲜时返回`ADC_NOT_READY`。

**Step 3: 数据失效保护**

- 20 ms无新DMA序号视为stale。
- IDLE禁启动。
- SWEEP/RUNNING触发`SYS_FAULT_ADC_STALE`。

**Step 4: 校准持久化挂起**

校准完成只设置`App_Storage`待保存标志，禁止在运行状态立即擦除。

**Step 5: Hardware test and Commit**

- 清除配置后启动，约0.6秒后ADC Ready。
- 校准完成前KEY4被拒绝。
- 暂停ADC触发验证20 ms故障。

```powershell
git add Keil_Project/Hardware/Adc_Driver.c Keil_Project/Hardware/Adc_Driver.h Keil_Project/User/Sys_Core.c Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h
git commit -m "fix: 完成ADC校准与失效联锁"
```

## Phase 3 — SPI1共享与W25Q128底层

### Task 10: 新增SPI1共享总线模块

**Files:**

- Create: `Keil_Project/Hardware/Spi1_Shared.h`
- Create: `Keil_Project/Hardware/Spi1_Shared.c`
- Modify: `Keil_Project/Project.uvprojx`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 定义总线模式与结果**

```c
typedef enum {
    SPI1_SHARED_RESULT_OK = 0,
    SPI1_SHARED_RESULT_BUSY,
    SPI1_SHARED_RESULT_TIMEOUT,
    SPI1_SHARED_RESULT_INVALID
} Spi1_Shared_Result;

typedef enum {
    SPI1_SHARED_MODE_TFT_8 = 0,
    SPI1_SHARED_MODE_TFT_16,
    SPI1_SHARED_MODE_FLASH_8
} Spi1_Shared_Mode;
```

**Step 2: 实现总线不变量**

- Acquire前双CS HIGH。
- 等待BSY带超时。
- 清DR/RXNE/OVR。
- 切换DFF和PA6方向。
- Release恢复TFT data态并双CS HIGH。
- 任何失败路径调用Force Release。

**Step 3: 加入Keil工程**

在Hardware组加入`.c/.h`。

**Step 4: Verify and Commit**

```powershell
git add Keil_Project/Hardware/Spi1_Shared.c Keil_Project/Hardware/Spi1_Shared.h Keil_Project/Project.uvprojx Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "feat: 新增SPI1共享总线控制层"
```

### Task 11: 迁移TFT到共享总线并修复初始化时序

**Files:**

- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/Hardware/Tft_Driver.h`
- Modify: `Keil_Project/User/main.c`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/Hardware/Led_Driver.c`

**Step 1: 替换TFT本地SPI模式切换**

- WrCmd/WrDat获取TFT_8模式。
- DMA获取TFT_16模式。
- 删除直接改DFF的重复函数。
- 所有超时调用共享Force Release。

**Step 2: 调整初始化顺序**

主程序先安全钳位PB10/ESP，再`Sys_Timer_Init()`，之后硬件初始化。

**Step 3: 替换NOP延时**

- ST7735 RST、SLPOUT、DISPON使用`Sys_Timer_Delay_Ms()`。
- LED自检使用准确500 ms。

**Step 4: TFT-only smoke test**

- 无W25连接时Splash、中文ROM回退、页面切换正常。
- 连续NRST至少10次无白屏。

**Step 5: Commit**

```powershell
git add Keil_Project/Hardware/Tft_Driver.c Keil_Project/Hardware/Tft_Driver.h Keil_Project/User/main.c Keil_Project/User/Sys_Core.c Keil_Project/Hardware/Led_Driver.c
git commit -m "refactor: 迁移TFT共享SPI并校准启动延时"
```

### Task 12: 重写W25Q底层状态接口

**Files:**

- Modify: `Keil_Project/Hardware/W25Q_Driver.c`
- Modify: `Keil_Project/Hardware/W25Q_Driver.h`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 定义W25Q结果枚举**

包含OK、NO_DEVICE、INVALID_ARGUMENT、OUT_OF_RANGE、PAGE_CROSS、ERASE_BLOCKED、SPI_TIMEOUT、BUSY_TIMEOUT、VERIFY_FAILED。

**Step 2: 去除反向依赖**

- 删除`Sys_Core.h`和`App_Storage.h` include。
- 增加擦除锁setter，由Sys_Core在状态转换时控制。
- 将`Font_Header`和字库校验移到TFT字库层。

**Step 3: 实现边界和超时**

- 检查`addr + len`溢出及16 MB边界。
- 单页写内部化，拒绝跨页。
- 通用Write自动拆页。
- Page Program 10 ms，Sector Erase 500 ms。
- 超时后释放总线。

**Step 4: 合并字库头校验入口**

删除重复`Font_Header_Load`路径，`Tft_Driver_Font_Init()`只调用一个校验实现。

**Step 5: Flash smoke tests**

- JEDEC正常和Flash缺失两种启动。
- TFT/Flash交替读取1000次无白屏、乱码。
- NRST重复复位。

**Step 6: Commit**

```powershell
git add Keil_Project/Hardware/W25Q_Driver.c Keil_Project/Hardware/W25Q_Driver.h Keil_Project/Hardware/Tft_Driver.c Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "fix: 重构W25Q边界超时与共享总线"
```

## Phase 4 — 配置存储与黑匣子V2

### Task 13: 配置保存改为可验证后台任务

**Files:**

- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/Hardware/Ui_Controller.c`

**Step 1: 为配置接口增加结果与pending状态**

- `App_Storage_Request_Save_Config()`只更新RAM副本和pending。
- `App_Storage_Task()`只在IDLE执行Flash擦写。
- 暴露`App_Storage_Get_Last_Result()`。

**Step 2: 修复结构初始化**

Defaults先清零整个结构，再逐字段赋值，避免padding参与CRC时包含未初始化数据。

**Step 3: 实现A/B验证写**

写A→回读CRC→成功后写B→回读CRC。失败不清pending，不报告成功。

**Step 4: 接入调度**

Sys_Core公共任务调用`App_Storage_Task()`；UI设置退出只请求保存。

**Step 5: Test and Commit**

- RUNNING修改设置不擦除。
- 回IDLE后写入。
- 复位后恢复。

```powershell
git add Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/User/Sys_Core.c Keil_Project/Hardware/Ui_Controller.c
git commit -m "fix: 配置保存改为后台验证写"
```

### Task 14: 定义黑匣子V2结构与迁移

**Files:**

- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/Hardware/W25Q_Driver.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 定义分区常量**

```text
META_A      0x310000
META_B      0x311000
LOG_START   0x312000
FAULT_START 0x6D0000
BLACK_END   0x710000
```

**Step 2: 定义12 B日志结构**

字段为timestamp/V/I/F/state/crc，CRC覆盖前11 B。

增加C89编译期断言：

```c
typedef char App_Storage_Log_Size_Check[
    (sizeof(App_Storage_Log_Entry) == 12U) ? 1 : -1];
```

**Step 3: 定义V2 metadata和fault header**

每个结构包含magic、version、size和CRC；所有保留字段显式清零。

**Step 4: 迁移行为**

启动找不到V2 metadata时设置空日志状态，不解析V1日志，不触碰配置A/B。

**Step 5: Verify and Commit**

```powershell
git add Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/Hardware/W25Q_Driver.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "feat: 定义黑匣子V2格式与迁移规则"
```

### Task 15: 实现双扇区元数据日志

**Files:**

- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`

**Step 1: 编写扫描器**

扫描A/B记录槽，选择CRC有效且generation最高记录。空Flash返回空状态，不读取未初始化局部变量。

**Step 2: 实现append**

每60条普通日志或状态退出时追加metadata。只有W25返回OK后才更新RAM中的已保存generation。

**Step 3: 实现扇区切换**

活动扇区满且系统IDLE时：擦另一扇区→写最新记录→回读验证→切换活动扇区。不得先擦唯一有效扇区。

**Step 4: Fault injection tests**

分别模拟A损坏、B损坏、最后一条CRC坏，确认恢复上一条有效记录。

**Step 5: Commit**

```powershell
git add Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h
git commit -m "feat: 实现黑匣子双扇区元数据恢复"
```

### Task 16: 实现循环日志扇区生命周期

**Files:**

- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/User/Sys_Core.c`

**Step 1: 写入前检查**

- 目标12 B范围必须为0xFF或属于已准备扇区。
- W25写失败时不推进pointer、count、sequence。

**Step 2: IDLE预擦除**

`App_Storage_Task()`在IDLE准备后续扇区。SWEEP/RUNNING绝不擦除。

**Step 3: 无扇区降级**

没有可写扇区时增加drop count并跳过，不覆盖旧数据，不阻塞功率控制。

**Step 4: 读取顺序**

根据write pointer、valid count和wrap count计算最旧记录地址；跨页由W25通用Read处理。

**Step 5: Boundary tests**

覆盖页边界、扇区边界、LOG_END回绕和重启恢复。

**Step 6: Commit**

```powershell
git add Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/User/Sys_Core.c
git commit -m "feat: 实现可恢复循环日志扇区管理"
```

### Task 17: 实现真实故障前后快照

**Files:**

- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/User/Sys_Core.c`

**Step 1: 增加RAM前置环形缓冲**

每200 ms保留最近25条有效采样。

**Step 2: Fault开始时冻结前半段**

记录故障原因和触发时间，进入POST_CAPTURE状态，不在安全关断调用栈中同步擦Flash。

**Step 3: 收集后5秒**

FAULT状态每200 ms收集25条。ADC stale故障时用有效标志标记缺失采样，不伪造数值。

**Step 4: 写独立4KB槽**

PWM和PB10确认关闭后：擦槽→写header和50条→回读CRC→推进fault slot。

**Step 5: Hardware test**

制造一次可控故障，读取快照确认前25/后25顺序和CRC。

**Step 6: Commit**

```powershell
git add Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/User/Sys_Core.c
git commit -m "feat: 实现故障前后五秒快照"
```

## Phase 5 — 五键与UI清理

### Task 18: 重构按键能力标志

**Files:**

- Modify: `Keil_Project/Hardware/Key_Driver.c`
- Modify: `Keil_Project/Hardware/Key_Driver.h`
- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 拆分能力位**

```c
#define KEY_DRIVER_CFG_DOUBLE_ENABLE  0x01U
#define KEY_DRIVER_CFG_LONG_ENABLE    0x02U
```

单击始终支持；未启用double的键释放后立即CLICK；未启用long的键持续按下不产生LONG。

**Step 2: 设置五键能力**

- KEY0: 0。
- KEY1: DOUBLE。
- KEY2: 0。
- KEY3: 0。
- KEY4: LONG。

**Step 3: FSM边界测试**

验证10 ms按下去抖、12 ms释放去抖、200 ms双击窗、3 s长按；事件消费后清零。

**Step 4: Commit**

```powershell
git add Keil_Project/Hardware/Key_Driver.c Keil_Project/Hardware/Key_Driver.h Keil_Project/User/Sys_Core.c Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "fix: 按键按能力启用单双击与长按"
```

### Task 19: 删除无效背光页面并简化设置

**Files:**

- Modify: `Keil_Project/Hardware/Ui_Controller.c`
- Modify: `Keil_Project/Hardware/Ui_Controller.h`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/User/App_Storage.c`
- Modify: `Keil_Project/User/App_Storage.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 更新页面枚举**

删除：

- `UI_PAGE_SETTING_BL`
- `UI_PAGE_SETTING_BL_MANUAL`
- `UI_PAGE_SETTING_BL_BREATHE`

颜色页改为13，系统共14页。

**Step 2: 删除相关状态与绘制代码**

删除亮度滑条、呼吸参数、长按加减、页面switch分支和文本。

**Step 3: 设置菜单改为4项**

语言、字间距、图标、颜色。同步光标边界和返回路径。

**Step 4: 保持Flash兼容**

配置结构保留backlight字段，但加载后忽略；保存时保持兼容默认值100。TFT初始化完成后GPIO背光常亮。

**Step 5: Verify and Commit**

```powershell
git add Keil_Project/Hardware/Ui_Controller.c Keil_Project/Hardware/Ui_Controller.h Keil_Project/Hardware/Tft_Driver.c Keil_Project/User/App_Storage.c Keil_Project/User/App_Storage.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "refactor: 删除GPIO背光下无效设置页面"
```

### Task 20: 将KEY4长按限制在WiFi配置页

**Files:**

- Modify: `Keil_Project/Hardware/Ui_Controller.c`
- Modify: `Keil_Project/Hardware/Ui_Controller.h`
- Modify: `Keil_Project/User/Sys_Core.c`

**Step 1: 删除全局长按拦截**

KEY4 LONG不再从任何页面跳转WiFi。

**Step 2: WiFi页局部处理**

仅`UI_PAGE_WIFI_SETUP`且系统IDLE、PWM关闭时发送`CMD:CLEAR`确认流程。

**Step 3: 统一事件传递**

Sys_Core扫描全部按键，先消费KEY0，再把剩余事件传给UI。UI不再回调Sys_Core处理KEY0。

建议接口：

```c
void Ui_Controller_Task(const Key_Driver_Event events[KEY_DRIVER_COUNT]);
```

**Step 4: Hardware test and Commit**

验证KEY1单双击、KEY2/3即时单击、KEY4非WiFi页长按无动作。

```powershell
git add Keil_Project/Hardware/Ui_Controller.c Keil_Project/Hardware/Ui_Controller.h Keil_Project/User/Sys_Core.c
git commit -m "fix: 限定WiFi页面确认键长按"
```

### Task 21: 降低UI重复SPI刷新

**Files:**

- Modify: `Keil_Project/Hardware/Ui_Controller.c`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`

**Step 1: 图标变化缓存**

顶部WiFi/MQTT图标只有状态、动画帧或配色变化时清槽重绘。

**Step 2: 去除重复全屏清除**

页面入场只清一次背景；局部变化使用既有增量区域。

**Step 3: SPI错误降级**

TFT底层保存last error；超时后本轮停止继续绘制，下一周期尝试恢复，不阻塞Safety。

**Step 4: Performance smoke test**

观察主菜单、仪表盘、WiFi页面无明显闪烁，ADC stale不因整页刷新误触发。

**Step 5: Commit**

```powershell
git add Keil_Project/Hardware/Ui_Controller.c Keil_Project/Hardware/Tft_Driver.c
git commit -m "perf: 减少TFT无效重绘与总线占用"
```

## Phase 6 — USART2与遥测一致性

### Task 22: USART2发送改为中断环形缓冲

**Files:**

- Modify: `Keil_Project/Hardware/Esp8266_Driver.c`
- Modify: `Keil_Project/Hardware/Esp8266_Driver.h`
- Modify: `Keil_Project/User/stm32f10x_it.c`
- Modify: `Keil_Project/User/stm32f10x_it.h`

**Step 1: 定义发送结果和静态队列**

固定256 B环形缓冲，返回OK、FULL、INVALID。

**Step 2: 实现入队临界区**

主循环复制字符串到队列，启用USART2 TXE中断，立即返回。

**Step 3: ISR发送**

TXE时发送一个字节；队列空时关闭TXE中断。RXNE优先处理，ORE顺序保持有效字节优先。

**Step 4: 调整调用点**

网络和UI检查返回值；队列满时记录诊断并等待下一周期，不阻塞。

**Step 5: Test and Commit**

模拟ESP断开，确认主循环和按键仍响应。

```powershell
git add Keil_Project/Hardware/Esp8266_Driver.c Keil_Project/Hardware/Esp8266_Driver.h Keil_Project/User/stm32f10x_it.c Keil_Project/User/stm32f10x_it.h
git commit -m "refactor: USART2发送改为中断队列"
```

### Task 23: 修复遥测状态映射和页面门控

**Files:**

- Modify: `Keil_Project/User/App_Network.c`
- Modify: `Keil_Project/User/App_Network.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: 增加显式协议映射函数**

```c
static uint8_t App_Network_Map_Telemetry_State(Sys_State state);
```

映射IDLE=0、SWEEP=1、RUNNING=2、FAULT=3。

**Step 2: 去除页面门控**

只要连接ONLINE，每500 ms发送。V/I使用显示滤波；F在SWEEP/RUNNING为实际频率，其他状态为0。

**Step 3: 检查远程命令幂等性**

- 已运行时ON不重复触发。
- IDLE时OFF无副作用。
- POWER OFF或FAULT时ON被统一控制API拒绝。

**Step 4: Protocol test**

向ESP输入四状态JSON，确认其`S==2`只在RUNNING上报Switch=true。

**Step 5: Commit**

```powershell
git add Keil_Project/User/App_Network.c Keil_Project/User/App_Network.h Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "fix: 对齐STM32遥测状态与ESP协议"
```

## Phase 7 — 公共调度、看门狗与结构整理

### Task 24: 合并状态公共任务并保持main简洁

**Files:**

- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/User/Sys_Core.h`
- Modify: `Keil_Project/User/main.c`

**Step 1: 提取公共前后任务**

顺序固定：ADC→Safety→Key→UI→state-specific→Network→Storage→LED/Buzzer→IWDG→WFI。

**Step 2: 状态函数只保留差异**

- IDLE后台维护。
- SWEEP软启动。
- RUNNING频率斜坡。
- FAULT后采样和复位等待。

**Step 3: 状态进入动作只执行一次**

STATUS LED等不再每圈重复写GPIO；状态转换函数负责进入动作。

**Step 4: main检查**

`main.c`保持初始化和switch，不加入按键、网络或Flash业务。

**Step 5: Verify**

运行静态脚本，确认main只包含初始化和状态分发；检查四个状态函数都经过同一公共任务入口。执行Keil Rebuild，要求0 Error、0 Warning。

**Step 6: Commit**

```powershell
git add Keil_Project/User/Sys_Core.c Keil_Project/User/Sys_Core.h Keil_Project/User/main.c
git commit -m "refactor: 统一系统公共调度顺序"
```

### Task 25: 校准IWDG与所有阻塞超时

**Files:**

- Modify: `Keil_Project/User/Sys_Core.c`
- Modify: `Keil_Project/Hardware/Tft_Driver.c`
- Modify: `Keil_Project/Hardware/W25Q_Driver.c`
- Modify: `Keil_Project/Hardware/Esp8266_Driver.c`
- Modify: `Keil_Project/System/Sys_Timer.c`

**Step 1: IWDG设置**

Prescaler 64、Reload 1500，窗口约1.6～2.4秒；更新全部错误的1.6秒/6.4秒注释。

**Step 2: 清查while等待**

ADC硬件校准、SPI BSY/RXNE、DMA TC、USART发送均必须有超时或仅存在于受控初始化阶段。

**Step 3: 超时不变量**

任何SPI/DMA超时必须关闭DMA请求、拉高CS并归还总线；任何运行时超时不得无限阻塞IWDG。

**Step 4: Verify and Commit**

```powershell
git add Keil_Project/User/Sys_Core.c Keil_Project/Hardware/Tft_Driver.c Keil_Project/Hardware/W25Q_Driver.c Keil_Project/Hardware/Esp8266_Driver.c Keil_Project/System/Sys_Timer.c
git commit -m "fix: 统一看门狗窗口与外设超时恢复"
```

### Task 26: ARMCC V5/C89与模块边界清理

**Files:**

- Modify: all `Keil_Project/Hardware/*.c/*.h`
- Modify: all `Keil_Project/System/*.c/*.h`
- Modify: all `Keil_Project/User/*.c/*.h`
- Modify: `Keil_Project/tests/verify_v5_0_2.ps1`

**Step 1: C89检查**

- 将语句后的变量声明移到代码块顶部。
- 删除代码中的`//`注释。
- 保持UTF-8中文只在注释；C字符串继续使用hex escape。

**Step 2: 命名与公开面**

- 静态函数统一模块前缀。
- 删除未使用公开函数和重复兼容getter。
- 头文件只公开必要接口。

**Step 3: 文档准确性**

修正PA12/PB12、四灯、14页面、TIM3采样、IWDG、Flash地址、CRC算法、遥测S映射等注释。

**Step 4: Static verify**

运行脚本，除版本同步规则外应全部PASS。

**Step 5: Keil Rebuild checkpoint C**

要求0 Error、0 Warning；记录ROM/RAM并与基线比较。

**Step 6: Commit**

只暂存33个STM32源文件和验证脚本：

```powershell
git add Keil_Project/Hardware Keil_Project/System Keil_Project/User Keil_Project/tests/verify_v5_0_2.ps1
git commit -m "refactor: 清理STM32 C89接口与失效说明"
```

提交前必须用`git diff --cached --name-only`确认没有用户无关文件和编译产物。

## Phase 8 — V5.0.2同步与最终验证

### Task 27: 统一V5.0.2版本和开发文档

**Files:**

- Modify: all STM32 `.c/.h` file headers
- Modify: `Keil_Project/Hardware/Tft_Driver.c` Splash string
- Modify: `README.md`
- Modify: `CLAUDE.md`
- Modify: `AGENTS.md` only after confirming the untracked file is intended for this repository
- Modify: `Claude_Files/docs/WPT无线充电系统-从零搭建全指南.md`
- Modify: `Claude_Files/docs/embedded-architect-system-prompt.md`
- Modify: relevant `Claude_Files/docs/specs/*.md`
- Modify: `ch341/README.md` only where partition/CRC description changes

**Step 1: 版本统一**

全部目标位置改为V5.0.2，历史版本保留在历史表，不机械替换外部工具版本。

**Step 2: 同步架构内容**

- 14页面设置系统。
- TIM3 500 Hz ADC。
- 强制PB10联锁。
- Blackbox V2分区。
- S=0/1/2/3协议。
- W25/TFT共享SPI恢复规则。

**Step 3: 处理根目录AGENTS.md冲突**

当前`AGENTS.md`为未跟踪用户文件。先比较其内容与仓库规则；未经用户确认不得直接加入提交。若确认纳入，再更新其中V4旧路径和V5.0.2信息。

**Step 4: Verify and Commit**

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project\tests\verify_v5_0_2.ps1
git diff --check
```

Expected: 全部PASS，无空白错误。

按确认后的文档文件精确暂存并提交：

```powershell
git commit -m "docs: 同步V5.0.2 STM32优化架构"
```

### Task 28: 最终编译、硬件回归与清理

**Files:**

- Verify: `Keil_Project/Objects/Project.build_log.htm`
- Verify: `Keil_Project/Listings/Project.map`
- Create: `../reports/2026-07-19-stm32-verification-V5.0.2.md`
- Run: `Keil_Project/keilkill.bat` only after recording final build evidence and immediately before push

**Step 1: Final Keil Rebuild**

用户在Keil执行Rebuild All。读取构建日志，要求：

```text
0 Error(s), 0 Warning(s)
```

记录最终Code、RO、RW、ZI、ROM、RAM及剩余空间。

**Step 2: 最终静态验证**

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project\tests\verify_v5_0_2.ps1
```

Expected: 全部PASS，退出码0。

**Step 3: 硬件回归矩阵**

按设计文档第17节逐项验证并记录：

- 上电/NRST/Flash缺失。
- KEY0～KEY4。
- 本地与远程启停联锁。
- SWEEP/RUNNING过流。
- ADC校准与stale。
- 设置延迟保存。
- 普通日志、重启恢复、故障快照。
- WiFi离线、本地继续工作。
- 四状态遥测。

**Step 4: 复核Git范围**

```powershell
git status --short
git diff --check
git log --oneline --decorate -15
```

确认用户原有未提交文件仍被保留，编译产物未跟踪。

**Step 5: 写入验证报告并提交**

验证报告记录：提交范围、静态脚本结果、Keil错误/警告、ROM/RAM、每项硬件测试的PASS/FAIL/NOT RUN及原因。未执行项目必须明确标记，禁止写成已通过。

```powershell
git add NONFILE/history/stm32-designs/reports/2026-07-19-stm32-verification-V5.0.2.md
git commit -m "test: 记录V5.0.2最终验证结果"
```

**Step 6: 清理并准备推送**

只有用户明确要求“提交/推送”后才执行：

```powershell
cmd.exe /c Keil_Project\keilkill.bat
git status --short
git push origin 5.0
```

清理前必须保存最终build log关键指标；push前确认没有`.obj/.lst/.axf`等产物。

**Step 7: Completion report**

报告：

- 完成的28个任务及提交列表。
- 修复的P0/P1问题。
- Keil编译结果和资源变化。
- 硬件测试通过/未执行项。
- 未推送时明确说明本地提交状态。

---

## 阶段停止条件

出现以下任一情况立即停止后续阶段并诊断：

- Keil出现新Error或Warning。
- PB10在任何异常路径保持HIGH。
- PWM在IDLE/FAULT仍有输出。
- NRST出现白屏或SPI切换乱码。
- ADC采样序号不稳定在约500 Hz。
- Flash写失败仍推进指针。
- ROM超过65536 B或RAM超过20480 B。
- Git暂存区出现用户无关文件或Keil产物。

## 完成定义

只有同时满足以下条件才可宣称V5.0.2完成：

1. 28个任务全部完成或有明确经用户接受的删减。
2. 静态验收脚本全部PASS。
3. Keil Rebuild 0 Error、0 Warning。
4. 资源不超STM32F103C8限制。
5. 安全、按键、SPI、Flash和遥测硬件矩阵通过。
6. 设计、代码和文档版本一致。
7. 用户无关改动得到完整保留。
