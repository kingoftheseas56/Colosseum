$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$liveHeaderPath = Join-Path $root "native/player/livestore.h"
$liveSourcePath = Join-Path $root "native/player/livestore.cpp"

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

if (!(Test-Path $liveHeaderPath)) {
    throw "Native LiveStore header must exist for process-backed DVR parity."
}
if (!(Test-Path $liveSourcePath)) {
    throw "Native LiveStore implementation must exist for process-backed DVR parity."
}

$liveHeader = Get-Content $liveHeaderPath -Raw
$liveSource = Get-Content $liveSourcePath -Raw

# Harbor parity P0: DVR cannot be just UI state; it needs a recorder process, output file, progress, and reveal.
Assert-Contains $liveHeader "Q_PROPERTY(QString defaultRecordingDir" `
    "LiveStore must expose the default DVR output directory to QML."
Assert-Contains $liveHeader "Q_INVOKABLE void revealRecording" `
    "LiveStore must expose an OS reveal action for completed recordings."
Assert-Contains $liveHeader "QProcess" `
    "LiveStore must own native recorder processes."
Assert-Contains $liveHeader "QTimer" `
    "LiveStore must own a progress timer for recording sessions."
Assert-Contains $liveHeader "QHash<QString, QProcess *>" `
    "LiveStore must track active recorder processes by session id."
Assert-Contains $liveSource "locateRecorder" `
    "LiveStore must locate a native recorder executable."
Assert-Matches $liveSource "mpv(\\.exe)?|ffmpeg(\\.exe)?" `
    "LiveStore must use a real media recorder executable."
Assert-Contains $liveSource ".ts" `
    "LiveStore must write DVR recordings as transport streams like Harbor."
Assert-Contains $liveSource "QStandardPaths::MoviesLocation" `
    "LiveStore default directory must prefer the user's Videos/Movies folder."
Assert-Contains $liveSource "startProgressTimer" `
    "LiveStore must start progress tracking while sessions are recording."
Assert-Contains $liveSource "updateRecordingProgress" `
    "LiveStore must update elapsed time and bytes written during recording."
Assert-Contains $liveSource "bytesWritten" `
    "LiveStore sessions must update bytesWritten from the output file."
Assert-Contains $liveSource "QProcess::finished" `
    "LiveStore must finalize sessions when the recorder exits."
Assert-Contains $liveSource "QStringLiteral(`"state`"), QStringLiteral(`"done`")" `
    "LiveStore must finalize successful recordings as done."
Assert-Contains $liveSource "QStringLiteral(`"state`"), QStringLiteral(`"error`")" `
    "LiveStore must finalize failed recordings as error."
Assert-Contains $liveSource "QDesktopServices::openUrl" `
    "LiveStore must reveal recording output through the OS file browser."

Assert-Contains $player "Live.defaultRecordingDir" `
    "PlayerPage DVR panel must show/use the native default recording directory."
Assert-Contains $player "Live.revealRecording" `
    "PlayerPage DVR panel must expose reveal for completed recordings."
Assert-Contains $player "outputPath" `
    "PlayerPage DVR rows must surface output paths."
Assert-Contains $player "bytesWritten" `
    "PlayerPage DVR rows must surface recorded byte progress."
Assert-Contains $player "Reveal" `
    "PlayerPage DVR rows must include a reveal action."
Assert-Contains $player "modelData.error" `
    "PlayerPage DVR rows must display native recorder errors."

Write-Host "Player DVR backend parity contract checks passed."
