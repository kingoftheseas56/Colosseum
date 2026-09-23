param(
  [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
  [int]$FailingPort = 49815,
  [int]$SurvivingPort = 49816
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'controlled_peer_startup.ps1')
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/failure-drain-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 failure-drain prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$failingWire = Join-Path $run 'failing-peer-wire.log'
$survivingWire = Join-Path $run 'surviving-peer-wire.log'
$failing = Start-ControlledPeer -Log $failingWire -Arguments @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$FailingPort,
  '--info-hash',$hash,'--log',$failingWire,'--pieces','0','--peer-tag','fail',
  '--disconnect-on-request')
$surviving = Start-ControlledPeer -Log $survivingWire -Arguments @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$SurvivingPort,
  '--info-hash',$hash,'--log',$survivingWire,'--pieces','1','--peer-tag','survive')
try {
  Wait-ControlledPeers -Case 'K10 failure-drain' -Peers @($failing, $surviving) -Logs @($failingWire, $survivingWire)
  $FailingPort = Get-ControlledPeerPort $failingWire
  $SurvivingPort = Get-ControlledPeerPort $survivingWire
  & $exe --failure-drain $run $FailingPort $SurvivingPort *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 failure-drain case failed: $LASTEXITCODE" }
} finally {
  foreach ($peer in @($failing, $surviving)) {
    if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
  }
}
Assert-NoUtpDial -Case 'K10 failure-drain' -Logs @($failingWire, $survivingWire)
$failingRequests = @(Select-String -LiteralPath $failingWire -Pattern '^REQUEST ' | ForEach-Object Line)
$survivingRequests = @(Select-String -LiteralPath $survivingWire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($failingRequests.Count -ne 1 -or $failingRequests[0] -notmatch 'piece=0 start=0 length=16384$') {
  throw "K10 failure-drain failing-peer wire mismatch: $($failingRequests -join '; ')"
}
if ($survivingRequests.Count -ne 1 -or $survivingRequests[0] -notmatch 'piece=1 start=0 length=16384$') {
  throw "K10 failure-drain surviving-peer wire mismatch: $($survivingRequests -join '; ')"
}
$failedAt = [int64]([regex]::Match($failingRequests[0], 'epoch_ms=(\d+)').Groups[1].Value)
$survivedAt = [int64]([regex]::Match($survivingRequests[0], 'epoch_ms=(\d+)').Groups[1].Value)
$gap = $survivedAt - $failedAt
if ($gap -lt 0 -or $gap -ge 1500) { throw "K10 failure-drain was not bounded: gap_ms=$gap" }
Write-Output "K10-02 PASS failure-drain requests=2 gap_ms=$gap run=$run"
