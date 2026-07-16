# test_comic_series_notavailable.ps1 — the "Not available from sources yet" banner must never
# paint over a DB-backed series (the Avatar The Promise report), and MUST still show for a
# genuinely no-source series. Two headless QtQuick harnesses; verdict rides the exit code.
$ErrorActionPreference = "Stop"
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

foreach ($h in @("comic_series_notavailable_race_harness.qml",
                 "comic_series_notavailable_honest_harness.qml")) {
    $harness = Join-Path $PSScriptRoot $h
    $out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$h failed (exit $LASTEXITCODE):`n$out" }
    Write-Host "  PASS $h"
}
Write-Host "test_comic_series_notavailable PASS"
