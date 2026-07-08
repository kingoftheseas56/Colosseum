# HUD font registration gate. QML silently falls back (Tahoma) on an unknown family —
# no error, no warning — so a mis-bundled font (e.g. a variable TTF that registers as
# "<Name> Variable" instead of the plain name) ships as "the font isn't rendering".
# Runs HEADED on purpose: the offscreen platform's thin font db soft-matches near names
# and would hide exactly the fallback this gate exists to catch (probe-proven 2026-07-08).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

$harness = Join-Path $PSScriptRoot "hud_font_registration_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "HUD font registration gate failed (exit $LASTEXITCODE):`n$out" }

Write-Host "HUD font registration: OK"
