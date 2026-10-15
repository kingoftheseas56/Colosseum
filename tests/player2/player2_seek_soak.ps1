# Player 2 seek-soak gate (Task 16). Drives the harness's scripted-seek mode: N deterministic seeks
# (fixed forward/backward fraction pattern), each counted complete only when playback lands within 3s
# of its target. PASS requires: every seek completed, the session still healthy (reachedPlaying, zero
# device errors / CPU transfers), and A/V p95 within the bar across the whole seek storm.
#
# Run (smoke):   powershell -NoProfile -File tests/player2/player2_seek_soak.ps1 -SeekCount 12
# Run (promote): powershell -NoProfile -File tests/player2/player2_seek_soak.ps1   (100 seeks)
param(
    [string]$Clip = "$env:USERPROFILE\Downloads\Colosseum\The Wire - S4E10 - Misgivings - 20260720_175049.mp4",
    [int]$SeekCount = 100,
    [int]$SeekIntervalMs = 1500,
    [double]$MaxP95Ms = 40.0
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

$report = Join-Path $env:TEMP 'player2_seek_soak_report.json'
Remove-Item $report -ErrorAction SilentlyContinue
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $harness --file $Clip --report $report --seek-count $SeekCount --seek-interval-ms $SeekIntervalMs 2>$null 1>$null
$exit = $LASTEXITCODE
$ErrorActionPreference = $prevEAP
if (-not (Test-Path $report)) { throw "player2_seek_soak: FAIL (no report written; harness exit $exit)" }
$j = Get-Content -Raw $report | ConvertFrom-Json

$fails = @()
if (-not $j.passed) { $fails += "harness gate did not pass (msg: $($j.message))" }
if ([int]$j.seeksCompleted -lt [int]$j.seeksRequested) {
    $fails += "only $($j.seeksCompleted)/$($j.seeksRequested) scripted seeks landed"
}
if ([int]$j.seeksRequested -ne $SeekCount) { $fails += "harness ran $($j.seeksRequested) seeks, expected $SeekCount" }
if (-not $j.reachedPlaying) { $fails += "session never reached Playing" }
if ([int64]$j.deviceErrors -ne 0) { $fails += "$($j.deviceErrors) device errors" }
if ([int64]$j.cpuTransfers -ne 0) { $fails += "$($j.cpuTransfers) CPU transfers (zero-copy violated)" }
$p95 = [double]$j.avP95Ms
if ($p95 -gt $MaxP95Ms) { $fails += "A/V p95 $([math]::Round($p95,2))ms > ${MaxP95Ms}ms across the seek storm" }

Write-Output ("player2_seek_soak: seeks=$($j.seeksCompleted)/$($j.seeksRequested) " +
    "p95=$([math]::Round($p95,2))ms underruns=$($j.audioUnderruns) elapsed=$([math]::Round([double]$j.elapsedSeconds,1))s")
if ($fails.Count -gt 0) {
    $fails | ForEach-Object { Write-Output "  FAIL: $_" }
    throw "player2_seek_soak: FAIL ($($fails.Count) threshold(s))"
}
Write-Output "player2_seek_soak: PASS ($SeekCount deterministic seeks, all landed, sync held)"
