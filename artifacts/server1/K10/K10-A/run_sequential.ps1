param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49811)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/sequential-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 sequential prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$peer = Start-Process -FilePath python -ArgumentList @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','0,1') -PassThru -WindowStyle Hidden
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ((Get-Date) -lt $deadline -and -not ((Test-Path $wire) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) { Start-Sleep -Milliseconds 20 }
  & $exe --sequential $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 sequential case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($requests.Count -ne 2 -or $requests[0] -notmatch 'piece=0 start=0 length=16384$' -or
    $requests[1] -notmatch 'piece=1 start=0 length=16384$') {
  throw "K10 sequential wire order mismatch: $($requests -join '; ')"
}
$first = [long]([regex]::Match($requests[0], 'epoch_ms=(\d+)').Groups[1].Value)
$second = [long]([regex]::Match($requests[1], 'epoch_ms=(\d+)').Groups[1].Value)
$gap = $second - $first
if ($gap -ge 1500) { throw "K10 sequential drain exceeded the locked plugin tick bound: gap_ms=$gap" }
Write-Output "K10-02 PASS sequential real-wire requests=2 gap_ms=$gap head_retry=1 run=$run"
