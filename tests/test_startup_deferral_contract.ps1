$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$probe = Get-Content (Join-Path $root 'native/GuiStallProbe.h') -Raw
$nativeMain = Get-Content (Join-Path $root 'native/main.cpp') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main.Contains('id: postFrameStartupWork')) `
    'Startup backfill work must have a dedicated deferred timer.'
Need ($main.Contains('onFirstFrameReady')) `
    'Deferred startup work must begin from the native first-frame boundary.'
Need ($main.Contains('interval: 350')) `
    'Deferred startup work must allow a short post-frame settling window.'
Need ($main.Contains('win.runCollectionBackfill()')) `
    'Deferred timer must retain the Collection backfill operation.'
Need ($main.Contains('win.enrichBiblioCovers()')) `
    'Deferred timer must retain Biblio cover enrichment.'
Need ($probe.Contains('firstFrameReady')) `
    'The probe bridge must expose the first-frame signal to QML.'
Need ($nativeMain.Contains('guiStallProbe.notifyFirstFrame()')) `
    'Native startup must notify QML after the first frame is observed.'

$timer = $main.IndexOf('id: postFrameStartupWork')
$backfill = $main.IndexOf('win.runCollectionBackfill()', $timer)
$enrich = $main.IndexOf('win.enrichBiblioCovers()', $timer)
$completed = $main.IndexOf('Component.onCompleted:')
$completedBackfill = $main.IndexOf('win.runCollectionBackfill()', $completed)
$completedEnrich = $main.IndexOf('win.enrichBiblioCovers()', $completed)
Need ($completed -ge 0 -and $timer -ge 0 -and $backfill -gt $timer -and $enrich -gt $timer `
      -and ($completedBackfill -lt 0) -and ($completedEnrich -lt 0)) `
    'Backfill/enrichment calls must live in the deferred timer, not before it.'

Write-Host 'Startup deferral contract: PASS'
