$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$sub = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# The redesigned footer keeps Find/Load and compact Sync on the same baseline.
# No cross-parent anchor is allowed, and Sync stays right-aligned inside mainPane.

Assert-NotContains $sub "anchors.bottom: delayRow.top" `
    "Footer must not anchor to delayRow across parent boundaries."
Assert-Contains $sub "anchors.bottomMargin: 13" `
    "Footer and compact Sync must share the same bottom inset."
Assert-Contains $sub "id: delayRow" `
    "Subtitle menu must retain the compact sync control."
Assert-Contains $sub 'icon: "chevronLeft"' `
    "Compact sync must use a left SVG chevron."
Assert-Contains $sub 'icon: "chevronRight"' `
    "Compact sync must use a right SVG chevron."

Write-Host "Subtitle menu footer layout contract checks passed."
