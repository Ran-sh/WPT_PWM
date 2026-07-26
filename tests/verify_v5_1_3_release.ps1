$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Version = 'V5.1.3'
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
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    return [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
}

$businessC = Get-ChildItem (Join-Path $Root 'Keil_Project') -Recurse -Filter '*.c' |
    Where-Object { $_.FullName -notmatch '[\\/](Library|Start)[\\/]' }
$badC = @($businessC | Where-Object {
    ([IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)) -notmatch [regex]::Escape($Version)
})
Assert-True ($badC.Count -eq 0) 'all STM32 business C headers use V5.1.3'

$tft = Read-Utf8 'Keil_Project/Hardware/Tft_Driver.c'
Assert-True ($tft -match 'Show_String\(7, 14, "V5\.1\.3"') 'TFT splash displays V5.1.3'

$arduino = Read-Utf8 'Arduino_Project/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino'
Assert-True ($arduino -match 'V5\.1\.3') 'ESP8266 firmware uses V5.1.3'
Assert-True ($arduino -match 'ESP8266_DEBUG_ENABLED\s+0') 'ESP8266 debug output defaults to off'
Assert-True ($arduino -notmatch 'Serial\.(?:print|println)\s*\(\s*"\[') 'ESP8266 protocol UART has no direct debug text'

$miniRoot = @(Get-ChildItem $Root -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'app.js')
})[0].FullName
$miniFiles = @(
    Get-ChildItem $miniRoot -Recurse -File -Include '*.js','*.wxml','*.wxss' |
        Where-Object { $_.FullName -notmatch '[\\/](server|node_modules|docs)[\\/]' }
)
$badMini = @($miniFiles | Where-Object {
    ([IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)) -notmatch [regex]::Escape($Version)
})
Assert-True ($badMini.Count -eq 0) 'all mini-program JS WXML WXSS files mark V5.1.3'

$bridgePackage = Get-Content -Raw -Encoding UTF8 (Join-Path $miniRoot 'server/package.json') | ConvertFrom-Json
Assert-True ($bridgePackage.version -eq '5.1.3') 'local bridge version is 5.1.3'

$font = Read-Utf8 'ch341/generate_font.py'
$burn = Read-Utf8 'ch341/burn_flash.py'
Assert-True ($font -match 'PROJECT_VER\s*=\s*"V5\.1\.3"') 'font generator follows V5.1.3'
Assert-True ($burn -match 'V5\.1\.3') 'font burner follows V5.1.3'

foreach ($file in @('README.md', 'CLAUDE.md', 'AGENTS.md')) {
    Assert-True ((Read-Utf8 $file) -match 'V5\.1\.3') "$file current version is V5.1.3"
}
$manuals = @(Get-ChildItem $Root -File -Filter '*.md' | Where-Object {
    $_.Name -notin @('README.md', 'CLAUDE.md', 'AGENTS.md') -and $_.Name -notlike 'task-*-report.md'
})
Assert-True ($manuals.Count -eq 1 -and ([IO.File]::ReadAllText($manuals[0].FullName, [Text.Encoding]::UTF8)) -match 'V5\.1\.3') 'root has one consolidated V5.1.3 manual'

Assert-True (Test-Path -LiteralPath (Join-Path $Root '.agents/skills/embedded-architect/SKILL.md')) 'project embedded skill path is fixed'
Assert-True ((Read-Utf8 '.agents/skills/embedded-architect/SKILL.md') -match 'V5\.1\.3') 'project embedded skill follows V5.1.3'
Assert-True (Test-Path -LiteralPath (Join-Path $Root 'NONFILE/README.md')) 'NONFILE has placement rules'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $Root 'Claude_Files'))) 'legacy Claude_Files directory is migrated'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $Root 'docs/superpowers'))) 'historical implementation plans are archived'
Assert-True (@(Get-ChildItem $Root -File -Filter 'task-*-report.md').Count -eq 0) 'task reports are not scattered in root'

Write-Host "V5.1.3 release checks: $script:Passed passed, $script:Failed failed"
if ($script:Failed -ne 0) { exit 1 }
