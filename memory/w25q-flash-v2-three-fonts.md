---
name: w25q-flash-v2-three-fonts
description: W25Q128 Flash字库V2三字体方案: 华文楷体中文 + Arial字母 + 宋体符号
metadata:
  type: reference
---

W25Q Flash font_data.bin V2 使用三种字体:
1. **中文 (CJK 20902字)**: 华文楷体 STKAITI.TTF 16px — 对应 `cjk_font`
2. **字母数字 (A-Z a-z 0-9)**: Arial 16px — 对应 `ascii_font`
3. **符号 ( {}[]。、等 33字符)**: 宋体 simsun.ttc 16px — 对应 `sym_font`，与 ROM 一致

在 `generate_font.py` 的 `render_ascii(letter_font, symbol_font)` 函数中，`ch.isalpha() or ch.isdigit()` 判断选择 Arial，其余走宋体。

ROM (TFT_Font_Data.h) 仅保留 ASCII 95 + CN 4字 (无/线/充/电)，用于 Flash 无效时 SPLASH 和全英文回退 UI。
