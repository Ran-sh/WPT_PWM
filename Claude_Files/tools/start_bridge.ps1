# WPT 桥接服务器 + ngrok 一键启动脚本
# 用途: 启动 Node.js 桥接服务器 + ngrok 公网隧道，微信小程序即可远程连接
# 用法: powershell -ExecutionPolicy Bypass -File start_bridge.ps1

$ErrorActionPreference = "Stop"
$BridgeDir  = Join-Path $PSScriptRoot "..\..\安卓app\server"
$NgrokLog   = Join-Path $env:TEMP "ngrok_wpt.log"
$BridgePort = 3000

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WPT 桥接服务器 + ngrok 启动脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ── 1. 检查 Node.js ──
Write-Host "[1/4] 检查 Node.js..." -ForegroundColor Yellow
$nodeVersion = node --version 2>$null
if (-not $nodeVersion) {
    Write-Host "  [错误] 未找到 Node.js，请安装 https://nodejs.org/" -ForegroundColor Red
    pause
    exit 1
}
Write-Host "  Node.js $nodeVersion OK" -ForegroundColor Green

# ── 2. 检查 ngrok ──
Write-Host "[2/4] 检查 ngrok..." -ForegroundColor Yellow
$ngrokVersion = ngrok version 2>$null
if (-not $ngrokVersion) {
    Write-Host "  [错误] 未找到 ngrok，请安装 https://ngrok.com/download" -ForegroundColor Red
    pause
    exit 1
}
Write-Host "  ngrok 已安装" -ForegroundColor Green

# ── 3. 检查端口占用 ──
Write-Host "[3/4] 检查端口 $BridgePort..." -ForegroundColor Yellow
$portProc = netstat -ano 2>$null | Select-String ":$BridgePort\s"
if ($portProc) {
    Write-Host "  [警告] 端口 $BridgePort 已被占用，尝试释放..." -ForegroundColor Magenta
    $pidMatch = [regex]::Match($portProc, '\s+(\d+)\s*$')
    if ($pidMatch.Success) {
        Stop-Process -Id $pidMatch.Groups[1].Value -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
        Write-Host "  已释放端口" -ForegroundColor Green
    }
}

# ── 4. 启动桥接服务器 ──
Write-Host "[4/4] 启动桥接服务器..." -ForegroundColor Yellow
$bridgeJob = Start-Job -ScriptBlock {
    param($dir)
    Set-Location $dir
    node bridge.cjs 2>&1
} -ArgumentList $BridgeDir

Start-Sleep -Seconds 2

# 验证桥接服务器
try {
    $health = Invoke-RestMethod -Uri "http://localhost:$BridgePort/health" -TimeoutSec 3
    Write-Host "  桥接服务器 OK — MQTT: $($health.mqtt)" -ForegroundColor Green
} catch {
    Write-Host "  [错误] 桥接服务器启动失败: $_" -ForegroundColor Red
    Stop-Job $bridgeJob -ErrorAction SilentlyContinue
    Remove-Job $bridgeJob -ErrorAction SilentlyContinue
    pause
    exit 1
}

# ── 5. 启动 ngrok 隧道 ──
Write-Host ""
Write-Host "[ngrok] 启动公网隧道..." -ForegroundColor Yellow
$ngrokProc = Start-Process -FilePath "ngrok" -ArgumentList "http","$BridgePort","--log=stdout" -NoNewWindow -PassThru -RedirectStandardOutput $NgrokLog

Start-Sleep -Seconds 5

# ── 提取 ngrok 公网 URL ──
$ngrokContent = Get-Content $NgrokLog -Raw -ErrorAction SilentlyContinue
$urlMatch = [regex]::Match($ngrokContent, 'url=(https://[^\s]+\.ngrok-free\.dev)')
if ($urlMatch.Success) {
    $publicUrl = $urlMatch.Groups[1].Value
} else {
    Write-Host "  [警告] 未能从日志提取 ngrok URL，尝试 API 获取..." -ForegroundColor Magenta
    try {
        $apiResult = Invoke-RestMethod -Uri "http://localhost:4040/api/tunnels" -TimeoutSec 3
        $publicUrl = $apiResult.tunnels[0].public_url
    } catch {
        Write-Host "  [错误] 无法获取 ngrok 公网地址" -ForegroundColor Red
        Write-Host "  请检查 ngrok 是否正常启动" -ForegroundColor Red
        pause
        exit 1
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  隧道已就绪!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  公网地址: $publicUrl" -ForegroundColor White
Write-Host "  数据接口: $publicUrl/data" -ForegroundColor White
Write-Host "  控制接口: $publicUrl/cmd" -ForegroundColor White
Write-Host "  健康检查: $publicUrl/health" -ForegroundColor White
Write-Host ""

# ── 验证公网链路 ──
Write-Host "[验证] 测试公网数据接口..." -ForegroundColor Yellow
try {
    $headers = @{ "ngrok-skip-browser-warning" = "true" }
    $data = Invoke-RestMethod -Uri "$publicUrl/data" -Headers $headers -TimeoutSec 5
    Write-Host "  数据接口 OK — V=$($data.voltage) I=$($data.current) F=$($data.frequency)" -ForegroundColor Green
} catch {
    Write-Host "  [警告] 公网验证失败，请稍后重试: $_" -ForegroundColor Magenta
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  微信小程序 BRIDGE 地址:" -ForegroundColor Yellow
Write-Host "  const BRIDGE = '$publicUrl';" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "按 Ctrl+C 停止全部服务" -ForegroundColor Gray

# ── 等待用户中断 ──
try {
    while ($true) {
        Start-Sleep -Seconds 10
        # 静默心跳 — 检查进程存活
        if ($ngrokProc.HasExited) {
            Write-Host "[错误] ngrok 已退出 (code: $($ngrokProc.ExitCode))" -ForegroundColor Red
            break
        }
    }
} finally {
    Write-Host ""
    Write-Host "正在停止服务..." -ForegroundColor Yellow
    Stop-Process -Id $ngrokProc.Id -Force -ErrorAction SilentlyContinue
    Stop-Job $bridgeJob -ErrorAction SilentlyContinue
    Remove-Job $bridgeJob -ErrorAction SilentlyContinue
    Write-Host "已停止全部服务" -ForegroundColor Green
}
