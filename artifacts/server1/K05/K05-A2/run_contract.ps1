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
$source = Join-Path $PSScriptRoot 'contract_consumer.cpp'
$include = Join-Path $repo 'native/colosseum_server_v1/include'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$positiveObject = Join-Path $BuildDir 'server1_k05_a2_contract.obj'
& $compiler /nologo /std:c++17 /EHsc ('/I' + $include) /c $source ('/Fo:' + $positiveObject)
if ($LASTEXITCODE -ne 0) { throw "K05-A2 positive compile failed: $LASTEXITCODE" }

$mutations = @(
    'K05_A2_NEGATE_COUNTS_TYPE',
    'K05_A2_NEGATE_QUEUED',
    'K05_A2_NEGATE_HANDSHAKING',
    'K05_A2_NEGATE_READY',
    'K05_A2_NEGATE_QUERY'
)
foreach ($mutation in $mutations) {
    $object = Join-Path $BuildDir ($mutation + '.obj')
    $output = & $compiler /nologo /std:c++17 /EHsc ('/I' + $include) /c $source `
        ('/D' + $mutation) ('/Fo:' + $object) 2>&1
    if ($LASTEXITCODE -eq 0) { throw "K05-A2 mutation compiled unexpectedly: $mutation" }
    $signal = ($output | Select-String -Pattern 'error C[0-9]+' | Select-Object -First 1).Line
    Write-Output "$mutation REJECTED $signal"
}

Write-Output "K05-A2 PASS positive=1 mutations=$($mutations.Count)/$($mutations.Count)"
