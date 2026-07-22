$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$storageH = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.h"
$storageC = Get-Content -Raw -Encoding UTF8 "$repo/Keil_Project/User/App_Storage.c"

function Assert-Contains([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

Assert-Contains $storageH '#define CFG_VERSION\s+2U' 'Configuration version must upgrade to 2'
Assert-Contains $storageH 'uint32_t\s+startup_low_freq_hz;' 'Missing low-band frequency field'
Assert-Contains $storageH 'uint32_t\s+startup_high_freq_hz;' 'Missing high-band frequency field'
Assert-Contains $storageH 'uint8_t\s+startup_freq_band;' 'Missing selected frequency band field'
Assert-Contains $storageH 'uint8_t\s+menu_cursor_icon;' 'Missing global cursor field'
Assert-Contains $storageC 'App_Storage_Config_V1' 'Missing V1 migration structure'
Assert-Contains $storageC 'App_Storage_Migrate_V1' 'Missing V1 migration function'
Write-Host 'Settings configuration contract passed'
