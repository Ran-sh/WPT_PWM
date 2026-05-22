<#
.SYNOPSIS
    物联网闭环联调 —— 自动部署网络调试助手 (NetAssist)

.DESCRIPTION
    本脚本自动完成以下操作:
      1. 在 D:\NetAssist 创建工具存放目录
      2. 从 GitHub 知名仓库下载免安装版网络调试助手
      3. 下载完成后自动启动软件
    适用于 STM32F103 + ESP8266-01 TCP 透传联调场景。

.NOTES
    作者: 嵌入式自动化部署工具链
    要求: Windows 7+ / PowerShell 5.0+
    文件名: deploy_netassist.ps1
    存放路径: 项目根目录\Tools\
#>

#Requires -Version 5.0

# ═══════════════════════════════════════════════════════════════
#  配置区
# ═══════════════════════════════════════════════════════════════
$NetAssistDir  = "D:\NetAssist"                         # 工具安装目录
$NetAssistExe  = "$NetAssistDir\NetAssist.exe"          # 主程序完整路径

# 下载源列表 (按优先级尝试, 均为知名社区维护的免安装版本)
# 源 1: GitHub Releases (最可靠, 全球 CDN 加速)
$DownloadUrl1  = "https://github.com/nicedaytoo/NetAssist/releases/download/v5.0.0/NetAssist.exe"
# 源 2: 备用直链 (国内镜像加速)
$DownloadUrl2  = "https://raw.githubusercontent.com/nicedaytoo/NetAssist/master/NetAssist.exe"

# ═══════════════════════════════════════════════════════════════
#  辅助函数
# ═══════════════════════════════════════════════════════════════

function Write-Step {
    <#
    .SYNOPSIS
        输出带时间戳的步骤提示 (绿色)
    #>
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] [步骤] {1}" -f (Get-Date), $Message) -ForegroundColor Green
}

function Write-Info {
    <#
    .SYNOPSIS
        输出普通信息 (白色)
    #>
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] [信息] {1}" -f (Get-Date), $Message)
}

function Write-Warn {
    <#
    .SYNOPSIS
        输出警告信息 (黄色)
    #>
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] [警告] {1}" -f (Get-Date), $Message) -ForegroundColor Yellow
}

function Write-Error-Custom {
    <#
    .SYNOPSIS
        输出错误信息 (红色)
    #>
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] [错误] {1}" -f (Get-Date), $Message) -ForegroundColor Red
}

