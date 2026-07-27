# Facade probe gate: the probe's own verdict AND the Qt runtime warnings, in one exit code.
#
# WHY THIS EXISTS, and it is not a hypothetical. On 2026-07-27 a refactor left two dead
# `_adoptSubDelay` / `_adoptAudioDelay` call sites in PlayerEngineP2's Connections block. Every
# delay change threw a QML TypeError, the engine->facade delay re-sync was dead, and the probe
# reported RESULT PASS across 57 assertions. The only reason it did not ship was that somebody
# happened to grep the log by hand. House doctrine here is that Qt runtime warnings ARE test
# results - a TypeError or a binding loop is a failure, not noise - so the safety net has to be in
# the artifact, not in a habit.
#
# WHY A GATE AND NOT THE PROBE ITSELF: engine-level warnings (TypeError, binding loops, unknown
# signal connections) are emitted by the QML engine straight to Qt's message handler. QML has no API
# to install one, and this app installs none to expose (grepped: no qInstallMessageHandler anywhere
# in native/). A .qml file therefore CANNOT observe them - the process that owns the output stream
# is the only thing that can. That is this file, and it is the same shape as the other gates here.
#
#   powershell -NoProfile -File tests/player2/player2_facade_probe_gate.ps1 `
#       -Fixtures <player2-fixtures dir> -LongMedia <any clip longer than 60s>
#
#   powershell -NoProfile -File tests/player2/player2_facade_probe_gate.ps1 `
#       -Media <one file> -Mode transport            # single case, for development
#
# Exit 0 only if every case passed AND no case emitted a forbidden warning.

