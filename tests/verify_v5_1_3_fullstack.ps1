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

foreach ($relative in @('tools/start_bridge.ps1', 'tools/stop_bridge.ps1')) {
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
$bridgeStart = Read-Utf8 'tools/start_bridge.ps1'
Assert-True ($bridgeStart -match "'\.\.\\安卓app\\server'" -and $bridgeStart -notmatch "\.\.\\\.\.\\安卓app") 'bridge launcher resolves the server from the migrated tools directory'

$arduino = Read-Utf8 'Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino'
Assert-True ($arduino -match '#define\s+FREQ_MIN_HZ\s+20000') 'ESP8266 accepts 20kHz minimum'
Assert-True ($arduino -match '#define\s+FREQ_MAX_HZ\s+200000') 'ESP8266 accepts 200kHz maximum'
Assert-True ($arduino -match '#define\s+PUBLIC_MQTT_ENABLED\s+0') 'public MQTT is disabled by default'
Assert-True ($arduino -match 'Mqtt_Task_Quantize_Frequency') 'ESP8266 uses dual-band frequency quantization'
Assert-True ($arduino -match 's_serial_overflowed') 'ESP8266 drops complete oversized serial frames with a module-scoped flag'
Assert-True ($arduino -match 'V5\.1\.3') 'ESP8266 version is synchronized'

$miniControl = Read-Utf8 '安卓app/pages/control/control.js'
Assert-True ($miniControl -match '_startPolling') 'control page centralizes polling restart'
$miniMonitoring = Read-Utf8 '安卓app/pages/monitoring/monitoring.js'
Assert-True ($miniMonitoring -match '_startPolling') 'monitoring page centralizes polling restart'

$burn = Read-Utf8 'ch341/burn_flash.py'
Assert-True ($burn -match 'VERIFY_LEN\s*=\s*FONT_SIZE') 'font burner verifies the whole 2MB partition'
Assert-True ($burn -match 'backup_\{[A-Za-z_]+\}_16MB\.bin') 'font burner creates a fresh timestamped backup'
Assert-True ($burn -match 'len\(verify_data\)\s*!=\s*CHIP_SIZE') 'font burner rejects short readback files'
Assert-True ($burn -notmatch '\bassert\s+compute_crc32') 'font burner CRC self-test remains active under Python optimization'
$font = Read-Utf8 'ch341/generate_font.py'
Assert-True ($font -match 'FONT_DATA_BIN\s*=\s*os\.path\.join\(SCRIPT_DIR') 'font generator output is anchored to its script directory'

$ignore = Read-Utf8 '.gitignore'
Assert-True ($ignore -match '/安卓app/server/node_modules/') 'Android bridge dependencies are ignored for future installs'

$keilCleanup = Read-Utf8 'Keil_Project/keilkill.bat'
Assert-True ($keilCleanup -match '%~dp0' -and $keilCleanup -match 'Objects' -and $keilCleanup -match 'Listings') 'Keil cleanup is anchored to its own build directories'
Assert-True ($keilCleanup -notmatch '(?m)^\s*del\s+\*\.[^\r\n]*\s+/s\s*$') 'Keil cleanup has no current-directory recursive delete'
Assert-True ($keilCleanup -match '(?:^|\s)_ia(?:\s|$)') 'Keil cleanup removes ARMCC _ia analysis artifacts'

$allC = Get-ChildItem (Join-Path $Root 'Keil_Project') -Recurse -Filter '*.c' |
    Where-Object { $_.FullName -notmatch '[\\/](Library|Start)[\\/]' }
$badC = @($allC | Where-Object { ([IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)) -notmatch 'V5\.1\.3' })
Assert-True ($badC.Count -eq 0) 'all STM32 C headers use V5.1.3'

$appNetwork = Read-Utf8 'Keil_Project/User/App_Network.c'
$retryBody = [regex]::Match(
    $appNetwork,
    '(?s)static uint32_t App_Network_Get_Retry_Timeout\(void\)\s*\{(?<body>.*?)\n\}'
).Groups['body'].Value
Assert-True ($retryBody -match 's_retry_count\s*<\s*3[^;]*return\s+5000') 'network retry uses 5s for the first three attempts'
Assert-True ($retryBody -match 'return\s+15000' -and $retryBody -notmatch '30000|60000|120000|300000|1800000') 'network retry removes unreachable timeout tiers'

$readme = Read-Utf8 'README.md'
Assert-True ($readme -match '20,?000-200,?000|20000-200000|20–200kHz') 'README documents the 20-200kHz contract'
Assert-True ($readme -notmatch '95000-150000') 'README no longer exposes the obsolete cloud frequency range'

Write-Host "Full-stack checks: $script:Passed passed, $script:Failed failed"
if ($script:Failed -ne 0) { exit 1 }
