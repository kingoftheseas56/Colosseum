param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49810)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'controlled_peer_startup.ps1')
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/submit-ready-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$peer = Start-ControlledPeer -Log $wire -Arguments @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','0,1')
try {
  Wait-ControlledPeers -Case 'K10 submit-after-ready' -Peers @($peer) -Logs @($wire)
  $Port = Get-ControlledPeerPort $wire
  & $exe --submit-ready $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 submit-after-ready case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
Assert-NoUtpDial -Case 'K10 submit-after-ready' -Logs @($wire)
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ').Count
$exact = @(Select-String -LiteralPath $wire -Pattern '^REQUEST .*piece=0 start=0 length=16384$').Count
if ($requests -ne 1 -or $exact -ne 1) { throw "K10 submit-after-ready wire mismatch: requests=$requests exact=$exact" }
Write-Output "K10-01 PASS submit-after-ready single-peer requests=1 exact=1 unowned=0 run=$run"
