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
# Exit 0 only if every case reports its expected final probe verdict, exits as expected, and emits
# no unexpected QML/Qt runtime diagnostic.

param(
    # Directory holding the built fixtures: av.mkv, stats.mkv, chaptered.mkv, tracks-long.mkv.
    [string]$Fixtures = '',
    # Any clip longer than 60s. The transport sequence needs runway; the 2s fixtures have none.
    [string]$LongMedia = '',
    # Single-case mode, bypassing the matrix.
    [string]$Media = '',
    [string]$Mode = 'auto',
    # The mpv-boot regression is a control, not a P2 test: skip it only if mpv cannot run here.
    [switch]$SkipMpvBoot,
    # Test seam: a contract harness supplies a disposable child so it can prove this script's
    # process/exit/output policy without launching the app. Empty keeps the production executable.
    [string]$ProbeExecutable = '',
    # Test seam: keep harness logs out of the user's artifacts directory. Empty keeps production logs.
    [string]$LogDir = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exePath = if ($ProbeExecutable) { (Resolve-Path -LiteralPath $ProbeExecutable).Path }
           else { Join-Path $repoRoot 'native\build-msvc\colosseum.exe' }
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

# Diagnostics emitted under `qml:` (except the probe's own structured progress/result lines) and
# Qt's QML/Quick categories are failures by default. The old vocabulary-only list let a new runtime
# warning pass merely because its wording was unfamiliar.
$runtimeErrorFragments = @(
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

# Measured on the five original facade runs (2026-07-27). These are exact strings, deliberately
# not patterns: an adjacent/new scenegraph or SQL diagnostic must fail for inspection instead of
# inheriting a blanket "renderer noise" pardon.
$benignRuntimeLines = @(
    'No QSGTexture provided from updateSampledImage(). This is wrong.',
    'qt.sql.qsqldatabase: QSqlDatabase requires a QCoreApplication'
)

function Test-BenignRuntimeLine([string]$line) {
    return $benignRuntimeLines -ccontains $line.Trim()
}

function Test-QmlQtRuntimeDiagnostic([string]$line) {
    $text = $line.Trim()
    if (-not $text.Length -or (Test-BenignRuntimeLine $text)) { return $false }
    # Probe logging is expected output; every other qml: line is a QML diagnostic until proven safe.
    if ($text -match '^qml:(?!\s*FACADE PROBE:)') { return $true }
    # Every Qt category is fail-closed after the exact measured exceptions above. A new qt.sql,
    # qt.network, or renderer warning is evidence to inspect, not noise inherited from qml/quick.
    if ($text -cmatch '^(qt\.|QML\b|QQml\b|Qt\.)') { return $true }
    foreach ($fragment in $runtimeErrorFragments) {
        if ($text -like "*$fragment*") { return $true }
    }
    return $false
}

function Get-VideoDimensions([string]$mediaPath) {
    $fixedFfprobe = 'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffprobe.exe'
    $ffprobe = if (Test-Path -LiteralPath $fixedFfprobe) { $fixedFfprobe }
               else {
                   $command = Get-Command ffprobe -ErrorAction SilentlyContinue
                   if (-not $command) { throw 'facade gate: ffprobe is required for exact stats dimensions' }
                   $command.Source
               }
    $json = & $ffprobe -v error -select_streams v:0 `
        -show_entries stream=width,height -of json -- $mediaPath
    if ($LASTEXITCODE -ne 0) { throw "facade gate: ffprobe failed for $mediaPath" }
    $parsed = $json | ConvertFrom-Json
    $streams = @($parsed.streams)
    if ($streams.Count -ne 1) {
        throw "facade gate: expected exactly one selected video stream in $mediaPath, got $($streams.Count)"
    }
    return @{ Width = [int]$streams[0].width; Height = [int]$streams[0].height }
}

$logDir = if ($LogDir) { $LogDir } else { Join-Path $repoRoot 'artifacts' }
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

# Build the case matrix.
$cases = @()
if ($Media) {
    $cases += @{ Name = 'single'; Media = $Media; Mode = $Mode; Player2 = $true; ExpectFail = @()
                 ExpectedExit = 0; ExpectedResult = 'RESULT PASS' }
} else {
    if (-not $Fixtures) { throw 'facade gate: -Fixtures (or -Media) is required' }
    if (-not $LongMedia) { throw 'facade gate: -LongMedia is required (the transport sequence needs runway)' }
    $cases += @{ Name = 'eof-av';        Media = (Join-Path $Fixtures 'av.mkv');          Mode = 'eof';       Player2 = $true;  ExpectFail = @(); ExpectedExit = 0; ExpectedResult = 'RESULT PASS' }
    $cases += @{ Name = 'eof-chaptered'; Media = (Join-Path $Fixtures 'chaptered.mkv');   Mode = 'eof';       Player2 = $true;  ExpectFail = @(); ExpectedExit = 0; ExpectedResult = 'RESULT PASS' }
    $cases += @{ Name = 'transport';     Media = $LongMedia;                              Mode = 'transport'; Player2 = $true;  ExpectFail = @(); ExpectedExit = 0; ExpectedResult = 'RESULT PASS' }
    $cases += @{ Name = 'tracks';        Media = (Join-Path $Fixtures 'tracks-long.mkv'); Mode = 'tracks';    Player2 = $true;  ExpectFail = @(); ExpectedExit = 0; ExpectedResult = 'RESULT PASS' }
    # A dedicated fixture is mandatory here. tracks-long.mkv currently cannot submit video after
    # PlayerPage's metadata-driven auto-selection, while arbitrary $LongMedia has no hand-known
    # dimensions. stats.mkv is deliberately 426x240 and metadata-light; ffprobe below independently
    # verifies those exact dimensions before the runtime assertion is allowed to run.
    $statsMedia = Join-Path $Fixtures 'stats.mkv'
    if (-not (Test-Path -LiteralPath $statsMedia)) {
        throw "facade gate: deterministic stats fixture is missing - $statsMedia"
    }
    $statsDimensions = Get-VideoDimensions $statsMedia
    if ($statsDimensions.Width -ne 426 -or $statsDimensions.Height -ne 240) {
        throw "facade gate: stats fixture dimensions changed: ffprobe=$($statsDimensions.Width)x$($statsDimensions.Height), expected=426x240"
    }
    Write-Host "facade gate: ffprobe verified stats fixture 426x240"
    $cases += @{ Name = 'stats';         Media = $statsMedia;                              Mode = 'stats';     Player2 = $true;  ExpectFail = @() }
    if (-not $SkipMpvBoot) {
        # The mpv boot is a CONTROL: the same probe against the daily driver. Two assertions are
        # P2-only by construction and are expected to fail there; a THIRD failure means the port
        # broke the shipped player, which is the regression this case exists to catch.
        $cases += @{ Name = 'mpv-boot'; Media = $LongMedia; Mode = 'transport'; Player2 = $false
                     ExpectFail = @('booted on the Player 2 branch', 'capture capability is off on Player 2')
                     ExpectedExit = 1; ExpectedResult = 'RESULT FAIL' }
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

    $expectedDimensions = if ($case.Mode -eq 'stats') {
        Get-VideoDimensions $case.Media
    } else {
        @{ Width = 0; Height = 0 }
    }

    $outLog = Join-Path $logDir ("facade-gate-{0}.log" -f $case.Name)
    $errLog = "$outLog.err"
    # QUOTE EVERY ARGUMENT. Start-Process joins -ArgumentList with spaces and does no quoting of its
    # own, so an unquoted media path containing a space arrives as several argv entries: the probe
    # opens a truncated path, the session goes to Error, and it reports "position never advanced"
    # with duration 0 - i.e. a harness bug wearing a player defect's clothes. Caught on this gate's
    # first run, against a real filename with spaces in it.
    $quoted = @("`"$probePath`"", "`"$($case.Media)`"", "`"$($case.Mode)`"",
                "`"$($expectedDimensions.Width)`"", "`"$($expectedDimensions.Height)`"")
    $proc = Start-Process -FilePath $exePath -PassThru -Wait `
        -ArgumentList $quoted `
        -RedirectStandardOutput $outLog -RedirectStandardError $errLog
    $code = $proc.ExitCode

    $lines = @()
    foreach ($f in @($outLog, $errLog)) {
        if (Test-Path -LiteralPath $f) { $lines += Get-Content -LiteralPath $f }
    }

    # --- 1. the probe's own verdict and process termination ---------------------------------
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
    if ($code -ne $case.ExpectedExit) {
        $failures += "$($case.Name): unexpected process exit $code (expected $($case.ExpectedExit))"
        Write-Host "  UNEXPECTED PROCESS EXIT: $code (expected $($case.ExpectedExit))"
    }
    $finalResults = @($lines | Where-Object { $_ -match 'FACADE PROBE: .*RESULT (PASS|FAIL)\s*$' })
    if ($finalResults.Count -eq 0) {
        $failures += "$($case.Name): missing final $($case.ExpectedResult)"
        Write-Host "  MISSING FINAL $($case.ExpectedResult)"
    } elseif ($finalResults.Count -ne 1) {
        $failures += "$($case.Name): expected one final probe result, found $($finalResults.Count)"
        Write-Host "  AMBIGUOUS FINAL RESULT: $($finalResults.Count)"
    } elseif ($finalResults[0] -notmatch [regex]::Escape($case.ExpectedResult) + '\s*$') {
        $failures += "$($case.Name): final probe result was not $($case.ExpectedResult): $($finalResults[0].Trim())"
        Write-Host "  WRONG FINAL RESULT: $($finalResults[0].Trim())"
    }
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

    # --- 2. QML/Qt runtime diagnostics -------------------------------------------------------
    # Reported per LINE, verbatim and with its number: a warning you cannot locate is nearly as
    # useless as one you never saw. Non-QML process noise is outside this classifier; QML/Qt is
    # fail-closed except the two exact measured lines above.
    $hits = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if (Test-QmlQtRuntimeDiagnostic $line) {
            $hits += ("line {0}: {1}" -f ($i + 1), $line.Trim())
        }
    }
    if ($hits.Count) {
        Write-Host ("  QT WARNINGS: {0}" -f $hits.Count)
        foreach ($h in $hits) {
            Write-Host "    $h"
            $failures += "$($case.Name): unexpected QML/Qt runtime diagnostic - $h"
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
