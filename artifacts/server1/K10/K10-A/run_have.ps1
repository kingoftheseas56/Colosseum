param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49812)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/have-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 HAVE prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$marker = Join-Path $run 'send-have.marker'
$peer = Start-Process -FilePath python -ArgumentList @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','1','--have-piece','0','--have-marker',$marker) -PassThru -WindowStyle Hidden
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ((Get-Date) -lt $deadline -and -not ((Test-Path $wire) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) { Start-Sleep -Milliseconds 20 }
  & $exe --have $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 HAVE case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($requests.Count -ne 2 -or $requests[0] -notmatch 'piece=1 start=0 length=16384$' -or
    $requests[1] -notmatch 'piece=0 start=0 length=16384$') {
  throw "K10 HAVE wire order mismatch: $($requests -join '; ')"
}
if (@(Select-String -LiteralPath $wire -Pattern '^HAVE_SENT .*piece=0$').Count -ne 1) {
  throw 'K10 HAVE peer did not advertise piece 0 exactly once'
}
Write-Output "K10-02 PASS HAVE real-wire order=piece1,piece0 requests=2 run=$run"
