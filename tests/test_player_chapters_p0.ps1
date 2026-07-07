$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$mpvHeader = Get-Content (Join-Path $root "native/player/mpvitem.h") -Raw
$mpvSource = Get-Content (Join-Path $root "native/player/mpvitem.cpp") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

Assert-Contains $mpvHeader "Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)" `
    "MpvItem must expose mpv chapters to QML."
Assert-Contains $mpvHeader "QVariantList chapters() const" `
    "MpvItem must provide a chapters getter."
Assert-Contains $mpvHeader "void chaptersChanged();" `
    "MpvItem must notify QML when chapters change."
Assert-Contains $mpvSource 'observeProperty(QStringLiteral("chapter-list"), MPV_FORMAT_NODE)' `
    "MpvItem must observe mpv's chapter-list property."
Assert-Contains $mpvSource 'property == QLatin1String("chapter-list")' `
    "MpvItem must handle chapter-list updates."
Assert-Contains $mpvSource 'QStringLiteral("title")' `
    "Chapter entries must include a title field."
Assert-Contains $mpvSource 'QStringLiteral("startSec")' `
    "Chapter entries must include a startSec field."

Write-Host "Player chapter contract checks passed."
