# Comic Reader - FULLSCREEN TRANSITION gate.
#
# Holds the invariant behind Hemanth's "going in and out of fullscreen looks incredibly rough" and
# his follow-up after a first, insufficient fix: "even after your fix, it remained shaky unlike the
# video player's fullscreen transition." His reference is PLAYER 2's transition, the smoothest in
# the app.
#
# THE INVARIANT: the reader's column reflow must land BEFORE the shared FullscreenTransitionShield
# lifts its cover, so the first frame shown at the new size is already settled. Player 2 satisfies
# this for free (one textured quad, letterboxes within the frame). The strip only satisfies it
# because its width report bypasses the 16ms viewport throttle - route it back through the throttle
# and the reflow lands 2-5 frames late, in full view. See the harness header for the measured
# ledger and the negative control.
#
# UNLIKE every sibling comicreader gate, this one CANNOT run -platform offscreen: an offscreen
# window never really resizes and never presents frames, so the ordering under test would not exist
# to observe. It opens a small real window for ~3 seconds and closes itself.
#
# This file is deliberately pure ASCII. The sibling gates carry a few non-ASCII characters in
# comments and survive, but PowerShell 5.1 reading a BOM-less UTF-8 script mis-frames multi-byte
# characters INSIDE quoted strings, which swallowed a closing quote here and produced a bogus
# "Missing closing '}'" parse error. Not worth re-learning.

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

# --- static assertion: the width path must NOT be routed through the throttle ---
# The behavioral gate below is the real test, but it needs a visible window and a compositor. This
# grep fails fast and unambiguously if someone reverts the one line that matters, even on a machine
# where the windowed run is skipped or flaky.
$stripQml = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderStripSurface.qml"
if (!(Test-Path -LiteralPath $stripQml)) {
    Write-Host "FAIL: ComicReaderStripSurface.qml not found at $stripQml"
    exit 1
}
$widthHandler = Select-String -LiteralPath $stripQml -Pattern "onWidthChanged\s*:"
if (!$widthHandler) {
    Write-Host "FAIL: ComicReaderStripSurface.qml has no onWidthChanged handler at all"
    exit 1
}
foreach ($h in $widthHandler) {
    if ($h.Line -notmatch "_flushViewportReportNow") {
        Write-Host "FAIL: the strip's onWidthChanged must report SYNCHRONOUSLY (_flushViewportReportNow)."
        Write-Host "      Deferring a width change by even one frame puts the reflow in front of the"
        Write-Host "      fullscreen cover instead of behind it - that is the shake Hemanth reported."
        Write-Host ("      line " + $h.LineNumber + ": " + $h.Line.Trim())
        exit 1
    }
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comicreader_fullscreen_timing_probe.qml"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_FULLSCREEN_OK")) {
    Write-Host "FAIL: comic reader fullscreen transition gate (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_FULLSCREEN_OK"
