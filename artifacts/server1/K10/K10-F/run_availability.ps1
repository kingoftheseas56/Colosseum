param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'), [int]$Port = 49818)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/availability-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& $exe --prepare-availability $run *> (Join-Path $run 'prepare.transcript')
if ($LASTEXITCODE -ne 0) { throw "K10-F prepare failed: $LASTEXITCODE" }
$hash = (Get-Content -Raw -LiteralPath (Join-Path $run 'info_hash.txt')).Trim()
$wire = Join-Path $run 'peer-wire.log'
$peer = Start-Process -FilePath python -ArgumentList @(
  (Join-Path $PSScriptRoot 'availability_peer.py'), '--port', [string]$Port,
  '--info-hash', $hash, '--log', $wire, '--first-pieces', '3,1,3', '--second-pieces', ',',
  '--have-piece', '0', '--have-marker', (Join-Path $run 'send-have.marker'),
  '--disconnect-marker', (Join-Path $run 'disconnect-first.marker'),
  '--disconnected-marker', (Join-Path $run 'first-disconnected.marker')) -PassThru -WindowStyle Hidden
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ((Get-Date) -lt $deadline -and -not ((Test-Path $wire) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) { Start-Sleep -Milliseconds 20 }
  if (-not ((Test-Path $wire) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) {
    throw 'K10-F peer listener did not start'
  }
  & $exe --availability $run $Port *> (Join-Path $run 'candidate.transcript')
  if ($LASTEXITCODE -ne 0) { throw "K10-F availability case failed: $LASTEXITCODE" }
} finally {
  if ($peer -and -not $peer.HasExited) { Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue }
}
if (@(Select-String -LiteralPath $wire -Pattern '^BITFIELD_SENT connection=').Count -ne 2) {
  throw 'K10-F did not execute both real connection bitfields'
}
if (@(Select-String -LiteralPath $wire -Pattern '^HAVE_SENT connection=1 piece=0$').Count -ne 1) {
  throw 'K10-F did not execute the real HAVE transition'
}
Write-Output "K10-F PASS real-wire snapshots/reconnect/stale-callback isolation run=$run"
