$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$downloadHeaderPath = Join-Path $root "native/player/downloadstore.h"
$downloadSourcePath = Join-Path $root "native/player/downloadstore.cpp"

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

if (!(Test-Path $downloadHeaderPath)) {
    throw "Native DownloadStore header must exist for Harbor-style player download parity."
}
if (!(Test-Path $downloadSourcePath)) {
    throw "Native DownloadStore implementation must exist for Harbor-style player download parity."
}

$downloadHeader = Get-Content $downloadHeaderPath -Raw
$downloadSource = Get-Content $downloadSourcePath -Raw

# Harbor parity P0: player download needs native progress/cancel/reveal state, not just a dead icon.
Assert-Contains $cmake "player/downloadstore.cpp" `
    "CMake must compile DownloadStore."
Assert-Contains $cmake "player/downloadstore.h" `
    "CMake must include DownloadStore."
Assert-Contains $main '#include "player/downloadstore.h"' `
    "main.cpp must include DownloadStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("Download")' `
    "main.cpp must expose Download to QML."

Assert-Matches $downloadHeader "class\s+DownloadStore\s*:\s*public\s+QObject" `
    "DownloadStore must be a QObject QML-facing state object."
Assert-Contains $downloadHeader "Q_PROPERTY(QVariantMap status" `
    "DownloadStore must expose Harbor-like status state."
Assert-Contains $downloadHeader "Q_PROPERTY(QString defaultDownloadDir" `
    "DownloadStore must expose the default output folder."
Assert-Contains $downloadHeader "Q_INVOKABLE void startDownload" `
    "DownloadStore must expose startDownload."
Assert-Contains $downloadHeader "Q_INVOKABLE void cancelDownload" `
    "DownloadStore must expose cancelDownload."
Assert-Contains $downloadHeader "Q_INVOKABLE void revealDownload" `
    "DownloadStore must expose revealDownload."
Assert-Contains $downloadHeader "Q_INVOKABLE void resetDownload" `
    "DownloadStore must expose resetDownload."
Assert-Contains $downloadHeader "QNetworkAccessManager" `
    "DownloadStore must use Qt network download plumbing."
Assert-Contains $downloadSource "QNetworkReply::downloadProgress" `
    "DownloadStore must update progress from QNetworkReply."
Assert-Contains $downloadSource ".part" `
    "DownloadStore must write partial files before finalizing."
Assert-Contains $downloadSource "receivedBytes" `
    "DownloadStore status must include receivedBytes."
Assert-Contains $downloadSource "totalBytes" `
    "DownloadStore status must include totalBytes."
Assert-Contains $downloadSource "ratio" `
    "DownloadStore status must include ratio."
Assert-Contains $downloadSource "QStringLiteral(`"preparing`")" `
    "DownloadStore must expose preparing state."
Assert-Contains $downloadSource "QStringLiteral(`"downloading`")" `
    "DownloadStore must expose downloading state."
Assert-Contains $downloadSource "QStringLiteral(`"done`")" `
    "DownloadStore must expose done state."
Assert-Contains $downloadSource "QStringLiteral(`"error`")" `
    "DownloadStore must expose error state."
Assert-Contains $downloadSource "QDesktopServices::openUrl" `
    "DownloadStore must reveal completed downloads through the OS file browser."

Assert-Contains $player "function startVideoDownload" `
    "PlayerPage must expose a player download start action."
Assert-Contains $player "Download.startDownload" `
    "PlayerPage must call native Download.startDownload."
Assert-Contains $player "Download.cancelDownload" `
    "PlayerPage must call native Download.cancelDownload."
Assert-Contains $player "Download.revealDownload" `
    "PlayerPage must call native Download.revealDownload."
Assert-Contains $player "Download.resetDownload" `
    "PlayerPage must call native Download.resetDownload."
Assert-Contains $player "Download.status.kind" `
    "PlayerPage must react to Harbor-like download status."
Assert-Contains $player "downloadTooltip" `
    "PlayerPage must expose progress/error/download tooltips."
Assert-Contains $player "icon: root.downloadIcon()" `
    "PlayerPage transport controls must expose a download action."
Assert-Contains $player "Downloading" `
    "PlayerPage must surface downloading status."
Assert-Contains $player "Saved to" `
    "PlayerPage must surface completed download status."
Assert-Contains $player "Failed:" `
    "PlayerPage must surface failed download status."

Write-Host "Player download P0 parity contract checks passed."
