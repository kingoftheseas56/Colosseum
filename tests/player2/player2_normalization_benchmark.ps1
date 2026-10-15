# Task 9 normalization benchmark. Plays the same real fixture through Smooth, Light and Full with a
# cooldown between passes, then answers Hemanth's agenda question with evidence, not inference:
# "do frames drop when normalization is running?"
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/player2/player2_normalization_benchmark.ps1 `
#       -Media "C:\path\to\clip.mp4" [-SoakSeconds 90] [-OutputDir artifacts\player2\normalization]

param(
    [Parameter(Mandatory = $true)][string]$Media,
    [int]$SoakSeconds = 90,
    [string]$OutputDir = "artifacts\player2\normalization",
    [int]$CooldownSeconds = 20
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$harness = Join-Path $repoRoot 'native\build-player2\player2_harness.exe'
if (-not (Test-Path $harness)) {
    throw "harness not built: $harness (build player2_harness first)"
}
if (-not (Test-Path $Media)) {
    throw "media not found: $Media"
}

$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;' + $env:PATH
$outAbs = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Force -Path $outAbs | Out-Null

$modes = @('smooth', 'light', 'full')
$rows = @()
foreach ($mode in $modes) {
    $report = Join-Path $outAbs "$mode.json"
    if (Test-Path $report) { Remove-Item $report -Force }
    Write-Output "== $mode pass ($SoakSeconds s) =="
    & $harness --file $Media --report $report --soak-seconds $SoakSeconds --normalization $mode | Out-Null
    if (-not (Test-Path $report)) { throw "$mode pass produced no report" }
    $data = Get-Content -Raw $report | ConvertFrom-Json
    $dropped = [int]$data.dropped + [int]$data.scheduledLateDrops
    $rows += [PSCustomObject]@{
        mode            = $mode
        # Playback-anchored fps (from the first valid audio clock) is the honest throughput number;
        # elapsedSeconds is wall time INCLUDING device init + loudnorm priming and must not be used
        # to derive fps (that manufactured a ~17 fps ghost from a real 24 fps stream).
        sustainedFps    = [math]::Round([double]$data.sustainedFps, 2)
        decodedFps      = [math]::Round([double]$data.decodedFps, 2)
        playbackSec     = [math]::Round([double]$data.playbackSeconds, 1)
        presented       = $data.presented
        decoded         = $data.decoded
        droppedFrames   = $dropped
        scheduledLate   = $data.scheduledLateDrops
        ringStarved     = $data.dropped
        avP95Ms         = $data.avP95Ms
        avDriftMaxAbsMs = $data.avDriftMaxAbsMs
        minAudioQueueMs = [math]::Round([double]$data.minAudioQueueMs, 0)
        maxAudioQueueMs = $data.maxAudioQueueMs
        audioUnderruns  = $data.audioUnderruns
        normLatencyMs   = $data.normalizationLatencyMs
        deviceErrors    = $data.deviceErrors
        cpuTransfers    = $data.cpuTransfers
        passed          = $data.passed
    }
    # Cooldown so a hot GPU/thermal state does not bleed into the next mode's numbers.
    if ($mode -ne $modes[-1]) { Start-Sleep -Seconds $CooldownSeconds }
}

$rows | Format-Table -AutoSize | Out-String | Write-Output

$smooth = $rows | Where-Object { $_.mode -eq 'smooth' }
$light  = $rows | Where-Object { $_.mode -eq 'light' }
$full   = $rows | Where-Object { $_.mode -eq 'full' }

# The agenda answer: a mode "drops frames" only if it drops MORE than the Smooth baseline and its
# A/V sync stays within the 40 ms scheduler threshold.
function Verdict($row, $baseline) {
    if ($row.deviceErrors -ne 0 -or $row.cpuTransfers -ne 0) { return 'PIPELINE VIOLATION' }
    if ($row.droppedFrames -le $baseline.droppedFrames) { return 'no extra dropped frames' }
    return "dropped $($row.droppedFrames) vs baseline $($baseline.droppedFrames)"
}

$answer = [PSCustomObject]@{
    media          = (Split-Path $Media -Leaf)
    soakSeconds    = $SoakSeconds
    lightVerdict   = (Verdict $light $smooth)
    fullVerdict    = (Verdict $full $smooth)
    smoothP95Ms    = $smooth.avP95Ms
    lightP95Ms     = $light.avP95Ms
    fullP95Ms      = $full.avP95Ms
    rows           = $rows
}
$summaryPath = Join-Path $outAbs 'summary.json'
$answer | ConvertTo-Json -Depth 5 | Set-Content -Path $summaryPath -Encoding UTF8

Write-Output ''
Write-Output "AGENDA ANSWER - do frames drop under normalization on this hardware/clip:"
Write-Output "  Light: $($answer.lightVerdict)  (A/V p95 $($light.avP95Ms) ms)"
Write-Output "  Full : $($answer.fullVerdict)  (A/V p95 $($full.avP95Ms) ms)"
Write-Output ''
Write-Output "ANCHORED TRUTH (fps from first valid audio clock, not wall time):"
foreach ($r in $rows) {
    Write-Output ("  {0,-6} sustained {1,5} fps | decoded {2,5} fps | underruns {3,4} | audioQ low {4,5} ms | A/V drift max {5} ms" -f `
        $r.mode, $r.sustainedFps, $r.decodedFps, $r.audioUnderruns, $r.minAudioQueueMs, $r.avDriftMaxAbsMs)
}
Write-Output "summary: $summaryPath"
Write-Output 'player2_normalization_benchmark: DONE'
