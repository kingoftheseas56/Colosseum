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
$shell_qml = Read-File 'qml/player2/Player2Shell.qml'
$demux_cpp = Read-File 'native/player2/core/DemuxSession.cpp'
$http_cpp  = Read-File 'native/player2/network/HttpMediaSource.cpp'

foreach ($pair in @(@('qml/player2host/ColosseumHostServices.qml', $host_qml),
                    @('qml/player2host/Player2Page.qml', $page_qml),
                    @('qml/Main.qml', $main_qml),
                    @('native/CMakeLists.txt', $cmake),
                    @('native/main.cpp', $main_cpp),
                    @('native/player2/host/Player2HostServices.h', $seam_h),
                    @('qml/player2/Player2Shell.qml', $shell_qml),
                    @('native/player2/core/DemuxSession.cpp', $demux_cpp),
                    @('native/player2/network/HttpMediaSource.cpp', $http_cpp))) {
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

# Task 5: progress visibility is part of the seam, not an implementation detail. The shell's
# ordinary cadence must be able to request a silent write while lifecycle boundaries request a
# visible write, and the production host must preserve that distinction into Progress.
if ($seam_h -notmatch 'reportProgress\s*\(\s*const QString\s*&mediaId\s*,\s*double\s+position\s*,\s*double\s+duration\s*,\s*bool\s+silent\s*\)') {
    $violations += 'Player2HostServices::reportProgress must carry the four-argument (mediaId, position, duration, silent) contract'
}
if ($host_qml -notmatch 'function\s+reportProgress\s*\(\s*mediaId\s*,\s*position\s*,\s*duration\s*,\s*silent\s*\)[\s\S]{0,2500}Progress\.recordSilent\s*\([\s\S]{0,1200}Progress\.record\s*\(') {
    $violations += 'production host reportProgress must accept silent visibility'
}
if ($host_qml -notmatch 'function\s+reportProgress\s*\(\s*mediaId\s*,\s*position\s*,\s*duration\s*,\s*silent\s*\)[\s\S]{0,2500}if\s*\(\s*silent\s*\)[\s\S]{0,300}Progress\.recordSilent\s*\([\s\S]{0,1200}else\s*Progress\.record\s*\(') {
    $violations += 'production host reportProgress must branch silent writes to recordSilent and visible writes to record'
}
if ($shell_qml -notmatch 'function\s+reportProgress\s*\(\s*forceVisible\s*\)[\s\S]{0,1200}reportProgress\s*\([\s\S]{0,500}!forceVisible') {
    $violations += 'Player2Shell must pass !forceVisible as the four-argument host visibility flag'
}
if ($shell_qml -notmatch 'onTriggered\s*:\s*\{[\s\S]{0,250}shell\.reportProgress\s*\(\s*false\s*\)') {
    $violations += 'ordinary Player2 timer cadence must request a silent progress write'
}
if ($shell_qml -notmatch 'onPausedChanged\s*:\s*if\s*\(\s*shell\.paused\s*\)\s*reportProgress\s*\(\s*true\s*\)') {
    $violations += 'entering Paused must request a visible progress write'
}
if ($shell_qml -notmatch 'onBackRequested\s*:\s*\{[\s\S]{0,300}reportProgress\s*\(\s*true\s*\)[\s\S]{0,300}shell\.backRequested\s*\(\s*\)') {
    $violations += 'TopBar Back forwarding must report final visible progress before emitting shell.backRequested'
}
if ($shell_qml -notmatch 'onMinimizeRequested\s*:\s*\{[\s\S]{0,300}reportProgress\s*\(\s*true\s*\)[\s\S]{0,300}shell\.minimizeRequested\s*\(\s*\)') {
    $violations += 'TopBar Minimize forwarding must report final visible progress before emitting shell.minimizeRequested'
}
if ($shell_qml -notmatch 'onConfirmed\s*:\s*\{[\s\S]{0,400}reportProgress\s*\(\s*true\s*\)[\s\S]{0,300}shell\.closeRequested\s*\(\s*\)') {
    $violations += 'confirmed Close must report final visible progress before emitting closeRequested'
}
if ($shell_qml -notmatch 'function\s+onStateChanged\s*\([^)]*\)[\s\S]{0,300}Ended[\s\S]{0,300}reportProgress\s*\(\s*true\s*\)[\s\S]{0,500}activityNaturalEof\s*\(\s*\)') {
    $violations += 'Ended must report visible progress before retaining the activityNaturalEof path'
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
# TASK 18: the flag is opt-in and defaults OFF. What must still hold is that the OLD player is
# reachable, because the choice remains a BOOT choice both ways: one environment variable back to mpv.
if ($cmake -notmatch 'option\(COLOSSEUM_PLAYER2_IN_APP.*OFF\)') {
    $violations += 'COLOSSEUM_PLAYER2_IN_APP must exist and default OFF (Player 2 is discontinued/opt-in)'
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
# The single-line check above stops at the first line end under (?m), so a term wrapped onto a
# continuation line (`Player2Available === true` then `&& !win.someFlag` on the next line) passes it
# undetected. Catch that shape explicitly: nothing may be ANDed/ORed/ternaried onto the boot fact,
# same line or next.
if ($main_qml -match 'usePlayer2:\s*Player2Available\s*===\s*true\s*(\r?\n\s*)?(&&|\|\||\?)') {
    $violations += 'usePlayer2 must have no extra terms - nothing may be ANDed onto the boot fact'
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
if ($main_cpp -notmatch 'COLOSSEUM_PLAYER1') {
    $violations += 'the mpv escape hatch (COLOSSEUM_PLAYER1) must exist - the default flip was approved on the condition that the old player stays one boot away'
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

# 9. A PARKED NETWORK READ MUST STAY INTERRUPTIBLE (T2d). The demux thread is the only thread that
#    services transport commands, and it parks in three places: the audio queue, the video queue and
#    the network read. The first two were woken from the start; the third was not, so at the download
#    frontier a viewer's seek sat queued to a loop nobody was running until the source went terminal
#    (measured 2026-07-25: press seek, nothing, ~110 s, dead). Runtime proof is
#    player2_frontier_seek_probe.qml; this pins the wiring that probe depends on.
if ($demux_cpp -notmatch 'setInterruptPredicate') {
    $violations += 'DemuxSession must give HttpMediaSource an interrupt predicate - without it a parked read never learns a command is waiting'
}
if ($demux_cpp -notmatch 'wakeRead\(\)') {
    $violations += 'DemuxSession::enqueueCommand must wake a parked network read, the same way it interrupts the audio and video queues'
}
#    ONLY A REPOSITIONING COMMAND MAY ABANDON A READ (cross-model review, 2026-07-26). An aborted
#    read can leave the container's private sample cursor advanced, and avformat_flush does not undo
#    that - only a seek does. So a Pause or track swap that interrupted a read would corrupt the
#    stream with no repair behind it. The predicate must therefore be the reposition COUNT, never
#    the generic pending flag, and the wake must be gated the same way.
if ($demux_cpp -notmatch 'setInterruptPredicate[\s\S]{0,600}?m_pendingRepositions') {
    $violations += 'the interrupt predicate must be the reposition count, not m_commandPending - a non-seek command must never abandon a read (nothing would repair the container position)'
}
if ($demux_cpp -notmatch 'if\s*\(repositions\(command\.type\)\)\s*\{[\s\S]{0,300}?wakeRead\(\)') {
    $violations += 'the network-read wake must be gated on repositions(command.type)'
}
#    A SEEK THAT DID NOT HAPPEN MUST NOT BE REPORTED AS ONE. applySeek commits the reposition before
#    it seeks; ignoring av_seek_frame's result left the session promising a jump it never made -
#    frozen picture, engine still reading. Leading suspect for the two dead runs of 2026-07-26.
if ($demux_cpp -notmatch 'seekResult\s*=\s*av_seek_frame') {
    $violations += 'av_seek_frame''s result must be captured - a failed seek that is ignored freezes the picture while the engine reads on'
}
if ($demux_cpp -notmatch 'seekResult\s*<\s*0') {
    $violations += 'a failed av_seek_frame must be acted on, not just captured'
}
if ($demux_cpp -notmatch 'consumeReadInterrupt\(\)') {
    $violations += 'the demux read loop must ask consumeReadInterrupt() - otherwise an interrupted read is reported to the viewer as a decode failure'
}
#    THE LIVELOCK RULE, written down because it is invisible and load-bearing: the loop clears the
#    pending flag BEFORE it reads again, which is the only reason an interrupted read does not
#    immediately interrupt itself. Anyone who moves the clear after the read gets a spin, not a bug
#    report.
if ($demux_cpp -notmatch 'm_commandPending\.exchange\(false') {
    $violations += 'the demux loop must clear m_commandPending with exchange(false) before reading again - that is what stops the interrupt predicate from firing forever'
}
if ($http_cpp -notmatch 'm_interruptPredicate') {
    $violations += 'HttpMediaSource::read must consult the interrupt predicate - that wait IS the frontier stall'
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host 'PLAYER2 INTEGRATED CONTRACT: FAIL'
    exit 1
}

Write-Host 'PLAYER2 INTEGRATED CONTRACT: PASS'
exit 0
