$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$mainCpp = Get-Content (Join-Path $root "native/main.cpp") -Raw
$sheet   = Get-Content (Join-Path $root "qml/SourcesSheet.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Native clipboard reach (QML alone has none).
Assert-Contains $mainCpp '#include "ClipboardHelper.h"' `
    "main.cpp must include the clipboard helper."
Assert-Contains $mainCpp 'setContextProperty(QStringLiteral("Clipboard")' `
    "main.cpp must expose Clipboard to QML."
if (!(Test-Path (Join-Path $root "native/ClipboardHelper.h"))) {
    throw "native/ClipboardHelper.h must exist."
}

# Row affordance: copy button builds the link via Magnet.js and ships it to the clipboard.
Assert-Contains $sheet 'import "Magnet.js" as Magnet' `
    "SourcesSheet must import the magnet builder."
Assert-Contains $sheet "Magnet.linkFor(row.modelData)" `
    "Copy button must build the row's link."
Assert-Contains $sheet "Clipboard.copy(link)" `
    "Copy button must put the link on the system clipboard."
Assert-Contains $sheet "copiedTick" `
    "Copy button must show a brief copied tick."

Write-Host "Copy-magnet contract checks passed."
