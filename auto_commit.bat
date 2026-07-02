@echo off
cd /d "D:\Claude Code Project\WPT_PWM_V4.0_ONENET_TFT"
echo === Step 1: Clean Keil build artifacts ===
call Keil_Project\keilkill.bat
echo === Step 2: Git status ===
git status --short
echo === Step 3: Git add & commit ===
git add -A
git commit -m "feat: V4.5.0 — 设置系统重构 (8页PIC预览/字间距0-6px/亮度滚动/颜色6预设)"
echo === Step 4: Git push ===
git push origin 4.0TFT
echo === Done ===
pause
