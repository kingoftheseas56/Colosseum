param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49814)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'controlled_peer_startup.ps1')
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/thread-guard-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 thread guard prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$peer = Start-ControlledPeer -Log $wire -Arguments @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','0')
try {
  Wait-ControlledPeers -Case 'K10 thread guard' -Peers @($peer) -Logs @($wire)
  $Port = Get-ControlledPeerPort $wire
  & $exe --thread-guard $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 thread guard case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
Assert-NoUtpDial -Case 'K10 thread guard' -Logs @($wire)
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($requests.Count -ne 1 -or $requests[0] -notmatch 'piece=0 start=0 length=16384$') {
  throw "K10 thread guard wire mismatch: $($requests -join '; ')"
}
Write-Output "K10-03 PASS established_native_thread=1 forbidden=2 native_touches=0 request=1 run=$run"
