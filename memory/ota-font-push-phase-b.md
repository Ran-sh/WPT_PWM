---
name: ota-font-push-phase-b
description: GB2312 全字库 OTA 方案B — 后续升级到 6763 汉字推送，方案A跑通后复用协议层
metadata:
  type: project
---

方案 B 是完整 GB2312 全字库版（6763 汉字 + ASCII + 图标，约 668KB）。

**前置条件**：方案 A 跑通（TCP→ESP→USART→STM32→Flash 链路验证）

**核心差异**：
- 数据量从 4KB→668KB，2600+ 页，115200bps 下约 50s (加上 busy 等待 + Base64 膨胀 ≈ 70s)
- PC 端需要字模提取工具链 (FreeType + PIL 从 win10 系统字体生成 Unicode bin)
- 传输协议层完全复用方案 A，仅页数变化
- TFT 进度条需要两档刷新（<50% / >80%）而非逐页刷新

**触发方式**: 方案 A 跑通后，对比当前 76 汉字 TFT 显示效果 → 决定是否升级全字库
**Why:** 用户担心忘记方案B，写此记忆留存
**How to apply:** 方案 A 完成后自动提示此记忆
