param([string]$BuildDir = (Join-Path $PSScriptRoot 'build'))

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
$source = Join-Path $repo 'native/colosseum_server_v1/tests/test_torrent_transport_contract.cpp'
$include = Join-Path $repo 'native/colosseum_server_v1/include'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$positive = Join-Path $BuildDir 'server1_p08_t3_contract.exe'
$positiveObject = Join-Path $BuildDir 'server1_p08_t3_contract.obj'
$positiveArguments = @('/nologo', '/std:c++17', '/EHsc', ('/I' + $include), $source,
    ('/Fo:' + $positiveObject), ('/Fe:' + $positive))
& $compiler @positiveArguments
if ($LASTEXITCODE -ne 0) { throw "P08-T3 positive compile failed: $LASTEXITCODE" }
& $positive
if ($LASTEXITCODE -ne 0) { throw "P08-T3 positive consumer failed: $LASTEXITCODE" }

$mutations = @(
    'P08_NEGATE_OWNERSHIP', 'P08_NEGATE_GENERATION', 'P08_NEGATE_EXACT_BLOCK',
    'P08_NEGATE_BLOCK_IDENTITY', 'P08_NEGATE_CANCELLATION', 'P08_NEGATE_OBSERVATION',
    'P08_NEGATE_STATISTICS', 'P08_NEGATE_AUTONOMY', 'P08_NEGATE_SOURCE_GENERATION',
    'P08_NEGATE_V1_INFO_HASH', 'P08_NEGATE_OPEN_REQUEST', 'P08_NEGATE_CONNECT_ACTION',
    'P08_NEGATE_METADATA_READY', 'P08_NEGATE_SOURCE_FAILURE',
    'P08_NEGATE_AVAILABLE_PIECES'
)
foreach ($mutation in $mutations) {
    $object = Join-Path $BuildDir ($mutation + '.obj')
    $arguments = @('/nologo', '/std:c++17', '/EHsc', ('/I' + $include), '/c', $source,
        ('/D' + $mutation), ('/Fo:' + $object))
    $output = & $compiler @arguments 2>&1
    if ($LASTEXITCODE -eq 0) { throw "P08-T3 mutation compiled unexpectedly: $mutation" }
    $signal = ($output | Select-String -Pattern 'error C[0-9]+' | Select-Object -First 1).Line
    Write-Output "$mutation REJECTED $signal"
}

Write-Output "P08-T3 PASS positive=1 mutations=$($mutations.Count)/$($mutations.Count)"
