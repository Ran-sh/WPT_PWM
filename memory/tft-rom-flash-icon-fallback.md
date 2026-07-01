---
name: tft-rom-flash-icon-fallback
description: ROM 保留 3 图标 (WIFI_OFF/WIFI_REMOVE/MQTT_NO)，Flash 优先全量 31 图标。中文 仿宋 + ASCII 宋体。
metadata:
  type: reference
---

## TFT 图标架构 (V4.3.2 最终方案)

### 数据流

```
Draw_TopRight_Icons()
  └─ Tft_Driver_Draw_Icon_By_Id(icon_id, frame)
       ├─ Flash 有效 (s_font_flash_valid=1)
       │    └─ W25Q128 Icon Table lookup → 31 图标/54 帧全量
       └─ Flash 无效 (s_font_flash_valid=0)
            ├─ ICON_ID_WIFI_OFF    → ROM WIFI_OFF_ICON    ✅
            ├─ ICON_ID_WIFI_REMOVE → ROM WIFI_REMOVE_ICON ✅
            ├─ ICON_ID_MQTT_NO     → ROM MQTT_NO_ICON     ✅
            └─ 其余 28 图标        → return 0 (空白)
```

### ROM 回退（TFT_Font_Data.h）

| 数据 | 大小 | 说明 |
|:--|:--|:--|
| ASCII 95 字 | 1520B | 宋体, 与 Flash 一致 |
| CN 4字 | 128B | 无/线/充/电, SPLASH 开机动画 |
| WIFI_OFF_ICON | 32B | Flash 无效时仍显示的 WiFi 断开图标 |
| WIFI_REMOVE_ICON | 32B | Flash 无效时仍显示的 WiFi 未配置图标 |
| MQTT_NO_ICON | 32B | Flash 无效时仍显示的 MQTT 断开图标 |
| **ROM 总计** | **~1.8KB** | |

### Flash (V2 font_data.bin)

| 数据 | 字体 | 说明 |
|:--|:--|:--|
| ASCII 95 字 | **宋体** simsun.ttc | 与 ROM 一致 |
| CJK 20902 字 | **仿宋** SIMFANG.TTF | GB2312 一级汉字 |
| 图标 31个/54帧 | — | V2 48B header, icon_table_offset |

### UI 中英回退（Ui_Controller.c）

- `Ui_Lang(zh, en)` → Flash 有效返回中文, 无效返回英文
- 所有 S_* 宏改为 `Ui_Lang("...", "ENGLISH")`
- snprintf 格式串改为 `"%sF:%3lu...", S_FREQ, ...` (不再编译期拼接)
