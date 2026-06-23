@echo off
chcp 65001 >nul
title OTA 字库推送 — Python 环境自动配置
cd /d "%~dp0"

echo ╔══════════════════════════════════════════════╗
echo ║     OTA 字库推送 — 一键环境配置              ║
echo ╚══════════════════════════════════════════════╝
echo.

:: ── 第 1 步：检查 Python 是否已安装 ──
set PYTHON_CMD=
for %%p in (python python3) do (
    where %%p >nul 2>nul
    if !errorlevel!==0 (
        %%p --version >nul 2>&1
        if !errorlevel!==0 set PYTHON_CMD=%%p
    )
)

if not "%PYTHON_CMD%"=="" (
    echo [✓] Python 已安装:
    %PYTHON_CMD% --version
    goto :verify
)

:: ── 第 2 步：检查常见安装路径 ──
echo [ ] Python 未在 PATH 中找到，正在搜索常见路径...
for %%d in (
    "%LOCALAPPDATA%\Programs\Python"
    "%PROGRAMFILES%\Python"
    "%PROGRAMFILES(x86)%\Python"
    "C:\Python"
    "E:\Anaconda3"
    "%USERPROFILE%\Anaconda3"
    "%LOCALAPPDATA%\Microsoft\WindowsApps"
) do (
    if exist "%%d\python.exe" (
        set "PATH=%%d;%PATH%"
        set PYTHON_CMD=python
        echo [✓] 在 %%d 找到 Python
        %PYTHON_CMD% --version
        goto :verify
    )
)

:: ── 第 3 步：自动下载安装 Python ──
echo.
echo [ ] Python 未找到，正在自动下载安装 Python 3.12...
echo      (需要联网，约 25MB)
echo.

set PYTHON_URL=https://www.python.org/ftp/python/3.12.3/python-3.12.3-amd64.exe
set PYTHON_INSTALLER=%TEMP%\python-3.12.3-amd64.exe

:: 方法 1: curl (Windows 10+)
curl --version >nul 2>&1
if %errorlevel%==0 (
    echo [ ] 正在下载...
    curl -L -o "%PYTHON_INSTALLER%" "%PYTHON_URL%" --progress-bar
    if %errorlevel%==0 goto :install
)

:: 方法 2: powershell
echo [ ] curl 不可用，改用 PowerShell 下载...
powershell -Command "Invoke-WebRequest -Uri '%PYTHON_URL%' -OutFile '%PYTHON_INSTALLER%'"
if %errorlevel%==0 goto :install

echo [✗] 下载失败 — 请手动安装 Python:
echo      1. 浏览器打开: https://python.org/downloads/
echo      2. 下载 Python 3.x → 安装时勾选 "Add Python to PATH"
echo      3. 重启命令提示符 → 重新运行本脚本
pause
exit /b 1

:install
echo [ ] 正在安装 Python 3.12...
echo      (请在弹出的安装向导中点击 Install Now)
"%PYTHON_INSTALLER%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0

if %errorlevel%==0 (
    echo [✓] Python 安装完成
    set PYTHON_CMD=python
) else (
    echo [ ] 静默安装失败，尝试交互安装...
    "%PYTHON_INSTALLER%"
    echo [ ] 请完成安装后按任意键继续...
    pause >nul
)

:: 刷新 PATH
set "PATH=%LOCALAPPDATA%\Programs\Python\Python312;%LOCALAPPDATA%\Programs\Python\Python312\Scripts;%PATH%"
set PYTHON_CMD=python

:: ── 第 4 步：验证 Python 安装 ──
:verify
echo.
echo ─── 环境验证 ───

%PYTHON_CMD% --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [✗] Python 启动失败
    echo     请手动安装: https://python.org/downloads/
    echo     安装时勾选 "Add Python to PATH"
    pause
    exit /b 1
)

%PYTHON_CMD% --version
echo [✓] Python 正常

:: ── 第 5 步：验证推送工具 ──
echo.
if not exist "ota_font_push.py" (
    echo [✗] ota_font_push.py 未找到 — 请确认本脚本放在 Claude_Files\tools\ 目录下
    pause
    exit /b 1
)

echo [ ] 测试字库解析...
%PYTHON_CMD% -c "import sys; sys.path.insert(0, '.'); from ota_font_push import build_font_bin; b,n=build_font_bin('../../Keil_Project/Hardware/TFT_Font_Data.h'); print(f'[✓] 字库解析成功: {len(b)}B, {n} 页')"
if %errorlevel% neq 0 (
    echo [✗] 字库解析失败 — 请检查 TFT_Font_Data.h 是否存在
    pause
    exit /b 1
)

:: ── 收工 ──
echo.
echo ╔══════════════════════════════════════════════╗
echo ║  [✓] 环境配置完成!                           ║
echo ║                                            ║
echo ║  下一步:                                    ║
echo ║  1. 确认 ESP8266 IP 地址                     ║
echo ║  2. 在命令提示符中运行:                       ║
echo ║     python ota_font_push.py --ip ^<IP^>      ║
echo ╚══════════════════════════════════════════════╝
echo.
echo 按任意键退出...
pause >nul
exit /b 0
