# Player 2 promotion gate summary (Task 16, deterministic tier). Runs every fast, deterministic gate —
# the correctness unit tests, the isolation/orchestration contracts, the QML logic gate, and a
# numeric-proof smoke of the report path — and prints one verdict. Exits nonzero if ANY gate fails, so
# it is the single "is Player 2 correct + isolated?" check before promotion.
#
# NOT included here (separate long runs, hours each, and they compare against the production player):
#   player2_av_sync_gate.ps1 (30-min A/V p95<=40ms), player2_seek_soak.ps1 (100 seeks),
#   player2_memory_soak.ps1 (2-hr Wire soak + 50 open/close), player2_efficiency_abba.ps1
#   (>=25% lower GPU/CPU vs mpvqt), player2_hardware_matrix.ps1 (Intel + discrete GPU).
# Run those on a release build before flipping the default; this summary certifies the deterministic tier.
# Run: powershell -NoProfile -File tests/player2/player2_gate_summary.ps1

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root 'native/build-player2'
$testsDir = $PSScriptRoot

# Runtime deps on PATH + the offscreen license bypass, so the gate is self-contained.
$qtBin = 'C:/Qt/6.11.1/msvc2022_64/bin'
$ffBin = 'C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin'
if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
if (Test-Path $ffBin) { $env:PATH = "$ffBin;$env:PATH" }
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'

$results = @()
function Record([string]$Name, [bool]$Passed, [string]$Detail = '') {
    $script:results += [pscustomobject]@{ Gate = $Name; Passed = $Passed; Detail = $Detail }
}

# --- correctness unit tests (pure, no media fixtures) ---
$unitTests = @(
    'player2_host_services_test', 'player2_subtitle_image_test', 'player2_subtitle_schedule_test',
    'player2_state_machine_test', 'player2_packet_queue_test', 'player2_playback_metrics_test',
    'player2_audio_normalizer_test', 'player2_track_policy_test', 'player2_clock_scheduler_test'
)
foreach ($t in $unitTests) {
    $exe = Join-Path $build "$t.exe"
    if (-not (Test-Path $exe)) { Record $t $false 'binary missing (build it)'; continue }
    & $exe *> $null
    Record $t ($LASTEXITCODE -eq 0) "exit $LASTEXITCODE"
}

# --- isolation / orchestration / logic contracts ---
$contracts = @(
    'player2_shell_contract.ps1', 'player2_orchestration_contract.ps1', 'player2_browser_logic_contract.ps1',
    'player2_shortcuts_contract.ps1'
)
foreach ($c in $contracts) {
    $path = Join-Path $testsDir $c
    if (-not (Test-Path $path)) { Record $c $false 'script missing'; continue }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $path *> $null
    Record $c ($LASTEXITCODE -eq 0) "exit $LASTEXITCODE"
}

# --- numeric-proof smoke: the synthetic report path must reach a passing gate ---
$harness = Join-Path $build 'player2_harness.exe'
if (Test-Path $harness) {
    $report = Join-Path $env:TEMP 'player2_gate_smoke_report.json'
    Remove-Item $report -ErrorAction SilentlyContinue
    & $harness --scenario synthetic --report $report *> $null
    $ok = $false; $detail = "exit $LASTEXITCODE"
    if ((Test-Path $report)) {
        try {
            $j = Get-Content -Raw $report | ConvertFrom-Json
            $ok = [bool]$j.passed -and ($j.deviceErrors -eq 0) -and ($j.cpuTransfers -eq 0)
            $detail = "passed=$($j.passed) presented=$($j.presented) deviceErrors=$($j.deviceErrors)"
        } catch { $detail = "report parse failed: $_" }
    }
    Record 'numeric-proof (synthetic report)' $ok $detail
} else {
    Record 'numeric-proof (synthetic report)' $false 'harness missing'
}

# --- verdict ---
Write-Output ''
Write-Output 'Player 2 promotion gates (deterministic tier):'
foreach ($r in $results) {
    $tag = if ($r.Passed) { 'PASS' } else { 'FAIL' }
    Write-Output ("  [{0}] {1,-42} {2}" -f $tag, $r.Gate, $r.Detail)
}
$failed = @($results | Where-Object { -not $_.Passed })
Write-Output ''
if ($failed.Count -gt 0) {
    Write-Output "PLAYER2 PROMOTION GATES: FAIL ($($failed.Count) of $($results.Count) gate(s) failed)"
    exit 1
}
Write-Output "PLAYER2 PROMOTION GATES: PASS ($($results.Count) deterministic gates)"
exit 0