param(
    # Directory holding the built fixtures: av.mkv, chaptered.mkv, tracks-long.mkv.
    [string]$Fixtures = '',
    # Any clip longer than 60s. The transport sequence needs runway; the 2s fixtures have none.
    [string]$LongMedia = '',
    # Single-case mode, bypassing the matrix.
    [string]$Media = '',
    [string]$Mode = 'auto',
    # The mpv-boot regression is a control, not a P2 test: skip it only if mpv cannot run here.
    [switch]$SkipMpvBoot
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exePath = Join-Path $repoRoot 'native\build-msvc\colosseum.exe'
$probePath = Join-Path $repoRoot 'tests\player2\player2_facade_probe.qml'
foreach ($required in @($exePath, $probePath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "facade gate: missing $required" }
}

# Qt's bin dir must LEAD the path or the exe cannot load its own DLLs - it exits 127 with
# "error while loading shared libraries: avfilter-11.dll", which reads exactly like a probe that
# never reached playback (2026-07-27).
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;' +
            'C:\tools\mpvqt-feasibility\libmpv-prefix\bin;' +
            'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;' + $env:PATH
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
# Without QSG_NO_VSYNC the QML timers stop firing once Player 2 playback starts and the probe emits
# nothing at all (cost hours on 2026-07-25; do not remove).
$env:QSG_NO_VSYNC = '1'

# The failure vocabulary. Every one of these is the QML engine reporting that something the code
# asked for does not exist or did not happen - never a diagnostic about the media.
#   TypeError / ReferenceError / is not a function - a member that is not there (THE bug above)
#   Unable to assign / Cannot assign                - a type or property mismatch in a binding
#   binding loop                                    - a property fighting itself
#   Detected function / non-existent signal         - a Connections handler wired to nothing, which
#                                                     is the silently-dead-relay failure this whole
#                                                     port was written to avoid
#
# Two of these were NEGATIVE-CONTROLLED rather than assumed, by breaking the code on purpose and
# checking the gate turned red (2026-07-27):
#   * a dead _adopt call site  -> "TypeError: Property '_adoptSubDelay' ... is not a function"
#   * a handler for a signal that does not exist
#       -> 'QML Connections: Detected function "onX" ... no signal of the target matches the name'
# "Detected function" is the wording Qt 6.11 actually uses; "non-existent signal" is the older form,
# kept so the gate does not go quiet on a Qt upgrade. In BOTH controls the probe itself reported
# RESULT PASS with zero failed assertions - which is the entire reason this file exists.
$forbidden = @(
    'TypeError',
    'ReferenceError',
    'is not a function',
    'Unable to assign',
    'Cannot assign',
    'binding loop',
    'non-existent signal',
    'Detected function',
    # PlayerEngineP2 warning that PlayerPage asked it for a property key it has no case for. Same
    # class as the rest: the code asked for something that is not there. It is how the two keys the
    # port plan missed (audio-params/channel-count, video-params/transfer) were found at all - they
    # were spamming this line once a second while the probe reported PASS (2026-07-27).
    'mpvProperty has no mapping'
)

# NO ALLOWLIST, and that is a measured claim rather than an omission: all five cases below were run
# clean on 2026-07-27 and produced ZERO hits in every category above. The real stderr noise on this
# path is "No QSGTexture provided from updateSampledImage.", FFmpeg's analyzeduration advice, a
# QSqlDatabase-without-QCoreApplication line at teardown and the app's own [net]/[img] logging -
# none of which match anything in $forbidden.
# If that ever changes: allowlist the exact line, narrowly, with a comment saying why it is not a
# defect. Do NOT drop a whole category to silence one line - the category IS the safety net.
$allowlist = @()

$logDir = Join-Path $repoRoot 'artifacts'
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

# Build the case matrix.
$cases = @()
if ($Media) {
    $cases += @{ Name = 'single'; Media = $Media; Mode = $Mode; Player2 = $true; ExpectFail = @() }
} else {
    if (-not $Fixtures) { throw 'facade gate: -Fixtures (or -Media) is required' }
    if (-not $LongMedia) { throw 'facade gate: -LongMedia is required (the transport sequence needs runway)' }
    $cases += @{ Name = 'eof-av';        Media = (Join-Path $Fixtures 'av.mkv');          Mode = 'eof';       Player2 = $true;  ExpectFail = @() }
    $cases += @{ Name = 'eof-chaptered'; Media = (Join-Path $Fixtures 'chaptered.mkv');   Mode = 'eof';       Player2 = $true;  ExpectFail = @() }
    $cases += @{ Name = 'transport';     Media = $LongMedia;                              Mode = 'transport'; Player2 = $true;  ExpectFail = @() }
    $cases += @{ Name = 'tracks';        Media = (Join-Path $Fixtures 'tracks-long.mkv'); Mode = 'tracks';    Player2 = $true;  ExpectFail = @() }
    # $LongMedia, NOT a built fixture, and that is a measured choice rather than convenience. The
    # stats sequence is the first thing in this suite that needs a frame to have been PRESENTED
    # (width/height are published off the decoded frame), and tracks-long.mkv never presents one on
    # this machine: 485 frames decode, ZERO submit, 484 device errors, hardwareFormat and
    # inputFormat both empty. Bisected 2026-07-27 against six purpose-built clips - it is NOT the
    # resolution (320x180 presents fine), NOT the stream count (2 audio + subtitle presents fine).
    # The trigger is the LANGUAGE/TITLE METADATA on those tracks: byte-identical files differing
    # only in `-metadata:s:a:0 language=eng ...` pass without it and fail with it, which points at
    # PlayerPage's own maybeAutoSelectTracks() firing a track selection at open. Video never
    # recovers; audio plays on. Written up for Task 10 - it is an ENGINE defect, not a probe one,
    # and the tracks case above passes straight through it because nothing there asserts a frame.
    # Repro pair kept in the Task 5 report: 2-audio+subtitle WITH metadata fails, WITHOUT passes.
    $cases += @{ Name = 'stats';         Media = $LongMedia;                              Mode = 'stats';     Player2 = $true;  ExpectFail = @() }
    if (-not $SkipMpvBoot) {
        # The mpv boot is a CONTROL: the same probe against the daily driver. Two assertions are
        # P2-only by construction and are expected to fail there; a THIRD failure means the port
        # broke the shipped player, which is the regression this case exists to catch.
        $cases += @{ Name = 'mpv-boot'; Media = $LongMedia; Mode = 'transport'; Player2 = $false
                     ExpectFail = @('booted on the Player 2 branch', 'capture capability is off on Player 2') }
    }
}

$failures = @()

foreach ($case in $cases) {
    Write-Host ''
    Write-Host ("=== case: {0} ({1}, {2} boot)" -f $case.Name, $case.Mode, $(if ($case.Player2) { 'Player 2' } else { 'mpv' }))
    if (-not (Test-Path -LiteralPath $case.Media)) {
        $failures += "$($case.Name): media not found - $($case.Media)"
        Write-Host "  MEDIA NOT FOUND: $($case.Media)"
        continue
    }
    if ($case.Player2) { $env:COLOSSEUM_PLAYER2 = '1' } else { Remove-Item Env:\COLOSSEUM_PLAYER2 -ErrorAction SilentlyContinue }

    $outLog = Join-Path $logDir ("facade-gate-{0}.log" -f $case.Name)
    $errLog = "$outLog.err"
    # QUOTE EVERY ARGUMENT. Start-Process joins -ArgumentList with spaces and does no quoting of its
    # own, so an unquoted media path containing a space arrives as several argv entries: the probe
    # opens a truncated path, the session goes to Error, and it reports "position never advanced"
    # with duration 0 - i.e. a harness bug wearing a player defect's clothes. Caught on this gate's
    # first run, against a real filename with spaces in it.
    $quoted = @("`"$probePath`"", "`"$($case.Media)`"", "`"$($case.Mode)`"")
    $proc = Start-Process -FilePath $exePath -PassThru -Wait `
        -ArgumentList $quoted `
        -RedirectStandardOutput $outLog -RedirectStandardError $errLog
    $code = $proc.ExitCode

    $lines = @()
    foreach ($f in @($outLog, $errLog)) {
        if (Test-Path -LiteralPath $f) { $lines += Get-Content -LiteralPath $f }
    }

    # --- 1. the probe's own verdict ---------------------------------------------------------
    $reported = @($lines | Where-Object { $_ -match 'FACADE PROBE: FAIL ' } |
                  ForEach-Object { ($_ -replace '^.*FACADE PROBE: FAIL ', '').Trim() })
    $unexpected = @()
    foreach ($r in $reported) {
        $known = $false
        foreach ($e in $case.ExpectFail) { if ($r.StartsWith($e)) { $known = $true } }
        if (-not $known) { $unexpected += $r }
    }
    $passCount = @($lines | Where-Object { $_ -match 'FACADE PROBE: PASS ' }).Count
    Write-Host ("  passes={0} reportedFailures={1} exit={2}" -f $passCount, $reported.Count, $code)
    foreach ($e in $case.ExpectFail) {
        if (-not ($reported | Where-Object { $_.StartsWith($e) })) {
            $failures += "$($case.Name): expected failure never happened - '$e' (this case is a control; if it now passes, update the gate)"
            Write-Host "  EXPECTED-FAILURE MISSING: $e"
        } else {
            Write-Host "  expected failure present (control): $e"
        }
    }
    foreach ($u in $unexpected) {
        $failures += "$($case.Name): $u"
        Write-Host "  FAIL $u"
    }
    if ($passCount -eq 0) {
        $failures += "$($case.Name): the probe produced no PASS lines at all - it did not run"
        Write-Host '  THE PROBE PRODUCED NO OUTPUT'
    }

    # --- 2. Qt runtime warnings -------------------------------------------------------------
    # Reported per LINE, verbatim and with its number: a warning you cannot locate is nearly as
    # useless as one you never saw.
    $hits = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        foreach ($pattern in $forbidden) {
            if ($line -like "*$pattern*") {
                $excused = $false
                foreach ($a in $allowlist) { if ($line -like $a) { $excused = $true } }
                if (-not $excused) { $hits += ("line {0}: {1}" -f ($i + 1), $line.Trim()) }
                break
            }
        }
    }
    if ($hits.Count) {
        Write-Host ("  QT WARNINGS: {0}" -f $hits.Count)
        foreach ($h in $hits) {
            Write-Host "    $h"
            $failures += "$($case.Name): qt runtime warning - $h"
        }
    } else {
        Write-Host '  qt warnings: none'
    }
    Write-Host "  log: $outLog"
}

Write-Host ''
if ($failures.Count) {
    Write-Host "--- failures ($($failures.Count)) ---"
    $failures | ForEach-Object { Write-Host "  - $_" }
    Write-Host "FACADE PROBE GATE: FAIL ($($failures.Count))"
    exit 1
}
Write-Host 'FACADE PROBE GATE: PASS'
exit 0
