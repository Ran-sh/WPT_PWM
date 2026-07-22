$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$storageH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.h"
$storageC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.c"
$uiH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Ui_Controller.h"
$uiC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Ui_Controller.c"
$sysCore = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/Sys_Core.c"
$pwmH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Pwm_Driver.h"
$invH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Inverter_Control.h"
$invC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/Hardware/Inverter_Control.c"

function Assert-Contains([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

function Assert-NotContains([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) { throw $message }
}

function Assert-CursorIconNamesMatchCandidates([string]$text) {
    $match = [regex]::Match($text,
        'static const char\* Ui_Controller_Get_Icon_Name\(uint8_t icon_id\)\s*\{(?<body>[\s\S]*?)\n\s*\}\s*\n\s*\}')
    if (-not $match.Success) { throw 'Cannot find cursor icon name table' }

    $expected = @(
        'ICON_ID_STAR', 'ICON_ID_CHECK', 'ICON_ID_ROCKET_ANIM', 'ICON_ID_LIGHTNING',
        'ICON_ID_HOME', 'ICON_ID_GEAR', 'ICON_ID_REFRESH', 'ICON_ID_ARROW_RT'
    )
    $actual = [regex]::Matches($match.Groups['body'].Value, 'case\s+(ICON_ID_[A-Z_]+)\s*:') |
        ForEach-Object { $_.Groups[1].Value }

    if ($actual.Count -ne $expected.Count -or @($actual | Where-Object { $_ -notin $expected }).Count -ne 0 -or
        @($expected | Where-Object { $_ -notin $actual }).Count -ne 0) {
        throw 'Cursor icon name table must contain exactly the eight selectable icon IDs'
    }
}

Assert-Contains $storageH '#define CFG_VERSION\s+2U' 'Configuration version must upgrade to 2'
Assert-Contains $storageH 'uint32_t\s+startup_low_freq_hz;' 'Missing low-band frequency field'
Assert-Contains $storageH 'uint32_t\s+startup_high_freq_hz;' 'Missing high-band frequency field'
Assert-Contains $storageH 'uint8_t\s+startup_freq_band;' 'Missing selected frequency band field'
Assert-Contains $storageH 'uint8_t\s+menu_cursor_icon;' 'Missing global cursor field'
Assert-Contains $storageC 'App_Storage_Config_V1' 'Missing V1 migration structure'
Assert-Contains $storageC 'App_Storage_Migrate_V1' 'Missing V1 migration function'
Assert-Contains $storageC 'memcpy\(dst->ssid, src->ssid' 'V1 SSID is not migrated field by field'
Assert-Contains $storageC 'dst->adc_i_offset = src->adc_i_offset;' 'V1 ADC current calibration is not migrated'
Assert-Contains $storageC 'dst->letter_spacing = src->letter_spacing;' 'V1 letter spacing is not migrated'
Assert-Contains $storageC 'dst->color_bg = src->color_bg;' 'V1 colour settings are not migrated'
Assert-Contains $uiH 'Ui_Controller_Apply_Settings\s*\([\s\S]*uint32_t\s+startup_freq_low_hz[\s\S]*uint32_t\s+startup_freq_high_hz[\s\S]*uint8_t\s+startup_freq_band[\s\S]*uint8_t\s+cursor_icon' 'UI settings interface does not accept V2 frequency state'
Assert-Contains $uiC 's_startup_low_freq_hz\s*=\s*startup_freq_low_hz;' 'Loaded low-band frequency is not applied to UI state'
Assert-Contains $uiC 's_startup_high_freq_hz\s*=\s*startup_freq_high_hz;' 'Loaded high-band frequency is not applied to UI state'
Assert-Contains $uiC 's_startup_freq_band\s*=\s*startup_freq_band;' 'Loaded frequency band is not applied to UI state'
Assert-Contains $uiC 's_menu_cursor_icon\s*=\s*cursor_icon;' 'Loaded cursor icon is not applied to UI state'
Assert-Contains $uiC 'App_Storage_Request_Save_Settings\([\s\S]*s_startup_low_freq_hz[\s\S]*s_startup_high_freq_hz[\s\S]*s_startup_freq_band[\s\S]*s_menu_cursor_icon' 'UI save path does not preserve V2 frequency state'
Assert-Contains $sysCore 'Ui_Controller_Apply_Settings\([\s\S]*low_freq_hz[\s\S]*high_freq_hz[\s\S]*freq_band[\s\S]*cursor_icon' 'Sys_Core does not forward loaded V2 state to UI'
Assert-Contains $pwmH 'PWM_DRIVER_FREQ_MIN_HZ\s+20000' 'PWM lower limit is not 20kHz'
Assert-Contains $pwmH 'PWM_DRIVER_FREQ_MAX_HZ\s+200000' 'PWM upper limit is not 200kHz'
Assert-Contains $invH 'Inverter_Control_Configure_Startup' 'Missing startup band configuration interface'
Assert-Contains $invH 'Inverter_Control_Get_Sweep_Start_Freq' 'Missing sweep start query interface'
Assert-Contains $invH 'Inverter_Control_Get_Sweep_Target_Freq' 'Missing sweep target query interface'
Assert-Contains $invC 's_sweep_start_freq\s*=\s*99900U;' 'Low-band sweep start is not 99.9kHz'
Assert-Contains $invC 's_sweep_step_hz\s*=\s*100U;' 'Low-band sweep step is not 100Hz'
Assert-Contains $invC 's_sweep_start_freq\s*=\s*200000U;' 'High-band sweep start is not 200kHz'
Assert-Contains $invC 's_sweep_step_hz\s*=\s*1000U;' 'High-band sweep step is not 1kHz'
Assert-Contains $sysCore 'Inverter_Control_Configure_Startup\([\s\S]*s_sys_config\.startup_low_freq_hz[\s\S]*s_sys_config\.startup_high_freq_hz' 'Sys_Core does not inject stored startup frequency state'
Assert-Contains $uiC 'Inverter_Control_Get_Sweep_Start_Freq' 'UI does not read the dynamic sweep start frequency'
Assert-Contains $uiC 'Inverter_Control_Get_Sweep_Target_Freq' 'UI does not read the dynamic sweep target frequency'
Assert-Contains $uiC 'start_freq\s*<=\s*target_freq' 'UI does not handle a zero-length sweep safely'
Assert-NotContains $uiC 'SOFTSTART_(START|TARGET)_FREQ_HZ|SOFTSTART_STEP_HZ' 'UI still references removed fixed sweep macros'
Assert-Contains $uiC 'UI_PAGE_SETTING_FREQUENCY' 'Missing startup frequency page'
Assert-Contains $uiC 's_startup_low_freq_hz' 'UI missing low-band edit state'
Assert-Contains $uiC 's_startup_high_freq_hz' 'UI missing high-band edit state'
Assert-Contains $uiC 's_frequency_edit_band' 'UI missing active frequency edit band'
Assert-Contains $uiC 's_frequency_edit_value' 'UI missing frequency edit copy'
Assert-Contains $uiC 's_frequency_editing' 'UI missing frequency edit state flag'
Assert-Contains $uiC 's_menu_cursor_icon' 'UI missing global cursor state'
Assert-Contains $uiC 's_cursor_icon_ids\[8\]' 'Cursor icon choices are not fixed to eight entries'
Assert-CursorIconNamesMatchCandidates $uiC
Assert-Contains $uiC 'Ui_Controller_Draw_Menu_Cursor' 'Menus do not use the unified cursor renderer'
Assert-NotContains $uiC 'Sys_Timer_Delay_Ms' 'Settings UI must not block with runtime delays'
Assert-Contains $uiC 'k1\s*==\s*KEY_DRIVER_EVENT_DOUBLE_CLICK[\s\S]{0,300}if\s*\(s_settings_dirty\)\s*\{[\s\S]{0,100}Ui_Controller_Save_Settings' 'Settings double-click exit does not save confirmed changes'
Assert-NotContains $uiC '#if\s+0' 'Legacy icon browser dead code remains'
Write-Host 'Settings configuration contract passed'
