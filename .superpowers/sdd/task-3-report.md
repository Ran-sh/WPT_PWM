# Task 3 完成报告

- 基线：`3c57722`。
- 提交说明：本报告随 Task 3 实现一并提交。

## RED

先扩展 `Keil_Project/tests/verify_settings_frequency.ps1` 的契约，首次运行失败：

```
Missing startup frequency page
```

失败原因符合预期：旧界面没有 `UI_PAGE_SETTING_FREQUENCY`，也没有频率编辑副本和八项光标候选表。

## GREEN

- 设置菜单固定为语言、启动频率、字间距、光标图标、配色方案五项。
- 启动频率页支持低档 20.0–99.9kHz（100Hz 步进）与高档 100–200kHz（1kHz 步进）；编辑使用副本，返回取消不写回，确认后保存并更新下一轮软启动配置。
- 全部菜单光标经 `Ui_Controller_Draw_Menu_Cursor()` 绘制；候选图标固定为八项，持久化的是候选索引。外部字库无效或图标绘制失败时回退片内星标。
- 保存提示使用 SysTick 时间戳自动失效，未引入运行期延时；旧背光驱动能力未删除。

## 验证

- `powershell -ExecutionPolicy Bypass -File Keil_Project/tests/verify_settings_frequency.ps1`：通过。
- `cmd.exe /c Keil_Project/keilkill.bat` 后，Keil μVision `-r Project.uvprojx` 全量重建：`0 Error(s), 0 Warning(s)`。
- 程序尺寸：Code=55438，RO-data=3958，RW-data=540，ZI-data=7180；Code+RO-data=59396 bytes，低于 STM32F103C8 的 64KB Flash。

## 偏差与说明

`Target 1.BAT` 在清理后仍引用不存在的 `Objects/w25q_driver_1.__i`，不能独立重建；改用 Keil μVision 的 `-r Project.uvprojx` 完成同一全量 ARMCC 重建。该脚本问题不由本任务引入，未修改其内容。

## 审查修复

- 返回键双击从任意设置子页直接退出时，先丢弃未确认的频率副本；若已有确认但尚未写入的设置，则统一调用 `Ui_Controller_Save_Settings()` 后再回主菜单。
- 删除两段旧的 35 项图标浏览死代码和过时注释，图标页仅保留八项候选实现。
- 语言、字间距和配色页删除与全局图标光标重复的星号选择标记；配色勾号仅表示已保存方案。
- 审查修复后的全量重建：Code=55282，RO-data=3958，RW-data=540，ZI-data=7180；Code+RO=59240 bytes，距 64KB Flash 还余 6296 bytes，`0 Error(s), 0 Warning(s)`。
