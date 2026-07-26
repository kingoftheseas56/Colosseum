# player2_engine_facade_contract.ps1 - every mpv-surface member PlayerPage actually uses must be
# declared by PlayerEngine.qml. Enumerated FROM PlayerPage each run, so a new mpv.* usage in
# PlayerPage automatically fails this contract until the facade answers it. Both directions of the
# drift are fatal: a missing member is a runtime TypeError mid-playback, not a build error.
# Run: powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# EVERY section below reads CODE, never comments. This is a text grep, so without the strip a
# facade whose declarations are all commented out passes with the text preserved verbatim - which
# is exactly what a cross-model review produced against this file on 2026-07-27. It matters most
# for PlayerEngineP2.qml, which Task 3 writes against this contract: a stub of comments must not
# read as a working branch. Stripping PlayerPage too is not just symmetry - a `mpv.x` that appears
# only inside a comment is not a usage and must not manufacture a requirement.
. (Join-Path $PSScriptRoot 'qml_lexer.ps1')

$page = Remove-QmlComments (Get-Content -Raw (Join-Path $root 'qml/PlayerPage.qml'))
$enginePath = Join-Path $root 'qml/PlayerEngine.qml'
if (-not (Test-Path $enginePath)) { Write-Host 'FACADE CONTRACT: FAIL (PlayerEngine.qml missing)'; exit 1 }
$engine = Remove-QmlComments (Get-Content -Raw $enginePath)

$violations = @()
$deferred = @()

function Test-Member($text, $m) {
    return [bool]($text -match "\b(property\s+\w+\s+$m\b|function\s+$m\s*\(|signal\s+$m\s*\()")
}

# --- 1. Members the mpv.* scan CAN see -------------------------------------------------------
$members = [regex]::Matches($page, '\bmpv\.([A-Za-z]+)') | ForEach-Object { $_.Groups[1].Value } |
           Sort-Object -Unique | Where-Object { $_ -notmatch '^on[A-Z]' }  # signal handlers checked separately
foreach ($m in $members) {
    if (-not (Test-Member $engine $m)) {
        $violations += "PlayerEngine does not declare '$m' (PlayerPage uses mpv.$m)"
    }
}

# --- 2. Members reached INDIRECTLY - the scan above is structurally blind to these ------------
# setSubOption: SubStyleBar.qml receives the engine as `player: mpv` (PlayerPage.qml:2935) and
# calls player.setSubOption(key, value) for all six subtitle style controls (SubStyleBar.qml:45-59).
# The string "mpv.setSubOption" therefore never appears in PlayerPage and section 1 will never
# demand it - yet dropping it silently kills every subtitle style control on BOTH boots.
# Anything else handed the engine through a property must be listed here by hand too.
foreach ($m in @('setSubOption')) {
    if (-not (Test-Member $engine $m)) {
        $violations += "PlayerEngine does not declare '$m' (reached indirectly - see comment)"
    }
}

# --- 3. Signals -------------------------------------------------------------------------------
# Every signal PlayerPage handles on the engine object: the 13 handlers installed at the
# instantiation site (PlayerPage.qml:2821-2904) plus the 4 in the Connections block at
# PlayerPage.qml:1896-1904. A missing signal is a SILENTLY DEAD handler, not a build error - lose
# fileLoaded and resume-at-play, auto-subtitle selection and skip-segment loading all just stop.
# A `<name>Changed` requirement is satisfied by declaring the property `<name>` (QML generates the
# change signal); everything else must be an explicit `signal`.
$requiredSignals = @(
    'currentUrlChanged', 'fileStarted', 'fileLoaded', 'playbackError', 'endFile',
    'pauseChanged', 'positionChanged', 'gifSaved', 'gifFailed', 'durationChanged',
    'chaptersChanged', 'trackListChanged', 'coreSeekingChanged', 'speedChanged'
)
foreach ($sig in $requiredSignals) {
    $ok = [bool]($engine -match "\bsignal\s+$sig\s*\(")
    if (-not $ok -and $sig -cmatch '^(?<p>.+)Changed$') {
        $ok = [bool]($engine -match "\bproperty\s+\w+\s+$($matches.p)\b")
    }
    if (-not $ok) { $violations += "PlayerEngine must expose the signal '$sig'" }
}

# --- 4. The stats card's mpv property keys ----------------------------------------------------
# On the mpv boot libmpv answers these natively through the forwarded mpvProperty(), so there is
# nothing for the facade to translate. The Player 2 boot has no mpv, so ITS branch must map every
# key by hand - which is why the check targets PlayerEngineP2.qml and arms itself the moment
# Task 3 creates that file. Nothing here needs a human to re-enable it.
$statKeys = @('video-bitrate','audio-bitrate','frame-drop-count','vo-drop-frame-count',
              'estimated-vf-fps','container-fps','video-codec','audio-codec','hwdec-current',
              'cache-buffering-state','width','height')
$p2Path = Join-Path $root 'qml/PlayerEngineP2.qml'
if (Test-Path $p2Path) {
    $p2 = Remove-QmlComments (Get-Content -Raw $p2Path)
    foreach ($k in $statKeys) {
        if ($p2 -notmatch [regex]::Escape('"' + $k + '"')) {
            $violations += "PlayerEngineP2's mpvProperty() does not handle '$k'"
        }
    }
} else {
    $deferred += 'stat-key mapping (qml/PlayerEngineP2.qml not created yet - Task 3)'
}

# --- 5. Each branch the facade can load must exist ---------------------------------------------
if (-not (Test-Path (Join-Path $root 'qml/PlayerEngineMpv.qml'))) {
    $violations += 'qml/PlayerEngineMpv.qml missing (PlayerEngine loads it on the mpv boot)'
}

if ($violations.Count) { $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host "FACADE CONTRACT: FAIL ($($violations.Count))"; exit 1 }

# A deferral is not a clean pass. The bare string 'FACADE CONTRACT: PASS' is reserved for the run
# where every check actually ran, so a gate grepping for that exact line does not match while one
# is still outstanding - the deferral is loud to a human AND detectable by a machine.
if ($deferred.Count) {
    $deferred | ForEach-Object { Write-Host "FACADE CONTRACT: DEFERRED - $_" }
    Write-Host "FACADE CONTRACT: PASS ($($deferred.Count) CHECK DEFERRED)"
    exit 0
}
Write-Host 'FACADE CONTRACT: PASS'
