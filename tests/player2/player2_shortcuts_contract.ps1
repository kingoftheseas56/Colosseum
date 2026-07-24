# Behavioural + drift gate for the shortcuts sheet. Two parts:
#   1. Drives Player2Shortcuts.js headless through qml.exe offscreen (catalog well-formed; every visible
#      glyph maps 1:1 to a coveredQtKeys() token).
#   2. The DRIFT GUARD: extracts every `Qt.Key_X` handled in Player2Shell.qml's Keys.onPressed switch and
#      asserts it equals coveredQtKeys() in Player2Shortcuts.js — BIDIRECTIONALLY. A binding cannot be
#      added to (or removed from) the player without the sheet's map moving with it, or this fails.
# The verdict rides the harness exit code (console output does not flush before Qt.exit). Self-contained:
# it puts the Qt runtime on PATH itself, valid standalone and under ctest.
# Run: powershell -NoProfile -File tests/player2/player2_shortcuts_contract.ps1

$ErrorActionPreference = 'Stop'
$harness  = Join-Path $PSScriptRoot 'player2_shortcuts_harness.qml'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '../..')
$shell    = Join-Path $repoRoot 'qml/player2/Player2Shell.qml'
$catalog  = Join-Path $repoRoot 'qml/player2/controls/Player2Shortcuts.js'
foreach ($f in @($harness, $shell, $catalog)) {
    if (-not (Test-Path $f)) { throw "shortcuts contract input missing: $f" }
}

# --- Part 1: locate qml.exe and run the catalog logic harness ---
$qmlCandidates = @()
if ($env:QTDIR)   { $qmlCandidates += (Join-Path $env:QTDIR 'bin/qml.exe') }
if ($env:QT_ROOT) { $qmlCandidates += (Join-Path $env:QT_ROOT 'bin/qml.exe') }
$qmlCandidates += 'C:/Qt/6.11.1/msvc2022_64/bin/qml.exe'
$qml = $qmlCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $qml) {
    $onPath = Get-Command qml.exe -ErrorAction SilentlyContinue
    if ($onPath) { $qml = $onPath.Source }
}
if (-not $qml) { throw "qml.exe not found (set QTDIR or install Qt 6.11.1 msvc2022_64)" }

$qtBin = Split-Path -Parent $qml
$env:PATH = "$qtBin;$env:PATH"
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'

& $qml -platform offscreen $harness
if ($LASTEXITCODE -ne 0) {
    throw "player2_shortcuts_logic: FAIL (harness exit $LASTEXITCODE - catalog malformed or glyph/token drift)"
}

# --- Part 2: bidirectional drift guard between the shell switch and coveredQtKeys() ---
# Keys handled by the shell: every `Qt.Key_X` inside Player2Shell.qml.
$shellText = Get-Content -Raw $shell
$shellKeys = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in [regex]::Matches($shellText, 'Qt\.Key_(\w+)')) { [void]$shellKeys.Add($m.Groups[1].Value) }
if ($shellKeys.Count -eq 0) { throw "shortcuts drift guard: no Qt.Key_ cases found in Player2Shell.qml (schema drift?)" }

# Keys the catalog claims to cover: the string literals inside coveredQtKeys()'s return array.
$catText = Get-Content -Raw $catalog
$covMatch = [regex]::Match($catText, 'function\s+coveredQtKeys\s*\(\s*\)\s*\{\s*return\s*\[([^\]]*)\]')
if (-not $covMatch.Success) { throw "shortcuts drift guard: coveredQtKeys() not found in Player2Shortcuts.js" }
$coveredKeys = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in [regex]::Matches($covMatch.Groups[1].Value, '"([^"]+)"')) { [void]$coveredKeys.Add($m.Groups[1].Value) }

$inShellNotCatalog = @($shellKeys | Where-Object { -not $coveredKeys.Contains($_) })
$inCatalogNotShell = @($coveredKeys | Where-Object { -not $shellKeys.Contains($_) })
if ($inShellNotCatalog.Count -gt 0) {
    throw "player2_shortcuts: FAIL - shell binds Qt.Key_$($inShellNotCatalog -join ', Qt.Key_') but the sheet does not document it"
}
if ($inCatalogNotShell.Count -gt 0) {
    throw "player2_shortcuts: FAIL - the sheet documents Qt.Key_$($inCatalogNotShell -join ', Qt.Key_') but the shell binds nothing to it"
}

Write-Output "player2_shortcuts: PASS (catalog well-formed; $($shellKeys.Count) shell bindings match the sheet, no drift)"
