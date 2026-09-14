param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$PortA = 49510, [int]$PortB = 49511)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "K10 executable missing: $exe" }
$run = Join-Path $PSScriptRoot ('raw/lifecycle-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 lifecycle prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wireA = Join-Path $run 'peer-A-wire.log'
$wireB = Join-Path $run 'peer-B-wire.log'
$peerScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../P08/P08-A/tiny_peer.py')).Path
$argsA = @($peerScript,'--port',[string]$PortA,'--label','K10-LIFE-A','--info-hash',$hash,
  '--log',$wireA,'--piece-length','16384','--file-length','32768','--first-byte','K','--second-byte','K')
$argsB = @($peerScript,'--port',[string]$PortB,'--label','K10-LIFE-B','--info-hash',$hash,
  '--log',$wireB,'--piece-length','16384','--file-length','32768','--first-byte','K','--second-byte','K')
$peerA = Start-Process -FilePath python -ArgumentList $argsA -PassThru -WindowStyle Hidden
$peerB = Start-Process -FilePath python -ArgumentList $argsB -PassThru -WindowStyle Hidden
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ((Get-Date) -lt $deadline) {
    if ((Test-Path $wireA) -and (Test-Path $wireB) -and
        (Select-String -Quiet -SimpleMatch 'LISTEN ' $wireA) -and
        (Select-String -Quiet -SimpleMatch 'LISTEN ' $wireB)) { break }
    Start-Sleep -Milliseconds 20
  }
  & $exe --lifecycle $run $PortA $PortB *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 lifecycle case failed: $LASTEXITCODE" }
  $disconnectDeadline = (Get-Date).AddSeconds(3)
  while ((Get-Date) -lt $disconnectDeadline) {
    if ((Select-String -Quiet -SimpleMatch 'CLIENT_DISCONNECTED' $wireA) -and
        (Select-String -Quiet -SimpleMatch 'CLIENT_DISCONNECTED' $wireB)) { break }
    Start-Sleep -Milliseconds 20
  }
} finally {
  foreach ($peer in @($peerA,$peerB)) {
    if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
  }
}
$requestsA = @(Select-String -LiteralPath $wireA -Pattern '^REQUEST ').Count
$requestsB = @(Select-String -LiteralPath $wireB -Pattern '^REQUEST ').Count
$disconnectsA = @(Select-String -LiteralPath $wireA -Pattern '^CLIENT_DISCONNECTED').Count
$disconnectsB = @(Select-String -LiteralPath $wireB -Pattern '^CLIENT_DISCONNECTED').Count
$exact = @(Select-String -LiteralPath $wireB -Pattern '^REQUEST .*piece=0 start=0 length=16384$').Count
if ($requestsA -ne 0 -or $requestsB -ne 1 -or $exact -ne 1) {
  throw "K10 lifecycle ownership mismatch: A=$requestsA B=$requestsB exact=$exact"
}
if ($disconnectsA -lt 1 -or $disconnectsB -lt 1) {
  throw "K10 lifecycle did not observe both late disconnects: A=$disconnectsA B=$disconnectsB"
}
@{ schema='colosseum-server1-k10-lifecycle/v1'; state='PASS'; requests=1; exact=1;
   unowned=0; framed=1; stop_during_on_piece=$true; late_completion=0; replayed=0;
   peer_a_disconnects=$disconnectsA; peer_b_disconnects=$disconnectsB; run=$run } |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'result.json')
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'LAST-LIFECYCLE-RUN.txt') -Value @('state=PASS',('run='+$run),'requests=1','replayed=0')
Write-Output "K10-02 PASS lifecycle requests=1 exact=1 unowned=0 stop-during-on_piece=1 late-completion=0 replayed=0 disconnects=A$disconnectsA/B$disconnectsB run=$run"
