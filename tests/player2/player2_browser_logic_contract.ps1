# Behavioural gate for the drawer's pure display derivations (Player2Browser.js). Drives the logic
# headless through qml.exe offscreen; the verdict rides the harness exit code (console output does not
# flush before Qt.exit). Self-contained: it puts the Qt (and ffmpeg) runtime on PATH itself, so it is
# valid both standalone and under ctest regardless of the caller's environment.
# Run: powershell -NoProfile -File tests/player2/player2_browser_logic_contract.ps1

$ErrorActionPreference = 'Stop'
$harness = Join-Path $PSScriptRoot 'player2_browser_logic_harness.qml'
if (-not (Test-Path $harness)) { throw "browser logic harness missing: $harness" }

# Locate qml.exe: explicit QTDIR/QT_ROOT first, then the known install, then PATH.
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

# Qt runtime on PATH; the license bypass keeps the offscreen run from stalling on the dev license.
$qtBin = Split-Path -Parent $qml
$env:PATH = "$qtBin;$env:PATH"
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'

& $qml -platform offscreen $harness
if ($LASTEXITCODE -ne 0) {
    throw "player2_browser_logic: FAIL (harness exit $LASTEXITCODE - a derivation returned the wrong state)"
}
Write-Output 'player2_browser_logic: PASS (row-state precedence, progress clamp, season pills upheld)'
