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

$peerDrainTest = Join-Path (Split-Path -Parent $PeerScript) 'test_tiny_peer_drain.py'
if (-not (Test-Path -LiteralPath $peerDrainTest)) {
    throw "GREEN precondition violated: tiny peer drain test is missing: $peerDrainTest"
}
$peerDrainOutput = & python $peerDrainTest 2>&1
$peerDrainOutput | Set-Content -LiteralPath (Join-Path $run 'tiny-peer-drain.transcript')
if ($LASTEXITCODE -ne 0) { throw "tiny peer drain test failed with exit $LASTEXITCODE" }

$probeSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ProbeExe).Hash.ToLowerInvariant()
$control = Join-Path $run 'control'
New-Item -ItemType Directory -Force -Path $control | Out-Null
$prepare = & $ProbeExe '--prepare' $control 2>&1
$prepare | Set-Content -LiteralPath (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "prepare failed with exit $LASTEXITCODE" }
Add-Content -LiteralPath (Join-Path $control 'dependency_identity.txt') -Value "probe_executable_sha256=$probeSha256"

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
    foreach ($spec in $peerSpecs) {
        $readyDeadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $readyDeadline) {
            if ((Test-Path -LiteralPath $spec[2]) -and
                (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $spec[2])) {
                break
            }
            Start-Sleep -Milliseconds 20
        }
        if (-not (Test-Path -LiteralPath $spec[2]) -or
            -not (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $spec[2])) {
            throw "tiny peer did not become ready: $($spec[1])"
        }
    }
    $probeOutput = & $ProbeExe '--run' $control `
        '49101' '49102' $controlLogA $controlLogB `
        '49103' '49104' $controlledLogA $controlledLogB `
        '49105' '49106' $lifecycleLogA $lifecycleLogB $release $probeSha256 2>&1
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
$controlledTranscriptPath = Join-Path $control 'lifecycle-controlled.transcript'
$handshakeALine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'HANDSHAKE_VALID peer=A' | Select-Object -First 1).LineNumber
$handshakeBLine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'HANDSHAKE_VALID peer=B' | Select-Object -First 1).LineNumber
$advertiseALine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'ADVERTISE peer=A piece=0' | Select-Object -First 1).LineNumber
$advertiseBLine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'ADVERTISE peer=B piece=0' | Select-Object -First 1).LineNumber
$commandLine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'COMMAND target=B piece=0 offset=0 length=16384' | Select-Object -First 1).LineNumber
$receivedLine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'PIECE_RECEIVED peer=B piece=0 offset=0 length=16384' | Select-Object -First 1).LineNumber
$acceptedLine = (Select-String -LiteralPath $controlledTranscriptPath -SimpleMatch 'PIECE_ACCEPTED num_pieces=1' | Select-Object -First 1).LineNumber
if (@($handshakeALine, $handshakeBLine, $advertiseALine, $advertiseBLine, $commandLine, $receivedLine, $acceptedLine) -contains $null) {
    throw 'controlled lifecycle transcript is missing required ordered evidence'
}
if ($commandLine -le $handshakeALine -or $commandLine -le $handshakeBLine -or
    $commandLine -le $advertiseALine -or $commandLine -le $advertiseBLine) {
    throw 'commanded request occurred before both valid handshakes and piece advertisements'
}
if ($receivedLine -le $commandLine -or $acceptedLine -le $receivedLine) {
    throw 'commanded response receipt/acceptance order is invalid'
}
$feasibilityPath = Join-Path $PSScriptRoot '..\..\..\docs\server1\P08A-FEASIBILITY.json'
Copy-Item -LiteralPath $resultPath -Destination $feasibilityPath -Force
if ($result.case_01.state -ne 'PASS') { throw 'P08A-01 did not pass' }
if ($result.case_02.state -ne 'PASS') { throw 'P08A-02 did not pass' }
if ($result.case_03.state -ne 'PASS') { throw 'P08A-03 did not pass' }
if ($null -eq $result.case_01.peer_a_valid_handshakes -or $result.case_01.peer_a_valid_handshakes -lt 1) {
    throw 'controlled peer A did not complete a valid info-hash handshake'
}
if ($null -eq $result.case_01.peer_b_valid_handshakes -or $result.case_01.peer_b_valid_handshakes -lt 1) {
    throw 'controlled peer B did not complete a valid info-hash handshake'
}
if ($null -eq $result.case_01.peer_a_piece_advertisements -or $result.case_01.peer_a_piece_advertisements -lt 1) {
    throw 'controlled peer A did not advertise the commanded piece'
}
if ($null -eq $result.case_01.peer_b_piece_advertisements -or $result.case_01.peer_b_piece_advertisements -lt 1) {
    throw 'controlled peer B did not advertise the commanded piece'
}
if (-not $result.case_01.command_issued_after_both_peers_advertised) {
    throw 'commanded request was not gated on both peers advertising the piece'
}
if ($result.case_01.peer_a_exact_requests -ne 0) { throw 'controlled peer A received the exact request' }
if (-not $result.case_02.ordinary_picker_available) { throw 'normal picker control phase did not emit wire work' }
if ($null -eq $result.case_02.controlled_suppressed_picker_attempts -or $result.case_02.controlled_suppressed_picker_attempts -le 0) {
    throw 'controlled phase did not observe a suppressed ordinary-picker attempt'
}
if (-not $result.case_01.commanded_response_received_and_accepted) { throw 'commanded response was not accepted' }
if ($result.case_03.replayed_or_second_requests -ne 0) { throw 'lifecycle phase replayed the request' }
if (-not $result.case_03.post_destroy_process_alive) { throw 'probe did not survive session destruction' }
if (-not $result.case_03.late_peer_event_after_destroy) { throw 'peer did not send or drop after destruction' }
if (-not $result.case_03.no_orphaned_client_connections) { throw 'lifecycle left an orphaned client connection' }
if ($result.case_03.peer_a_valid_handshakes -lt 1 -or $result.case_03.peer_b_valid_handshakes -lt 1) {
    throw 'lifecycle phase did not connect both peers'
}
if ($result.dependency.probe_executable_sha256 -ne $probeSha256) {
    throw 'result is not bound to the executable used for this run'
}
Write-Output "GREEN PASS: $($result.seam_classification); controlled_handshakes=A$($result.case_01.peer_a_valid_handshakes)/B$($result.case_01.peer_b_valid_handshakes); both_advertised_before_command=$($result.case_01.command_issued_after_both_peers_advertised); control_picker_requests=$($result.case_02.control_phase_wire_requests); controlled_suppressed=$($result.case_02.controlled_suppressed_picker_attempts); controlled_B_exact=$($result.case_01.peer_b_exact_requests); controlled_A_exact=$($result.case_01.peer_a_exact_requests); controlled_unowned=$($result.case_02.unowned_wire_requests); lifecycle_handshakes=$($result.case_03.connected_peer_handshakes); lifecycle_replayed=$($result.case_03.replayed_or_second_requests)"
