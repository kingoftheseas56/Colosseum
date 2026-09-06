param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('red', 'green')]
    [string]$Phase,
    [string]$ProbeExe = (Join-Path $PSScriptRoot 'bin/exact_block_probe.exe'),
    [string]$PeerScript = (Join-Path $PSScriptRoot 'probe-src/tiny_peer.py')
)

$ErrorActionPreference = 'Stop'
$raw = Join-Path $PSScriptRoot 'raw'
$run = Join-Path $raw ("run-" + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null

if ($Phase -eq 'red') {
    if (Test-Path -LiteralPath $ProbeExe) {
        throw "RED precondition violated: probe binary already exists: $ProbeExe"
    }
    Write-Output 'RED expected failure: probe executable is not present'
    exit 1
}

if (-not (Test-Path -LiteralPath $ProbeExe)) {
    throw "GREEN precondition violated: probe executable is missing: $ProbeExe"
}
if (-not (Test-Path -LiteralPath $PeerScript)) {
    throw "GREEN precondition violated: tiny peer script is missing: $PeerScript"
}

$control = Join-Path $run 'control'
New-Item -ItemType Directory -Force -Path $control | Out-Null
$prepare = & $ProbeExe '--prepare' $control 2>&1
$prepare | Set-Content -LiteralPath (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "prepare failed with exit $LASTEXITCODE" }

$infoHash = (Get-Content -Raw (Join-Path $control 'info_hash.txt')).Trim()
$controlLogA = Join-Path $run 'control-peer-A-wire.log'
$controlLogB = Join-Path $run 'control-peer-B-wire.log'
$controlledLogA = Join-Path $run 'controlled-peer-A-wire.log'
$controlledLogB = Join-Path $run 'controlled-peer-B-wire.log'
$lifecycleLogA = Join-Path $run 'lifecycle-peer-A-wire.log'
$lifecycleLogB = Join-Path $run 'lifecycle-peer-B-wire.log'
$release = Join-Path $run 'lifecycle.release'

$peerSpecs = @(
    @('49101', 'A-control', $controlLogA),
    @('49102', 'B-control', $controlLogB),
    @('49103', 'A-controlled', $controlledLogA),
    @('49104', 'B-controlled', $controlledLogB),
    @('49105', 'A-lifecycle', $lifecycleLogA),
    @('49106', 'B-lifecycle', $lifecycleLogB)
)
$peerJobs = @()
foreach ($spec in $peerSpecs) {
    $args = @($PeerScript, '--port', $spec[0], '--label', $spec[1], '--info-hash', $infoHash, '--log', $spec[2], '--delay-ms', '250')
    if ($spec[1] -eq 'B-lifecycle') {
        $args += @('--delay-ms', '800', '--release-file', $release)
    }
    $peerJobs += Start-Process -FilePath 'python' -ArgumentList $args -PassThru -WindowStyle Hidden
}
try {
    Start-Sleep -Milliseconds 500
    $probeOutput = & $ProbeExe '--run' $control `
        '49101' '49102' $controlLogA $controlLogB `
        '49103' '49104' $controlledLogA $controlledLogB `
        '49105' '49106' $lifecycleLogA $lifecycleLogB $release 2>&1
    $probeOutput | Set-Content -LiteralPath (Join-Path $run 'probe.transcript')
    $probeExit = $LASTEXITCODE
}
finally {
    foreach ($process in $peerJobs) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
}

if ($probeExit -ne 0) { throw "probe failed with exit $probeExit" }
$resultPath = Join-Path $control 'result.json'
$result = Get-Content -Raw $resultPath | ConvertFrom-Json
$feasibilityPath = Join-Path $PSScriptRoot '..\..\..\docs\server1\P08A-FEASIBILITY.json'
Copy-Item -LiteralPath $resultPath -Destination $feasibilityPath -Force
if ($result.case_01.state -ne 'PASS') { throw 'P08A-01 did not pass' }
if ($result.case_02.state -ne 'PASS') { throw 'P08A-02 did not pass' }
if ($result.case_03.state -ne 'PASS') { throw 'P08A-03 did not pass' }
if (-not $result.case_02.ordinary_picker_available) { throw 'normal picker control phase did not emit wire work' }
if (-not $result.case_01.commanded_response_received_and_accepted) { throw 'commanded response was not accepted' }
if ($result.case_03.replayed_or_second_requests -ne 0) { throw 'lifecycle phase replayed the request' }
if (-not $result.case_03.post_destroy_process_alive) { throw 'probe did not survive session destruction' }
if (-not $result.case_03.late_peer_event_after_destroy) { throw 'peer did not send or drop after destruction' }
if (-not $result.case_03.no_orphaned_client_connections) { throw 'lifecycle left an orphaned client connection' }
Write-Output "GREEN PASS: $($result.seam_classification); control_picker_requests=$($result.case_02.control_phase_wire_requests); controlled_B_exact=$($result.case_01.peer_b_exact_requests); controlled_A_exact=$($result.case_01.peer_a_exact_requests); controlled_unowned=$($result.case_02.unowned_wire_requests); lifecycle_replayed=$($result.case_03.replayed_or_second_requests)"
