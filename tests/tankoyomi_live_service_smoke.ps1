$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'native\build-tankoyomi\tankoyomi_service_runtime_harness.exe'
if (-not (Test-Path $exe)) { throw "Build tankoyomi_service_runtime_harness first: $exe" }
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
$env:QT_FORCE_STDERR_LOGGING = '1'

$cases = @(
    @('en', 'One Piece', 'weebcentral'),
    @('es', 'One Piece', 'zonatmo'),
    # Live fallback oracle: ZonaTMO misses; NiAdd owns the result.
    @('es', 'Apotheosis', 'niadd'),
    @('pt-BR', 'Hunter x Hunter', 'taiyo'),
    @('fr', 'One Piece', 'sushiscan-fr')
)

foreach ($case in $cases) {
    $language, $query, $provider = $case
    Write-Host "=== $language | $query | $provider ==="
    & $exe $language $query $provider
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host 'TANKOYOMI_LIVE_SERVICE_SMOKE_OK'
exit 0
