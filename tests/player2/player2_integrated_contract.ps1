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
    $violations += 'Main.qml must gate the backend on Player2Available (the actual boot, not a saved setting)'
}

# 4. The single-line swap. If someone starts branching playerLayer.item.* per backend, the facade has
#    stopped earning its keep and the hot dispatch block is back in play for merge conflicts.
if ($main_qml -notmatch 'win\.usePlayer2\s*\?\s*"player2host/Player2Page\.qml"\s*:\s*"PlayerPage\.qml"') {
    $violations += 'Main.qml must select the backend by Loader source, not by branching the call sites'
}

# 5. The backend is a BOOT fact: usePlayer2 must be EXACTLY Player2Available, with no runtime term.
#    mpv cannot render on a D3D11 boot, so any runtime re-route is a black screen, not a fallback.
if ($main_qml -notmatch '(?m)^\s*readonly\s+property\s+bool\s+usePlayer2:\s*Player2Available\s*===\s*true\s*$') {
    $violations += 'usePlayer2 must bind to exactly `Player2Available === true` - no extra runtime terms'
}
if ($main_qml -match 'backend(Fallback|RestartRequired)\.connect[\s\S]{0,300}?(activateSession|playerLayer\.source|usePlayer2\s*=)') {
    $violations += 'a Player 2 failure handler must not re-route the player (no runtime backend swap)'
}
# And the layer below: nothing stops Player2Available reverting to a build-time-only truth (the
# original mismatch that made mpv take a playback it could never render, 2026-07-25).
if ($main_cpp -notmatch 'setContextProperty\(QStringLiteral\("Player2Available"\),\s*bootPlayer2\)') {
    $violations += 'Player2Available must report the ACTUAL boot (bootPlayer2), not the build flag alone'
}

# 6. The production host must live OUTSIDE qml/player2. That directory is the isolated player, and the
#    Task 14 orchestration contract forbids production surfaces inside it - the host is not the player.
if (Test-Path (Join-Path $root 'qml/player2/ColosseumHostServices.qml')) {
    $violations += 'the production host must not live in qml/player2 (it is the host, not the player)'
}

# 7. Boot-mode coherence (Task 1). The RHI is picked once per process before QGuiApplication
#    exists, so there is no legal runtime swap between Player 2 and mpv. These pin that the old
#    opt-in-setting-plus-runtime-fallback shape does not creep back in.
if ($main_qml -match 'playerBackendSettings') {
    $violations += 'qml/Main.qml must not reference playerBackendSettings - the backend is a boot fact, not a saved setting'
}
if ($main_qml -match 'player2FallbackActive') {
    $violations += 'qml/Main.qml must not reference player2FallbackActive - there is no runtime fallback in a Player 2 boot'
}

# 8. EVERY path that gives up on a playback (router decline, torrent failure, or a post-first-frame
#    death) must funnel into the one place that sets errorText - not just the two Connections handlers,
#    which is where finding 1 (2026-07-25 review) actually lived: _open()'s decline branch called
#    backendFallback directly and never touched errorText, leaving a black page with no message.
if ($page_qml -notmatch 'function\s+_failPlayback\s*\([^)]*\)\s*\{([\s\S]*?)\}') {
    $violations += 'Player2Page must implement a single _failPlayback(reason) funnel'
} elseif ($Matches[1] -notmatch 'errorText') {
    $violations += '_failPlayback must set page.errorText (that is the only thing that makes the error screen show)'
}
# Each anchored on its OWN function definition, not a bare name match - '_open' in particular is
# called from four other functions before it is ever defined, so a bare substring match would anchor
# on a call site and never find _failPlayback within range.
$callerDefs = @{
    'onFallbackRequested' = 'function\s+onFallbackRequested\s*\('
    'onStreamError'       = 'function\s+onStreamError\s*\('
    '_open'                = 'function\s+_open\s*\('
}
foreach ($caller in $callerDefs.Keys) {
    if ($page_qml -notmatch "$($callerDefs[$caller])[\s\S]{0,900}?_failPlayback\(") {
        $violations += "Player2Page's '$caller' must route through _failPlayback - a decline that skips it is a silent black screen"
    }
}
# onRestartRequired is the one legitimate exception (it emits backendRestartRequired, not
# backendFallback - there IS no runtime swap to route through _failPlayback for), but it must still
# set errorText itself so a post-first-frame death is not silent either.
if ($page_qml -notmatch 'function\s+onRestartRequired\s*\([^)]*\)\s*\{([\s\S]*?)\}') {
    $violations += 'Player2Page must implement onRestartRequired'
} elseif ($Matches[1] -notmatch 'errorText') {
    $violations += 'Player2Page onRestartRequired must set page.errorText (a post-first-frame failure must surface on the page)'
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host 'PLAYER2 INTEGRATED CONTRACT: FAIL'
    exit 1
}

Write-Host 'PLAYER2 INTEGRATED CONTRACT: PASS'
exit 0
