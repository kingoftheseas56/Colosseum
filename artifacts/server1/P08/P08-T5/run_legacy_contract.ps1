param([string]$BuildDir = (Join-Path $PSScriptRoot 'legacy-build'))

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../../..')).Path
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw "Visual Studio environment missing: $vsDevCmd" }
$environment = & cmd.exe /d /c ('"' + $vsDevCmd + '" -arch=amd64 -host_arch=amd64 >nul && set')
foreach ($line in $environment) {
    $parts = $line -split '=', 2
    if ($parts.Count -eq 2) { Set-Item -LiteralPath ('Env:' + $parts[0]) -Value $parts[1] }
}

$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
$include = Join-Path $repo 'native/colosseum_server_v1/include'
$mainSource = Join-Path $repo 'native/colosseum_server_v1/tests/test_torrent_transport_contract.cpp'
$pauseSource = Join-Path $PSScriptRoot 'legacy_pause_consumer.cpp'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

function Invoke-Positive([string]$Name, [string]$Source) {
    $exe = Join-Path $BuildDir ($Name + '.exe')
    $obj = Join-Path $BuildDir ($Name + '.obj')
    & $compiler /nologo /std:c++17 /EHsc ('/I' + $include) $Source ('/Fo:' + $obj) ('/Fe:' + $exe)
    if ($LASTEXITCODE -ne 0) { throw "$Name positive compile failed: $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$Name positive consumer failed: $LASTEXITCODE" }
}

function Assert-MutationsRejected([string]$Source, [string[]]$Mutations) {
    foreach ($mutation in $Mutations) {
        $object = Join-Path $BuildDir ($mutation + '.obj')
        $output = & $compiler /nologo /std:c++17 /EHsc ('/I' + $include) /c $Source `
            ('/D' + $mutation) ('/Fo:' + $object) 2>&1
        if ($LASTEXITCODE -eq 0) { throw "Legacy mutation compiled unexpectedly: $mutation" }
        $signal = ($output | Select-String -Pattern 'error C[0-9]+' | Select-Object -First 1).Line
        Write-Output "$mutation REJECTED $signal"
    }
}

$mainMutations = @(
    'P08_NEGATE_OWNERSHIP', 'P08_NEGATE_GENERATION', 'P08_NEGATE_EXACT_BLOCK',
    'P08_NEGATE_BLOCK_IDENTITY', 'P08_NEGATE_CANCELLATION', 'P08_NEGATE_OBSERVATION',
    'P08_NEGATE_STATISTICS', 'P08_NEGATE_AUTONOMY', 'P08_NEGATE_SOURCE_GENERATION',
    'P08_NEGATE_V1_INFO_HASH', 'P08_NEGATE_OPEN_REQUEST', 'P08_NEGATE_CONNECT_ACTION',
    'P08_NEGATE_METADATA_READY', 'P08_NEGATE_SOURCE_FAILURE', 'P08_NEGATE_AVAILABLE_PIECES'
)
$pauseMutations = @(
    'P08_T4_NEGATE_PAUSE_ACTION', 'P08_T4_NEGATE_PAUSE_GENERATION',
    'P08_T4_NEGATE_PAUSED_STATISTIC', 'P08_T4_NEGATE_ACTION_VARIANT'
)

Invoke-Positive 'server1_p08_legacy_contract' $mainSource
Invoke-Positive 'server1_p08_legacy_pause_contract' $pauseSource
Assert-MutationsRejected $mainSource $mainMutations
Assert-MutationsRejected $pauseSource $pauseMutations
Write-Output "P08 legacy PASS positive=2 mutations=19/19"
