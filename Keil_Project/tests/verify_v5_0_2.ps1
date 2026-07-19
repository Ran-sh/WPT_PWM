param(
    [ValidateSet('All', 'Baseline', 'Checksum', 'Pwm', 'Safety', 'Adc',
                 'Spi', 'Storage', 'Keys', 'Ui', 'Network', 'Scheduler',
                 'Version', 'Build')]
    [string]$Scope = 'All'
)

$ErrorActionPreference = 'Stop'
$script:FailureCount = 0
$script:PassCount = 0
$keilRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $keilRoot

function Test-CategoryEnabled {
    param([string]$Category)

    return ($Scope -eq 'All' -or $Scope -eq $Category -or $Category -eq 'Baseline')
}

function Write-Check {
    param(
        [string]$Category,
        [string]$Name,
        [bool]$Passed,
        [string]$Detail = ''
    )

    if (-not (Test-CategoryEnabled $Category)) {
        return
    }

    if ($Passed) {
        $script:PassCount++
        Write-Host ("PASS [{0}] {1}{2}" -f $Category, $Name,
            $(if ($Detail) { ": $Detail" } else { '' })) -ForegroundColor Green
    }
    else {
        $script:FailureCount++
        Write-Host ("FAIL [{0}] {1}{2}" -f $Category, $Name,
            $(if ($Detail) { ": $Detail" } else { '' })) -ForegroundColor Red
    }
}

