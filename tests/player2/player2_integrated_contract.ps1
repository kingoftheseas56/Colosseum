# Integrated contract (Task 17). Player 2 is wired into the production app behind an opt-in flag.
# This pins the SHAPE of that wiring - the things that would rot silently if someone edited one side
# and not the other. Grep-based (shape, not behaviour); the runtime proof is the smokes + eyes-on.
# Run: powershell -NoProfile -File tests/player2/player2_integrated_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$violations = @()

function Read-File($relative) {
    $path = Join-Path $root $relative
    if (-not (Test-Path $path)) { return $null }
    return Get-Content -Raw $path
}

$host_qml  = Read-File 'qml/player2host/ColosseumHostServices.qml'
$page_qml  = Read-File 'qml/player2host/Player2Page.qml'
$main_qml  = Read-File 'qml/Main.qml'
$cmake     = Read-File 'native/CMakeLists.txt'
$main_cpp  = Read-File 'native/main.cpp'
$seam_h    = Read-File 'native/player2/host/Player2HostServices.h'

foreach ($pair in @(@('qml/player2host/ColosseumHostServices.qml', $host_qml),
                    @('qml/player2host/Player2Page.qml', $page_qml),
                    @('qml/Main.qml', $main_qml),
                    @('native/CMakeLists.txt', $cmake),
                    @('native/main.cpp', $main_cpp),
                    @('native/player2/host/Player2HostServices.h', $seam_h))) {
    if ($null -eq $pair[1]) { $violations += "missing file: $($pair[0])" }
}
if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host 'PLAYER2 INTEGRATED CONTRACT: FAIL'
    exit 1
}

# 1. The production host must implement EVERY request on the C++ seam and emit EVERY signal. If the
#    seam grows a method and the production host does not, the lab would pass and production would
#    silently no-op - the exact drift this catches.
$requests = @('requestAdjacentEpisode', 'requestSeasonEpisodes', 'requestAlternateSources',
              'requestOnlineSubtitles', 'requestSkipSegments', 'requestDownload',
              'requestMetadata', 'reportProgress')
$signals  = @('adjacentEpisodeResolved', 'seasonEpisodesResolved', 'alternateSourcesResolved',
              'onlineSubtitlesResolved', 'skipSegmentsResolved', 'downloadStateChanged',
              'metadataResolved')
foreach ($name in $requests) {
    if ($seam_h -notmatch [regex]::Escape($name)) {
        $violations += "seam drift: '$name' is checked here but no longer on Player2HostServices.h"
    }
    if ($host_qml -notmatch "function\s+$([regex]::Escape($name))\s*\(") {
        $violations += "production host does not implement '$name' (the shell would call into nothing)"
    }
}
foreach ($name in $signals) {
    if ($host_qml -notmatch "signal\s+$([regex]::Escape($name))\s*\(") {
        $violations += "production host does not declare signal '$name' (requests would never resolve)"
    }
}

# 2. The facade must expose the WHOLE interface Main.qml drives a player with. A missing method here
#    is a runtime TypeError at the worst possible moment (mid-playback), not a build error.
$facade = @('playTorrent', 'playLocalFile', 'playRemoteUrl', 'stop', 'captureState',
            'restoreState', 'suspendForMinimize', 'resumeFromMinimize')
foreach ($name in $facade) {
    if ($page_qml -notmatch "function\s+$([regex]::Escape($name))\s*\(") {
        $violations += "Player2Page is missing '$name' - it must match PlayerPage's interface exactly"
    }
}
foreach ($name in @('backRequested', 'minimizeRequested', 'fullscreenRequested', 'closeRequested')) {
    if ($page_qml -notmatch "signal\s+$([regex]::Escape($name))\s*\(") {
        $violations += "Player2Page is missing signal '$name' - Main.qml connects to it on load"
    }
}

# 3. Opt-in only, and never routable into a binary that lacks the backend.
# NOTE: the option's description itself contains parentheses, so this must not stop at the first ')'.
if ($cmake -notmatch 'option\(COLOSSEUM_PLAYER2_IN_APP.*OFF\)') {
    $violations += 'COLOSSEUM_PLAYER2_IN_APP must exist and default to OFF (a stock build links nothing new)'
}
if ($main_cpp -notmatch '#ifdef COLOSSEUM_PLAYER2') {
    $violations += 'main.cpp must guard every Player 2 reference behind #ifdef COLOSSEUM_PLAYER2'
}
if ($main_qml -notmatch 'Player2Available\s*===\s*true') {
    $violations += 'Main.qml must gate the backend on Player2Available (build fact), not on the setting alone'
}

# 4. The single-line swap. If someone starts branching playerLayer.item.* per backend, the facade has
#    stopped earning its keep and the hot dispatch block is back in play for merge conflicts.
if ($main_qml -notmatch 'win\.usePlayer2\s*\?\s*"player2host/Player2Page\.qml"\s*:\s*"PlayerPage\.qml"') {
    $violations += 'Main.qml must select the backend by Loader source, not by branching the call sites'
}

# 5. No hot swap after the first frame. The restart handler must NOT route into the fallback path -
#    that would hand a running session to the other backend and put two clocks on one playback.
if ($main_qml -match 'function\s+handlePlayer2Restart[\s\S]{0,400}?handlePlayer2Fallback') {
    $violations += 'handlePlayer2Restart must not call the fallback path (no mid-session backend swap)'
}

# 6. The production host must live OUTSIDE qml/player2. That directory is the isolated player, and the
#    Task 14 orchestration contract forbids production surfaces inside it - the host is not the player.
if (Test-Path (Join-Path $root 'qml/player2/ColosseumHostServices.qml')) {
    $violations += 'the production host must not live in qml/player2 (it is the host, not the player)'
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host 'PLAYER2 INTEGRATED CONTRACT: FAIL'
    exit 1
}

Write-Host 'PLAYER2 INTEGRATED CONTRACT: PASS'
exit 0
