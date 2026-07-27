# player2_engine_facade_contract.ps1 - every mpv-surface member PlayerPage actually uses must be
# declared by PlayerEngine.qml. Enumerated FROM PlayerPage each run, so a new mpv.* usage in
# PlayerPage automatically fails this contract until the facade answers it. Both directions of the
# drift are fatal: a missing member is a runtime TypeError mid-playback, not a build error.
# Run: powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1
#
# WHAT A GREEN RUN DOES **NOT** PROVE. This is a static contract, so it can only see DECLARATION.
# `signal fileStarted()` that nothing ever emits passes clean here, and so does a property that is
# declared and never fed. Emission and wiring are proved by the probes, not by this file: the mpv
# branch by the Task 2 relay probe, the P2 branch by Task 3's. Do not read a PASS as "the signals
# fire" - read it as "nothing PlayerPage reaches for is missing".
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
function Test-Signal($text, $sig) {
    if ($text -match "\bsignal\s+$sig\s*\(") { return $true }
    if ($sig -cmatch '^(?<p>.+)Changed$') {
        return [bool]($text -match "\bproperty\s+\w+\s+$($matches.p)\b")
    }
    return $false
}
foreach ($sig in $requiredSignals) {
    if (-not (Test-Signal $engine $sig)) { $violations += "PlayerEngine must expose the signal '$sig'" }
}

# --- 3b. The facade's own WIRING, not just its declarations -----------------------------------
# Every relay call site names its member twice - `onSubDelayChanged: engine._push("subDelay")` - and
# a transposition (`_push("audioDelay")` under onSubDelayChanged) is valid QML that compiles and
# half-works. Same for the readonly forwards, where the declared name and the forwarded name must
# agree. Declaration checks cannot see any of that, so assert the names match at each site.
foreach ($m in [regex]::Matches($engine, '(?m)^\s*on(?<h>[A-Za-z]\w*)Changed\s*:\s*engine\._push\("(?<k>\w+)"\)')) {
    $h = $m.Groups['h'].Value; $k = $m.Groups['k'].Value
    $expect = $h.Substring(0,1).ToLower() + $h.Substring(1)
    if ($expect -cne $k) { $violations += "push relay transposed: on${h}Changed pushes '$k' (expected '$expect')" }
}
foreach ($m in [regex]::Matches($engine, 'function\s+on(?<h>[A-Za-z]\w*)Changed\s*\(\s*\)\s*\{\s*engine\._pull\("(?<k>\w+)"\)\s*\}')) {
    $h = $m.Groups['h'].Value; $k = $m.Groups['k'].Value
    $expect = $h.Substring(0,1).ToLower() + $h.Substring(1)
    if ($expect -cne $k) { $violations += "pull relay transposed: on${h}Changed pulls '$k' (expected '$expect')" }
}
foreach ($m in [regex]::Matches($engine, 'readonly\s+property\s+\w+\s+(?<d>\w+)\s*:\s*inner\s*\?\s*inner\.(?<f>\w+)')) {
    $d = $m.Groups['d'].Value; $f = $m.Groups['f'].Value
    if ($d -cne $f) { $violations += "readonly forward transposed: property '$d' forwards inner.$f" }
}
# Both push and pull must cover every relayed member - a member with a property but no relay is
# declared, forwarded and permanently dead in one direction.
$relayed = @()
if ($engine -match '(?s)relayedMembers\s*:\s*\[(?<body>.*?)\]') {
    $relayed = [regex]::Matches($matches.body, '"(\w+)"') | ForEach-Object { $_.Groups[1].Value }
}
if (-not $relayed.Count) { $violations += 'PlayerEngine no longer declares relayedMembers (the branch-parity check reads it)' }
foreach ($k in $relayed) {
    if ($engine -notmatch [regex]::Escape("_push(`"$k`")")) { $violations += "relayed member '$k' has no _push call site" }
    if ($engine -notmatch [regex]::Escape("_pull(`"$k`")")) { $violations += "relayed member '$k' has no _pull call site" }
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

# --- 5. BRANCH PARITY: the P2 branch must answer the surface the facade forwards ---------------
# Section 4 only ever checked PlayerEngineP2.qml for 12 stat-key strings, which is far thinner than
# this file's billing. The facade forwards the SAME surface to whichever branch loaded, so a branch
# that omits a member is a TypeError mid-playback, and one that omits a signal the facade's
# Connections names is a loud runtime warning plus a permanently dead relay direction.
# Everything required here is DERIVED FROM THE FACADE, never re-listed, so the two cannot drift:
#   - relay properties        <- PlayerEngine's own relayedMembers array
#   - readonly forwards       <- every `readonly property X n: inner ? inner.n` in the facade
#   - signals the branch owes <- every `function on<X>(` in the facade's Connections block
# Members the facade forwards GUARDED (mpvProperty, captureFrame, the GIF calls, setSubOption,
# command, revealCaptureFolder) are deliberately NOT required: they are Task 4/5/6 territory and the
# facade already answers a branch that lacks them without throwing.
if (Test-Path $p2Path) {
    $branchProps = @($relayed)
    $branchProps += [regex]::Matches($engine, 'readonly\s+property\s+\w+\s+(\w+)\s*:\s*inner\s*\?\s*inner\.\w+') |
                    ForEach-Object { $_.Groups[1].Value }
    foreach ($m in ($branchProps | Sort-Object -Unique)) {
        if (-not (Test-Member $p2 $m)) {
            $violations += "PlayerEngineP2 does not declare '$m' (PlayerEngine forwards it to every branch)"
        }
    }
    # setSubOption is forwarded GUARDED, so the loop above cannot demand it - but as of Task 4 the P2
    # branch really implements it (SubStyleBar's five live style controls are that function plus the
    # SubtitleLayer it drives), and losing it would put all five back to moving and doing nothing
    # with no error anywhere. Named by hand, exactly like the facade's own indirect check above.
    foreach ($m in @('setSubOption', 'addSubtitle')) {
        if (-not (Test-Member $p2 $m)) {
            $violations += "PlayerEngineP2 does not declare '$m' (reached indirectly - see comment)"
        }
    }
    # `function on<X>(` appears ONLY inside the facade's Connections block - its own handlers use
    # property syntax (`onPauseChanged:`), so this scan is exactly the set of signals the facade
    # connects to on `inner`. videoFillChanged is the one that is easy to miss: it is not a property
    # change signal on either branch, it is declared by hand on both.
    foreach ($h in ([regex]::Matches($engine, 'function\s+on([A-Z]\w*)\s*\(') |
                    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)) {
        $sig = $h.Substring(0,1).ToLower() + $h.Substring(1)
        if (-not (Test-Signal $p2 $sig)) {
            $violations += "PlayerEngineP2 must expose the signal '$sig' (PlayerEngine's relay connects to it)"
        }
    }
}

# --- 6. Each branch the facade can load must exist ---------------------------------------------
if (-not (Test-Path (Join-Path $root 'qml/PlayerEngineMpv.qml'))) {
    $violations += 'qml/PlayerEngineMpv.qml missing (PlayerEngine loads it on the mpv boot)'
}

# Violations print via Write-Host, NOT Write-Error: PowerShell wraps Write-Error at console width and
# interleaves CategoryInfo/FullyQualifiedErrorId noise, which shreds each message across several lines
# and makes a multi-violation failure genuinely unreadable. exit 1 is the machine signal; these lines
# are for the human reading why.
if ($violations.Count) { $violations | ForEach-Object { Write-Host "  - $_" }
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
