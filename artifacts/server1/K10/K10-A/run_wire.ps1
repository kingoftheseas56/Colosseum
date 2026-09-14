param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$PortA = 49410, [int]$PortB = 49411)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "K10 executable missing: $exe" }
$run = Join-Path $PSScriptRoot ('raw/wire-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wireA = Join-Path $run 'peer-A-wire.log'
$wireB = Join-Path $run 'peer-B-wire.log'
$peerScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../P08/P08-A/tiny_peer.py')).Path
$argsA = @($peerScript,'--port',[string]$PortA,'--label','K10-A','--info-hash',$hash,
  '--log',$wireA,'--piece-length','16384','--file-length','32768','--first-byte','K','--second-byte','K','--delay-ms','25')
$argsB = @($peerScript,'--port',[string]$PortB,'--label','K10-B','--info-hash',$hash,
  '--log',$wireB,'--piece-length','16384','--file-length','32768','--first-byte','K','--second-byte','K','--delay-ms','25')
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
  & $exe --wire $run $PortA $PortB *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 wire case failed: $LASTEXITCODE" }
} finally {
  foreach ($peer in @($peerA,$peerB)) {
    if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
  }
}
$requestsA = @(Select-String -LiteralPath $wireA -Pattern '^REQUEST ').Count
$requestsB = @(Select-String -LiteralPath $wireB -Pattern '^REQUEST ').Count
$requests = $requestsA + $requestsB
$exact = @(Select-String -LiteralPath $wireB -Pattern '^REQUEST .*piece=0 start=0 length=16384$').Count
if ($requestsA -ne 0 -or $requestsB -ne 1 -or $exact -ne 1) { throw "K10 ownership mismatch: A=$requestsA B=$requestsB exact=$exact" }
if (@(Select-String -LiteralPath $wireB -Pattern '^REQUEST .*piece=(?!0)|^REQUEST .*start=(?!0)|^REQUEST .*length=(?!16384)').Count -ne 0) {
  throw 'K10 unowned wire activity observed'
}
@{ schema='colosseum-server1-k10-wire/v1'; state='PASS'; requests=$requests; exact=$exact;
   unowned=0; queued=1; framed=1; autonomous_mutations=0; replayed=0;
   peer=77; piece=0; offset=0; length=16384; run=$run } |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'result.json')
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'LAST-WIRE-RUN.txt') -Value @('state=PASS',('run='+$run),'requests=1','unowned=0')
Write-Output "K10-01 PASS real-wire requests=1 exact=1 unowned=0 run=$run"
