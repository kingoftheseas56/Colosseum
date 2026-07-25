# Player 2 memory-soak gate (Task 16). Long playback + close/reopen cycles while an EXTERNAL observer
# (this script) samples the process working set — the process cannot flatter its own memory numbers.
# PASS requires: every cycle completed (each reopen climbs back to Playing), the run healthy, and a
# bounded memory slope: the linear fit over the second half of the samples must grow less than
# MaxSlopeMBPerHour (leaks show as a persistent upward line; caches plateau).
#
# Run (smoke):   powershell -NoProfile -File tests/player2/player2_memory_soak.ps1 -SoakSeconds 90 -Cycles 3
# Run (promote): powershell -NoProfile -File tests/player2/player2_memory_soak.ps1   (2h + 50 cycles)
param(
    [string]$Clip = "$env:USERPROFILE\Downloads\Colosseum\The Wire - S4E10 - Misgivings - 20260720_175049.mp4",
    [int]$SoakSeconds = 7200,
    [int]$Cycles = 50,
    [int]$CycleDwellSeconds = 8,
    [double]$MaxSlopeMBPerHour = 40.0,
    [int]$SampleIntervalSeconds = 5
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$harness = Join-Path $root 'native/build-player2/player2_harness.exe'
if (-not (Test-Path $harness)) { throw "harness missing: $harness (build player2_harness)" }
if (-not (Test-Path $Clip)) { throw "clip missing: $Clip (pass -Clip PATH)" }

$qtBin = 'C:/Qt/6.11.1/msvc2022_64/bin'
$ffBin = 'C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin'
if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
if (Test-Path $ffBin) { $env:PATH = "$ffBin;$env:PATH" }
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'

$report = Join-Path $env:TEMP 'player2_memory_soak_report.json'
Remove-Item $report -ErrorAction SilentlyContinue

# Launch detached so this script stays free to sample the process from outside. The window must stay
# VISIBLE: a hidden QQuickWindow never renders, the video item never initializes, and the file never
# opens (measured via the event ledger, 2026-07-25 — every cycle-tick showed state=0 opened=0).
$proc = Start-Process -FilePath $harness -PassThru -ArgumentList @(
    '--file', "`"$Clip`"", '--report', "`"$report`"", '--soak-seconds', $SoakSeconds,
    '--cycles', $Cycles, '--cycle-dwell-seconds', $CycleDwellSeconds)

$samples = New-Object System.Collections.Generic.List[object]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while (-not $proc.HasExited) {
    Start-Sleep -Seconds $SampleIntervalSeconds
    try {
        $proc.Refresh()
        if (-not $proc.HasExited) {
            $samples.Add([pscustomobject]@{ Seconds = $sw.Elapsed.TotalSeconds
                                            WorkingSetMB = $proc.WorkingSet64 / 1MB })
        }
    } catch { break }
    # Hard stop: the harness's own watchdog should end the run; this is the outer belt.
    if ($sw.Elapsed.TotalSeconds -gt ($SoakSeconds + $Cycles * ($CycleDwellSeconds * 2 + 10) + 300)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        throw "player2_memory_soak: FAIL (outer watchdog: run exceeded its budget)"
    }
}

if (-not (Test-Path $report)) { throw "player2_memory_soak: FAIL (no report written; harness exit $($proc.ExitCode))" }
$j = Get-Content -Raw $report | ConvertFrom-Json

$fails = @()
if (-not $j.passed) { $fails += "harness gate did not pass (msg: $($j.message))" }
if ([int]$j.cyclesCompleted -lt [int]$j.cyclesRequested) {
    $fails += "only $($j.cyclesCompleted)/$($j.cyclesRequested) close/reopen cycles completed"
}
if ([int]$j.cyclesRequested -ne $Cycles) { $fails += "harness ran $($j.cyclesRequested) cycles, expected $Cycles" }
if ([int64]$j.deviceErrors -ne 0) { $fails += "$($j.deviceErrors) device errors" }

# Memory slope: least-squares fit over the SECOND HALF of samples (startup allocation excluded).
$slopeMBPerHour = 0.0
if ($samples.Count -ge 6) {
    $half = $samples | Select-Object -Skip ([int]($samples.Count / 2))
    $n = $half.Count
    $sumX = 0.0; $sumY = 0.0; $sumXY = 0.0; $sumXX = 0.0
    foreach ($s in $half) {
        $x = [double]$s.Seconds; $y = [double]$s.WorkingSetMB
        $sumX += $x; $sumY += $y; $sumXY += $x * $y; $sumXX += $x * $x
    }
    $den = ($n * $sumXX - $sumX * $sumX)
    if ([math]::Abs($den) -gt 1e-9) {
        $slopeMBPerHour = (($n * $sumXY - $sumX * $sumY) / $den) * 3600.0
    }
    if ($slopeMBPerHour -gt $MaxSlopeMBPerHour) {
        $fails += "memory slope $([math]::Round($slopeMBPerHour,1)) MB/h > $MaxSlopeMBPerHour MB/h (leak signature)"
    }
} else {
    $fails += "too few memory samples ($($samples.Count)) to fit a slope - run longer"
}

$peak = ($samples | Measure-Object -Property WorkingSetMB -Maximum).Maximum
Write-Output ("player2_memory_soak: cycles=$($j.cyclesCompleted)/$($j.cyclesRequested) " +
    "samples=$($samples.Count) peak=$([math]::Round($peak,0))MB slope=$([math]::Round($slopeMBPerHour,1))MB/h " +
    "elapsed=$([math]::Round([double]$j.elapsedSeconds,0))s")
if ($fails.Count -gt 0) {
    $fails | ForEach-Object { Write-Output "  FAIL: $_" }
    throw "player2_memory_soak: FAIL ($($fails.Count) threshold(s))"
}
Write-Output "player2_memory_soak: PASS (cycles complete, memory slope bounded)"
