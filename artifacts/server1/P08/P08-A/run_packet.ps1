param(
    [string]$ProbeExe = (Join-Path $PSScriptRoot 'bin/transport_contract_probe.exe'),
    [string]$PeerScript = (Join-Path $PSScriptRoot 'tiny_peer.py'),
    [ValidateSet('P08-01','P08-02','P08-03','P08-04','CONTROLS')]
    [string[]]$Cases = @('P08-01','P08-02','P08-03','P08-04','CONTROLS')
)

$ErrorActionPreference = 'Stop'
$probePath = (Resolve-Path -LiteralPath $ProbeExe).Path
$peerPath = (Resolve-Path -LiteralPath $PeerScript).Path
$rawRoot = Join-Path $PSScriptRoot 'raw'
$run = Join-Path $rawRoot ('packet-run-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$results = [ordered]@{}

function Invoke-PacketCase {
    param(
        [Parameter(Mandatory = $true)][string]$CaseId,
        [Parameter(Mandatory = $true)][hashtable]$PrepareOptions,
        [Parameter(Mandatory = $true)][int]$PortA,
        [Parameter(Mandatory = $true)][int]$PortB,
        [Parameter(Mandatory = $true)][array]$PeerDefinitions
    )
    $control = Join-Path $run $CaseId
    New-Item -ItemType Directory -Force -Path $control | Out-Null
    $prepareArgs = @('--p08-prepare', $control)
    foreach ($key in $PrepareOptions.Keys) {
        $prepareArgs += ('--' + [string]$key)
        $prepareArgs += [string]$PrepareOptions[$key]
    }
    $prepareDisplay = ($prepareArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Set-Content -LiteralPath (Join-Path $control 'prepare.command.txt') -Value ($probePath + ' ' + $prepareDisplay)
    & $probePath @prepareArgs *> (Join-Path $control 'prepare.transcript')
    $prepareExit = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $control 'prepare.exit.txt') -Value $prepareExit
    if ($prepareExit -ne 0) { throw "$CaseId prepare failed: $prepareExit" }
    $infoHash = (Get-Content -Raw -LiteralPath (Join-Path $control 'info_hash.txt')).Trim()
    $processes = @()
    try {
        foreach ($definition in $PeerDefinitions) {
            $peerArgs = @(
                $peerPath, '--port', [string]$definition.Port, '--label', $definition.Label,
                '--info-hash', $infoHash, '--log', (Join-Path $control $definition.Log)
            )
            foreach ($extra in $definition.Extra) { $peerArgs += [string]$extra }
            $processes += Start-Process -FilePath 'python' -ArgumentList $peerArgs -WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
        }
        foreach ($definition in $PeerDefinitions) {
            $logPath = Join-Path $control $definition.Log
            $readyDeadline = (Get-Date).AddSeconds(10)
            while ((Get-Date) -lt $readyDeadline) {
                if ((Test-Path -LiteralPath $logPath) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $logPath)) { break }
                Start-Sleep -Milliseconds 20
            }
            if (-not (Test-Path -LiteralPath $logPath) -or -not (Select-String -Quiet -SimpleMatch 'LISTEN ' -LiteralPath $logPath)) {
                throw "$CaseId tiny peer did not become ready: $($definition.Label)"
            }
        }
        $caseArgs = @('--p08-case', $CaseId, $control, [string]$PortA, [string]$PortB)
        $caseDisplay = ($caseArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
        Set-Content -LiteralPath (Join-Path $control 'case.command.txt') -Value ($probePath + ' ' + $caseDisplay)
        & $probePath @caseArgs *> (Join-Path $control 'case.transcript')
        $caseExit = $LASTEXITCODE
        Set-Content -LiteralPath (Join-Path $control 'case.exit.txt') -Value $caseExit
        if ($caseExit -ne 0) { throw "$CaseId failed: $caseExit" }
    }
    finally {
        foreach ($process in $processes) {
            if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        }
    }
    $caseJson = Get-Content -Raw -LiteralPath (Join-Path $control 'case.json') | ConvertFrom-Json
    if ($caseJson.state -ne 'PASS') { throw "$CaseId case.json is not PASS" }
    return [ordered]@{ state = $caseJson.state; raw = $control; exit = 0 }
}

if ($Cases -contains 'P08-01') { $results['P08-01'] = Invoke-PacketCase -CaseId 'P08-01' -PrepareOptions ([ordered]@{
    'piece-size' = 1048576; 'file-size' = 1048576; split = 524288; 'first-byte' = 'A'; 'second-byte' = 'B'
}) -PortA 0 -PortB 49301 -PeerDefinitions @(
    @{ Port = 49301; Label = 'P08-01-B'; Log = 'peer-B-wire.log'; Extra = @('--piece-length','1048576','--file-length','1048576','--split-offset','524288','--first-byte','A','--second-byte','B','--hold-after-offset','524288','--release-file',(Join-Path $run 'P08-01/release-tail.marker')) }
) }
if ($Cases -contains 'P08-02') { $results['P08-02'] = Invoke-PacketCase -CaseId 'P08-02' -PrepareOptions ([ordered]@{
    'piece-size' = 65536; 'file-size' = 171072
}) -PortA 49302 -PortB 49303 -PeerDefinitions @(
    @{ Port = 49302; Label = 'P08-02-A'; Log = 'peer-A-wire.log'; Extra = @('--piece-length','65536','--file-length','171072','--pieces','0,1') },
    @{ Port = 49303; Label = 'P08-02-B'; Log = 'peer-B-wire.log'; Extra = @('--piece-length','65536','--file-length','171072','--pieces','0,2','--hold-all','--release-file',(Join-Path $run 'P08-02/release-owned.marker')) }
) }
if ($Cases -contains 'P08-03') { $results['P08-03'] = Invoke-PacketCase -CaseId 'P08-03' -PrepareOptions ([ordered]@{
    'piece-size' = 16384; 'file-size' = 16384
}) -PortA 49304 -PortB 49305 -PeerDefinitions @(
    @{ Port = 49304; Label = 'P08-03-A'; Log = 'peer-A-wire.log'; Extra = @('--piece-length','16384','--file-length','16384','--hold-all','--release-file',(Join-Path $run 'P08-03/release-slow.marker'),'--duplicate-late','--delay-ms','0') },
    @{ Port = 49305; Label = 'P08-03-B'; Log = 'peer-B-wire.log'; Extra = @('--piece-length','16384','--file-length','16384','--delay-ms','50') }
) }
if ($Cases -contains 'P08-04') { $results['P08-04'] = Invoke-PacketCase -CaseId 'P08-04' -PrepareOptions ([ordered]@{
    'piece-size' = 65536; 'file-size' = 171072
}) -PortA 49306 -PortB 49307 -PeerDefinitions @(
    @{ Port = 49306; Label = 'P08-04-A'; Log = 'peer-A-wire.log'; Extra = @('--piece-length','65536','--file-length','171072','--pieces','0,1') },
    @{ Port = 49307; Label = 'P08-04-B'; Log = 'peer-B-wire.log'; Extra = @('--piece-length','65536','--file-length','171072','--pieces','0,2','--hold-all','--release-file',(Join-Path $run 'P08-04/release-owned.marker')) }
) }
if ($Cases -contains 'CONTROLS') { $results['CONTROLS'] = Invoke-PacketCase -CaseId 'CONTROLS' -PrepareOptions ([ordered]@{
    'piece-size' = 16384; 'file-size' = 16384
}) -PortA 49308 -PortB 49309 -PeerDefinitions @(
    @{ Port = 49308; Label = 'metadata'; Log = 'metadata-peer-wire.log'; Extra = @('--mode','metadata','--metadata-path',(Join-Path $run 'CONTROLS/metadata.info')) },
    @{ Port = 49309; Label = 'payload'; Log = 'payload-peer-wire.log'; Extra = @('--piece-length','16384','--file-length','16384','--choke-cycle','--release-file',(Join-Path $run 'CONTROLS/release-choke.marker')) }
) }

$aggregate = [ordered]@{
    schema = 'colosseum-server1-p08-packet-run/v1'
    state = 'PASS'
    executable = $probePath
    run = $run
    cases = $results
}
$aggregate | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'aggregate.json')
if ($results.Count -eq 5) {
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'LAST-PACKET-RUN.txt') -Value @('state=PASS', ('run=' + $run), 'P08-01=PASS', 'P08-02=PASS', 'P08-03=PASS', 'P08-04=PASS', 'CONTROLS=PASS')
}
Write-Output ('PACKET GREEN PASS: run=' + $run + '; cases=' + (($results.Keys | ForEach-Object { $_ + '=PASS' }) -join ','))
exit 0
