param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49814)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/thread-guard-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10 thread guard prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$peer = Start-Process -FilePath python -ArgumentList @(
  (Join-Path $PSScriptRoot 'controlled_peer.py'),'--port',[string]$Port,
  '--info-hash',$hash,'--log',$wire,'--pieces','0') -PassThru -WindowStyle Hidden
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ((Get-Date) -lt $deadline -and -not ((Test-Path $wire) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) { Start-Sleep -Milliseconds 20 }
  & $exe --thread-guard $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10 thread guard case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
$requests = @(Select-String -LiteralPath $wire -Pattern '^REQUEST ' | ForEach-Object Line)
if ($requests.Count -ne 1 -or $requests[0] -notmatch 'piece=0 start=0 length=16384$') {
  throw "K10 thread guard wire mismatch: $($requests -join '; ')"
}
Write-Output "K10-03 PASS established_native_thread=1 forbidden=2 native_touches=0 request=1 run=$run"
