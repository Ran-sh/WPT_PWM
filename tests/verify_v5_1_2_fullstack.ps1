$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$script:Passed = 0
$script:Failed = 0

function Assert-True([bool]$Condition, [string]$Name) {
    if ($Condition) {
        Write-Host "PASS $Name" -ForegroundColor Green
        $script:Passed++
    } else {
        Write-Host "FAIL $Name" -ForegroundColor Red
        $script:Failed++
    }
}

function Read-Utf8([string]$RelativePath) {
    return [IO.File]::ReadAllText((Join-Path $Root $RelativePath), [Text.Encoding]::UTF8)
}

foreach ($relative in @('Claude_Files/tools/start_bridge.ps1', 'Claude_Files/tools/stop_bridge.ps1')) {
    $path = Join-Path $Root $relative
    $tokens = $null
    $errors = $null
    [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors) | Out-Null
    Assert-True ($errors.Count -eq 0) "$relative parses in Windows PowerShell"
    $bytes = [IO.File]::ReadAllBytes($path)
    Assert-True ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) "$relative has UTF-8 BOM"
}

$package = Get-Content -Raw -Encoding UTF8 (Join-Path $Root '安卓app/server/package.json') | ConvertFrom-Json
Assert-True ($package.scripts.start -eq 'node bridge.mjs') 'bridge package start path is valid from its own directory'
Assert-True ($package.scripts.test -eq 'node --test ../../tests/bridge-core.test.mjs') 'bridge package exposes deterministic tests'

$arduino = Read-Utf8 'Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino'
Assert-True ($arduino -match '#define\s+FREQ_MIN_HZ\s+20000') 'ESP8266 accepts 20kHz minimum'
Assert-True ($arduino -match '#define\s+FREQ_MAX_HZ\s+200000') 'ESP8266 accepts 200kHz maximum'
Assert-True ($arduino -match '#define\s+PUBLIC_MQTT_ENABLED\s+0') 'public MQTT is disabled by default'
Assert-True ($arduino -match 'Mqtt_Task_Quantize_Frequency') 'ESP8266 uses dual-band frequency quantization'
Assert-True ($arduino -match 'Serial_Parse_Is_Overflowed') 'ESP8266 drops complete oversized serial frames'
Assert-True ($arduino -match 'V5\.1\.2') 'ESP8266 version is synchronized'

$miniControl = Read-Utf8 '安卓app/pages/control/control.js'
Assert-True ($miniControl -match '_startPolling') 'control page centralizes polling restart'
$miniMonitoring = Read-Utf8 '安卓app/pages/monitoring/monitoring.js'
Assert-True ($miniMonitoring -match '_startPolling') 'monitoring page centralizes polling restart'

$burn = Read-Utf8 'ch341/burn_flash.py'
Assert-True ($burn -match 'VERIFY_LEN\s*=\s*FONT_SIZE') 'font burner verifies the whole 2MB partition'
Assert-True ($burn -match 'backup_\{timestamp\}_16MB\.bin') 'font burner creates a fresh timestamped backup'
Assert-True ($burn -match 'len\(verify_data\)\s*!=\s*CHIP_SIZE') 'font burner rejects short readback files'
$font = Read-Utf8 'ch341/generate_font.py'
Assert-True ($font -match 'FONT_DATA_BIN\s*=\s*os\.path\.join\(SCRIPT_DIR') 'font generator output is anchored to its script directory'

$ignore = Read-Utf8 '.gitignore'
Assert-True ($ignore -match '/安卓app/server/node_modules/') 'Android bridge dependencies are ignored for future installs'

$allC = Get-ChildItem (Join-Path $Root 'Keil_Project') -Recurse -Filter '*.c' |
    Where-Object { $_.FullName -notmatch '[\\/](Library|Start)[\\/]' }
$badC = @($allC | Where-Object { ([IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)) -notmatch 'V5\.1\.2' })
Assert-True ($badC.Count -eq 0) 'all STM32 C headers use V5.1.2'

$readme = Read-Utf8 'README.md'
Assert-True ($readme -match '20,?000-200,?000|20000-200000|20–200kHz') 'README documents the 20-200kHz contract'
Assert-True ($readme -notmatch '95000-150000') 'README no longer exposes the obsolete cloud frequency range'

Write-Host "Full-stack checks: $script:Passed passed, $script:Failed failed"
if ($script:Failed -ne 0) { exit 1 }

