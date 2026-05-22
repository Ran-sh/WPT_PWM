# 非阻塞软启动扫频 + 主程序重构

- **日期**: 2026-05-18
- **状态**: Approved
- **涉及文件**: `Hardware/PWM.c`, `Hardware/PWM.h`, `User/main.c`, `Hardware/UI.c`, `Hardware/UI.h`

## 背景

当前 `Inverter_SoftStart()` 是阻塞实现（`SysTimer_DelayMs` 延时），扫频期间主循环停摆。需要改为非阻塞状态机，在主循环中与其他 Task 并发运行。同时加入 OLED 实时显示扫频进度，支持本地按键和远程 TCP 指令双重触发。

## 设计

### 1. PWM 模块：非阻塞软启动状态机

新增类型和函数声明（`Hardware/PWM.h`）：

```c
typedef enum {
    SS_IDLE  = 0,
    SS_SWEEP = 1,
    SS_DONE  = 2
} SoftStart_State_t;

void               Inverter_SoftStart_Trigger(void);
void               Inverter_SoftStart_Task(void);
void               Inverter_SoftStart_Stop(void);
SoftStart_State_t  Inverter_SoftStart_GetState(void);
uint32_t           Inverter_SoftStart_GetCurrentFreq(void);
```

实现（`Hardware/PWM.c`）采用 `static uint32_t last_step` 时间戳差值法，2ms/步，扫频 150kHz→100kHz 共 250 步 ≈ 500ms。内部状态变量全部 `static`，对外只通过上述接口访问。

关键行为：
- `PWM_Init`：配置 TIM1 时基/通道/死区，计数器运行但 **MOE 关闭（输出禁止）**，上电默认安全态
- `Trigger`：先设 150kHz，再使能 MOE，状态 → `SS_SWEEP`
- `Task`：每 2ms 降 200Hz，到达 100kHz 后状态 → `SS_DONE`
- `Stop`：立即关 MOE，状态 → `SS_IDLE`。**每次 ON 都是全新扫频**——下次 Trigger 必定从 150kHz 开始重新扫频，不复用上次状态
- `PWM_SetFrequency` 的 95kHz 硬限幅作为第二道防线

### 2. main.c 启动流程

```
阶段 1: PWM_Init (配置完成, MOE 关, 全桥输出禁止)
         OLED_Init, HardLED_Init, ADC_DMA_Init, KEY_Init
         OLED 显示 "Wireless Charge" 开机画面
阶段 2: SysTimer_Init
阶段 3: App_Net_Init (阻塞联网, 20~30s)
         联网成功 → OLED 显示 "Ready"
阶段 4: while(1)
          ├─ KEY_Task                   ← KEY0 → Trigger, KEY1 → Stop
          ├─ UI_Task                    ← 读取状态/频率, 刷新 OLED
          ├─ App_Net_Task               ← "ON" → Trigger, "OFF" → Stop
          ├─ Inverter_SoftStart_Task    ← 非阻塞步进 (内部 2ms 节拍)
          └─ HardLED_Task
```

`main.c` 中不再有任何裸操作 TIM1 寄存器的代码，全部通过 `PWM_Enable/Disable/Trigger/Stop` 接口。

### 3. UI 显示

每次切页前调用 `OLED_Clear()` 清屏，保证无残影。

`UI_Task` 内部根据 `Inverter_SoftStart_GetState()` 分页：

- `SS_IDLE`：清屏 → 显示 "Ready" / "Waiting trigger"
- `SS_SWEEP`：清屏（首次进入时）→ 实时刷新 "Sweeping..." + 频率值 + 进度条
- `SS_DONE`：清屏 → 切换回监控页（电压/电流/频率/功率）

### 4. App_Net 远程指令

在 `App_Net_Task` 的命令解析中增加 `SoftStart_State_t` 判断：收到 "ON" 时仅当状态为 `SS_IDLE` 才调用 `Trigger`，防止重复触发。收到 "OFF" 时调用 `Stop` 并反馈状态。

## 不变项

- 死区 1000ns（`DEADTIME_NS` 宏）
- 占空比 50% 锁定，AR+CCR 同步刷新
- 频率硬下限 95kHz（软件钳位安全底线，扫频目标频率仍为 100kHz）
- SysTimer 时间戳差值法
- SPL 标准库，不引入 HAL/LL

## 风险

| 风险 | 缓解 |
|:---|:---|
| 扫频被主循环调用频率影响（每轮 ~500μs） | Task 内部 2ms 时间戳保证节拍准确 |
| 远程/本地同时触发 | Trigger 时检查状态，SS_SWEEP 期间忽略二次触发 |
| 扫频中途关断再启动 | Stop→Trigger 重新从 150kHz 开始 |
