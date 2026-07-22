# Task 2 Report

- 状态：DONE
- 提交：本报告随 Task 2 提交一并创建。

## 修改摘要

- PWM 最终硬件频率边界调整为 20,000–200,000 Hz；原有 1us 死区、偶数计数周期、UDIS 原子更新和 50% 占空比逻辑保持不变。
- 逆变器控制层新增独立的启动档位枚举与配置接口。低档从 99,900 Hz 以 100 Hz/10 ms 降至已保存目标；高档从 200,000 Hz 以 1,000 Hz/10 ms 降至已保存目标。
- 配置非法时硬件层回退到高档 100,000 Hz；每次触发时才锁定起点、目标和步进，因此设置保存不会改变当前扫频快照。
- `Sys_Post_Init()` 从持久化配置注入启动档位；设置保存路径同步下一次启动参数。

## RED

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
```

实现前按预期失败：

```text
PWM lower limit is not 20kHz
```

## GREEN

```powershell
powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1
```

结果：`Settings configuration contract passed`

## ARMCC

```powershell
& 'D:\Keil5\UV4\UV4.exe' -b 'Keil_Project\Project.uvprojx' -j0
```

结果：

```text
".\Objects\Project.axf" - 0 Error(s), 0 Warning(s).
```

## 偏差

- 任务简报中指定的契约脚本是既有的 `verify_settings_frequency.ps1`；仓库中不存在 `verify_frequency_profiles.ps1`，因此未新建重复脚本。
- 为满足“设置保存后更新下一次触发配置”，最小关联修改了 `Ui_Controller.c`；该文件已有 `Inverter_Control.h` 引用，未引入新的层级依赖。
