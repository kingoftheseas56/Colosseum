$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content (Join-Path $root "native/player/livestore.cpp") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

$startBody = [regex]::Match($source, '(?s)QString LiveStore::startRecording\(.*?(?=\nvoid LiveStore::stopRecording)').Value
$finishBody = [regex]::Match($source, '(?s)void LiveStore::finishRecording\(.*?(?=\nvoid LiveStore::markRecordingError)').Value
if ([string]::IsNullOrWhiteSpace($startBody) -or [string]::IsNullOrWhiteSpace($finishBody)) {
    throw "Could not isolate LiveStore interactive lifecycle functions."
}

Assert-NotContains $startBody "waitForStarted" `
    "startRecording must not synchronously wait for the recorder to start on the UI thread."
Assert-NotContains $finishBody "waitForFinished" `
    "finishRecording must not synchronously wait for a recorder after an interactive stop."
Assert-Contains $startBody "QProcess::started" `
    "startRecording must observe asynchronous recorder startup through QProcess::started."
Assert-Contains $finishBody "requestRecorderStop" `
    "finishRecording must hand process shutdown to the asynchronous stop path."
Assert-Contains $source "QTimer" `
    "LiveStore must use a bounded asynchronous kill fallback."

Write-Host "LiveStore non-blocking lifecycle contract checks passed."
