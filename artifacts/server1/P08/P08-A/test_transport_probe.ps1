param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('red', 'green')]
    [string]$Phase,
    [string]$ProbeExe = (Join-Path $PSScriptRoot 'bin/transport_contract_probe.exe'),
    [string]$PeerScript = (Join-Path $PSScriptRoot 'tiny_peer.py')
)

$ErrorActionPreference = 'Stop'
$run = Join-Path $PSScriptRoot ("raw/run-" + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null

if ($Phase -eq 'red') {
    if (Test-Path -LiteralPath $ProbeExe) {
        throw "RED precondition violated: probe binary already exists: $ProbeExe"
    }
    Write-Output 'RED expected failure: transport probe executable is not present'
    exit 1
}

if (-not (Test-Path -LiteralPath $ProbeExe)) {
    throw "GREEN precondition violated: transport probe executable is missing: $ProbeExe"
}
if (-not (Test-Path -LiteralPath $PeerScript)) {
    throw "GREEN precondition violated: tiny peer script is missing: $PeerScript"
}

Write-Output "GREEN harness precondition satisfied: $ProbeExe"
exit 0
