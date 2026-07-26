$ErrorActionPreference = 'Stop'
$script:Failures = 0
$script:Passes = 0
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Read-RepoText {
    param([string]$RelativePath)
    return [System.IO.File]::ReadAllText(
        (Join-Path $repoRoot $RelativePath),
        [System.Text.Encoding]::UTF8)
}

function Test-Pattern {
    param(
        [string]$Name,
        [bool]$Passed
    )

    if ($Passed) {
        $script:Passes++
        Write-Host ("PASS {0}" -f $Name) -ForegroundColor Green
    }
    else {
        $script:Failures++
        Write-Host ("FAIL {0}" -f $Name) -ForegroundColor Red
    }
}

$tft = Read-RepoText 'Keil_Project\Hardware\Tft_Driver.c'
$ui = Read-RepoText 'Keil_Project\Hardware\Ui_Controller.c'
$network = Read-RepoText 'Keil_Project\User\App_Network.c'
$storage = Read-RepoText 'Keil_Project\User\App_Storage.c'
$checksumC = Read-RepoText 'Keil_Project\System\Checksum.c'
$checksumH = Read-RepoText 'Keil_Project\System\Checksum.h'
$generator = Read-RepoText 'ch341\generate_font.py'
$sysCoreC = Read-RepoText 'Keil_Project\User\Sys_Core.c'
$sysCoreH = Read-RepoText 'Keil_Project\User\Sys_Core.h'
$adcC = Read-RepoText 'Keil_Project\Hardware\Adc_Driver.c'
$adcH = Read-RepoText 'Keil_Project\Hardware\Adc_Driver.h'
$irq = Read-RepoText 'Keil_Project\User\stm32f10x_it.c'
$esp = Read-RepoText 'Keil_Project\Hardware\Esp8266_Driver.c'
$inverter = Read-RepoText 'Keil_Project\Hardware\Inverter_Control.c'
$startup = Read-RepoText 'Keil_Project\Start\startup_stm32f10x_md.s'

Test-Pattern 'external icon base follows table header and entry count' `
    (($tft -match 'TFT_FONT_ICON_TABLE_ADDR') -and
     ($tft -match 's_icon_data_addr') -and
     ($tft -match 'icon_count') -and
     ($tft -notmatch 'icon_id\s*>\s*34U') -and
     ($tft -notmatch '0x00000808'))

$windowFunction = [regex]::Match(
    $tft,
    '(?s)static\s+void\s+Tft_Driver_Set_Window\s*\([^)]*\)\s*\{(.*?)\r?\n\}')
$windowAcquireCount = if ($windowFunction.Success) {
    [regex]::Matches($windowFunction.Groups[1].Value, 'Spi1_Shared_Acquire').Count
} else {
    0
}
Test-Pattern 'address window is sent in one shared-bus transaction' `
    ($windowFunction.Success -and
     $windowAcquireCount -eq 1 -and
     $windowFunction.Groups[1].Value -notmatch 'Tft_Driver_WrCmd|Tft_Driver_WrDat')

Test-Pattern 'UI erase uses current theme background' `
    (($ui -match 'Ui_Controller_Erase_Area') -and
     ($ui -notmatch 'Tft_Driver_Erase_Pixel_Area'))

Test-Pattern 'custom color never becomes a preset array index' `
    (($ui -match 'Ui_Controller_Normalize_Color_Preset') -and
     ($ui -match 's_preview_choice\s*=\s*Ui_Controller_Normalize_Color_Preset'))

Test-Pattern 'network commands require exact complete frames' `
    (($network -match 'App_Network_Is_Exact_Frame') -and
     ($network -notmatch 'strstr\s*\(\s*local_buf\s*,\s*"CMD:') -and
     ($network -match 'App_Network_Is_Canonical_Number') -and
     ($network -match '\*endp\s*!=\s*''\\0'''))

Test-Pattern 'persistent config validates strings and field semantics' `
    (($storage -match 'App_Storage_Is_String_Terminated') -and
     ($storage -match 'language\s*>\s*1U') -and
     ($storage -match 'letter_spacing\s*>\s*3U') -and
     ($storage -match 'color_preset\s*!=\s*255U'))

Test-Pattern 'CRC32 supports streaming and font V2 verifies payload' `
    (($checksumH -match 'Checksum_CRC32_Begin') -and
     ($checksumH -match 'Checksum_CRC32_Update') -and
     ($checksumH -match 'Checksum_CRC32_Finish') -and
     ($checksumC -match 'Checksum_CRC32_Update') -and
     ($generator -match 'VERSION\s*=\s*2') -and
     ($tft -match 'Tft_Driver_Verify_Font_Payload') -and
     ($tft -match 'Tft_Driver_Is_Glyph_Offset_Valid'))

Test-Pattern 'startup includes nonblocking power settle time' `
    (($sysCoreC -match 'SYS_POWER_SETTLE_MS') -and
     ($sysCoreC -match 's_power_enabled_tick') -and
     ($sysCoreH -match 'SYS_CONTROL_RESULT_POWER_NOT_READY'))

Test-Pattern 'ADC analog watchdog provides fast overcurrent latch' `
    (($adcC -match 'ADC_AnalogWatchdog') -and
     ($adcC -match 'ADC_IT_AWD') -and
     ($adcH -match 'Adc_Driver_Is_Fast_Overcurrent_Latched') -and
     ($irq -match 'ADC1_2_IRQHandler'))

Test-Pattern 'NMI enters the minimal safe shutdown path' `
    ($irq -match 'void\s+NMI_Handler\s*\(void\)\s*\{\s*Stm32f10x_It_Fatal_Safe_Loop\s*\(\s*\)')

Test-Pattern 'RX frame copy checks bounds before reading the buffer' `
    ($esp -match 'len\s*\+\s*1U\s*<\s*max_len\s*&&\s*len\s*\+\s*1U\s*<\s*ESP8266_DRIVER_RX_BUF_SIZE')

Test-Pattern 'inverter stores the actual PWM frequency' `
    (($inverter -match 's_ss_current_freq\s*=\s*Pwm_Driver_Set_Frequency') -and
     ($ui -notmatch 'Pwm_Driver_Set_Frequency') -and
     ($ui -match 'Inverter_Control_Freq_Ramp_Trigger'))

Test-Pattern 'medium-density startup reserves a 2KB stack' `
    ($startup -match 'Stack_Size\s+EQU\s+0x00000800')

Write-Host ("Hardening checks: {0} passed, {1} failed" -f $script:Passes, $script:Failures)
if ($script:Failures -ne 0) {
    exit 1
}