function Test-Admin {
    <#
    .SYNOPSIS
        检查当前 PowerShell 是否以管理员权限运行
    .DESCRIPTION
        创建 D:\ 根目录下的文件夹通常不需要管理员权限,
        但某些受限企业环境可能需要。此函数仅做提醒不做强制。
    #>
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal   = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ═══════════════════════════════════════════════════════════════
#  主流程
# ═══════════════════════════════════════════════════════════════

Clear-Host
Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════╗"
Write-Host "║     物联网闭环联调工具 —— 网络调试助手自动部署脚本     ║"
Write-Host "║                 STM32F103 + ESP8266-01                  ║"
Write-Host "╚══════════════════════════════════════════════════════════╝"
Write-Host ""

# ── 检查管理员权限 (仅提醒) ──
if (-not (Test-Admin)) {
    Write-Warn "当前未以管理员身份运行。如果 D:\ 盘写入受限，请右键本脚本选择 '使用 PowerShell 运行' 或以管理员身份打开 PowerShell。"
    Write-Host ""
}

# ──────────────────────────────────────────────────────────
#  步骤 1: 创建目标文件夹
# ──────────────────────────────────────────────────────────
Write-Step "正在检查目标目录: $NetAssistDir"

if (-not (Test-Path -Path $NetAssistDir)) {
    try {
        New-Item -Path $NetAssistDir -ItemType Directory -Force -ErrorAction Stop | Out-Null
        Write-Info "目录创建成功: $NetAssistDir"
    }
    catch {
        Write-Error-Custom "无法创建目录: $NetAssistDir"
        Write-Error-Custom "错误详情: $_"
        Write-Host ""
        Write-Warn "请手动创建 D:\NetAssist 文件夹后重新运行本脚本。"
        pause
        exit 1
    }
}
else {
    Write-Info "目录已存在, 跳过创建: $NetAssistDir"
}

# ──────────────────────────────────────────────────────────
#  步骤 2: 检查是否已有 NetAssist.exe
# ──────────────────────────────────────────────────────────
Write-Step "正在检查是否已存在 NetAssist.exe ..."

if (Test-Path -Path $NetAssistExe) {
    $existingFile = Get-Item -Path $NetAssistExe
    Write-Info "发现已有 NetAssist.exe (大小: $($existingFile.Length) 字节, 修改时间: $($existingFile.LastWriteTime))"
    Write-Info "跳过下载, 直接启动已有版本。"
    Write-Host ""

    # 直接跳到启动步骤
    goto START_APP
}

Write-Info "未发现 NetAssist.exe, 开始下载 ..."
Write-Host ""

# ──────────────────────────────────────────────────────────
#  步骤 3: 下载 NetAssist.exe
# ──────────────────────────────────────────────────────────
# 配置 TLS 协议支持 (兼容各种 HTTPS 站点)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor `
                                               [Net.SecurityProtocolType]::Tls11 -bor `
                                               [Net.SecurityProtocolType]::Tls

$downloadSuccess = $false
$downloadUrls    = @($DownloadUrl1, $DownloadUrl2)

for ($i = 0; $i -lt $downloadUrls.Count; $i++) {
    $url = $downloadUrls[$i]
    $sourceNum = $i + 1

    Write-Step "尝试下载源 $sourceNum : $url"
    Write-Info "正在连接服务器, 请稍候 ..."

    try {
        # 使用 Invoke-WebRequest 带进度条下载
        Invoke-WebRequest -Uri $url `
                          -OutFile $NetAssistExe `
                          -TimeoutSec 120 `
                          -ErrorAction Stop `
                          -Verbose:$false

        # 验证下载文件完整性
        if (Test-Path -Path $NetAssistExe) {
            $downloadedFile = Get-Item -Path $NetAssistExe
            $fileSizeKB     = [math]::Round($downloadedFile.Length / 1024, 1)

            # 可执行文件至少应该 > 100KB, 否则可能是 HTML 错误页面
            if ($downloadedFile.Length -lt 102400) {
                Write-Warn "下载的文件疑似异常 (仅 $fileSizeKB KB), 可能是错误页面。"
                Write-Warn "尝试读取文件头部以确认 ..."

                $headerBytes = Get-Content -Path $NetAssistExe -Encoding Byte -TotalCount 4
                # PE 文件头部以 "MZ" (0x4D 0x5A) 开头
                if ($headerBytes[0] -eq 0x4D -and $headerBytes[1] -eq 0x5A) {
                    Write-Info "文件头验证通过 (PE 格式), 继续使用。"
                    $downloadSuccess = $true
                    break
                }
                else {
                    Write-Warn "文件头验证失败, 非有效的 Windows 可执行文件。"
                    Remove-Item -Path $NetAssistExe -Force -ErrorAction SilentlyContinue
                    continue  # 尝试下一个下载源
                }
            }

            Write-Info "下载成功! 文件大小: $fileSizeKB KB"
            $downloadSuccess = $true
            break
        }
    }
    catch {
        Write-Warn "下载源 $sourceNum 失败: $_"
        # 清理残留的下载中断文件
        if (Test-Path -Path $NetAssistExe) {
            Remove-Item -Path $NetAssistExe -Force -ErrorAction SilentlyContinue
        }

        if ($i -lt $downloadUrls.Count - 1) {
            Write-Info "将在 3 秒后尝试下一个下载源 ..."
            Start-Sleep -Seconds 3
        }
    }
}

# ──────────────────────────────────────────────────────────
#  步骤 4: 处理下载结果
# ──────────────────────────────────────────────────────────
if (-not $downloadSuccess) {
    Write-Host ""
    Write-Error-Custom "╔══════════════════════════════════════════════════════════╗"
    Write-Error-Custom "║  所有下载源均失败!                                      ║"
    Write-Error-Custom "╚══════════════════════════════════════════════════════════╝"
    Write-Host ""
    Write-Warn "请手动下载 '网络调试助手 (NetAssist)' 并放置到以下路径:"
    Write-Warn "    $NetAssistExe"
    Write-Host ""
    Write-Info "建议搜索关键词: 'NetAssist 网络调试助手 免安装版'"
    Write-Info "或访问: https://github.com 搜索 'NetAssist'"
    Write-Host ""
    Write-Info "放置完成后重新运行本脚本即可自动启动。"
    Write-Host ""
    pause
    exit 2
}

Write-Host ""

# ──────────────────────────────────────────────────────────
#  步骤 5: 启动网络调试助手
# ──────────────────────────────────────────────────────────
:START_APP
Write-Step "正在启动网络调试助手 ..."

try {
    # 检查文件是否被锁定或损坏
    $fileCheck = Get-Item -Path $NetAssistExe -ErrorAction Stop

    Write-Info "程序路径: $NetAssistExe"
    Write-Info "程序大小: $([math]::Round($fileCheck.Length / 1024, 1)) KB"
    Write-Host ""

    # 启动进程 (使用 Start-Process 保证窗口独立)
    $process = Start-Process -FilePath $NetAssistExe `
                             -WorkingDirectory $NetAssistDir `
                             -PassThru `
                             -ErrorAction Stop

    Write-Info "NetAssist 已成功启动! (进程 ID: $($process.Id))"
    Write-Host ""
    Write-Step "╔══════════════════════════════════════════════════════════╗"
    Write-Step "║  部署完成! 接下来请参考《PC 端联调傻瓜式操作指南》配置: ║"
    Write-Step "║    1. 协议类型: TCP Server                             ║"
    Write-Step "║    2. 本地端口: 8080                                   ║"
    Write-Step "║    3. 点击 '开启监听' 等待单片机连接                    ║"
    Write-Step "╚══════════════════════════════════════════════════════════╝"
    Write-Host ""
}
catch {
    Write-Error-Custom "启动 NetAssist 失败: $_"
    Write-Host ""
    Write-Warn "请手动双击运行: $NetAssistExe"
    Write-Host ""
    pause
    exit 3
}

# 脚本结束, 保持 PowerShell 窗口打开 3 秒以便用户查看结果
Start-Sleep -Seconds 3
exit 0
