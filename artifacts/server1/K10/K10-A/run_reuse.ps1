param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49813)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'controlled_peer_startup.ps1')
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/reuse-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 reuse prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$marker = Join-Path $run 'first-disconnected.marker'
$peer = Start-ControlledPeer -Log $wire -Arguments @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','0','--disconnect-first',
  '--disconnect-marker',$marker)
try {
  Wait-ControlledPeers -Case 'K10 live reuse' -Peers @($peer) -Logs @($wire)
  $Port = Get-ControlledPeerPort $wire
  & $exe --reuse $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 live reuse case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
Assert-NoUtpDial -Case 'K10 live reuse' -Logs @($wire)
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($requests.Count -ne 1 -or $requests[0] -notmatch 'connection=2 piece=0 start=0 length=16384$') {
  throw "K10 live reuse wire mismatch: $($requests -join '; ')"
}
if (@(Select-String -LiteralPath $wire -Pattern '^HANDSHAKE connection=').Count -ne 2) {
  throw 'K10 live reuse did not establish both real connections'
}
Write-Output "K10-02 PASS live reused endpoint disconnect=0/0 replacement_request=1 run=$run"
