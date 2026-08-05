# Comic Reader — RESUME RACE gate (2026-08 fix: Minimize/Continue-reading always landed on page 1).
#
# Drives qml/comicreader/ComicReaderShell.qml offscreen through comicreader_resume_race_harness.qml
# and asserts the fix for a confirmed, reproduced regression: reopening a Long Strip book (via
# Minimize->taskbar OR a plain Continue-reading open — no Sessions/Main.qml code needed to trigger
# it) always landed on page 1, and stayed that way, because the strip surface's mount-time
# "I'm showing page 1" report raced the resume door and — once — permanently overwrote the correct
# saved position. See the harness header for the full mechanism and the four fixes it pins.

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_resume_race_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_RESUME_RACE_OK")) {
    Write-Host "FAIL: comic reader resume-race offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_RESUME_RACE_OK"