function Read-ProjectText {
    param([string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        return ''
    }
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
}

function Test-Contains {
    param(
        [string]$Text,
        [string]$Pattern
    )

    return [System.Text.RegularExpressions.Regex]::IsMatch(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
}

Write-Host ("V5.0.2 STM32 verification (scope={0})" -f $Scope) -ForegroundColor Cyan

$expectedSources = @(
    'Keil_Project\Hardware\Adc_Driver.c',
    'Keil_Project\Hardware\Adc_Driver.h',
    'Keil_Project\Hardware\Buzzer_Driver.c',
    'Keil_Project\Hardware\Buzzer_Driver.h',
    'Keil_Project\Hardware\Esp8266_Driver.c',
    'Keil_Project\Hardware\Esp8266_Driver.h',
    'Keil_Project\Hardware\Inverter_Control.c',
    'Keil_Project\Hardware\Inverter_Control.h',
    'Keil_Project\Hardware\Key_Driver.c',
    'Keil_Project\Hardware\Key_Driver.h',
    'Keil_Project\Hardware\Led_Driver.c',
    'Keil_Project\Hardware\Led_Driver.h',
    'Keil_Project\Hardware\Pwm_Driver.c',
    'Keil_Project\Hardware\Pwm_Driver.h',
    'Keil_Project\Hardware\TFT_Font_Data.h',
    'Keil_Project\Hardware\Tft_Driver.c',
    'Keil_Project\Hardware\Tft_Driver.h',
    'Keil_Project\Hardware\Ui_Controller.c',
    'Keil_Project\Hardware\Ui_Controller.h',
    'Keil_Project\Hardware\W25Q_Driver.c',
    'Keil_Project\Hardware\W25Q_Driver.h',
    'Keil_Project\System\Sys_Timer.c',
    'Keil_Project\System\Sys_Timer.h',
    'Keil_Project\User\App_Network.c',
    'Keil_Project\User\App_Network.h',
    'Keil_Project\User\App_Storage.c',
    'Keil_Project\User\App_Storage.h',
    'Keil_Project\User\main.c',
    'Keil_Project\User\stm32f10x_conf.h',
    'Keil_Project\User\stm32f10x_it.c',
    'Keil_Project\User\stm32f10x_it.h',
    'Keil_Project\User\Sys_Core.c',
    'Keil_Project\User\Sys_Core.h'
)

$missingSources = @($expectedSources | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $repoRoot $_))
})
Write-Check 'Baseline' '33 baseline STM32 source files exist' ($missingSources.Count -eq 0) `
    $(if ($missingSources.Count) { $missingSources -join ', ' } else { '33/33' })

$buildLogPath = Join-Path $keilRoot 'Objects\Project.build_log.htm'
$mapPath = Join-Path $keilRoot 'Listings\Project.map'
$buildLog = ''
$mapText = ''
$romBytes = $null
$ramBytes = $null

if (Test-Path -LiteralPath $buildLogPath) {
    $buildLog = [System.IO.File]::ReadAllText($buildLogPath)
    $sizeMatch = [regex]::Match(
        $buildLog,
        'Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)'
    )
    if ($sizeMatch.Success) {
        $romBytes = [int]$sizeMatch.Groups[1].Value +
                    [int]$sizeMatch.Groups[2].Value +
                    [int]$sizeMatch.Groups[3].Value
        $ramBytes = [int]$sizeMatch.Groups[3].Value +
                    [int]$sizeMatch.Groups[4].Value
    }
}

Write-Check 'Baseline' 'Keil build log exists' (Test-Path -LiteralPath $buildLogPath) $buildLogPath
if ($buildLog) {
    $cleanBuild = $buildLog -match '0 Error\(s\), 0 Warning\(s\)'
    Write-Check 'Build' 'latest Keil build has zero errors and warnings' $cleanBuild
}
if ($null -ne $romBytes -and $null -ne $ramBytes) {
    Write-Check 'Build' 'ROM is within 64KB' ($romBytes -le 65536) ("{0} bytes" -f $romBytes)
    Write-Check 'Build' 'RAM is within 20KB' ($ramBytes -le 20480) ("{0} bytes" -f $ramBytes)
    Write-Host ("Baseline ROM = {0} bytes" -f $romBytes)
    Write-Host ("Baseline RAM = {0} bytes" -f $ramBytes)
    if ($buildLog -match '0 Error\(s\), 0 Warning\(s\)') {
        Write-Host 'Baseline build = 0 errors, 0 warnings'
    }
}
else {
    Write-Check 'Build' 'Keil resource usage is parseable' $false
}

if (Test-Path -LiteralPath $mapPath) {
    $mapText = [System.IO.File]::ReadAllText($mapPath)
}
Write-Check 'Build' 'Keil map file exists' (Test-Path -LiteralPath $mapPath) $mapPath
if ($mapText) {
    $roMatch = [regex]::Match($mapText, 'Total RO\s+Size[^\r\n]*?\s(\d+)\s+\(')
    $rwMatch = [regex]::Match($mapText, 'Total RW\s+Size[^\r\n]*?\s(\d+)\s+\(')
    Write-Check 'Build' 'map RO and RAM totals are parseable' ($roMatch.Success -and $rwMatch.Success) `
        $(if ($roMatch.Success -and $rwMatch.Success) {
            "RO={0}, RAM={1}" -f $roMatch.Groups[1].Value, $rwMatch.Groups[1].Value
        } else { '' })
}

$gitCandidates = @(
    'E:\Program Files\Git\cmd\git.exe',
    'git.exe',
    'git'
)
$gitExe = $null
foreach ($candidate in $gitCandidates) {
    if ($candidate -match '^[A-Za-z]:\\') {
        if (Test-Path -LiteralPath $candidate) {
            $gitExe = $candidate
            break
        }
    }
    elseif (Get-Command $candidate -ErrorAction SilentlyContinue) {
        $gitExe = $candidate
        break
    }
}

