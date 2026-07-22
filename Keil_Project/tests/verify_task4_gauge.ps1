$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Ui_Controller.c"
$activeUi = [regex]::Replace($ui,
    '(?s)#if defined\(UI_CONTROLLER_LEGACY_LINEAR_GAUGE\).*?#endif', '')
$tftH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Tft_Driver.h"
$tftC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Tft_Driver.c"

function Assert-Contains([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

function Assert-NotContains([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) { throw $message }
}

Assert-Contains $ui 'typedef\s+struct\s*\{[\s\S]*?float\s+start;[\s\S]*?float\s+end;[\s\S]*?float\s+step;' 'Gauge segments must be represented by a shared range table'
Assert-Contains $ui 'GAUGE_V_SEGMENTS[\s\S]*?0\.0f[\s\S]*?20\.0f[\s\S]*?2\.0f[\s\S]*?20\.0f[\s\S]*?40\.0f[\s\S]*?5\.0f[\s\S]*?40\.0f[\s\S]*?50\.0f[\s\S]*?10\.0f' 'Voltage gauge segments must be 0-20/2, 20-40/5, 40-50/10'
Assert-Contains $ui 'GAUGE_C_SEGMENTS[\s\S]*?0\.0f[\s\S]*?1\.0f[\s\S]*?0\.1f[\s\S]*?1\.0f[\s\S]*?3\.0f[\s\S]*?0\.5f[\s\S]*?3\.0f[\s\S]*?5\.0f[\s\S]*?1\.0f' 'Current gauge segments must be 0-1/0.1, 1-3/0.5, 3-5/1'
Assert-Contains $ui 'GAUGE_F_LOW_SEGMENTS[\s\S]*?20\.0f[\s\S]*?50\.0f[\s\S]*?5\.0f[\s\S]*?50\.0f[\s\S]*?80\.0f[\s\S]*?10\.0f[\s\S]*?80\.0f[\s\S]*?100\.0f[\s\S]*?20\.0f' 'Low frequency gauge segments are incomplete'
Assert-Contains $ui 'GAUGE_F_HIGH_SEGMENTS[\s\S]*?100\.0f[\s\S]*?140\.0f[\s\S]*?5\.0f[\s\S]*?140\.0f[\s\S]*?180\.0f[\s\S]*?10\.0f[\s\S]*?180\.0f[\s\S]*?200\.0f[\s\S]*?20\.0f' 'High frequency gauge segments are incomplete'
Assert-Contains $ui 'Ui_Controller_Gauge_Value_To_Angle' 'Gauge mapping must have one shared value-to-angle helper'
Assert-Contains $ui 'Ui_Controller_Gauge_Get_Frequency_Config' 'Frequency gauge must select its range from the active band'
Assert-Contains $ui 'INVERTER_CONTROL_SS_STATE_SWEEP[\s\S]{0,500}Inverter_Control_Get_Sweep_Start_Freq' 'Sweep gauge range must use the locked sweep band'
Assert-Contains $ui 'warn_start[\s\S]*?red_start' 'Gauge configuration must distinguish warning and red thresholds'
Assert-Contains $ui '4\.0f[\s\S]*?4\.5f' 'Current gauge must warn at 4A and turn red at 4.5A'
Assert-Contains $ui 'Tft_Driver_Show_5x10_String_Scaled_Pixel' 'Gauge must render the central value through the scaled 5x10 font'
Assert-Contains $tftH 'Tft_Driver_Show_5x10_String_Scaled_Pixel' 'Scaled 5x10 draw API must be public to the UI controller'
Assert-Contains $tftC 'void\s+Tft_Driver_Show_5x10_String_Scaled_Pixel' 'Scaled 5x10 draw API implementation is missing'
Assert-NotContains $activeUi 'range_max - cfg->range_min\) \* 180\.0f' 'Linear full-range gauge mapping must be removed'

Write-Host 'Task 4 segmented gauge contract passed' -ForegroundColor Green
