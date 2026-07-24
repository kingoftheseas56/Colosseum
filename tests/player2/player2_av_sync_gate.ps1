# Player 2 A/V sync gate (Task 16 numeric proof). Plays a real clip in report mode for a soak window
# and requires the playback-anchored A/V scheduling error to stay tight. The promotion bar is a 30-min
# soak with p95 |A/V error| <= 40 ms; the window is a parameter so it can be smoke-run in seconds and
# run for real (-SoakSeconds 1800) on a release build before promotion.
#
# Run (smoke):    powershell -NoProfile -File tests/player2/player2_av_sync_gate.ps1 -SoakSeconds 15
# Run (promote):  powershell -NoProfile -File tests/player2/player2_av_sync_gate.ps1 -SoakSeconds 1800
param(
    [string]$Clip = "$env:USERPROFILE\Downloads\Colosseum\The Wire - S4E10 - Misgivings - 20260720_175049.mp4",
    [int]$SoakSeconds = 1800,
    [double]$MaxP95Ms = 40.0,
    [double]$MaxDriftAbsMs = 80.0
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

$report = Join-Path $env:TEMP 'player2_av_sync_report.json'
Remove-Item $report -ErrorAction SilentlyContinue
# The harness streams ffmpeg diagnostics to stderr; Windows PowerShell turns native stderr into
# terminating errors under 'Stop', so drop to 'Continue' just for the run and redirect its streams.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $harness --file $Clip --report $report --soak-seconds $SoakSeconds 2>$null 1>$null
$exit = $LASTEXITCODE
$ErrorActionPreference = $prevEAP
if (-not (Test-Path $report)) { throw "player2_av_sync_gate: FAIL (no report written; harness exit $exit)" }
$j = Get-Content -Raw $report | ConvertFrom-Json

$p95 = [double]$j.avP95Ms
$drift = [double]$j.avDriftMaxAbsMs
$fps = [double]$j.sustainedFps
$fails = @()
if (-not $j.passed) { $fails += "harness gate did not pass (msg: $($j.message))" }
if ($p95 -gt $MaxP95Ms) { $fails += "A/V p95 $([math]::Round($p95,2))ms > ${MaxP95Ms}ms" }
if ($drift -gt $MaxDriftAbsMs) { $fails += "A/V max drift $([math]::Round($drift,2))ms > ${MaxDriftAbsMs}ms" }
if ($j.reachedPlaying -ne $true) { $fails += "never reached Playing" }

Write-Output ("player2_av_sync_gate: soak=${SoakSeconds}s p95=$([math]::Round($p95,2))ms " +
    "maxDrift=$([math]::Round($drift,2))ms fps=$([math]::Round($fps,2)) passed=$($j.passed)")
if ($fails.Count -gt 0) {
    $fails | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "player2_av_sync_gate: FAIL ($($fails.Count) threshold(s))"
}
Write-Output "player2_av_sync_gate: PASS (p95 <= ${MaxP95Ms}ms over ${SoakSeconds}s)"
