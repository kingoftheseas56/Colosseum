param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "K10 executable missing: $exe" }
$run = Join-Path $PSScriptRoot ('raw/inbound-utp-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$portFile = Join-Path $run 'listen-port.txt'
$stopFile = Join-Path $run 'probe-finished.marker'
$probeLog = Join-Path $run 'probe.log'
$transcript = Join-Path $run 'candidate.transcript'
$candidate = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
  -ArgumentList @('--inbound-utp-serve', (Join-Path $run 'case'), $portFile, $stopFile) `
  -RedirectStandardOutput $transcript -RedirectStandardError ($transcript + '.stderr.txt')
try {
  $deadline = (Get-Date).AddSeconds(10)
  while (-not (Test-Path -LiteralPath $portFile)) {
    if ($candidate.HasExited) { throw "K10 inbound uTP candidate exited before listening (exit=$($candidate.ExitCode))" }
    if ((Get-Date) -ge $deadline) { throw 'K10 inbound uTP candidate did not report a listen port' }
    Start-Sleep -Milliseconds 20
  }
  $port = [int](Get-Content -Raw -LiteralPath $portFile).Trim()
  & python (Join-Path $PSScriptRoot 'utp_probe.py') --port $port --log $probeLog
  if ($LASTEXITCODE -ne 0) { throw "K10 inbound uTP probe failed: $LASTEXITCODE" }
  Set-Content -LiteralPath $stopFile -Value 'done'
  if (-not $candidate.WaitForExit(15000)) { throw 'K10 inbound uTP candidate did not stop' }
  if ($candidate.ExitCode -ne 0) {
    Get-Content -LiteralPath ($transcript + '.stderr.txt')
    throw "K10 inbound uTP candidate failed: $($candidate.ExitCode)"
  }
} finally {
  if ($candidate -and -not $candidate.HasExited) { Stop-Process -Id $candidate.Id -Force -ErrorAction SilentlyContinue }
}
# Positive control: the adapter's TCP peer listener accepts on the same port.
if (@(Select-String -LiteralPath $probeLog -Pattern '^TCP_ACCEPTED ').Count -ne 1) {
  throw "K10 inbound uTP positive control failed: $((Get-Content -LiteralPath $probeLog) -join '; ')"
}
# Negative control: no uTP SYN is answered, as the source opens no uTP listener.
$summary = @(Select-String -LiteralPath $probeLog -Pattern '^SUMMARY ' | ForEach-Object Line)
if ($summary.Count -ne 1 -or $summary[0] -notmatch ' utp_state_replies=0 ') {
  throw "K10 inbound uTP SYN was answered; the source opens no uTP listener: $($summary -join '; ')"
}
Write-Output "K10-02 PASS inbound-utp tcp_accept=1 $($summary[0]) run=$run"