if ($null -ne $gitExe) {
    $tracked = @(& $gitExe -C $repoRoot ls-files 2>$null)
    $artifactPattern = '\.(obj|lst|axf|hex|map|crf|d|o|lnp|sct|dep)$|build_log\.htm$'
    $trackedArtifacts = @($tracked | Where-Object { $_ -match $artifactPattern })
    Write-Check 'Baseline' 'Git does not track Keil build artifacts' ($trackedArtifacts.Count -eq 0) `
        $(if ($trackedArtifacts.Count) { $trackedArtifacts -join ', ' } else { 'clean' })
}
else {
    Write-Check 'Baseline' 'Git is available for artifact check' $false
}

$appStorageC = Read-ProjectText 'Keil_Project\User\App_Storage.c'
$appStorageH = Read-ProjectText 'Keil_Project\User\App_Storage.h'
$tftC = Read-ProjectText 'Keil_Project\Hardware\Tft_Driver.c'
$w25C = Read-ProjectText 'Keil_Project\Hardware\W25Q_Driver.c'
$pwmC = Read-ProjectText 'Keil_Project\Hardware\Pwm_Driver.c'
$adcC = Read-ProjectText 'Keil_Project\Hardware\Adc_Driver.c'
$keyH = Read-ProjectText 'Keil_Project\Hardware\Key_Driver.h'
$sysCoreC = Read-ProjectText 'Keil_Project\User\Sys_Core.c'
$uiC = Read-ProjectText 'Keil_Project\Hardware\Ui_Controller.c'
$uiH = Read-ProjectText 'Keil_Project\Hardware\Ui_Controller.h'
$networkC = Read-ProjectText 'Keil_Project\User\App_Network.c'
$mainC = Read-ProjectText 'Keil_Project\User\main.c'
$projectText = Read-ProjectText 'Keil_Project\Project.uvprojx'

Write-Check 'Build' 'Keil does not rewrite environment-specific batch files' `
    ($projectText -match '<CreateBatchFile>0</CreateBatchFile>')

$checksumCPath = Join-Path $keilRoot 'System\Checksum.c'
$checksumHPath = Join-Path $keilRoot 'System\Checksum.h'
$checksumC = Read-ProjectText 'Keil_Project\System\Checksum.c'
Write-Check 'Checksum' 'Checksum.c and Checksum.h exist' `
    ((Test-Path -LiteralPath $checksumCPath) -and (Test-Path -LiteralPath $checksumHPath))
Write-Check 'Checksum' 'Keil project includes Checksum files' `
    (($projectText -match '<FileName>Checksum\.c</FileName>') -and
     ($projectText -match '<FileName>Checksum\.h</FileName>'))
Write-Check 'Checksum' 'App_Storage no longer exports CRC32_Compute' `
    (-not (Test-Contains $appStorageH '\bCRC32_Compute\s*\('))
Write-Check 'Checksum' 'App_Storage uses Checksum CRC APIs' `
    ((Test-Contains $appStorageC '\bChecksum_CRC8\s*\(') -and
     (Test-Contains $appStorageC '\bChecksum_CRC32\s*\('))
Write-Check 'Checksum' 'legacy CRC function names are removed from production code' `
    (($appStorageC -notmatch '\bCRC8_Compute\s*\(') -and
     ($appStorageC -notmatch '\bCRC32_Compute\s*\(') -and
     ($tftC -notmatch '\bCRC32_Compute\s*\(') -and
     ($w25C -notmatch '\bCRC32_Compute\s*\('))
Write-Check 'Checksum' 'TFT and W25Q use shared CRC32 implementation' `
    (($tftC -match '\bChecksum_CRC32\s*\(') -and
     ($w25C -match '\bChecksum_CRC32\s*\('))
Write-Check 'Checksum' 'fixed CRC self-test vectors exist' `
    (($checksumC -match '0xF4U') -and ($checksumC -match '0xFC891918UL'))
Write-Check 'Checksum' 'storage initialization executes CRC self-test' `
    (Test-Contains $appStorageC '\bChecksum_Self_Test\s*\(')

$oldPwmOrder = '(?s)CR1\s*\|=\s*TIM_CR1_UDIS.*?EGR\s*=\s*TIM_EGR_UG.*?CR1\s*&=\s*~TIM_CR1_UDIS'
Write-Check 'Pwm' 'TIM1 update does not use old UDIS-UG order' (-not (Test-Contains $pwmC $oldPwmOrder))
Write-Check 'Pwm' 'PWM exposes actual enable-state query' `
    (Test-Contains (Read-ProjectText 'Keil_Project\Hardware\Pwm_Driver.h') '\bPwm_Driver_Is_Enabled\s*\(')

Write-Check 'Safety' 'Sys_Core exposes unified start request' `
    (Test-Contains (Read-ProjectText 'Keil_Project\User\Sys_Core.h') '\bSys_Core_Request_Start\s*\(')
Write-Check 'Safety' 'overcurrent protection covers SWEEP and RUNNING' `
    ((Test-Contains $sysCoreC 'SYS_STATE_SWEEP') -and
     (Test-Contains $sysCoreC 'SYS_STATE_RUNNING') -and
     (Test-Contains $sysCoreC 'SYS_FAULT_OVERCURRENT'))

Write-Check 'Adc' 'ADC uses TIM3 TRGO' ($adcC -match 'ADC_ExternalTrigConv_T3_TRGO')
Write-Check 'Adc' 'ADC continuous conversion is disabled' ($adcC -match 'ADC_ContinuousConvMode\s*=\s*DISABLE')
Write-Check 'Adc' 'ADC no longer drops samples by DWT period' ($adcC -notmatch '144241|DWT->CYCCNT')
Write-Check 'Adc' 'ADC exposes data-freshness query' `
    (Test-Contains (Read-ProjectText 'Keil_Project\Hardware\Adc_Driver.h') '\bAdc_Driver_Is_Data_Fresh\s*\(')

$spiCPath = Join-Path $keilRoot 'Hardware\Spi1_Shared.c'
$spiHPath = Join-Path $keilRoot 'Hardware\Spi1_Shared.h'
Write-Check 'Spi' 'SPI1 shared module exists' `
    ((Test-Path -LiteralPath $spiCPath) -and (Test-Path -LiteralPath $spiHPath))
Write-Check 'Spi' 'Keil project includes SPI1 shared module' `
    (($projectText -match '<FileName>Spi1_Shared\.c</FileName>') -and
     ($projectText -match '<FileName>Spi1_Shared\.h</FileName>'))
Write-Check 'Spi' 'W25Q driver has no application dependency' `
    (($w25C -notmatch '#include\s+"Sys_Core\.h"') -and
     ($w25C -notmatch '#include\s+"App_Storage\.h"'))
Write-Check 'Spi' 'W25Q defines explicit result values' `
    (Test-Contains (Read-ProjectText 'Keil_Project\Hardware\W25Q_Driver.h') 'W25Q_DRIVER_RESULT_OK')

Write-Check 'Storage' 'blackbox log record is fixed at 12 bytes' `
    (($appStorageH -match '#define\s+BLACKBOX_ENTRY_SIZE\s+12U') -and
     ($appStorageH -match 'sizeof\s*\(\s*App_Storage_Log_Entry\s*\)\s*==\s*12U'))
Write-Check 'Storage' 'blackbox V2 partition addresses are complete' `
    (($appStorageH -match '0x310000') -and ($appStorageH -match '0x311000') -and
     ($appStorageH -match '0x312000') -and ($appStorageH -match '0x6D0000') -and
     ($appStorageH -match '0x710000'))
Write-Check 'Storage' 'config save uses background request API' `
    (Test-Contains $appStorageH '\bApp_Storage_Request_Save_Config\s*\(')

Write-Check 'Keys' 'key capabilities split DOUBLE and LONG' `
    (($keyH -match 'KEY_DRIVER_CFG_DOUBLE_ENABLE') -and
     ($keyH -match 'KEY_DRIVER_CFG_LONG_ENABLE') -and
     ($keyH -notmatch 'KEY_DRIVER_CFG_CLICK_ONLY|KEY_DRIVER_CFG_WITH_DOUBLE'))
Write-Check 'Keys' 'KEY0/2/3 click only, KEY1 double, KEY4 long' `
    ((Test-Contains $sysCoreC 'KEY_DRIVER_ID_POWER\s*,\s*0U') -and
     (Test-Contains $sysCoreC 'KEY_DRIVER_ID_BACK\s*,\s*KEY_DRIVER_CFG_DOUBLE_ENABLE') -and
     (Test-Contains $sysCoreC 'KEY_DRIVER_ID_UP\s*,\s*0U') -and
     (Test-Contains $sysCoreC 'KEY_DRIVER_ID_DOWN\s*,\s*0U') -and
     (Test-Contains $sysCoreC 'KEY_DRIVER_ID_CONFIRM\s*,\s*KEY_DRIVER_CFG_LONG_ENABLE'))

$uiCombined = $uiC + "`n" + $uiH
Write-Check 'Ui' 'GPIO backlight setting pages are removed' ($uiCombined -notmatch 'UI_PAGE_SETTING_BL')
Write-Check 'Ui' 'UI page count is 14' `
    (($uiH -match '14\s*pages') -or ($uiH -match 'UI_PAGE_COUNT\s*=\s*14'))

$directStart = ($uiC -match 'Inverter_Control_Soft_Start_Trigger\s*\(') -or
               ($networkC -match 'Inverter_Control_Soft_Start_Trigger\s*\(')
Write-Check 'Safety' 'UI and network do not start inverter directly' (-not $directStart)

$directStateFiles = @()
$scanFiles = Get-ChildItem (Join-Path $keilRoot 'Hardware'),
                           (Join-Path $keilRoot 'System'),
                           (Join-Path $keilRoot 'User') -Recurse -File |
    Where-Object { $_.Extension -eq '.c' -and $_.Name -ne 'Sys_Core.c' }
foreach ($file in $scanFiles) {
    $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
    if ($text -match '\bg_sys_state\s*=') {
        $directStateFiles += $file.FullName.Substring($repoRoot.Length + 1)
    }
}
Write-Check 'Safety' 'only Sys_Core writes system state' ($directStateFiles.Count -eq 0) `
    $(if ($directStateFiles.Count) { $directStateFiles -join ', ' } else { 'clean' })

Write-Check 'Network' 'telemetry uses explicit state mapping' `
    (Test-Contains $networkC '\bApp_Network_Map_Telemetry_State\s*\(')
Write-Check 'Network' 'telemetry mapping covers S=0/1/2/3' `
    (($networkC -match 'SYS_STATE_IDLE') -and ($networkC -match 'SYS_STATE_SWEEP') -and
     ($networkC -match 'SYS_STATE_RUNNING') -and ($networkC -match 'SYS_STATE_FAULT') -and
     ($networkC -match 'return\s+0U') -and ($networkC -match 'return\s+1U') -and
     ($networkC -match 'return\s+2U') -and ($networkC -match 'return\s+3U'))

Write-Check 'Scheduler' 'main dispatches through Sys_Core getter' `
    (($mainC -match 'Sys_Core_Get_State\s*\(') -and ($mainC -notmatch '\bg_sys_state\b'))
Write-Check 'Scheduler' 'common scheduler contains ADC-to-WFI chain' `
    (($sysCoreC -match 'Sys_Core_Run_Common') -and ($sysCoreC -match '__WFI\s*\('))

if (Test-CategoryEnabled 'Version') {
    $wrongVersionFiles = @()
    $allStm32Sources = Get-ChildItem (Join-Path $keilRoot 'Hardware'),
                                       (Join-Path $keilRoot 'System'),
                                       (Join-Path $keilRoot 'User') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }
    foreach ($sourceFile in $allStm32Sources) {
        $fullPath = $sourceFile.FullName
        $head = ([System.IO.File]::ReadAllLines($fullPath, [System.Text.Encoding]::UTF8) |
                 Select-Object -First 12) -join "`n"
        if ($head -notmatch 'V5\.0\.2') {
            $wrongVersionFiles += $fullPath.Substring($repoRoot.Length + 1)
        }
    }
    Write-Check 'Version' 'all STM32 headers use V5.0.2' ($wrongVersionFiles.Count -eq 0) `
        $(if ($wrongVersionFiles.Count) { "{0} files pending" -f $wrongVersionFiles.Count } else {
            "{0}/{0}" -f $allStm32Sources.Count
        })
    Write-Check 'Version' 'Splash displays V5.0.2' ($tftC -match '"V5\.0\.2"')
}

Write-Host ("Summary: {0} PASS, {1} FAIL" -f $script:PassCount, $script:FailureCount) -ForegroundColor Cyan
if ($script:FailureCount -ne 0) {
    exit 1
}
exit 0
