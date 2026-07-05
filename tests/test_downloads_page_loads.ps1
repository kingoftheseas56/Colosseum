# DownloadsPage is behind a LAZY Loader — boot smokes never instantiate it, so a
# creation-time QML error (e.g. a fractional literal on an int property, which
# qmllint does NOT catch) ships invisibly and the page simply refuses to open.
# This test actually instantiates the page headless and requires LOADER READY.
# Born 2026-07-05: font.pixelSize: 12.5 broke the page for a whole task arc.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$harness = Join-Path $PSScriptRoot "downloads_page_load_harness.qml"
$out = & $qmlExe $harness 2>&1 | Out-String

if ($out -notlike "*LOADER READY*") {
    throw "DownloadsPage failed to instantiate. Loader output:`n$out"
}

Write-Host "downloads page load: OK"
