param(
    [string]$ProbeExe = (Join-Path $PSScriptRoot 'bin/transport_contract_probe.exe'),
    [string]$PeerScript = 'artifacts/server1/P08A/probe-src/tiny_peer.py'
)

$ErrorActionPreference = 'Stop'
$probePath = (Resolve-Path -LiteralPath $ProbeExe).Path
$peerPath = (Resolve-Path -LiteralPath $PeerScript).Path
$peerDrainTest = Join-Path (Split-Path -Parent $peerPath) 'test_tiny_peer_drain.py'
$rawRoot = Join-Path $PSScriptRoot 'raw'
$run = Join-Path $rawRoot ('predecessor-regression-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
$control = Join-Path $run 'control'
New-Item -ItemType Directory -Force -Path $control | Out-Null

function Write-CommandAndRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $display = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Set-Content -LiteralPath (Join-Path $run ($Name + '.command.txt')) -Value ($probePath + ' ' + $display)
    & $probePath @Arguments *> (Join-Path $run ($Name + '.transcript'))
    $exitCode = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $run ($Name + '.exit.txt')) -Value $exitCode
    return $exitCode
}

if (-not (Test-Path -LiteralPath $peerDrainTest)) { throw "missing predecessor drain test: $peerDrainTest" }
$drainCommand = 'python "' + $peerDrainTest + '"'
Set-Content -LiteralPath (Join-Path $run 'tiny-peer-drain.command.txt') -Value $drainCommand
& python $peerDrainTest *> (Join-Path $run 'tiny-peer-drain.transcript')
$drainExit = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $run 'tiny-peer-drain.exit.txt') -Value $drainExit
if ($drainExit -ne 0) { throw "predecessor tiny-peer drain failed: $drainExit" }

$prepareExit = Write-CommandAndRun -Name 'prepare' -Arguments @('--prepare', $control)
if ($prepareExit -ne 0) { throw "predecessor prepare failed: $prepareExit" }
$probeSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $probePath).Hash.ToLowerInvariant()
Add-Content -LiteralPath (Join-Path $control 'dependency_identity.txt') -Value "probe_executable_sha256=$probeSha"
$infoHash = (Get-Content -Raw -LiteralPath (Join-Path $control 'info_hash.txt')).Trim()
$release = Join-Path $run 'lifecycle.release'

$peerSpecs = @(
    [ordered]@{ Port = 49101; Label = 'A-control'; Log = (Join-Path $run 'control-peer-A-wire.log'); Delay = 250 },
    [ordered]@{ Port = 49102; Label = 'B-control'; Log = (Join-Path $run 'control-peer-B-wire.log'); Delay = 250 },
    [ordered]@{ Port = 49103; Label = 'A-controlled'; Log = (Join-Path $run 'controlled-peer-A-wire.log'); Delay = 250 },
    [ordered]@{ Port = 49104; Label = 'B-controlled'; Log = (Join-Path $run 'controlled-peer-B-wire.log'); Delay = 250 },
    [ordered]@{ Port = 49105; Label = 'A-lifecycle'; Log = (Join-Path $run 'lifecycle-peer-A-wire.log'); Delay = 250 },
    [ordered]@{ Port = 49106; Label = 'B-lifecycle'; Log = (Join-Path $run 'lifecycle-peer-B-wire.log'); Delay = 800 }
)
$peerProcesses = @()
$probeExit = 2
try {
    foreach ($spec in $peerSpecs) {
        $peerArgs = @(
            $peerPath, '--port', [string]$spec.Port, '--label', $spec.Label,
            '--info-hash', $infoHash, '--log', $spec.Log, '--delay-ms', [string]$spec.Delay
        )
        if ($spec.Label -eq 'B-lifecycle') { $peerArgs += @('--release-file', $release) }
        $peerProcesses += Start-Process -FilePath 'python' -ArgumentList $peerArgs -WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
    }
    foreach ($spec in $peerSpecs) {
        $readyDeadline = (Get-Date).AddSeconds(8)
        while ((Get-Date) -lt $readyDeadline) {
            if ((Test-Path -LiteralPath $spec.Log) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $spec.Log)) { break }
            Start-Sleep -Milliseconds 20
        }
        if (-not (Test-Path -LiteralPath $spec.Log) -or -not (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $spec.Log)) {
            throw "predecessor tiny peer did not become ready: $($spec.Label)"
        }
    }
    $runArgs = @(
        '--run', $control,
        '49101', '49102', (Join-Path $run 'control-peer-A-wire.log'), (Join-Path $run 'control-peer-B-wire.log'),
        '49103', '49104', (Join-Path $run 'controlled-peer-A-wire.log'), (Join-Path $run 'controlled-peer-B-wire.log'),
        '49105', '49106', (Join-Path $run 'lifecycle-peer-A-wire.log'), (Join-Path $run 'lifecycle-peer-B-wire.log'),
        $release, $probeSha
    )
    $display = ($runArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Set-Content -LiteralPath (Join-Path $run 'probe.command.txt') -Value ($probePath + ' ' + $display)
    & $probePath @runArgs *> (Join-Path $run 'probe.transcript')
    $probeExit = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $run 'probe.exit.txt') -Value $probeExit
}
finally {
    foreach ($process in $peerProcesses) {
        if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    }
}

if ($probeExit -ne 0) { throw "predecessor probe failed: $probeExit" }
$result = Get-Content -Raw -LiteralPath (Join-Path $control 'result.json') | ConvertFrom-Json
if ($result.case_01.state -ne 'PASS' -or $result.case_02.state -ne 'PASS' -or $result.case_03.state -ne 'PASS') {
    throw 'predecessor case result did not remain green'
}
$summary = @(
    'safe_packet_local_predecessor_regression=true',
    ('probe_exit=' + $probeExit),
    ('tiny_peer_drain_exit=' + $drainExit),
    ('case_01=' + $result.case_01.state),
    ('case_02=' + $result.case_02.state),
    ('case_03=' + $result.case_03.state),
    ('raw_run=' + $run),
    'unowned_docs_mutated=false'
)
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'P08A-PREDECESSOR-REGRESSION.txt') -Value $summary
Write-Output ('PREDECESSOR GREEN PASS: run=' + $run + '; probe_exit=' + $probeExit + '; cases=PASS/PASS/PASS')
exit 0
