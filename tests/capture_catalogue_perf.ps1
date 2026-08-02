param(
    [ValidateSet("Probe", "App")] [string]$Mode = "Probe",
    [string]$Page = "Movies",
    [ValidateSet("Cold", "Warm")] [string]$Pass = "Cold",
    [int]$Seconds = 20
)

# Catalogue Poster and Shelf Polish - runtime profiling capture (Task 8).
#
#   -Mode Probe  : run the deterministic residency probe offscreen and PASS/FAIL the object-count
#                  plateau + horizontal-restore gates from its PROBE markers (fully automated).
#   -Mode App    : launch the built colosseum.exe with QSG_RENDER_TIMING=1 and stderr logging so a
#                  human can scroll Theatre and capture real warm/cold frame timing. The raw QSG log
#                  is written under the eyes-on folder; frame-budget parsing runs on that log.
#
# qml.exe / colosseum.exe are GUI-subsystem binaries, so QT_FORCE_STDERR_LOGGING routes console +
# QSG timing to the captured stream.
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
$appExe = Join-Path $root "native\build-msvc\colosseum.exe"
$outDir = Join-Path $root "agents\eyes-on\theatre-catalogue-polish"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$env:QT_FORCE_STDERR_LOGGING = "1"

# -- plateau + restore gates (bounds are viewport-derived, NOT row-count-derived) --
$PLATEAU_MAX = 12   # a 900px viewport over 25 rows can hold at most ~3 viewports of shelves live
$RESTORE_TOL = 1    # logical px

function Fail($msg) { Write-Host "CATALOGUE_PERF_FAIL: $msg"; exit 1 }

if ($Mode -eq "Probe") {
    if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }
    $probe = Join-Path $root "tests\catalogue_residency_probe.qml"
    $log = Join-Path $outDir "residency-probe.log"
    $out = cmd /c "`"$qmlExe`" -platform offscreen -I `"$(Join-Path $root 'qml')`" `"$probe`" 2>&1" | Out-String
    $code = $LASTEXITCODE
    Set-Content -LiteralPath $log -Value $out
    Write-Host "----- residency probe (exit $code) -----"
    Write-Host $out
    if ($code -ne 0) { Fail "probe exited $code" }
    if ($out -notmatch "CATALOGUE_RESIDENCY_PROBE_DONE") { Fail "probe did not complete" }

    if ($out -notmatch "PROBE total=(\d+)") { Fail "no total marker" }
    $total = [int]$Matches[1]
    if ($out -notmatch "PROBE liveShelfCount top=(\d+) middle=(\d+) bottom=(\d+) returnedTop=(\d+)") { Fail "no liveShelfCount marker" }
    $top = [int]$Matches[1]; $mid = [int]$Matches[2]; $bot = [int]$Matches[3]; $ret = [int]$Matches[4]
    if ($out -notmatch "PROBE restoreContentX want=([\d.]+) got=([-\d.]+) err=([\d.]+)") { Fail "no restore marker" }
    $err = [double]$Matches[3]

    $maxLive = ($top, $mid, $bot, $ret | Measure-Object -Maximum).Maximum
    Write-Host "total=$total  live[top=$top mid=$mid bot=$bot ret=$ret]  maxLive=$maxLive  restoreErr=$err"

    # gate 1: live counts must plateau (bounded by the viewport window, not the row total)
    if ($maxLive -ge $total) { Fail "live shelves reached the row total ($maxLive >= $total) - lazy residency broke" }
    if ($maxLive -gt $PLATEAU_MAX) { Fail "live shelves exceeded the viewport plateau ($maxLive > $PLATEAU_MAX)" }
    # gate 2: returning to the top must not balloon the live set (no leak)
    if ($ret -gt ($top + 1)) { Fail "returned-to-top live count grew beyond the initial plateau ($ret > $top + 1)" }
    # gate 3: horizontal position must restore within 1px
    if ($err -gt $RESTORE_TOL) { Fail "horizontal restore drifted ($err > $RESTORE_TOL px)" }

    Write-Host "CATALOGUE_PERF_PROBE_PASS (plateau maxLive=$maxLive of $total rows; restoreErr=$err)"
    exit 0
}

if ($Mode -eq "App") {
    if (!(Test-Path -LiteralPath $appExe)) { throw "colosseum.exe not found at $appExe - build native/build-msvc first" }
    $stamp = "$Page-$Pass"
    $log = Join-Path $outDir "qsg-frametiming-$stamp.log"
    $env:QSG_RENDER_TIMING = "1"
    Write-Host "Launching colosseum.exe with QSG_RENDER_TIMING for $Seconds s."
    Write-Host "SCROLL Theatre > $Page (and open a See-all) during capture - this is the eyes-on warm-scroll pass."
    Write-Host "Frame timing -> $log"
    $p = Start-Process -FilePath $appExe -PassThru -RedirectStandardError $log -RedirectStandardOutput (Join-Path $outDir "app-stdout-$stamp.log")
    Start-Sleep -Seconds $Seconds
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }

    if (Test-Path -LiteralPath $log) {
        # QSG_RENDER_TIMING prints per-frame timings in ms; flag any frame over the 100ms stall bound.
        $stalls = Select-String -LiteralPath $log -Pattern "Frame prepared.*total\s+(\d+)" -AllMatches |
                  ForEach-Object { $_.Matches } | ForEach-Object { [int]$_.Groups[1].Value } |
                  Where-Object { $_ -gt 100 }
        if ($stalls) { Write-Host "CATALOGUE_PERF_APP_WARN: frames over 100ms observed: $($stalls -join ', ')" }
        else { Write-Host "CATALOGUE_PERF_APP: no >100ms frame stall parsed in $log (confirm visually + warm-scroll <16.7ms is Hemanth's eyes-on)" }
    }
    exit 0
}
