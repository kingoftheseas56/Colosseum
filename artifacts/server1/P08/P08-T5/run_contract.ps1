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

$positive = Join-Path $BuildDir 'server1_p08_t5_contract.exe'
$positiveObject = Join-Path $BuildDir 'server1_p08_t5_contract.obj'
& $compiler /nologo /std:c++17 /EHsc ('/I' + $include) $source `
    ('/Fo:' + $positiveObject) ('/Fe:' + $positive)
if ($LASTEXITCODE -ne 0) { throw "P08-T5 positive compile failed: $LASTEXITCODE" }
& $positive
if ($LASTEXITCODE -ne 0) { throw "P08-T5 positive consumer failed: $LASTEXITCODE" }

$mutations = @(
    'P08_T5_NEGATE_UPLOAD_ID',
    'P08_T5_NEGATE_UPLOAD_OWNERSHIP',
    'P08_T5_NEGATE_ADVERTISE_ACTION',
    'P08_T5_NEGATE_RESPONSE_ACTION',
    'P08_T5_NEGATE_ABORT_ACTION',
    'P08_T5_NEGATE_REQUEST_OBSERVATION',
    'P08_T5_NEGATE_CANCEL_OBSERVATION',
    'P08_T5_NEGATE_ACTION_VARIANT',
    'P08_T5_NEGATE_OBSERVATION_VARIANT',
    'P08_T5_NEGATE_PEER_CAP',
    'P08_T5_NEGATE_GLOBAL_CAP',
    'P08_T5_NEGATE_OUTSTANDING_STAT',
    'P08_T5_NEGATE_ACCEPTED_STAT',
    'P08_T5_NEGATE_REJECTED_STAT',
    'P08_T5_NEGATE_RESPONSE_STAT',
    'P08_T5_NEGATE_CANCEL_STAT',
    'P08_T5_NEGATE_ACTION_REJECTED_STAT',
    'P08_T5_NEGATE_PAYLOAD_BYTES_STAT',
    'P08_T5_NEGATE_ABORTED_STAT'
)
foreach ($mutation in $mutations) {
    $object = Join-Path $BuildDir ($mutation + '.obj')
    $output = & $compiler /nologo /std:c++17 /EHsc ('/I' + $include) /c $source `
        ('/D' + $mutation) ('/Fo:' + $object) 2>&1
    if ($LASTEXITCODE -eq 0) { throw "P08-T5 mutation compiled unexpectedly: $mutation" }
    $signal = ($output | Select-String -Pattern 'error C[0-9]+' | Select-Object -First 1).Line
    Write-Output "$mutation REJECTED $signal"
}

Write-Output "P08-T5 PASS positive=1 mutations=$($mutations.Count)/$($mutations.Count)"
