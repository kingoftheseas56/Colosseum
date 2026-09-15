param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'))

$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'server1_k10_native_transport_test.exe'
$run = Join-Path $PSScriptRoot ('raw/upload-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$peers = @()
try {
    $feasibility = Join-Path $run 'feasibility'
    & $exe --prepare-upload $feasibility *> (Join-Path $run 'prepare-feasibility.transcript')
    if ($LASTEXITCODE -ne 0) { throw "K10-H feasibility prepare failed: $LASTEXITCODE" }
    $hash = (Get-Content -Raw -LiteralPath (Join-Path $feasibility 'info_hash.txt')).Trim()
    $wire = Join-Path $run 'feasibility-wire.log'
    $peer = Start-Process python -ArgumentList @(
        (Join-Path $PSScriptRoot 'upload_peer.py'), '--port', '0', '--info-hash', $hash,
        '--log', $wire) -PassThru -WindowStyle Hidden
    $peers += $peer
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline -and -not ((Test-Path $wire) -and
        (Select-String -Quiet -SimpleMatch 'LISTEN ' $wire))) { Start-Sleep -Milliseconds 20 }
    if (-not (Test-Path $wire)) { throw 'K10-H feasibility listener missing' }
    $port = [int](((@(Select-String -LiteralPath $wire -Pattern '^LISTEN port=')[0]).Line -split '=')[1])
    & $exe --upload-feasibility $feasibility $port *> (Join-Path $run 'feasibility.transcript')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $run 'feasibility.transcript')
        throw "K10-H feasibility failed: $LASTEXITCODE"
    }
    $peer.WaitForExit(5000) | Out-Null
    if (@(Select-String -LiteralPath $wire -Pattern '^HAVE_RECEIVED piece=1 raw=0400000001$').Count -ne 1 `
        -or @(Select-String -LiteralPath $wire -Pattern '^PIECE_RECEIVED piece=1 offset=0 length=73 raw=07').Count -ne 1) {
        throw 'K10-H feasibility raw HAVE/PIECE mismatch'
    }

    $pending = Join-Path $run 'pending-unchoke'
    & $exe --prepare $pending *> (Join-Path $run 'prepare-pending.transcript')
    if ($LASTEXITCODE -ne 0) { throw "K10-H pending prepare failed: $LASTEXITCODE" }
    $hash = (Get-Content -Raw -LiteralPath (Join-Path $pending 'info_hash.txt')).Trim()
    $pendingWire = Join-Path $run 'pending-unchoke-wire.log'
    $trigger = Join-Path $pending 'pre-dispatch-request.trigger'
    $chokedMarker = Join-Path $pending 'explicit-choke.observed'
    $preMarker = Join-Path $pending 'pre-wire-request.sent'
    $pendingPeer = Start-Process python -ArgumentList @(
        (Join-Path $PSScriptRoot 'upload_pending_peer.py'), '--port', '0', '--info-hash', $hash,
        '--log', $pendingWire, '--trigger', $trigger,
        '--choked-marker', $chokedMarker, '--pre-marker', $preMarker) -PassThru -WindowStyle Hidden
    $peers += $pendingPeer
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline -and -not ((Test-Path $pendingWire) -and
        (Select-String -Quiet -SimpleMatch 'LISTEN ' $pendingWire))) { Start-Sleep -Milliseconds 20 }
    if (-not (Test-Path $pendingWire)) { throw 'K10-H pending listener missing' }
    $pendingPort = [int](((@(Select-String -LiteralPath $pendingWire -Pattern '^LISTEN port=')[0]).Line -split '=')[1])
    & $exe --upload-pending-unchoke $pending $pendingPort *> (Join-Path $run 'pending-unchoke.transcript')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $run 'pending-unchoke.transcript')
        throw "K10-H pending unchoke case failed: $LASTEXITCODE"
    }
    Start-Sleep -Milliseconds 150
    $pendingLines = @(Get-Content -LiteralPath $pendingWire)
    $chokeIndex = [Array]::IndexOf($pendingLines, 'CHOKE_RECEIVED')
    $preIndex = [Array]::IndexOf($pendingLines, 'PRE_WIRE_REQUEST_SENT piece=0 offset=0 length=16384')
    $unchokeIndex = [Array]::IndexOf($pendingLines, 'UNCHOKE_RECEIVED')
    $retryIndex = [Array]::IndexOf($pendingLines, 'POST_WIRE_RETRY_SENT piece=0 offset=0 length=16384')
    if ($chokeIndex -lt 0 -or $preIndex -le $chokeIndex -or $unchokeIndex -le $preIndex `
        -or $retryIndex -le $unchokeIndex `
        -or (@(Select-String -LiteralPath $pendingWire -Pattern '^UNEXPECTED_PIECE').Count -ne 0)) {
        throw 'K10-H pending unchoke raw ordering mismatch'
    }

    $caps = Join-Path $run 'caps'
    & $exe --prepare-upload-caps $caps *> (Join-Path $run 'prepare-caps.transcript')
    if ($LASTEXITCODE -ne 0) { throw "K10-H caps prepare failed: $LASTEXITCODE" }
    $hash = (Get-Content -Raw -LiteralPath (Join-Path $caps 'info_hash.txt')).Trim()
    $ports = @()
    for ($index = 0; $index -lt 6; ++$index) {
        $peerWire = Join-Path $run ("caps-peer-$index.log")
        $arguments = @((Join-Path $PSScriptRoot 'upload_burst_peer.py'), '--port', '0',
            '--info-hash', $hash, '--log', $peerWire, '--tag', ("peer$index"),
            '--requests', $(if ($index -eq 0) {'5'} else {'4'}))
        if ($index -eq 0) { $arguments += @('--duplicate-first', '--invalid-first') }
        $burst = Start-Process python -ArgumentList $arguments -PassThru -WindowStyle Hidden
        $peers += $burst
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and -not ((Test-Path $peerWire) -and
            (Select-String -Quiet -SimpleMatch 'LISTEN ' $peerWire))) { Start-Sleep -Milliseconds 20 }
        if (-not (Test-Path $peerWire)) { throw "K10-H caps listener $index missing" }
        $ports += [int](((@(Select-String -LiteralPath $peerWire -Pattern '^LISTEN port=')[0]).Line -split '=')[1])
    }
    & $exe --upload-caps $caps @ports *> (Join-Path $run 'caps.transcript')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $run 'caps.transcript')
        throw "K10-H caps failed: $LASTEXITCODE"
    }
    for ($index = 0; $index -lt 6; ++$index) {
        $peerWire = Join-Path $run ("caps-peer-$index.log")
        if (@(Select-String -LiteralPath $peerWire -Pattern '^HAVE piece=').Count -ne 5) {
            throw "K10-H replay/idempotence mismatch peer=$index"
        }
        if (@(Select-String -LiteralPath $peerWire -Pattern '^UNEXPECTED_PIECE').Count -ne 0) {
            throw "K10-H abort leaked PIECE peer=$index"
        }
    }

    $lifecycle = Join-Path $run 'lifecycle'
    & $exe --prepare $lifecycle *> (Join-Path $run 'prepare-lifecycle.transcript')
    if ($LASTEXITCODE -ne 0) { throw "K10-H lifecycle prepare failed: $LASTEXITCODE" }
    $hash = (Get-Content -Raw -LiteralPath (Join-Path $lifecycle 'info_hash.txt')).Trim()
    $lifecyclePorts = @()
    $modes = @('choke', 'detach', 'close')
    for ($index = 0; $index -lt 3; ++$index) {
        $peerWire = Join-Path $run ("lifecycle-peer-$index.log")
        $lifePeer = Start-Process python -ArgumentList @(
            (Join-Path $PSScriptRoot 'upload_lifecycle_peer.py'), '--port', '0',
            '--info-hash', $hash, '--log', $peerWire, '--mode', $modes[$index],
            '--tag', ("life$index"), '--piece', $(if ($index -eq 2) {'1'} else {'0'})) `
            -PassThru -WindowStyle Hidden
        $peers += $lifePeer
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and -not ((Test-Path $peerWire) -and
            (Select-String -Quiet -SimpleMatch 'LISTEN ' $peerWire))) { Start-Sleep -Milliseconds 20 }
        if (-not (Test-Path $peerWire)) { throw "K10-H lifecycle listener $index missing" }
        $lifecyclePorts += [int](((@(Select-String -LiteralPath $peerWire -Pattern '^LISTEN port=')[0]).Line -split '=')[1])
    }
    & $exe --upload-lifecycle $lifecycle @lifecyclePorts *> (Join-Path $run 'lifecycle.transcript')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $run 'lifecycle.transcript')
        throw "K10-H lifecycle failed: $LASTEXITCODE"
    }
    Start-Sleep -Milliseconds 250
    for ($index = 0; $index -lt 3; ++$index) {
        $peerWire = Join-Path $run ("lifecycle-peer-$index.log")
        $expectedHaves = if ($index -eq 1) { 1 } else { 2 }
        if ((@(Select-String -LiteralPath $peerWire -Pattern '^HAVE piece=').Count -lt $expectedHaves) `
            -or (@(Select-String -LiteralPath $peerWire -Pattern '^UNEXPECTED_PIECE').Count -ne 0)) {
            throw "K10-H lifecycle wire mismatch peer=$index"
        }
    }
    if ((@(Select-String -LiteralPath (Join-Path $run 'lifecycle-peer-0.log') -Pattern '^CHOKE$').Count -ne 1) `
        -or (@(Select-String -LiteralPath (Join-Path $run 'lifecycle-peer-1.log') -Pattern '^PEER_DETACH$').Count -ne 1)) {
        throw 'K10-H lifecycle terminal wire markers mismatch'
    }
} finally {
    foreach ($peer in $peers) {
        if ($peer -and -not $peer.HasExited) {
            Stop-Process -Id $peer.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Get-Content -LiteralPath (Join-Path $run 'feasibility.transcript')
Get-Content -LiteralPath (Join-Path $run 'pending-unchoke.transcript')
Get-Content -LiteralPath (Join-Path $run 'caps.transcript')
Get-Content -LiteralPath (Join-Path $run 'lifecycle.transcript')
Write-Output "K10-H upload real-wire gate PASS run=$run"
