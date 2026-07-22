# Task 4：独立分段仪表盘报告

## 审查修复

- 频率表在所有非 IDLE 状态使用 `Inverter_Control_Get_Sweep_Start_Freq()` 固定本轮量程，IDLE 才读取活动设置档。
- 中央测量值改用现有 8×16 ASCII 字模的 2×接口，单位宽度按实际像素计算；`kHz` 也参与整体居中计算。
- 新的 2×字符与字符串接口处理 NULL、完整 x/y 边界和绘制总线阻塞；阻塞后立即退出。
- 表盘状态优先显示 `Sys_Core_Get_Fault()` 返回的具体锁存故障并使用红色。现有 UI Phase 不强制跳转故障页，因此保持当前页面显示具体原因、同时保留故障菜单入口，避免改变既有导航语义。

## 完成内容

- 电压、电流及频率页面改为共享 `GaugeSegment` 分段映射；各视觉格等角度且跨段连续。
- 电压刻度：0–20V/2V、20–40V/5V、40–50V/10V；电流刻度：0–1A/0.1A、1–3A/0.5A、3–5A/1A。
- 电流在 4A 显示注意色、4.5A 起红色；频率依据活动低/高频档选择量程，扫频时依据锁定的扫频起点固定量程。
- 中心数据使用既有 8×16 ASCII 字模的 2×像素缩放；频率停机显示 0，圆弧归零。
- 200ms 刷新只更新跨越的刻度、中心数值和状态；中央擦除区避开圆弧两侧刻度，避免残影与断弧。

## 验证

- `powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_task4_gauge.ps1`：通过。
- `powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1`：通过。
- `cmd.exe /c Keil_Project/keilkill.bat` 后执行 `D:\Keil5\UV4\UV4.exe -r Project.uvprojx -t "Target 1"`：ARMCC V5.06 为 0 Error(s), 0 Warning(s)。
- 容量：Code 52250B、RO-data 4098B、RW-data 544B、ZI-data 7184B；ROM 56892B / 65536B，RAM 7728B / 20480B。

## 偏差

无功能偏差。为了不新增字库资源，2×数值由现有数字字模运行时整数倍放大实现。
