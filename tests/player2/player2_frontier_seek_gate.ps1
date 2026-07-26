# T2d gate: can a stalled stream answer a seek, REPEATEDLY?
#
# One green run proves nothing here. The defect this guards was intermittent in exactly the way that
# fools a single run: on 2026-07-26 four runs recovered in ~half a second and two presented nothing
# at all and died. So the gate owns the fixture, runs the probe N times, and fails if ANY run fails.
#
#   powershell -NoProfile -File tests/player2/player2_frontier_seek_gate.ps1 -Runs 5
#
# Exit code 0 only if every run passed. Anything else is a red gate.

param(
    [int]$Runs = 5,
    [int]$Port = 8791,
    # 24 MB of "already downloaded" bytes: enough that the picture plays ~30 s before the frontier
    # freezes it, which is what puts the demux thread in the parked state under test.
    [int]$WindowBytes = 25165824,
    [string]$Clip = 'artifacts/streamclip.mp4'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$clipPath = Join-Path $repoRoot $Clip
$exePath = Join-Path $repoRoot 'native\build-msvc\colosseum.exe'
$probePath = Join-Path $repoRoot 'tests\player2\player2_frontier_seek_probe.qml'
$fixturePath = Join-Path $PSScriptRoot 'player2_http_fixture_server.ps1'

foreach ($required in @($clipPath, $exePath, $probePath, $fixturePath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "frontier gate: missing $required" }
}

# The probe drives the REAL colosseum.exe, so it needs the Player 2 boot (D3D11) and the vsync
# escape hatch - without QSG_NO_VSYNC the QML timers stop firing once playback starts and the probe
# emits nothing at all (cost hours on 2026-07-25; do not remove).
$env:COLOSSEUM_PLAYER2 = '1'
$env:COLOSSEUM_DEV = '1'
$env:QSG_NO_VSYNC = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
# The transport timeline is where the sub-second requirement actually lives, so the gate reads it.
$env:COLOSSEUM_PLAYER2_NET_TRACE = '1'
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;' +
            'C:\tools\mpvqt-feasibility\libmpv-prefix\bin;' +
            'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;' + $env:PATH

$passes = 0
$results = @()
for ($run = 1; $run -le $Runs; $run++) {
    # A fresh fixture per run: window mode holds stalled connections open, so a reused server carries
    # the previous run's held responses into the next one.
    # Wait for the PREVIOUS run's listener to release the port, or this run's server never binds and
    # the probe reports a player failure that is really a harness failure (seen 2026-07-26: a run
    # scored NOPLAY/presented=0 for exactly this reason).
    $portFree = $false
    for ($i = 0; $i -lt 120; $i++) {
        $inUse = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
        if ($inUse.Count -eq 0) { $portFree = $true; break }
        Start-Sleep -Milliseconds 250
    }
    # Proceeding anyway was the bug: the new fixture failed to bind, the probe saw a dead origin, and
    # the gate blamed the player. A harness that cannot start must say so, not produce a verdict.
    if (-not $portFree) {
        throw "frontier gate: port $Port is still held after 30s - harness failure, not a player result"
    }
    $fixture = Start-Process -FilePath 'powershell' -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-File', $fixturePath, '-File', $clipPath, '-Port', $Port,
        '-Mode', 'window', '-WindowBytes', $WindowBytes)
    # Poll until the origin actually serves bytes. Sleeping a fixed 5 s was a guess, and a guess is
    # what made this gate report engine defects it had caused itself.
    # Readiness = the listener is actually bound. Deliberately NOT an HTTP request: Invoke-WebRequest
    # against this fixture proved unreliable here (2026-07-26) and a readiness check that can fail
    # for its own reasons is worse than none - it manufactures player defects.
    $ready = $false
    for ($i = 0; $i -lt 60; $i++) {
        if ($fixture.HasExited) { break }
        $bound = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
        if ($bound.Count -gt 0) { $ready = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) {
        if (-not $fixture.HasExited) { Stop-Process -Id $fixture.Id -Force -ErrorAction SilentlyContinue }
        throw "frontier gate: the fixture never became ready on port $Port - harness failure, not a player result"
    }
    # Keep every run's output. An intermittent defect is only diagnosable if the FAILING run left a
    # log behind, and the failing run is the one you cannot reproduce on demand.
    $logDir = Join-Path $repoRoot 'artifacts'
    if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
    $runLog = Join-Path $logDir "frontier-gate-run$run.log"
    try {
        $probe = Start-Process -FilePath $exePath -PassThru -Wait -ArgumentList @($probePath) `
            -RedirectStandardOutput $runLog -RedirectStandardError "$runLog.err"
        $code = $probe.ExitCode
    } finally {
        # Ask it to stop and give it time to actually release the socket; killing it immediately is
        # what left the port held into the next run.
        try { Invoke-WebRequest -Uri "http://localhost:$Port/quit" -TimeoutSec 3 -UseBasicParsing | Out-Null } catch { }
        if (-not $fixture.WaitForExit(5000)) {
            Stop-Process -Id $fixture.Id -Force -ErrorAction SilentlyContinue
            $fixture.WaitForExit(5000) | Out-Null
        }
    }
    # THE sub-second requirement, read off the engine's own transport timeline: from the parked read
    # being abandoned to the fetch thread having a connection open at the new byte offset. This is
    # what T2d actually fixed (it used to be "never"), and it is the number that must stay under a
    # second - not the visible-picture time, which also carries the audio-readiness barrier.
    $traceLines = @(Select-String -Path @($runLog, "$runLog.err") -Pattern 'player2\.net t=(\d+)ms (READ INTERRUPTED|SEEK opened)' -ErrorAction SilentlyContinue)
    $interruptAt = ($traceLines | Where-Object { $_.Line -match 'READ INTERRUPTED' } | Select-Object -First 1)
    $openedAt = ($traceLines | Where-Object { $_.Line -match 'SEEK opened' } | Select-Object -First 1)
    $transportMs = -1
    if ($interruptAt -and $openedAt) {
        $a = [int]([regex]::Match($interruptAt.Line, 't=(\d+)ms').Groups[1].Value)
        $b = [int]([regex]::Match($openedAt.Line, 't=(\d+)ms').Groups[1].Value)
        $transportMs = $b - $a
    }
    if ($code -eq 0 -and ($transportMs -lt 0 -or $transportMs -gt 1000)) {
        Write-Host "player2_frontier_seek_gate: run $run - the probe passed but the seek reached the transport in ${transportMs}ms (requirement: under 1000ms, and it must be traced at all)"
        $code = 1
    }
    if ($code -eq 0) { $passes++ }
    $verdict = (Select-String -Path @($runLog, "$runLog.err") -Pattern 'FRONTIER SEEK PROBE: (PASS|FAIL) .*' -ErrorAction SilentlyContinue |
                Select-Object -Last 1).Line
    $verdict = "[transport ${transportMs}ms] $verdict"
    $results += "run ${run}: exit $code  $verdict"
    Write-Host "player2_frontier_seek_gate: run $run of $Runs -> exit $code  $verdict"
}

$results | ForEach-Object { Write-Host "  $_" }
if ($passes -eq $Runs) {
    Write-Host "player2_frontier_seek_gate: PASS ($passes/$Runs)"
    exit 0
}
Write-Host "player2_frontier_seek_gate: FAIL ($passes/$Runs passed)"
exit 1
