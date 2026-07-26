# WPT 桥接服务器与 ngrok 安全启动脚本
# 只管理本脚本创建的进程，不会终止占用端口的其他程序。

$ErrorActionPreference = 'Stop'
$BridgeDir = Join-Path $PSScriptRoot '..\..\安卓app\server'
$BridgePort = 3000
$StateFile = Join-Path $env:TEMP 'wpt_bridge_state.json'
$BridgeOutLog = Join-Path $env:TEMP 'wpt_bridge_stdout.log'
$BridgeErrLog = Join-Path $env:TEMP 'wpt_bridge_stderr.log'
$NgrokOutLog = Join-Path $env:TEMP 'wpt_ngrok_stdout.log'
$NgrokErrLog = Join-Path $env:TEMP 'wpt_ngrok_stderr.log'

function Stop-OwnedProcess([int]$ProcessId, [string]$ExpectedName) {
    if ($ProcessId -le 0) { return }
    $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($process -and $process.ProcessName -eq $ExpectedName) {
        Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
    }
}

if (-not (Test-Path (Join-Path $BridgeDir 'bridge.mjs'))) {
    throw "桥接服务目录无效: $BridgeDir"
}
if (-not (Test-Path (Join-Path $BridgeDir 'node_modules'))) {
    throw "依赖尚未安装，请先在 $BridgeDir 执行 npm install"
}

$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
$ngrokCommand = Get-Command ngrok -ErrorAction SilentlyContinue
if (-not $nodeCommand) { throw '未找到 Node.js' }
if (-not $ngrokCommand) { throw '未找到 ngrok' }

$listening = netstat -ano 2>$null | Select-String ":$BridgePort\s+.*LISTENING"
if ($listening) {
    throw "端口 $BridgePort 已被占用。为避免误杀其他程序，脚本已停止。"
}

if (-not $env:WPT_BRIDGE_API_KEY) {
    $random = New-Object byte[] 24
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $generator.GetBytes($random) } finally { $generator.Dispose() }
    $env:WPT_BRIDGE_API_KEY = [Convert]::ToBase64String($random).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

$bridgeProcess = $null
$ngrokProcess = $null
try {
    $bridgeProcess = Start-Process -FilePath $nodeCommand.Source -ArgumentList 'bridge.mjs' `
        -WorkingDirectory $BridgeDir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $BridgeOutLog -RedirectStandardError $BridgeErrLog

    $healthy = $false
    for ($retry = 0; $retry -lt 20; $retry++) {
        Start-Sleep -Milliseconds 250
        if ($bridgeProcess.HasExited) { break }
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$BridgePort/health" -TimeoutSec 2
            $healthy = $true
            break
        } catch { }
    }
    if (-not $healthy) {
        $detail = Get-Content -Raw $BridgeErrLog -ErrorAction SilentlyContinue
        throw "桥接服务启动失败: $detail"
    }

    $ngrokProcess = Start-Process -FilePath $ngrokCommand.Source `
        -ArgumentList 'http', "$BridgePort", '--log=stdout' -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $NgrokOutLog -RedirectStandardError $NgrokErrLog

    $publicUrl = $null
    for ($retry = 0; $retry -lt 24; $retry++) {
        Start-Sleep -Milliseconds 250
        if ($ngrokProcess.HasExited) { break }
        try {
            $tunnels = Invoke-RestMethod -Uri 'http://127.0.0.1:4040/api/tunnels' -TimeoutSec 2
            $publicUrl = $tunnels.tunnels[0].public_url
            if ($publicUrl) { break }
        } catch { }
    }
    if (-not $publicUrl) { throw 'ngrok 已启动，但未能取得公网地址' }

    @{
        BridgePid = $bridgeProcess.Id
        NgrokPid = $ngrokProcess.Id
        StartedAt = (Get-Date).ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $StateFile -Encoding UTF8

    Write-Host '桥接服务已启动' -ForegroundColor Green
    Write-Host "公网地址: $publicUrl"
    Write-Host "控制密钥: $env:WPT_BRIDGE_API_KEY"
    Write-Host '调用 /cmd 时请在 X-WPT-Key 请求头中携带该密钥。'
    Write-Host '按 Ctrl+C 停止本次启动的服务。'

    while (-not $bridgeProcess.HasExited -and -not $ngrokProcess.HasExited) {
        Start-Sleep -Seconds 2
    }
} finally {
    if ($ngrokProcess) { Stop-OwnedProcess $ngrokProcess.Id 'ngrok' }
    if ($bridgeProcess) { Stop-OwnedProcess $bridgeProcess.Id 'node' }
    Remove-Item -LiteralPath $StateFile -Force -ErrorAction SilentlyContinue
}
