param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'

$shell = Get-Content (Join-Path $Root 'qml/reader2/ReaderShell.qml') -Raw
$logic = Get-Content (Join-Path $Root 'qml/reader2/Reader2Logic.js') -Raw
$stores = Get-Content (Join-Path $Root 'native/reader/BookStores.cpp') -Raw
$nativeCmake = Get-Content (Join-Path $Root 'native/CMakeLists.txt') -Raw
$testsCmake = Get-Content (Join-Path $Root 'tests/CMakeLists.txt') -Raw
$fail = @()

function Require([bool]$ok, [string]$message) {
    if (-not $ok) { $script:fail += $message }
}

Require ($logic -match 'function\s+progressSaveContext\s*\(') `
    'Reader2Logic is missing progressSaveContext()'
Require ($shell -match 'property var pendingSaveContext:\s*null') `
    'ReaderShell is missing pendingSaveContext'
Require ($shell -notmatch 'pendingSaveId|pendingSaveBookPath') `
    'parallel pending-save identity fields remain'
Require ($shell -match 'L\.progressSaveContext\(shell\.bookId,\s*shell\.bookPath,\s*shell\.bookMeta\)') `
    'relocated does not snapshot the producing book'
Require ($shell -match 'var ctx = shell\.pendingSaveContext') `
    'flushProgressSave does not consume the pending context'
Require ($shell -match 'Reader2Bridge\.progressSave\(ctx\.bookId,\s*L\.progressRecord\(prev,\s*pend,\s*ctx\.bookPath\)\)') `
    'Reader2 progress write is not owned by the snapshot'
Require ($shell -match 'var m = ctx\.bookMeta \|\| \(\{\}\)') `
    'Continue metadata is not read from the snapshot'
Require ($shell -notmatch 'var m = shell\.bookMeta \|\| \(\{\}\)') `
    'flushProgressSave still reads live bookMeta'
Require ($shell -match '"caption":\s*m\.title \|\| "Book"') `
    'Continue caption does not use the snapshot-only fallback'
Require ($shell -match '"title":\s*m\.title \|\| "Book"') `
    'Continue title does not use the snapshot-only fallback'
Require ($shell -notmatch '"caption":\s*m\.title \|\| shell\.bookTitle|"title":\s*m\.title \|\| shell\.bookTitle') `
    'Continue identity still falls back to live bookTitle'
Require ($shell -match '"resume":\s*\{\s*"path":\s*ctx\.bookPath,\s*"book":\s*m\s*\}') `
    'Continue resume path is not owned by the snapshot'

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
