param(
    [ValidateSet('All', 'Baseline', 'Checksum', 'Pwm', 'Safety', 'Adc',
                 'AdcTrigger', 'AdcFilter', 'AdcCal',
                 'Control', 'ControlCallers', 'Spi', 'SpiShared', 'W25',
                 'Storage', 'StorageConfig', 'Blackbox', 'BlackboxJournal',
                  'BlackboxLifecycle', 'BlackboxSnapshot',
                  'Keys', 'KeyRouting', 'Ui', 'UiPerf', 'UartTx', 'Network', 'Scheduler', 'Timeout',
                 'CodeQuality',
                 'Version', 'Build', 'SettingsGauge', 'Documentation')]
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

Write-Host ("V5.1.0 STM32 integration verification (scope={0})" -f $Scope) -ForegroundColor Cyan

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
$tftH = Read-ProjectText 'Keil_Project\Hardware\Tft_Driver.h'
$w25C = Read-ProjectText 'Keil_Project\Hardware\W25Q_Driver.c'
$w25H = Read-ProjectText 'Keil_Project\Hardware\W25Q_Driver.h'
$pwmC = Read-ProjectText 'Keil_Project\Hardware\Pwm_Driver.c'
$pwmH = Read-ProjectText 'Keil_Project\Hardware\Pwm_Driver.h'
$adcC = Read-ProjectText 'Keil_Project\Hardware\Adc_Driver.c'
$adcH = Read-ProjectText 'Keil_Project\Hardware\Adc_Driver.h'
$irqC = Read-ProjectText 'Keil_Project\User\stm32f10x_it.c'
$espC = Read-ProjectText 'Keil_Project\Hardware\Esp8266_Driver.c'
$espH = Read-ProjectText 'Keil_Project\Hardware\Esp8266_Driver.h'
$keyC = Read-ProjectText 'Keil_Project\Hardware\Key_Driver.c'
$keyH = Read-ProjectText 'Keil_Project\Hardware\Key_Driver.h'
$sysCoreC = Read-ProjectText 'Keil_Project\User\Sys_Core.c'
$sysCoreH = Read-ProjectText 'Keil_Project\User\Sys_Core.h'
$inverterC = Read-ProjectText 'Keil_Project\Hardware\Inverter_Control.c'
$invC = $inverterC
$invH = Read-ProjectText 'Keil_Project\Hardware\Inverter_Control.h'
$buzzerC = Read-ProjectText 'Keil_Project\Hardware\Buzzer_Driver.c'
$ledC = Read-ProjectText 'Keil_Project\Hardware\Led_Driver.c'
$uiC = Read-ProjectText 'Keil_Project\Hardware\Ui_Controller.c'
$uiH = Read-ProjectText 'Keil_Project\Hardware\Ui_Controller.h'
$networkC = Read-ProjectText 'Keil_Project\User\App_Network.c'
$mainC = Read-ProjectText 'Keil_Project\User\main.c'
$projectText = Read-ProjectText 'Keil_Project\Project.uvprojx'
$timerC = Read-ProjectText 'Keil_Project\System\Sys_Timer.c'
$developerGuideFile = Get-ChildItem (Join-Path $repoRoot 'Claude_Files\docs') `
    -File -Filter 'WPT*.md' | Select-Object -First 1
$developerGuide = if ($null -ne $developerGuideFile) {
    [System.IO.File]::ReadAllText($developerGuideFile.FullName, [System.Text.Encoding]::UTF8)
} else {
    ''
}
$ch341Readme = Read-ProjectText 'ch341\README.md'
$stm32Files = Get-ChildItem (Join-Path $keilRoot 'Hardware'),
                               (Join-Path $keilRoot 'System'),
                               (Join-Path $keilRoot 'User') -File |
    Where-Object { $_.Extension -in '.c', '.h' }
$stm32Text = ($stm32Files | ForEach-Object {
    [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)
}) -join "`n"

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
Write-Check 'Checksum' 'storage and TFT use shared CRC32 implementation' `
    (($appStorageC -match '\bChecksum_CRC32\s*\(') -and
     ($tftC -match '\bChecksum_CRC32\s*\('))
Write-Check 'Checksum' 'fixed CRC self-test vectors exist' `
    (($checksumC -match '0xF4U') -and ($checksumC -match '0xFC891918UL'))
Write-Check 'Checksum' 'storage initialization executes CRC self-test' `
    (Test-Contains $appStorageC '\bChecksum_Self_Test\s*\(')

$oldPwmOrder = '(?s)CR1\s*\|=\s*TIM_CR1_UDIS.*?EGR\s*=\s*TIM_EGR_UG.*?CR1\s*&=\s*~TIM_CR1_UDIS'
Write-Check 'Pwm' 'TIM1 update does not use old UDIS-UG order' (-not (Test-Contains $pwmC $oldPwmOrder))
Write-Check 'Pwm' 'PWM exposes actual enable-state query' `
    (Test-Contains (Read-ProjectText 'Keil_Project\Hardware\Pwm_Driver.h') '\bPwm_Driver_Is_Enabled\s*\(')

Write-Check 'Safety' 'Sys_Core exposes unified start request' `
    (Test-Contains $sysCoreH '\bSys_Core_Request_Start\s*\(')
Write-Check 'Safety' 'overcurrent protection covers SWEEP and RUNNING' `
    ((Test-Contains $sysCoreC 'SYS_STATE_SWEEP') -and
     (Test-Contains $sysCoreC 'SYS_STATE_RUNNING') -and
     (Test-Contains $sysCoreC 'SYS_FAULT_OVERCURRENT'))
Write-Check 'Safety' 'safety task enters fault through unified API' `
    (Test-Contains $sysCoreC 'Sys_Core_Trigger_Fault\s*\(\s*SYS_FAULT_OVERCURRENT\s*\)')
Write-Check 'Safety' 'fault page renders the latched fault reason' `
    (($uiC -match '\bSys_Core_Get_Fault\s*\(') -and
     ($uiC -match 'SYS_FAULT_ADC_STALE') -and
     ($uiC -match 'SYS_FAULT_CONTROL_INVARIANT'))
$buzzerSetMatch = [regex]::Match(
    $buzzerC,
    '(?s)void\s+Buzzer_Driver_Set_State\s*\([^)]*\)\s*\{(.*?)\r?\n\}'
)
Write-Check 'Safety' 'fault beep starts immediately when requested' `
    ($buzzerSetMatch.Success -and
     ($buzzerSetMatch.Groups[1].Value -match 'BUZZER_DRIVER_STATE_BEEP') -and
     ($buzzerSetMatch.Groups[1].Value -match 'GPIO_SetBits'))
Write-Check 'Safety' 'fault reset silences the buzzer' `
    (Test-Contains $sysCoreC '(?s)Sys_Core_Reset_Fault\s*\([^)]*\).*?Buzzer_Driver_Set_State\s*\(\s*BUZZER_DRIVER_STATE_OFF\s*\)')

Write-Check 'Control' 'control result and fault enums are defined' `
    (($sysCoreH -match 'SYS_CONTROL_RESULT_POWER_OFF') -and
     ($sysCoreH -match 'SYS_CONTROL_RESULT_FAULT_LATCHED') -and
     ($sysCoreH -match 'SYS_CONTROL_RESULT_ADC_NOT_READY') -and
     ($sysCoreH -match 'SYS_FAULT_CONTROL_INVARIANT'))
Write-Check 'Control' 'unified start stop reset and fault APIs exist' `
    ((Test-Contains $sysCoreH '\bSys_Core_Request_Start\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Request_Stop\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Reset_Fault\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Trigger_Fault\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Get_State\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Get_Fault\s*\(') -and
     (Test-Contains $sysCoreH '\bSys_Core_Is_Power_Enabled\s*\('))
Write-Check 'Control' 'system checks power and PWM invariants' `
    ((Test-Contains $sysCoreC '\bSys_Core_Check_Control_Invariant\s*\(') -and
     (Test-Contains $sysCoreC '\bPwm_Driver_Is_Enabled\s*\('))
Write-Check 'Control' 'fault path stops PWM before cutting PB10' `
    (Test-Contains $sysCoreC '(?s)Sys_Core_Trigger_Fault\s*\([^)]*\).*?Inverter_Control_Soft_Start_Fault\s*\(\s*\).*?GPIO_ResetBits\s*\(\s*GPIOB\s*,\s*GPIO_Pin_10\s*\)')
$normalStopMatch = [regex]::Match(
    $inverterC,
    '(?s)void\s+Inverter_Control_Soft_Start_Stop\s*\(void\)\s*\{(.*?)\r?\n\}'
)
Write-Check 'Control' 'normal inverter stop cancels frequency ramp' `
    ($normalStopMatch.Success -and
     ($normalStopMatch.Groups[1].Value -match 's_ramp_state\s*=\s*INVERTER_CONTROL_RAMP_IDLE'))

Write-Check 'AdcTrigger' 'ADC uses TIM3 TRGO' ($adcC -match 'ADC_ExternalTrigConv_T3_TRGO')
Write-Check 'AdcTrigger' 'ADC continuous conversion is disabled' ($adcC -match 'ADC_ContinuousConvMode\s*=\s*DISABLE')
Write-Check 'AdcTrigger' 'ADC no longer drops samples by DWT period' ($adcC -notmatch '144241|DWT->CYCCNT')
Write-Check 'AdcTrigger' 'TIM3 runs at 500Hz with update TRGO' `
    (($adcC -match 'TIM_TRGOSource_Update') -and
     ($adcC -match '#define\s+ADC_DRIVER_TIM3_PERIOD\s+1999U') -and
     ($adcC -match 'TIM_Period\s*=\s*ADC_DRIVER_TIM3_PERIOD'))
Write-Check 'AdcTrigger' 'DMA1 Channel1 circular transfer interrupt is handled' `
    (($adcC -match 'DMA_Mode_Circular') -and
     ($adcC -match 'DMA_ITConfig\s*\(\s*DMA1_Channel1\s*,\s*DMA_IT_TC') -and
     ($irqC -match 'DMA1_Channel1_IRQHandler') -and
     ($irqC -match 'Adc_Driver_DMA_Transfer_Complete_ISR'))
Write-Check 'AdcTrigger' 'ADC tracks sample sequence and timestamp internally' `
    (($adcC -match 's_adc_sample_sequence') -and
     ($adcC -match 's_adc_last_sample_tick') -and
     (Test-Contains $adcH '\bAdc_Driver_Get_Processed_Sequence\s*\(') -and
     (Test-Contains $adcH '\bAdc_Driver_Is_Data_Fresh\s*\('))
Write-Check 'AdcFilter' 'ADC defines 64-sample display and 8-sample safety windows' `
    (($adcC -match 'ADC_DRIVER_DISPLAY_WINDOW\s+64U') -and
     ($adcC -match 'ADC_DRIVER_SAFETY_WINDOW\s+8U'))
Write-Check 'AdcFilter' 'ADC exposes separate display and safety values' `
    ((Test-Contains $adcH '\bAdc_Driver_Get_Display_Voltage\s*\(') -and
     (Test-Contains $adcH '\bAdc_Driver_Get_Display_Current\s*\(') -and
     (Test-Contains $adcH '\bAdc_Driver_Get_Safety_Current\s*\(') -and
     (Test-Contains $adcH '\bAdc_Driver_Get_Processed_Sequence\s*\('))
Write-Check 'AdcFilter' 'ADC applies voltage gain and clamps negative current' `
    (($adcC -match '\bs_v_gain\b') -and
     ($adcC -match 's_display_current\s*<\s*0\.0f') -and
     ($adcC -match 's_safety_current\s*<\s*0\.0f'))
Write-Check 'AdcFilter' 'overcurrent requires three new safety samples' `
    (($sysCoreC -match 'SYS_SAFETY_CONFIRM_SAMPLES\s+3U') -and
     ($sysCoreC -match 'Adc_Driver_Get_Processed_Sequence') -and
     ($sysCoreC -match 'Adc_Driver_Get_Safety_Current'))
Write-Check 'AdcCal' 'ADC exposes data-freshness query' `
    (Test-Contains $adcH '\bAdc_Driver_Is_Data_Fresh\s*\(')
Write-Check 'AdcCal' 'ADC calibration state machine defines five states' `
    (($adcH -match 'ADC_DRIVER_CAL_UNINITIALIZED') -and
     ($adcH -match 'ADC_DRIVER_CAL_FILLING') -and
     ($adcH -match 'ADC_DRIVER_CAL_CALIBRATING') -and
     ($adcH -match 'ADC_DRIVER_CAL_READY') -and
     ($adcH -match 'ADC_DRIVER_CAL_ERROR'))
Write-Check 'AdcCal' 'calibration task requires power-off permission and reports completion' `
    ((Test-Contains $adcH '\bAdc_Driver_Calibration_Task\s*\(') -and
     (Test-Contains $adcH '\bAdc_Driver_Take_Calibration_Completed\s*\(') -and
     ($adcC -match 'power_enabled\s*!=\s*0U'))
Write-Check 'AdcCal' 'ADC freshness timeout is 20ms' `
    (($adcC -match 'ADC_DRIVER_STALE_TIMEOUT_MS\s+20U') -and
     ($adcC -match 's_adc_sample_sequence\s*==\s*0U'))
Write-Check 'AdcCal' 'start request requires ready and fresh ADC data' `
    (($sysCoreC -match 'Adc_Driver_Get_Calibration_State') -and
     ($sysCoreC -match 'ADC_DRIVER_CAL_READY') -and
     ($sysCoreC -match 'Adc_Driver_Is_Data_Fresh'))
Write-Check 'AdcCal' 'active ADC staleness triggers ADC_STALE fault' `
    (Test-Contains $sysCoreC 'Sys_Core_Trigger_Fault\s*\(\s*SYS_FAULT_ADC_STALE\s*\)')
Write-Check 'AdcCal' 'calibration completion requests deferred persistence' `
    ((Test-Contains $appStorageH '\bApp_Storage_Request_Save_ADC_Calibration\s*\(') -and
     (Test-Contains $sysCoreC '\bAdc_Driver_Take_Calibration_Completed\s*\(') -and
     (Test-Contains $sysCoreC '\bApp_Storage_Request_Save_ADC_Calibration\s*\(') -and
     (Test-Contains $appStorageC '\bApp_Storage_Request_Save_Config\s*\('))

$spiCPath = Join-Path $keilRoot 'Hardware\Spi1_Shared.c'
$spiHPath = Join-Path $keilRoot 'Hardware\Spi1_Shared.h'
Write-Check 'SpiShared' 'SPI1 shared module exists' `
    ((Test-Path -LiteralPath $spiCPath) -and (Test-Path -LiteralPath $spiHPath))
Write-Check 'SpiShared' 'Keil project includes SPI1 shared module' `
    (($projectText -match '<FileName>Spi1_Shared\.c</FileName>') -and
     ($projectText -match '<FileName>Spi1_Shared\.h</FileName>'))
Write-Check 'W25' 'W25Q driver has no application dependency' `
    (($w25C -notmatch '#include\s+"Sys_Core\.h"') -and
     ($w25C -notmatch '#include\s+"App_Storage\.h"'))
Write-Check 'W25' 'W25Q defines explicit result values' `
    (($w25H -match 'W25Q_DRIVER_RESULT_OK') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_NO_DEVICE') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_INVALID_ARGUMENT') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_OUT_OF_RANGE') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_PAGE_CROSS') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_ERASE_BLOCKED') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_SPI_TIMEOUT') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_BUSY_TIMEOUT') -and
     ($w25H -match 'W25Q_DRIVER_RESULT_VERIFY_FAILED'))
Write-Check 'W25' 'W25Q uses shared SPI and an application-controlled erase gate' `
    (($w25C -match '#include\s+"Spi1_Shared\.h"') -and
     ($w25C -match 'Spi1_Shared_Acquire\s*\(\s*SPI1_SHARED_MODE_FLASH_8') -and
     (Test-Contains $w25H '\bW25Q_Driver_Set_Erase_Allowed\s*\(') -and
     (Test-Contains $sysCoreC '\bW25Q_Driver_Set_Erase_Allowed\s*\('))
Write-Check 'W25' 'W25Q validates chip bounds without address overflow' `
    (($w25C -match 'addr\s*>=\s*W25Q_CHIP_SIZE') -and
     ($w25C -match 'len\s*>\s*\(?W25Q_CHIP_SIZE\s*-\s*addr\)?'))
Write-Check 'W25' 'page writes reject crossing and generic writes split pages' `
    (($w25C -match 'W25Q_DRIVER_RESULT_PAGE_CROSS') -and
     (Test-Contains $w25H '\bW25Q_Driver_Write\s*\(') -and
     ($w25C -match 'W25Q_PAGE_SIZE\s*-\s*page_offset'))
Write-Check 'W25' 'program and erase busy waits have separate limits' `
    (($w25C -match 'W25Q_PROGRAM_TIMEOUT_MS\s+10U') -and
     ($w25C -match 'W25Q_ERASE_TIMEOUT_MS\s+500U'))
Write-Check 'W25' 'font parsing is private to TFT layer' `
    (($w25H -notmatch 'Font_Header|W25Q_Font_Index_Binary_Search|Font_Header_Load|W25Q_Enter_Mode|W25Q_SPI_Transfer') -and
     ($tftC -match 'static\s+uint32_t\s+Tft_Driver_Font_Index_Binary_Search'))
if ((Test-Path -LiteralPath $spiCPath) -and (Test-Path -LiteralPath $spiHPath)) {
    $spiSharedC = Read-ProjectText 'Keil_Project\Hardware\Spi1_Shared.c'
    $spiSharedH = Read-ProjectText 'Keil_Project\Hardware\Spi1_Shared.h'
}
else {
    $spiSharedC = ''
    $spiSharedH = ''
}
Write-Check 'SpiShared' 'shared SPI defines modes and result states' `
    (($spiSharedH -match 'SPI1_SHARED_RESULT_OK') -and
     ($spiSharedH -match 'SPI1_SHARED_RESULT_BUSY') -and
     ($spiSharedH -match 'SPI1_SHARED_RESULT_TIMEOUT') -and
     ($spiSharedH -match 'SPI1_SHARED_MODE_TFT_8') -and
     ($spiSharedH -match 'SPI1_SHARED_MODE_TFT_16') -and
     ($spiSharedH -match 'SPI1_SHARED_MODE_FLASH_8'))
Write-Check 'SpiShared' 'shared SPI owns dual CS and force-release recovery' `
    (($spiSharedC -match 'GPIO_Pin_4') -and ($spiSharedC -match 'GPIO_Pin_12') -and
     ($spiSharedC -match '\bSpi1_Shared_Force_Release\s*\(') -and
     ($spiSharedC -match 'SPI_I2S_DMAReq_Tx'))
Write-Check 'SpiShared' 'shared SPI clears RXNE and OVR before mode changes' `
    (($spiSharedC -match 'SPI_I2S_FLAG_RXNE') -and
     ($spiSharedC -match 'SPI_I2S_FLAG_OVR') -and
     ($spiSharedC -match 'SPI_CR1_DFF'))
Write-Check 'Spi' 'TFT uses shared SPI ownership for polling and DMA' `
    (($tftC -match '#include\s+"Spi1_Shared\.h"') -and
     ($tftC -match 'Spi1_Shared_Acquire\s*\(\s*SPI1_SHARED_MODE_TFT_8') -and
     ($tftC -match 'Spi1_Shared_Acquire\s*\(\s*SPI1_SHARED_MODE_TFT_16') -and
     ($tftC -match 'Spi1_Shared_Force_Release\s*\('))
Write-Check 'Spi' 'TFT no longer edits SPI frame mode directly' `
    ($tftC -notmatch 'SPI_CR1_DFF')
Write-Check 'Spi' 'SysTick starts before hardware initialization' `
    ($mainC -match 'Sys_Timer_Init\s*\(\s*\)\s*;[\s\S]*Sys_Hardware_Init\s*\(\s*\)\s*;')
Write-Check 'Spi' 'TFT reset delays use the millisecond timebase' `
    (($tftC -notmatch '\bTft_Driver_Dly\s*\(') -and
     ($tftC -match 'Sys_Timer_Delay_Ms\s*\(\s*120U?\s*\)'))
Write-Check 'Spi' 'LED startup self-test is an exact 500ms delay' `
    ($ledC -match 'Sys_Timer_Delay_Ms\s*\(\s*500U?\s*\)')

Write-Check 'Blackbox' 'blackbox log record is fixed at 12 bytes' `
    (($appStorageH -match '#define\s+BLACKBOX_ENTRY_SIZE\s+12U') -and
     ($appStorageH -match 'sizeof\s*\(\s*App_Storage_Log_Entry\s*\)\s*==\s*12U'))
Write-Check 'Blackbox' 'blackbox CRC8 covers exactly the first 11 bytes' `
    ($appStorageC -match 'Checksum_CRC8\s*\([^,]+,\s*11U\s*\)')
Write-Check 'Blackbox' 'blackbox V2 partition addresses are complete' `
    (($appStorageH -match '0x310000') -and ($appStorageH -match '0x311000') -and
     ($appStorageH -match '0x312000') -and ($appStorageH -match '0x6D0000') -and
     ($appStorageH -match '0x710000'))
Write-Check 'Blackbox' 'V2 metadata and fault headers are self-describing' `
    (($appStorageH -match 'App_Storage_Blackbox_Metadata') -and
     ($appStorageH -match 'App_Storage_Fault_Header') -and
     ($appStorageH -match 'magic') -and ($appStorageH -match 'version') -and
     ($appStorageH -match 'size') -and ($appStorageH -match 'crc32') -and
     ($appStorageC -match 'memset\s*\([^;]+sizeof\([^;]+\)'))
Write-Check 'Blackbox' 'migration keeps config partitions and rejects V1 pointers' `
    ((($appStorageH -match 'W25Q_ADDR_CFG_A|APP_STORAGE_CFG_A') -or
      (($w25H -match 'W25Q_ADDR_CFG_A\s+0x300000') -and
       ($w25H -match 'W25Q_ADDR_CFG_B\s+0x301000'))) -and
     ($appStorageC -notmatch 'ptr_buf\s*\[\s*3\s*\]'))
Write-Check 'BlackboxJournal' 'metadata scanner walks both sectors and ranks generation' `
    (($appStorageC -match 'App_Storage_Scan_Metadata_Sector') -and
     ($appStorageC -match 'APP_STORAGE_META_A_ADDR') -and
     ($appStorageC -match 'APP_STORAGE_META_B_ADDR') -and
     ($appStorageC -match 'generation\s*>'))
Write-Check 'BlackboxJournal' 'empty metadata is initialized lazily in IDLE task' `
    (($appStorageC -match 's_metadata_checkpoint_pending') -and
     ($appStorageC -match 's_blackbox_v2_ready\s*==\s*0U') -and
     ($appStorageC -match 'W25Q_Driver_Erase_Sector\s*\(\s*target_base\s*\)'))
Write-Check 'BlackboxJournal' 'metadata checkpoint is requested every 60 records' `
    (($appStorageC -match 'APP_STORAGE_METADATA_INTERVAL\s+60U') -and
     ($appStorageC -match 'entry_count\s*-\s*s_metadata_saved_entry_count'))
Write-Check 'BlackboxJournal' 'metadata generation advances only after readback verification' `
    (($appStorageC -match 'App_Storage_Verify_Metadata_Record') -and
     ($appStorageC -match 'if\s*\(\s*result\s*==\s*APP_STORAGE_RESULT_OK\s*\)[\s\S]*s_blackbox_metadata\s*=\s*candidate'))
Write-Check 'BlackboxJournal' 'full active metadata sector switches through the other sector' `
    (($appStorageC -match 'target_base\s*=.*APP_STORAGE_META_[AB]_ADDR') -and
     ($appStorageC -match 'W25Q_Driver_Erase_Sector\s*\(\s*target_base\s*\)[\s\S]*App_Storage_Verify_Metadata_Record'))
Write-Check 'BlackboxLifecycle' 'log writes require a prepared sector and erased 12-byte target' `
    (($appStorageC -match 'App_Storage_Log_Is_Sector_Prepared') -and
     ($appStorageC -match 'App_Storage_Log_Is_Target_Erased') -and
     ($appStorageC -match '0xFFU'))
Write-Check 'BlackboxLifecycle' 'IDLE storage task maintains log sectors' `
    (($appStorageC -match 'App_Storage_Log_Maintenance_Task\s*\(') -and
     ($sysCoreC -match 's_sys_state\s*==\s*SYS_STATE_IDLE\s*\)[\s\S]{0,120}App_Storage_Task\s*\('))
Write-Check 'BlackboxLifecycle' 'write pointer advances only after write and readback succeed' `
    (($appStorageC -match 'App_Storage_Verify_Log_Entry') -and
     ($appStorageC -match 'if\s*\(\s*result\s*==\s*W25Q_DRIVER_RESULT_OK[\s\S]*write_addr\s*='))
Write-Check 'BlackboxLifecycle' 'unavailable sectors increment drop count without pointer advance' `
    (($appStorageC -match 'dropped_count\+\+') -and
     ($appStorageC -match 's_blackbox_metadata\.dropped_count\+\+;[\s\S]*return;'))
Write-Check 'BlackboxLifecycle' 'oldest-first read uses slot mapping and wrap capacity' `
    (($appStorageC -match 'App_Storage_Log_Address_To_Slot') -and
     ($appStorageC -match 'App_Storage_Log_Slot_To_Address') -and
     ($appStorageC -match 'APP_STORAGE_LOG_CAPACITY'))
Write-Check 'BlackboxLifecycle' 'leaving an active state requests metadata checkpoint' `
    ($sysCoreC -match 'App_Storage_Request_Blackbox_Checkpoint\s*\(')
Write-Check 'BlackboxSnapshot' 'fault window is 25 pre and 25 post samples at 200ms' `
    (($appStorageC -match 'APP_STORAGE_FAULT_PRE_SAMPLES\s+25U') -and
     ($appStorageC -match 'APP_STORAGE_FAULT_POST_SAMPLES\s+25U') -and
     ($sysCoreC -match 'SYS_BLACKBOX_SAMPLE_PERIOD_MS\s+200U'))
Write-Check 'BlackboxSnapshot' '150kHz frequency is scaled before storage in a 16-bit field' `
    (($appStorageH -match 'frequency_100hz') -and
     ($appStorageH -match 'Blackbox_Log_Tick\s*\([^;]*uint32_t\s+freq_hz') -and
     ($appStorageC -match 'frequency_100hz\s*=\s*\(uint16_t\)\(\(freq_hz\s*\+\s*50U\)\s*/\s*100U\)'))
Write-Check 'BlackboxSnapshot' 'RAM pretrigger ring freezes into a 50-entry snapshot' `
    (($appStorageC -match 's_fault_pre_ring\s*\[\s*APP_STORAGE_FAULT_PRE_SAMPLES\s*\]') -and
     ($appStorageC -match 's_fault_snapshot\s*\[\s*APP_STORAGE_FAULT_TOTAL_SAMPLES\s*\]') -and
     ($appStorageC -match 'APP_STORAGE_FAULT_CAPTURE_POST'))
Write-Check 'BlackboxSnapshot' 'each new run clears stale pretrigger samples' `
    (($appStorageH -match 'Blackbox_Reset_Pretrigger\s*\(\s*void\s*\)') -and
     ($appStorageC -match 'void\s+Blackbox_Reset_Pretrigger[\s\S]*s_fault_pre_count\s*=\s*0U') -and
     ($sysCoreC -match 'Sys_Core_Request_Start[\s\S]*Blackbox_Reset_Pretrigger\s*\(\s*\)[\s\S]*Sys_Core_Set_State\s*\(\s*SYS_STATE_SWEEP'))
Write-Check 'BlackboxSnapshot' 'fault trigger records reason without Flash erase or write' `
    (($appStorageH -match 'Blackbox_Lock_Fault_Snapshot\s*\(\s*uint8_t\s+fault_reason\s*\)') -and
     ($appStorageH -match 'fault_reason') -and
     ($appStorageH -match 'data_crc32') -and
     ($sysCoreC -match 'Sys_Core_Trigger_Fault[\s\S]*Blackbox_Lock_Fault_Snapshot\s*\(') -and
     ($appStorageC -match 'void\s+Blackbox_Lock_Fault_Snapshot[\s\S]*APP_STORAGE_FAULT_CAPTURE_POST') -and
     ($appStorageC -notmatch 'void\s+Blackbox_Lock_Fault_Snapshot[\s\S]{0,1200}W25Q_Driver_Erase_Sector'))
Write-Check 'BlackboxSnapshot' 'post-trigger samples carry explicit ADC validity' `
    (($appStorageH -match 'APP_STORAGE_LOG_STATE_INVALID\s+0x80U') -and
     ($appStorageH -match 'Blackbox_Capture_Tick\s*\([^;]*uint8_t\s+sample_valid') -and
     ($appStorageC -match 'sample_valid\s*==\s*0U') -and
     ($sysCoreC -match 'Adc_Driver_Is_Data_Fresh\s*\(\s*\)[\s\S]*Blackbox_Capture_Tick'))
Write-Check 'BlackboxSnapshot' 'fault persistence is gated by confirmed power-safe state' `
    (($appStorageH -match 'Blackbox_Fault_Persist_Task\s*\(\s*uint8_t\s+power_safe\s*\)') -and
     ($appStorageC -match 'power_safe\s*==\s*0U') -and
     ($sysCoreC -match 'Pwm_Driver_Is_Enabled\s*\(\s*\)[\s\S]*Sys_Core_Is_Power_Enabled\s*\(\s*\)[\s\S]*Blackbox_Fault_Persist_Task'))
Write-Check 'BlackboxSnapshot' 'fault slot advances only after data and header readback CRC verification' `
    (($appStorageC -match 'App_Storage_Verify_Fault_Snapshot') -and
     ($appStorageC -match 'W25Q_Driver_Erase_Sector\s*\(\s*slot_base\s*\)') -and
     ($appStorageC -match 'data_crc32\s*=\s*Checksum_CRC32') -and
     ($appStorageC -match 'if\s*\(\s*result\s*==\s*APP_STORAGE_RESULT_OK\s*\)[\s\S]*next_fault_slot\s*='))
Write-Check 'BlackboxSnapshot' 'boot scan recovers latest committed fault generation and next slot' `
    (($appStorageC -match 'App_Storage_Recover_Fault_Slots') -and
     ($appStorageC -match 'APP_STORAGE_FAULT_SLOT_COUNT') -and
     ($appStorageC -match 'header\.generation\s*>\s*s_fault_generation'))
Write-Check 'StorageConfig' 'config save uses background request API' `
    (Test-Contains $appStorageH '\bApp_Storage_Request_Save_Config\s*\(')
Write-Check 'StorageConfig' 'storage exposes task and result state' `
    ((Test-Contains $appStorageH '\bApp_Storage_Task\s*\(') -and
     (Test-Contains $appStorageH '\bApp_Storage_Get_Last_Result\s*\(') -and
     ($appStorageH -match 'APP_STORAGE_RESULT_PENDING') -and
     ($appStorageC -match 's_config_save_pending'))
Write-Check 'StorageConfig' 'config defaults clear the full structure first' `
    ($appStorageC -match 'memset\s*\(\s*cfg\s*,\s*0\s*,\s*sizeof\s*\(\s*\*cfg\s*\)\s*\)')
Write-Check 'StorageConfig' 'A and B copies are read back and CRC verified' `
    (($appStorageC -match 'App_Storage_Verify_Config_Copy') -and
     ($appStorageC -match 'W25Q_ADDR_CFG_A[\s\S]*App_Storage_Verify_Config_Copy\s*\(\s*W25Q_ADDR_CFG_A') -and
     ($appStorageC -match 'W25Q_ADDR_CFG_B[\s\S]*App_Storage_Verify_Config_Copy\s*\(\s*W25Q_ADDR_CFG_B'))
Write-Check 'StorageConfig' 'pending save clears only after both copies verify' `
    ($appStorageC -match 'if\s*\(\s*result\s*==\s*APP_STORAGE_RESULT_OK\s*\)[\s\S]*s_config_save_pending\s*=\s*0U')
Write-Check 'StorageConfig' 'IDLE scheduler runs storage task' `
    ($sysCoreC -match 's_sys_state\s*==\s*SYS_STATE_IDLE\s*\)[\s\S]{0,120}App_Storage_Task\s*\(')
Write-Check 'StorageConfig' 'UI settings save only requests persistence' `
    (($uiC -notmatch '\bApp_Storage_Save_Settings\s*\(') -and
     ($uiC -match '\bApp_Storage_Request_Save_Settings\s*\('))

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
Write-Check 'Keys' 'key timing boundaries remain 10/12/200/3000ms' `
    (($keyC -match 'KEY_DRIVER_DEBOUNCE_MS\s+10U?') -and
     ($keyC -match 'KEY_DRIVER_RELEASE_DEBOUNCE_MS\s+12U?') -and
     ($keyC -match 'KEY_DRIVER_DOUBLE_WINDOW_MS\s+200U?') -and
     ($keyC -match 'KEY_DRIVER_LONG_PRESS_MS\s+3000U?'))
Write-Check 'Keys' 'FSM gates long and double paths independently' `
    (($keyC -match 'long_enabled[\s\S]*KEY_DRIVER_LONG_PRESS_MS') -and
     ($keyC -match 'double_enabled[\s\S]*KEY_DRIVER_FSM_WAIT_DOUBLE'))
Write-Check 'Keys' 'batch event read is atomic and consumes each event' `
    (($keyC -match '__get_PRIMASK\s*\(') -and
     ($keyC -match '__disable_irq\s*\(') -and
     ($keyC -match 's_keys\s*\[\s*i\s*\]\.event\s*=\s*KEY_DRIVER_EVENT_NONE') -and
     ($keyC -match '__set_PRIMASK\s*\('))
Write-Check 'KeyRouting' 'key count is public and UI accepts filtered const events' `
    (($keyH -match '#define\s+KEY_DRIVER_COUNT\s+5U') -and
     ($uiH -match 'Ui_Controller_Task\s*\(\s*const\s+Key_Driver_Event\s+events\s*\[\s*KEY_DRIVER_COUNT\s*\]\s*\)'))
Write-Check 'KeyRouting' 'UI no longer scans keys or calls the system power handler' `
    (($uiC -notmatch 'Key_Driver_Get_All_Events') -and
     ($uiC -notmatch 'Sys_Power_Control_Handle'))
Write-Check 'KeyRouting' 'Sys_Core scans, consumes KEY0, then forwards remaining events' `
    ($sysCoreC -match 'Key_Driver_Get_All_Events\s*\(\s*events\s*\)[\s\S]*Sys_Core_Handle_Power_Key\s*\(\s*events\s*\)[\s\S]*Ui_Controller_Task\s*\(\s*events\s*\)')
Write-Check 'KeyRouting' 'KEY4 long press is local to WiFi page and power-safe IDLE only' `
    (($uiC -match 'page\s*==\s*UI_PAGE_WIFI_SETUP[\s\S]{0,500}k4\s*==\s*KEY_DRIVER_EVENT_LONG_PRESS') -and
     ($uiC -match 'k4\s*==\s*KEY_DRIVER_EVENT_LONG_PRESS[\s\S]{0,500}Sys_Core_Get_State\s*\(\s*\)\s*==\s*SYS_STATE_IDLE') -and
     ($uiC -match 'k4\s*==\s*KEY_DRIVER_EVENT_LONG_PRESS[\s\S]{0,700}Pwm_Driver_Is_Enabled\s*\(\s*\)\s*==\s*0U') -and
     ([regex]::Matches($uiC, 'CMD:CLEAR\\n').Count -eq 1))
Write-Check 'KeyRouting' 'WiFi clear long press does not jump pages or issue disconnect' `
    ($uiC -notmatch 'KEY_DRIVER_EVENT_LONG_PRESS[\s\S]{0,900}(s_page\s*=\s*UI_PAGE_WIFI_SETUP|App_Network_Manual_Disconnect)')

$uiCombined = $uiC + "`n" + $uiH
Write-Check 'Ui' 'GPIO backlight setting pages are removed' ($uiCombined -notmatch 'UI_PAGE_SETTING_BL')
Write-Check 'Ui' 'UI page count is 15' `
    ($uiH -match 'UI_PAGE_COUNT\s*=\s*15')
Write-Check 'Ui' 'brightness and breathing implementation is removed' `
    ($uiCombined -notmatch 's_bl_user_val|s_br_speed|s_br_min|s_br_max|Draw_BL_|Handle_BL_|Brightness|Breathing')
Write-Check 'Ui' 'settings menu has five items and frequency is index one' `
    (($uiC -match 'UI_SETTING_ITEM_COUNT') -and
     ($uiC -match 'UI_SETTING_ITEM_FREQUENCY') -and
     ($uiC -match 'case\s+UI_SETTING_ITEM_FREQUENCY\s*:\s*s_page\s*=\s*UI_PAGE_SETTING_FREQUENCY') -and
     ($uiC -match 'case\s+UI_SETTING_ITEM_COLOR\s*:\s*s_page\s*=\s*UI_PAGE_SETTING_COLOR'))
Write-Check 'Ui' 'backlight config field stays compatible but is forced to 100' `
    (($appStorageH -match '\bbacklight\b') -and
     ($appStorageC -match '\*bl\s*=\s*100U?') -and
     ($appStorageC -match '\(void\)bl') -and
     ($appStorageC -match 'cfg\.backlight\s*=\s*100U?'))
Write-Check 'Ui' 'GPIO backlight turns on after TFT init without fake fade' `
    (($tftC -match 'Tft_Driver_Clear\s*\(\s*TFT_COLOR_BLACK\s*\)\s*;\s*Tft_Driver_Set_Backlight\s*\(\s*255U?\s*\)') -and
     ($tftC -notmatch 'Tft_Driver_Set_Backlight\s*\(\s*0U?\s*\)') -and
     ($tftC -notmatch 'Sys_Timer_Delay_Ms\s*\(\s*150U?\s*\)'))

$gaugeFullMatch = [regex]::Match(
    $uiC,
    'static\s+void\s+Ui_Controller_Draw_Gauge_Full\s*\([\s\S]*?\n\}'
)
$colorKeysMatch = [regex]::Match(
    $uiC,
    'static\s+void\s+Ui_Controller_Handle_Color_Keys\s*\([\s\S]*?\n\}'
)
Write-Check 'UiPerf' 'gauge and color handlers do not duplicate full-screen clear' `
    ($gaugeFullMatch.Success -and $colorKeysMatch.Success -and
     ($gaugeFullMatch.Value -notmatch 'Tft_Driver_Clear\s*\(') -and
     ($colorKeysMatch.Value -notmatch 'Tft_Driver_Clear\s*\('))
Write-Check 'UiPerf' 'top-right icon cache includes palette colors' `
    (($uiC -match 's_last_icon_bg') -and
     ($uiC -match 's_last_icon_fg') -and
     ($uiC -match 'Ui_Controller_Get_Background_Color\s*\(\s*\)\s*!=\s*s_last_icon_bg') -and
     ($uiC -match 'Ui_Controller_Get_Text_Color\s*\(\s*\)\s*!=\s*s_last_icon_fg'))
Write-Check 'UiPerf' 'TFT exposes draw-cycle error state' `
    (($tftH -match 'Tft_Driver_Result') -and
     ($tftH -match 'Tft_Driver_Begin_Draw_Cycle\s*\(') -and
     ($tftH -match 'Tft_Driver_Get_Last_Result\s*\(') -and
     ($tftH -match 'Tft_Driver_Is_Draw_Blocked\s*\(') -and
     ($tftC -match 's_tft_draw_blocked'))
Write-Check 'UiPerf' 'low-level TFT transfers stop after the first cycle error' `
    (([regex]::Matches($tftC, 'if\s*\(\s*s_tft_draw_blocked\s*\)\s*return').Count -ge 3) -and
     ($tftC -match 'Tft_Driver_Record_Error\s*\('))
Write-Check 'UiPerf' 'UI retries failed full and incremental draw cycles' `
    (($uiC -match 'Tft_Driver_Begin_Draw_Cycle\s*\(\s*\)') -and
     ($uiC -match 'if\s*\(\s*!Tft_Driver_Is_Draw_Blocked\s*\(\s*\)\s*\)[\s\S]{0,250}s_page_drawn\s*=\s*1') -and
     ($uiC -match 'Tft_Driver_Is_Draw_Blocked\s*\(\s*\)[\s\S]{0,100}s_page_drawn\s*=\s*0'))

$usartIrqMatch = [regex]::Match($irqC,
    '(?ms)^void\s+USART2_IRQHandler\s*\([^)]*\)\s*\{.*?^\}')
$usartIrq = $usartIrqMatch.Value
$rxnePos = $usartIrq.IndexOf('USART_IT_RXNE')
$orePos = $usartIrq.IndexOf('USART_FLAG_ORE')
$txePos = $usartIrq.IndexOf('USART_IT_TXE')
Write-Check 'UartTx' 'USART2 TX uses a fixed 256-byte ring' `
    (($espC -match 'ESP8266_DRIVER_TX_BUF_SIZE\s+256U?') -and
     ($espC -match 's_tx_buf\s*\[\s*ESP8266_DRIVER_TX_BUF_SIZE\s*\]') -and
     ($espC -match 'volatile\s+uint16_t\s+s_tx_head') -and
     ($espC -match 'volatile\s+uint16_t\s+s_tx_tail'))
Write-Check 'UartTx' 'send API returns OK FULL or INVALID without TXE polling' `
    (($espH -match 'ESP8266_DRIVER_TX_OK') -and
     ($espH -match 'ESP8266_DRIVER_TX_FULL') -and
     ($espH -match 'ESP8266_DRIVER_TX_INVALID') -and
     ($espH -match 'Esp8266_Driver_Tx_Result\s+Esp8266_Driver_Send_String') -and
     ($espC -notmatch 'while\s*\(\s*USART_GetFlagStatus\s*\(\s*USART2\s*,\s*USART_FLAG_TXE'))
Write-Check 'UartTx' 'enqueue is atomic and enables TXE interrupt' `
    (($espC -match 'Esp8266_Driver_Send_String[\s\S]{0,1800}__disable_irq\s*\(') -and
     ($espC -match 'Esp8266_Driver_Send_String[\s\S]{0,2200}USART_ITConfig\s*\(\s*USART2\s*,\s*USART_IT_TXE\s*,\s*ENABLE\s*\)') -and
     ($espC -match 'Esp8266_Driver_Send_String[\s\S]{0,2300}__set_PRIMASK\s*\('))
Write-Check 'UartTx' 'TXE ISR sends one byte and disables itself when empty' `
    (($espH -match 'Esp8266_Driver_Tx_Ready_ISR\s*\(') -and
     ($espC -match 'Esp8266_Driver_Tx_Ready_ISR[\s\S]{0,800}USART_SendData\s*\(') -and
     ($espC -match 'Esp8266_Driver_Tx_Ready_ISR[\s\S]{0,1000}USART_ITConfig\s*\(\s*USART2\s*,\s*USART_IT_TXE\s*,\s*DISABLE\s*\)'))
Write-Check 'UartTx' 'USART2 interrupt preserves RXNE ORE TXE priority' `
    (($rxnePos -ge 0) -and ($orePos -gt $rxnePos) -and ($txePos -gt $orePos) -and
     ($irqC -match 'Esp8266_Driver_Tx_Ready_ISR\s*\('))
Write-Check 'UartTx' 'network and UI inspect enqueue results' `
    (($networkC -match 'Esp8266_Driver_Send_String\s*\([^;]+\)\s*!=\s*ESP8266_DRIVER_TX_OK') -and
     ($uiC -match 'Esp8266_Driver_Send_String\s*\([^;]+\)\s*!=\s*ESP8266_DRIVER_TX_OK'))

$directStart = ($uiC -match 'Inverter_Control_Soft_Start_Trigger\s*\(') -or
               ($networkC -match 'Inverter_Control_Soft_Start_Trigger\s*\(')
Write-Check 'ControlCallers' 'UI and network do not start inverter directly' (-not $directStart)

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
Write-Check 'ControlCallers' 'only Sys_Core writes system state' ($directStateFiles.Count -eq 0) `
    $(if ($directStateFiles.Count) { $directStateFiles -join ', ' } else { 'clean' })
Write-Check 'ControlCallers' 'system state is not exported as a global' `
    ($sysCoreH -notmatch '\bextern\s+volatile\s+Sys_State\s+g_sys_state\b')
Write-Check 'ControlCallers' 'UI and network use unified control requests' `
    (($uiC -match '\bSys_Core_Request_Start\s*\(') -and
     ($uiC -match '\bSys_Core_Request_Stop\s*\(') -and
     ($networkC -match '\bSys_Core_Request_Start\s*\(') -and
     ($networkC -match '\bSys_Core_Request_Stop\s*\('))
Write-Check 'ControlCallers' 'main dispatches through state getter' `
    (($mainC -match '\bSys_Core_Get_State\s*\(') -and ($mainC -notmatch '\bg_sys_state\b'))

Write-Check 'Network' 'telemetry uses explicit state mapping' `
    (Test-Contains $networkC '\bApp_Network_Map_Telemetry_State\s*\(')
Write-Check 'Network' 'telemetry mapping covers S=0/1/2/3' `
    (($networkC -match 'SYS_STATE_IDLE') -and ($networkC -match 'SYS_STATE_SWEEP') -and
     ($networkC -match 'SYS_STATE_RUNNING') -and ($networkC -match 'SYS_STATE_FAULT') -and
     ($networkC -match 'return\s+0U') -and ($networkC -match 'return\s+1U') -and
      ($networkC -match 'return\s+2U') -and ($networkC -match 'return\s+3U'))
Write-Check 'Network' 'ONLINE telemetry is independent of the current UI page' `
    (($networkC -notmatch 'allow_telemetry') -and
     ($networkC -notmatch 'Ui_Controller_Get_Page\s*\(') -and
     ($networkC -match 's_conn_state\s*==\s*APP_NETWORK_CONN_ONLINE'))
Write-Check 'Network' 'telemetry uses display-filtered voltage and current' `
    (($networkC -match 'Adc_Driver_Get_Display_Voltage\s*\(') -and
     ($networkC -match 'Adc_Driver_Get_Display_Current\s*\('))
Write-Check 'Network' 'frequency is live only in SWEEP and RUNNING' `
    (($networkC -match 'state\s*==\s*SYS_STATE_SWEEP') -and
     ($networkC -match 'state\s*==\s*SYS_STATE_RUNNING') -and
     ($networkC -match 'frequency_hz\s*=\s*0U') -and
     ($networkC -match 'frequency_hz\s*=\s*Pwm_Driver_Get_Frequency\s*\('))
Write-Check 'Network' 'telemetry period advances only after successful enqueue' `
    ($networkC -match 'Esp8266_Driver_Send_String\s*\(\s*json_buf\s*\)\s*==\s*ESP8266_DRIVER_TX_OK[\s\S]{0,160}last_telemetry\s*=')
Write-Check 'Network' 'remote ON and OFF remain idempotent through unified control' `
    (($networkC -match 'CMD:ON[\s\S]{0,500}INVERTER_CONTROL_SS_STATE_IDLE[\s\S]{0,300}Sys_Core_Request_Start') -and
     ($networkC -match 'CMD:OFF[\s\S]{0,500}INVERTER_CONTROL_SS_STATE_SWEEP[\s\S]{0,400}Sys_Core_Request_Stop'))

Write-Check 'Scheduler' 'main dispatches through Sys_Core getter' `
    (($mainC -match 'Sys_Core_Get_State\s*\(') -and ($mainC -notmatch '\bg_sys_state\b'))
Write-Check 'Scheduler' 'common scheduler contains ADC-to-WFI chain' `
    (($sysCoreC -match 'Sys_Core_Run_Common') -and ($sysCoreC -match '__WFI\s*\('))
$commonStart = $sysCoreC.IndexOf('static void Sys_Core_Run_Common')
$commonEnd = $sysCoreC.IndexOf('void Sys_Run_Idle', [Math]::Max(0, $commonStart))
$commonBody = ''
if (($commonStart -ge 0) -and ($commonEnd -gt $commonStart)) {
    $commonBody = $sysCoreC.Substring($commonStart, $commonEnd - $commonStart)
}
$scheduleCalls = @(
    'Adc_Driver_Filter_Task',
    'Sys_Core_Safety_Task',
    'Sys_Core_Run_Key_And_Ui_Task',
    'state_task();',
    'App_Network_Task',
    'App_Storage_Task',
    'Sys_Core_Run_Led_Tick',
    'Sys_Core_Run_Buzzer_Tick',
    'IWDG_ReloadCounter',
    '__WFI'
)
$schedulePositions = @($scheduleCalls | ForEach-Object { $commonBody.IndexOf($_) })
$scheduleOrdered = ($schedulePositions.Count -eq $scheduleCalls.Count) -and
                   (@($schedulePositions | Where-Object { $_ -lt 0 }).Count -eq 0)
if ($scheduleOrdered) {
    for ($i = 1; $i -lt $schedulePositions.Count; $i++) {
        if ($schedulePositions[$i] -le $schedulePositions[$i - 1]) {
            $scheduleOrdered = $false
            break
        }
    }
}
Write-Check 'Scheduler' 'common scheduler order is ADC Safety Key UI state Network Storage LED Buzzer IWDG WFI' `
    $scheduleOrdered
Write-Check 'Scheduler' 'all four state runners delegate to the common scheduler' `
    (($sysCoreC -match 'void\s+Sys_Run_Idle\s*\([^)]*\)\s*\{[\s\S]{0,180}Sys_Core_Run_Common') -and
     ($sysCoreC -match 'void\s+Sys_Run_Sweep\s*\([^)]*\)\s*\{[\s\S]{0,180}Sys_Core_Run_Common') -and
     ($sysCoreC -match 'void\s+Sys_Run_Running\s*\([^)]*\)\s*\{[\s\S]{0,180}Sys_Core_Run_Common') -and
     ($sysCoreC -match 'void\s+Sys_Run_Fault\s*\([^)]*\)\s*\{[\s\S]{0,180}Sys_Core_Run_Common'))
$stateRunnerStart = $sysCoreC.IndexOf('void Sys_Run_Idle')
$stateRunnerBody = if ($stateRunnerStart -ge 0) { $sysCoreC.Substring($stateRunnerStart) } else { '' }
Write-Check 'Scheduler' 'state runners do not rewrite STATUS LED every loop' `
    ($stateRunnerBody -notmatch 'Led_Driver_Set_Status\s*\(')
Write-Check 'Scheduler' 'state transition owns one-time STATUS LED entry action' `
    (($sysCoreC -match 'Sys_Core_Set_State[\s\S]{0,1400}LED_DRIVER_STATE_SLOW') -and
     ($sysCoreC -match 'Sys_Core_Set_State[\s\S]{0,1400}LED_DRIVER_STATE_ON') -and
     ($sysCoreC -match 'Sys_Core_Set_State[\s\S]{0,1400}LED_DRIVER_STATE_OFF'))
Write-Check 'Scheduler' 'main contains initialization and state dispatch only' `
    (($mainC -match 'switch\s*\(\s*Sys_Core_Get_State\s*\(\s*\)\s*\)') -and
     ($mainC -notmatch 'Key_Driver_Task\s*\(|App_Network_Task\s*\(|App_Storage_Task\s*\(|Adc_Driver_Filter_Task\s*\('))

Write-Check 'Timeout' 'IWDG uses prescaler 64 and reload 1500' `
    (($sysCoreC -match 'IWDG_SetPrescaler\s*\(\s*IWDG_Prescaler_64\s*\)') -and
     ($sysCoreC -match 'IWDG_SetReload\s*\(\s*1500U?\s*\)') -and
     ($sysCoreC -notmatch 'IWDG_SetReload\s*\(\s*4000U?\s*\)'))
Write-Check 'Timeout' 'ADC hardware calibration waits are bounded and fail closed' `
    (($adcC -match 'ADC_DRIVER_HW_CAL_TIMEOUT_MS') -and
     ($adcC -match 'ADC_GetResetCalibrationStatus[\s\S]{0,500}Sys_Timer_Get_Tick') -and
     ($adcC -match 'ADC_GetCalibrationStatus[\s\S]{0,500}Sys_Timer_Get_Tick') -and
     ($adcC -match 's_cal_state\s*=\s*ADC_DRIVER_CAL_ERROR') -and
     ($adcC -notmatch 'while\s*\(\s*ADC_Get(?:Reset)?CalibrationStatus\s*\([^)]*\)\s*\)\s*;'))
Write-Check 'Timeout' 'ADC snapshot retry loop is explicitly bounded' `
    (($adcC -match 'ADC_DRIVER_SNAPSHOT_RETRY_LIMIT') -and
     ($adcC -match 'snapshot_attempts\s*>=\s*ADC_DRIVER_SNAPSHOT_RETRY_LIMIT'))
Write-Check 'Timeout' 'SPI RX drain is bounded before OVR recovery' `
    (($spiSharedC -match 'SPI1_SHARED_RX_DRAIN_LIMIT') -and
     ($spiSharedC -match 'drain_count\s*<\s*SPI1_SHARED_RX_DRAIN_LIMIT') -and
     ($spiSharedC -match 'SPI_I2S_FLAG_OVR'))
Write-Check 'Timeout' 'TFT DMA timeout uses wrap-safe elapsed time and full cleanup' `
    (($tftC -match 'Sys_Timer_Get_Tick\s*\(\s*\)\s*-\s*start') -and
     ($tftC -match 'TFT_DRIVER_RESULT_DMA_TIMEOUT') -and
     ($tftC -match 'SPI_I2S_DMACmd\s*\(\s*SPI1\s*,\s*SPI_I2S_DMAReq_Tx\s*,\s*DISABLE\s*\)[\s\S]{0,260}Spi1_Shared_Force_Release'))
Write-Check 'Timeout' 'W25 program and erase busy waits are bounded and recover the bus' `
    (($w25C -match 'W25Q_PROGRAM_TIMEOUT_MS\s+10U?') -and
     ($w25C -match 'W25Q_ERASE_TIMEOUT_MS\s+500U?') -and
     ($w25C -match 'Sys_Timer_Get_Tick\s*\(\s*\)\s*-\s*start') -and
     ($w25C -match 'Spi1_Shared_Force_Release\s*\(\s*\)[\s\S]{0,100}W25Q_DRIVER_RESULT_BUSY_TIMEOUT'))
Write-Check 'Timeout' 'initialization delay sleeps on interrupts instead of hot spinning' `
    (($timerC -match 'Sys_Timer_Delay_Ms[\s\S]{0,300}__WFI\s*\(\s*\)') -and
     ($timerC -match 's_sys_tick\s*-\s*start'))
Write-Check 'Timeout' 'USART2 runtime transmission has no polling wait' `
    ($espC -notmatch 'while\s*\(\s*USART_GetFlagStatus\s*\(\s*USART2')

$staticPrefixFailures = @()
$expectedStaticPrefixes = @{
    'stm32f10x_it.c' = 'Stm32f10x_It_'
}
foreach ($sourceFile in ($stm32Files | Where-Object { $_.Extension -eq '.c' })) {
    $modulePrefix = [System.IO.Path]::GetFileNameWithoutExtension($sourceFile.Name) + '_'
    if ($expectedStaticPrefixes.ContainsKey($sourceFile.Name)) {
        $modulePrefix = $expectedStaticPrefixes[$sourceFile.Name]
    }
    $sourceText = [System.IO.File]::ReadAllText(
        $sourceFile.FullName, [System.Text.Encoding]::UTF8)
    $staticMatches = [System.Text.RegularExpressions.Regex]::Matches(
        $sourceText,
        '(?m)^static\s+(?:const\s+)?[A-Za-z_][\w\s\*]*?\s+([A-Za-z_]\w*)\s*\(')
    foreach ($staticMatch in $staticMatches) {
        if (-not $staticMatch.Groups[1].Value.StartsWith(
                $modulePrefix, [System.StringComparison]::Ordinal)) {
            $staticPrefixFailures += ('{0}:{1}' -f $sourceFile.Name,
                $staticMatch.Groups[1].Value)
        }
    }
}
$removedPublicApis = @(
    'Adc_Driver_Get_Sample_Sequence', 'Adc_Driver_Get_Last_Sample_Tick',
    'Adc_Driver_Calibrate_Offset', 'Adc_Driver_Get_Voltage',
    'Adc_Driver_Get_Current', 'Esp8266_Driver_Get_Rx_Flag',
    'Esp8266_Driver_Get_Rx_Buffer', 'Esp8266_Driver_Clear_Rx_Buffer',
    'Esp8266_Driver_Copy_Rx_Frame', 'Inverter_Control_Freq_Ramp_Get_Target',
    'Tft_Driver_Get_Config', 'Tft_Driver_Set_Font_Scale',
    'Tft_Driver_Get_Font_Scale', 'Tft_Driver_Show_Num',
    'Tft_Driver_Show_Float', 'Ui_Controller_Get_Page',
    'Ui_Controller_Force_Page', 'Sys_Timer_Get_Cycles',
    'App_Network_Soft_Reset', 'App_Network_Is_Connected'
)
$remainingRemovedApis = @($removedPublicApis | Where-Object {
    $stm32Text -match ('\b' + [regex]::Escape($_) + '\s*\(')
})
Write-Check 'CodeQuality' 'C sources contain no double-slash comments' `
    ($stm32Text -notmatch '//')
Write-Check 'CodeQuality' 'for loops contain no C99 declarations' `
    ($stm32Text -notmatch 'for\s*\(\s*(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:char|short|int|long|float|double|uint\d+_t|int\d+_t)\s+')
Write-Check 'CodeQuality' 'known mixed declarations are moved to block starts' `
    (($stm32Text -notmatch 'INVERTER_CONTROL_RAMP_IDLE\s*\)\s*return;\s*uint32_t\s+current') -and
     ($stm32Text -notmatch 'Ui_Controller_Update_EMA\s*\(\s*\)\s*;\s*float\s+display_val') -and
     ($stm32Text -notmatch 'snprintf\s*\([^;]+;\s*uint8_t\s+col'))
Write-Check 'CodeQuality' 'all static functions use their module prefix' `
    ($staticPrefixFailures.Count -eq 0) `
    $(if ($staticPrefixFailures.Count) { $staticPrefixFailures -join ', ' } else { 'clean' })
Write-Check 'CodeQuality' 'obsolete public compatibility APIs are removed' `
    ($remainingRemovedApis.Count -eq 0) `
    $(if ($remainingRemovedApis.Count) { $remainingRemovedApis -join ', ' } else { 'clean' })
Write-Check 'CodeQuality' 'Sys_Core internal key and safety helpers are private' `
    (($sysCoreH -notmatch 'Key_Driver\.h|Sys_Safety_Task|Sys_Power_Control_Handle|Sys_Safety_Get_EMA') -and
     ($sysCoreC -match 'static\s+void\s+Sys_Core_Safety_Task') -and
     ($sysCoreC -match 'static\s+void\s+Sys_Core_Handle_Power_Key'))
$documentationChecks = [ordered]@{
    'UI H page count is 15' = ($uiH -match 'UI_PAGE_COUNT\s*=\s*15')
    'five-item settings enum' = (($uiC -match 'UI_SETTING_ITEM_FREQUENCY') -and
        ($uiC -match 'UI_SETTING_ITEM_COLOR') -and
        ($uiC -match 'UI_SETTING_ITEM_COUNT'))
    '20-200kHz PWM boundary' = (($pwmH -match 'PWM_DRIVER_FREQ_MIN_HZ\s+20000') -and
        ($pwmH -match 'PWM_DRIVER_FREQ_MAX_HZ\s+200000'))
    'five-key pin list' = (($keyH -match 'PB9') -and ($keyH -match 'PB8') -and
        ($keyH -match 'PB7') -and ($keyH -match 'PB6') -and ($keyH -match 'PB5'))
    'heartbeat PC13' = ($sysCoreC -match 'PC13')
    'main lists four LEDs' = (($mainC -match 'PA15') -and
        ($mainC -match 'PB4') -and ($mainC -match 'PB3') -and
        ($mainC -match 'PC13'))
    'GPIO backlight' = (($tftC -match 'PA12') -and
        ($stm32Text -notmatch 'backlight PWM'))
    'Flash font count' = (($tftC -match '20897') -and ($tftH -match 'ICON_ID_'))
    'V2 log partition' = ($appStorageC -match '0x312000~0x6CFFFF')
    'sweep safety' = (($sysCoreC -match 'SYS_STATE_SWEEP') -and
        ($sysCoreC -match 'SYS_STATE_RUNNING'))
    'no stale descriptions' = ($stm32Text -notmatch '17 pages|4 keys:|ROM 76|Flash 6763|95.{0,8}150|150kHz.{0,24}100kHz')
}
$documentationFailures = @($documentationChecks.GetEnumerator() |
    Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
Write-Check 'CodeQuality' 'driver comments describe V5 hardware and UI accurately' `
    ($documentationFailures.Count -eq 0) `
    $(if ($documentationFailures.Count) { $documentationFailures -join ', ' } else { 'clean' })

if (Test-CategoryEnabled 'Version') {
    $allStm32Sources = Get-ChildItem (Join-Path $keilRoot 'Hardware'),
                                       (Join-Path $keilRoot 'System'),
                                       (Join-Path $keilRoot 'User') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }
    $cHeaderFailures = @()
    $hGuardFailures = @()
    foreach ($sourceFile in $allStm32Sources) {
        $lines = [System.IO.File]::ReadAllLines($sourceFile.FullName, [System.Text.Encoding]::UTF8)
        if ($sourceFile.Extension -eq '.c') {
            $head = ($lines | Select-Object -First 48) -join "`n"
            if ($head -notmatch '@file' -or $head -notmatch '@brief' -or
                $head -notmatch '@note' -or $head -notmatch 'V5\.1\.0') {
                $cHeaderFailures += $sourceFile.FullName.Substring($repoRoot.Length + 1)
            }
        }
        elseif ($lines.Count -eq 0 -or $lines[0] -notmatch '^#ifndef\s+') {
            $hGuardFailures += $sourceFile.FullName.Substring($repoRoot.Length + 1)
        }
    }
    Write-Check 'Version' 'all STM32 C files have V5.1.0 Chinese file headers' `
        ($cHeaderFailures.Count -eq 0) `
        $(if ($cHeaderFailures.Count) { $cHeaderFailures -join ', ' } else { 'clean' })
    Write-Check 'Version' 'all STM32 headers begin directly with include guards' `
        ($hGuardFailures.Count -eq 0) `
        $(if ($hGuardFailures.Count) { $hGuardFailures -join ', ' } else { 'clean' })
    Write-Check 'Version' 'Splash displays V5.1.0' ($tftC -match '"V5\.1\.0"')
}

if (Test-CategoryEnabled 'SettingsGauge') {
    Write-Check 'SettingsGauge' 'frequency settings preserve two independent bands' `
        (($appStorageH -match 'startup_low_freq_hz') -and
         ($appStorageH -match 'startup_high_freq_hz') -and
         ($appStorageH -match 'startup_freq_band'))
    Write-Check 'SettingsGauge' 'soft start exposes both dynamic sweep endpoints' `
        (($invH -match 'Inverter_Control_Get_Sweep_Start_Freq') -and
         ($invH -match 'Inverter_Control_Get_Sweep_Target_Freq') -and
         ($invC -match '99900U') -and ($invC -match '200000U'))
    Write-Check 'SettingsGauge' 'UI uses shared segmented gauges and 2X numbers' `
        (($uiC -match 'Ui_Controller_Gauge_Value_To_Angle') -and
         ($uiC -match 'Ui_Controller_Gauge_Get_Frequency_Config') -and
         ($tftC -match 'Tft_Driver_Show_String_2X'))
}

if (Test-CategoryEnabled 'Documentation') {
    $guideStalePattern = 'TIM1[^\r\n]*95k(?:Hz)?\s*[~\-]\s*150kHz|SS_SWEEP\(150kHz\)|\u53c2\u6570:[^\r\n]*200Hz|\u9891\u7387\u786c\u4e0b\u9650\s*95kHz|UI\s*\u2014\s*14\u9875\u9762'
    $guideVersionCurrent = ($developerGuide -match '\u6587\u6863\u7248\u672c[^\r\n]*V5\.1\.0')
    $guideSettingsCurrent = (($developerGuide -match '\u4e94\u9879\u8bbe\u7f6e') -and
                             ($developerGuide -match '15\u9875\u9762'))
    $guideFrequencyCurrent = (($developerGuide -match '20\u2013200kHz') -and
                              ($developerGuide -match '99\.9kHz') -and
                              ($developerGuide -match '200kHz'))
    $guideGaugeCurrent = ($developerGuide -match '\u9012\u589e\u5f0f\u72ec\u7acb\u8868\u76d8')
    $guideCurrent = ($guideVersionCurrent -and $guideSettingsCurrent -and
                     $guideFrequencyCurrent -and $guideGaugeCurrent)
    $guideClean = ($developerGuide -notmatch $guideStalePattern)
    $ch341Current = (($ch341Readme -match 'WPT_PWM V5\.1\.0') -and
                     ($ch341Readme -match '\u6d41\u7a0b\u5747\u672a\u6539\u53d8') -and
                     ($ch341Readme -notmatch 'V5\.0\.2'))
    Write-Check 'Documentation' 'developer guide is synchronized to V5.1.0' $guideCurrent `
        ("version={0}, settings={1}, frequency={2}, gauge={3}" -f
         $guideVersionCurrent, $guideSettingsCurrent, $guideFrequencyCurrent, $guideGaugeCurrent)
    Write-Check 'Documentation' 'developer guide has no obsolete frequency UI or brightness description' $guideClean
    Write-Check 'Documentation' 'CH341 guide records V5.1.0 without claiming a workflow change' $ch341Current
}

Write-Host ("Summary: {0} PASS, {1} FAIL" -f $script:PassCount, $script:FailureCount) -ForegroundColor Cyan
if ($script:FailureCount -ne 0) {
    exit 1
}
exit 0
