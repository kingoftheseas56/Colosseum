<#
.SYNOPSIS
  Capture a video player's CPU + per-GPU-engine utilization — 2026-07-28 player-stutter A/B.
  Method mirrors the 2026-07-20 efficiency gate: per-PID "GPU Engine" counters (the Task
  Manager definition of process GPU%), plus process CPU via TotalProcessorTime deltas.

  Purpose: find the resource DELTA between a smooth reference player (standalone mpv /
  PotPlayer) and Colosseum on the same Tenet file. If Colosseum burns budget its players
  don't get, BOTH players in Colosseum judder identically regardless of render backend.

.PARAMETER ProcessName
  Process name without .exe, e.g. "colosseum", "mpv", "PotPlayerMini64". Used if ProcId omitted.
.PARAMETER ProcId
  Process id (takes precedence over ProcessName).
.PARAMETER Seconds   Sample duration (default 60).
.PARAMETER IntervalMs Sample cadence (default 1000).
.PARAMETER Label   Free-text tag written into the output, e.g. "colosseum-tenet", "mpv-tenet".

.EXAMPLE
  .\capture_player_perf.ps1 -ProcessName mpv -Seconds 60 -Label mpv-tenet
  .\capture_player_perf.ps1 -ProcessName colosseum -Seconds 60 -Label colosseum-tenet
#>
param(
  [string]$ProcessName = "",
  [int]   $ProcId = 0,
  [int]   $Seconds = 60,
  [int]   $IntervalMs = 1000,
  [string]$Label = "run"
)
$ErrorActionPreference = "SilentlyContinue"

if ($ProcId -gt 0) {
  $target = $ProcId
} elseif ($ProcessName) {
  $target = (Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1).Id
} else { $target = 0 }

if (-not $target) {
  Write-Host "ERROR: process not found (ProcessName='$ProcessName' ProcId=$ProcId). Start the player first." -ForegroundColor Red
  exit 2
}

$logicals = [Environment]::ProcessorCount
$frequency = [Diagnostics.Stopwatch]::Frequency   # QPC ticks per second
Write-Host "Capturing PID $target ($ProcessName) for $Seconds s  (label='$Label', $logicals logical CPUs)..." -ForegroundColor Cyan

$rows = New-Object System.Collections.Generic.List[object]
$sw = [Diagnostics.Stopwatch]::StartNew()

$prev = Get-Process -Id $target -ErrorAction SilentlyContinue
if (-not $prev) { Write-Host "ERROR: PID $target not running." -ForegroundColor Red; exit 2 }
$prevCpu  = $prev.TotalProcessorTime.Ticks     # 100-ns units
$prevWall = $sw.ElapsedTicks                    # QPC units

for ($i = 0; $i -lt $Seconds; $i++) {
  Start-Sleep -Milliseconds $IntervalMs
  $cur = Get-Process -Id $target -ErrorAction SilentlyContinue
  if (-not $cur) { Write-Host "PID $target ended at sample $i." -ForegroundColor Yellow; break }

  $curCpu  = $cur.TotalProcessorTime.Ticks
  $curWall = $sw.ElapsedTicks
  # CPU seconds (100ns ticks -> s) / wall seconds (QPC ticks -> s), normalized to whole-CPU %.
  $cpuSec  = ($curCpu - $prevCpu) / 10000000.0
  $wallSec = ($curWall - $prevWall) / $frequency
  $cpuPct  = if ($wallSec -gt 0) { ($cpuSec / $wallSec) * 100 / $logicals } else { 0 }

  # GPU: sum "Utilization Percentage" across THIS pid's GPU Engine instances (per-PID, the
  # Task Manager definition). Instance names look like pid_<PID>_engtype_<n>_phys_<n>.
  $gpu = 0.0
  $gc = Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue
  if ($gc) {
    $sum = ($gc.CounterSamples |
      Where-Object { $_.InstanceName -like "pid_$target*" } |
      Measure-Object -Property CookedValue -Sum).Sum
    if ($sum) { $gpu = $sum }
  }

  $rows.Add([pscustomobject]@{
    sample   = $i
    cpu_pct  = [math]::Round($cpuPct, 2)
    gpu_pct  = [math]::Round($gpu, 2)
    ws_mb    = [math]::Round($cur.WorkingSet64 / 1MB, 0)
  })
  $prevCpu = $curCpu; $prevWall = $curWall
}

if ($rows.Count -eq 0) { Write-Host "No samples captured." -ForegroundColor Red; exit 1 }

# Drop the first sample (warm-up / timing skew), then summarize.
$use = $rows | Select-Object -Skip 1
function P95($v) { $a = $v | Sort-Object; return $a[[int][math]::Floor(0.95 * ($a.Count - 1))] }

$cpuMean = ($use | Measure-Object -Property cpu_pct -Average).Average
$gpuMean = ($use | Measure-Object -Property gpu_pct -Average).Average
$wsMean  = ($use | Measure-Object -Property ws_mb   -Average).Average

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $PSScriptRoot ("perf_${Label}_${stamp}.csv")
$use | Export-Csv -Path $out -NoTypeInformation

Write-Host ""
Write-Host ("=== {0}  (PID {1}, {2} samples) ===" -f $Label, $target, $use.Count) -ForegroundColor Green
Write-Host ("CPU   mean {0,6:N1}%   p95 {1,6:N1}%" -f $cpuMean, (P95 $use.cpu_pct))
Write-Host ("GPU   mean {0,6:N1}%   p95 {1,6:N1}%" -f $gpuMean, (P95 $use.gpu_pct))
Write-Host ("RAM   mean {0,6:N0} MB" -f $wsMean)
Write-Host "samples -> $out"
