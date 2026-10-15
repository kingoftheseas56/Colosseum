# tests/test_background_activity.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $PSScriptRoot "background_activity_section_harness.qml"

# Verdict rides the exit code (Qt.exit(0) pass / non-zero fail). qml.exe console
# output is not reliably capturable here, so we never grep its stdout for a sentinel.
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "section harness failed (exit $LASTEXITCODE):`n$out" }

# Wiring contracts (shape, not behavior — pixels are Hemanth's eyes).
$section = Get-Content (Join-Path $root "qml\BackgroundActivitySection.qml") -Raw
if ($section -notlike "*requestPause(modelData.id)*") { throw "Pause control must call requestPause with the row id" }
if ($section -notlike "*requestResume(modelData.id)*") { throw "Resume control must call requestResume with the row id" }

$dl = Get-Content (Join-Path $root "qml\DownloadsPage.qml") -Raw
if ($dl -notlike "*BackgroundActivitySection*") { throw "DownloadsPage must embed BackgroundActivitySection" }
if ($dl -notlike "*typeof BackgroundActivity*") { throw "DownloadsPage must guard the context property for harness loads" }

Write-Host "TEST_BACKGROUND_ACTIVITY_OK"
