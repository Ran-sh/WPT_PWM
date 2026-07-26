# WPT 桥接服务器与 ngrok 安全停止脚本
# V5.1.3：仅停止启动脚本记录的进程，避免误伤其他 Node.js 或 ngrok 服务。

$ErrorActionPreference = 'Stop'
$StateFile = Join-Path $env:TEMP 'wpt_bridge_state.json'

function Stop-OwnedProcess([int]$ProcessId, [string]$ExpectedName) {
    if ($ProcessId -le 0) { return $false }
    $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if (-not $process -or $process.ProcessName -ne $ExpectedName) { return $false }
    Stop-Process -Id $ProcessId -Force
    return $true
}

if (-not (Test-Path $StateFile)) {
    Write-Host '未找到桥接服务状态文件，没有停止任何进程。' -ForegroundColor Yellow
    exit 0
}

$state = Get-Content -Raw -Encoding UTF8 $StateFile | ConvertFrom-Json
$ngrokStopped = Stop-OwnedProcess ([int]$state.NgrokPid) 'ngrok'
$bridgeStopped = Stop-OwnedProcess ([int]$state.BridgePid) 'node'
Remove-Item -LiteralPath $StateFile -Force -ErrorAction SilentlyContinue

if ($ngrokStopped -or $bridgeStopped) {
    Write-Host '已停止本项目记录的桥接进程。' -ForegroundColor Green
} else {
    Write-Host '记录的进程已退出或身份不匹配，没有强制停止其他程序。' -ForegroundColor Yellow
}
