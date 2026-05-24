# SETFREQ 频率渐变斜坡 + 水平滚轮选取 — 设计规格

**日期**: 2026-05-24 | **状态**: 已批准

## 变更摘要

CMD:SETFREQ 从"瞬间跳频"改为"非阻塞渐变斜坡"，Mini Program 从 slider 改为水平滚轮 + SET 确认按钮。

## STM32: 频率渐变斜坡 (PWM.c/h)

### 新增函数

```c
void Inverter_FreqRamp_Trigger(uint32_t target_Hz);  // 启动渐变 (仅 SS_DONE)
void Inverter_FreqRamp_Task(void);                    // 主循环步进, 非阻塞
uint32_t Inverter_FreqRamp_GetTarget(void);           // 查询目标频率 (UI 用)
```

### 参数

| 参数 | 值 | 说明 |
|:---|:---|:---|
| FREQ_RAMP_STEP_HZ | 500 | 每步步长 |
| FREQ_RAMP_STEP_DELAY_MS | 10 | 步进间隔 |
| 速率 | 50kHz/s | 50kHz 跨度约 1s |

### 行为

- 仅 SS_DONE 时生效
- 每 10ms 向目标方向步进 500Hz
- 新 SETFREQ 覆盖旧目标，立即转向
- Inverter_SoftStart_Stop() 清零 s_ramp_target
- s_ramp_target == 0 → Task 直接 return

### App_Net.c 修改

```c
// 旧: PWM_SetFrequency(f) 直接跳变
// 新: Inverter_FreqRamp_Trigger((uint32_t)f) 启动渐变
```

### main.c 新增调用

```c
Inverter_FreqRamp_Task();  // 在 Inverter_SoftStart_Task() 之后
```

## Mini Program: 水平滚轮选取

### UI 组件

- scroll-view 水平滚动, white-space:nowrap
- 56 个数字块 (95-150)
- 当前值居中高亮 (青色放大)
- SET 按钮确认发送

### 行为

- 滚动松手: 仅更新 local selectedValue, 不发送
- 点 SET: 发送 CMD:SETFREQ:{selectedValue*1000}
- 遥测回读同步: 渐变过程中频率逐步变化, 滚轮位置跟随
- 仅在 isOn (SS_DONE 或 SS_SWEEP) 时可用

## 影响范围

| 文件 | 变更类型 |
|:---|:---|
| Keil_Project/Hardware/PWM.c | 新增 3 函数 + s_ramp_target 状态 |
| Keil_Project/Hardware/PWM.h | 新增 3 声明 + FREQ_RAMP 宏 |
| Keil_Project/User/App_Net.c | SETFREQ 处理器改调用 Inverter_FreqRamp_Trigger |
| Keil_Project/User/main.c | 新增 Inverter_FreqRamp_Task() 调用 |
| 安卓app/pages/index/index.wxml | slider → scroll-view 水平滚轮 + SET 按钮 |
| 安卓app/pages/index/index.js | onFreqChange → onScrollSelect + onSetFreq |
| 安卓app/pages/index/index.wxss | slider 样式 → scroll-picker 样式 |
