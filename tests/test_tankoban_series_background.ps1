$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$files = @(
    "qml/TheatreSeries.qml",
    "qml/ComicSeries.qml",
    "qml/MangaSeries.qml"
)
$tokens = @(
    'Rectangle { anchors.fill: parent; color: "#000000" }',
    'opacity: 0.5',
    'GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }',
    'GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }',
    'GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }'
)

foreach ($relativePath in $files) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
    foreach ($token in $tokens) {
        if (!$text.Contains($token)) {
            throw "$relativePath does not match Theatre's pitch-black background: $token"
        }
    }
}

Write-Host "tankoban series backgrounds: OK"
