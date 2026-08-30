param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'

$stores = Get-Content (Join-Path $Root 'native/reader/BookStores.cpp') -Raw
$nativeCmake = Get-Content (Join-Path $Root 'native/CMakeLists.txt') -Raw
$testsCmake = Get-Content (Join-Path $Root 'tests/CMakeLists.txt') -Raw
$fail = @()

function Require([bool]$ok, [string]$message) {
    if (-not $ok) { $script:fail += $message }
}

Require ($stores -match '#include <QSaveFile>') `
    'BookStores is not QSaveFile-backed'
Require ($stores -notmatch 'WriteOnly\s*\|\s*QIODevice::Truncate') `
    'BookStores still truncates live JSON in place'
Require ($stores -match 'f\.cancelWriting\(\)') `
    'BookStores does not cancel a short atomic write'
Require ($stores -match 'f\.commit\(\)') `
    'BookStores does not commit the atomic replacement'

foreach ($target in 'reader2_stores_harness', 'reader2_bridge_harness', 'reader2_autoattach_harness') {
    Require ($testsCmake -match ('colosseum_register_harness\(' + [regex]::Escape($target))) `
        "CTest is missing $target"
    Require ($nativeCmake -match ('add_custom_command\(TARGET\s+' + [regex]::Escape($target) + '\s+POST_BUILD')) `
        "$target does not stage its Windows Qt runtime"
}

Require ($testsCmake -match 'NAME colosseum\.reader2_logic') `
    'CTest is missing reader2_logic'
Require ($testsCmake -match 'NAME colosseum\.reader2_runtime_contract') `
    'CTest is missing reader2_runtime_contract'

if (Test-Path (Join-Path $Root 'tests/test_reader2_readalong.ps1')) {
    $fail += 'stale read-along wrapper still exists'
}

if ($fail.Count) {
    $fail | ForEach-Object { Write-Host "FAIL: $_" }
    exit 1
}

Write-Host 'test_reader2_runtime_p0: PASS'
