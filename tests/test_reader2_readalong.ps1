# Offscreen-logic gate for the Reader2 audiobook read-along wiring (Task 6).
#
# Drives tests/reader2_readalong_harness.qml under qml.exe -platform offscreen and requires
# the READALONG_OK sentinel plus a clean exit code (the exit code IS the verdict — a thrown
# JS error would HANG the offscreen process, so the harness signals via Qt.exit(0/1)). This
# pins the load-bearing LOGIC: mode -> style, scrub preview-without-seek, exactly one commit
# on release, double-click -> commitLocation, manual nav -> detach, return -> following, the
# playback -> controller feed, and the DORMANT gate. The visual read-along (wash tracking,
# enlargement, real scrub feel) is Task 14 eyes-on.
#
# [Agent 2 (Claude), biblio]

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "reader2_readalong_harness.qml"

# qml.exe emits benign warnings (font dir) on stderr; don't let ErrorActionPreference=Stop
# turn a native-command stderr line into a terminating error before we read the verdict.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "READALONG_OK")) {
    Write-Host "FAIL: reader2 read-along offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "reader2 read-along: OK"
