param(
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
    [int]$PortA = 49819,
    [int]$PortB = 49820
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../K10-A/controlled_peer_startup.ps1')
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/pause-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10-G prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$peerScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../K10-A/controlled_peer.py')).Path
$wireA = Join-Path $run 'active-wire.log'
$wireB = Join-Path $run 'deferred-wire.log'
$peerA = Start-ControlledPeer -Log $wireA -Arguments @(
    $peerScript, '--port', [string]$PortA, '--info-hash', $hash,
    '--log', $wireA, '--pieces', '0,1', '--byte', 'P', '--peer-tag', 'active')
$peerB = Start-ControlledPeer -Log $wireB -Arguments @(
    $peerScript, '--port', [string]$PortB, '--info-hash', $hash,
    '--log', $wireB, '--pieces', '0,1', '--byte', 'P', '--peer-tag', 'deferred')
try {
    Wait-ControlledPeers -Case 'K10-G' -Peers @($peerA, $peerB) -Logs @($wireA, $wireB)
    $PortA = Get-ControlledPeerPort $wireA
    $PortB = Get-ControlledPeerPort $wireB
    $candidateTranscript = Join-Path $run 'candidate.transcript'
    & $exe --pause $run $PortA $PortB *> $candidateTranscript
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $candidateTranscript
        throw "K10-G pause case failed: $LASTEXITCODE"
    }
} finally {
    foreach ($peer in @($peerA, $peerB)) {
        if ($peer -and -not $peer.HasExited) {
            Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Assert-NoUtpDial -Case 'K10-G' -Logs @($wireA, $wireB)
$activeHandshakes = @(Select-String -LiteralPath $wireA -Pattern '^HANDSHAKE ').Count
$deferredHandshakes = @(Select-String -LiteralPath $wireB -Pattern '^HANDSHAKE ').Count
$activeRequests = @(Select-String -LiteralPath $wireA -Pattern '^REQUEST .*piece=0 start=0 length=16384$').Count
$deferredRequests = @(Select-String -LiteralPath $wireB -Pattern '^REQUEST ').Count
$wireMismatch = $activeHandshakes -ne 1 -or $deferredHandshakes -ne 1 `
    -or $activeRequests -ne 1 -or $deferredRequests -ne 0
if ($wireMismatch) {
    throw "K10-G wire mismatch active_handshakes=$activeHandshakes deferred_handshakes=$deferredHandshakes active_requests=$activeRequests deferred_requests=$deferredRequests"
}
Write-Output "K10-G PASS active_request=1 deferred_connect=1 deferred_requests=0 run=$run"
