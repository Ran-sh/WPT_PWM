# WPT 桥接服务器 + ngrok 一键停止脚本
# 用法: powershell -ExecutionPolicy Bypass -File stop_bridge.ps1

Write-Host "WPT 桥接服务停止脚本" -ForegroundColor Cyan
Write-Host ""

$killed = $false

# ── 1. 停止 ngrok ──
Write-Host "[1/2] 停止 ngrok..." -ForegroundColor Yellow
$ngrokProcs = Get-Process -Name "ngrok" -ErrorAction SilentlyContinue
if ($ngrokProcs) {
    Stop-Process -Name "ngrok" -Force -ErrorAction SilentlyContinue
    Write-Host "  已停止 ngrok (PID: $(($ngrokProcs | ForEach-Object { $_.Id }) -join ', '))" -ForegroundColor Green
    $killed = $true
} else {
    Write-Host "  ngrok 未在运行" -ForegroundColor Gray
}

# ── 2. 停止 Node.js bridge ──
Write-Host "[2/2] 停止桥接服务器..." -ForegroundColor Yellow
$nodeProcs = Get-Process -Name "node" -ErrorAction SilentlyContinue | Where-Object {
    $_.CommandLine -match "bridge" -or $_.MainWindowTitle -match "bridge"
}
if (-not $nodeProcs) {
    # 备选: 查找监听 3000 端口的 node 进程
    $conn = netstat -ano 2>$null | Select-String ":3000\s+.*LISTENING"
    if ($conn) {
        $pidMatch = [regex]::Match($conn, '\s+(\d+)\s*$')
        if ($pidMatch.Success) {
            $pid = $pidMatch.Groups[1].Value
            Stop-Process -Id $pid -Force -ErrorAction SilentlyContinue
            Write-Host "  已停止端口 3000 上的进程 (PID: $pid)" -ForegroundColor Green
            $killed = $true
        }
    } else {
        Write-Host "  未找到桥接服务器进程" -ForegroundColor Gray
    }
} else {
    Stop-Process -Name "node" -Force -ErrorAction SilentlyContinue | Where-Object {
        $_.CommandLine -match "bridge"
    }
    Write-Host "  已停止桥接服务器" -ForegroundColor Green
    $killed = $true
}

Write-Host ""
if ($killed) {
    Write-Host "全部服务已停止" -ForegroundColor Green
} else {
    Write-Host "没有需要停止的服务" -ForegroundColor Gray
}
