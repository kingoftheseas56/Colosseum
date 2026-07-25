# Player 2 efficiency gate (Task 16): >=25% lower steady GPU-busy and/or normalized CPU than the
# production mpvqt player, measured ABBA (P2, prod, prod, P2) with cooldowns so thermal drift cancels.
# Samples GPU engine utilization + process CPU via performance counters while each contender plays the
# SAME clip with equivalent settings (Loudness=Smooth, subtitles off, chrome visible).
#
# HONEST PRECONDITION: needs the production player running the same clip side-by-side-in-time. Pass
# -ProdExe (the real colosseum.exe) or the gate refuses to run rather than fake a comparison.
# Run: powershell -NoProfile -File tests/player2/player2_efficiency_abba.ps1 -ProdExe "C:\...\colosseum.exe"
param(
    [string]$Clip = "$env:USERPROFILE\Downloads\Colosseum\The Wire - S4E10 - Misgivings - 20260720_175049.mp4",
    [string]$ProdExe = "",
    [int]$PassSeconds = 180,
    [int]$CooldownSeconds = 60,
    [double]$RequiredAdvantagePct = 25.0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$harness = Join-Path $root 'native/build-player2/player2_harness.exe'
if (-not (Test-Path $harness)) { throw "harness missing: $harness" }
if (-not (Test-Path $Clip)) { throw "clip missing: $Clip" }
if (-not $ProdExe -or -not (Test-Path $ProdExe)) {
    throw ("player2_efficiency_abba: NOT RUN - needs the production player for a fair ABBA " +
           "comparison (pass -ProdExe). This gate never fabricates a baseline.")
}

$qtBin = 'C:/Qt/6.11.1/msvc2022_64/bin'; $ffBin = 'C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin'
if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
if (Test-Path $ffBin) { $env:PATH = "$ffBin;$env:PATH" }
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'

function Measure-Pass([string]$exe, [string[]]$exeArgs, [int]$seconds, [string]$logPath) {
    # Capture the contender's own output. A pass that silently failed to play would otherwise score
    # as brilliantly efficient - the single most dangerous way this gate could lie.
    $p = if ($logPath) {
        Start-Process -FilePath $exe -ArgumentList $exeArgs -PassThru -RedirectStandardError $logPath
    } else {
        Start-Process -FilePath $exe -ArgumentList $exeArgs -PassThru
    }
    Start-Sleep -Seconds 20   # warmup: open + priming excluded from the steady window
    $cpu = New-Object System.Collections.Generic.List[double]
    $gpu = New-Object System.Collections.Generic.List[double]
    $cores = [Environment]::ProcessorCount
    $t0 = Get-Date; $prevCpu = $p.TotalProcessorTime
    while (((Get-Date) - $t0).TotalSeconds -lt $seconds -and -not $p.HasExited) {
        Start-Sleep -Seconds 5
        $p.Refresh()
        if ($p.HasExited) { break }
        $nowCpu = $p.TotalProcessorTime
        $cpu.Add((($nowCpu - $prevCpu).TotalSeconds / 5.0) * 100.0 / $cores)  # normalized %
        $prevCpu = $nowCpu
        $g = (Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -ErrorAction SilentlyContinue)
        if ($g) { $gpu.Add(($g.CounterSamples | Measure-Object CookedValue -Sum).Sum) }
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    [pscustomobject]@{
        CpuPct = ($cpu | Measure-Object -Average).Average
        GpuPct = ($gpu | Measure-Object -Average).Average
        Samples = $cpu.Count
    }
}

# Both backends are measured INSIDE THE REAL APP, playing the same file through the same session
# machinery, in the same window. The only difference between the two passes is which engine draws.
# That is the whole point of the swap, and it removes every asymmetry a synthetic side-by-side has:
# an earlier attempt pitted a bare lab harness against a probe window that decoded without ever
# painting a frame, which would have handed Player 2 a fabricated win.
$logDir = Join-Path $env:TEMP 'player2_abba'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$env:COLOSSEUM_ABBA_CLIP = $Clip

function Start-Pass([string]$name, [bool]$usePlayer2, [int]$seconds, [string]$log) {
    if ($usePlayer2) { $env:COLOSSEUM_PLAYER2 = '1' } else { Remove-Item Env:\COLOSSEUM_PLAYER2 -ErrorAction SilentlyContinue }
    $m = Measure-Pass $ProdExe @('qml\Main.qml') $seconds $log
    Remove-Item Env:\COLOSSEUM_PLAYER2 -ErrorAction SilentlyContinue
    return $m
}

# ABBA: P2, prod, prod, P2 - order effects and thermal ramp cancel in the pairwise means.
$order = @(
    @{ Name='P2';   P2=$true  },
    @{ Name='prod'; P2=$false },
    @{ Name='prod'; P2=$false },
    @{ Name='P2';   P2=$true  })
$results = @()
$passIndex = 0
foreach ($pass in $order) {
    $passIndex++
    Write-Output "player2_efficiency_abba: measuring $($pass.Name) for ${PassSeconds}s..."
    $log = Join-Path $logDir "$($pass.Name)-$passIndex.log"
    $results += [pscustomobject]@{ Name = $pass.Name
                                   Log  = $log
                                   M    = (Start-Pass $pass.Name $pass.P2 $PassSeconds $log) }
    Start-Sleep -Seconds $CooldownSeconds
}
Remove-Item Env:\COLOSSEUM_ABBA_CLIP -ErrorAction SilentlyContinue

# Refuse to score any pass that did not actually play. A pass that silently failed would look
# maximally efficient - the most dangerous way this gate could lie.
foreach ($r in $results) {
    $text = if (Test-Path $r.Log) { Get-Content -Raw $r.Log } else { '' }
    if ($text -notmatch '\[abba\] auto-playing') {
        throw "player2_efficiency_abba: INVALID - $($r.Name) never started the clip ($($r.Log))."
    }
    $expected = if ($r.Name -eq 'P2') { 'backend = PLAYER 2' } else { 'backend = mpv' }
    if ($text -notmatch [regex]::Escape($expected)) {
        throw "player2_efficiency_abba: INVALID - $($r.Name) pass did not run on the expected backend ($($r.Log))."
    }
    if ($r.Name -eq 'P2' -and $text -match 'falling back to mpv') {
        throw "player2_efficiency_abba: INVALID - the Player 2 pass fell back to mpv mid-measurement ($($r.Log))."
    }
}

$p2Cpu   = ($results | Where-Object Name -eq 'P2'   | ForEach-Object { $_.M.CpuPct } | Measure-Object -Average).Average
$prodCpu = ($results | Where-Object Name -eq 'prod' | ForEach-Object { $_.M.CpuPct } | Measure-Object -Average).Average
$p2Gpu   = ($results | Where-Object Name -eq 'P2'   | ForEach-Object { $_.M.GpuPct } | Measure-Object -Average).Average
$prodGpu = ($results | Where-Object Name -eq 'prod' | ForEach-Object { $_.M.GpuPct } | Measure-Object -Average).Average
$cpuAdv = if ($prodCpu -gt 0) { (1 - $p2Cpu / $prodCpu) * 100 } else { 0 }
$gpuAdv = if ($prodGpu -gt 0) { (1 - $p2Gpu / $prodGpu) * 100 } else { 0 }

Write-Output ""
Write-Output ("player2_efficiency_abba: CPU p2=$([math]::Round($p2Cpu,1))% prod=$([math]::Round($prodCpu,1))% adv=$([math]::Round($cpuAdv,1))% | " +
    "GPU p2=$([math]::Round($p2Gpu,1))% prod=$([math]::Round($prodGpu,1))% adv=$([math]::Round($gpuAdv,1))%")
Write-Output ("player2_efficiency_abba: both passes ran the SAME app on the SAME clip; only the engine differed. " +
    "The GPU counter is SYSTEM-WIDE, so the machine must be otherwise idle.")

if ($cpuAdv -lt $RequiredAdvantagePct -and $gpuAdv -lt $RequiredAdvantagePct) {
    throw "player2_efficiency_abba: FAIL (advantage reaches neither ${RequiredAdvantagePct}% on CPU nor GPU - HARD NO-GO)"
}
Write-Output "player2_efficiency_abba: PASS (>=${RequiredAdvantagePct}% advantage held)"
