$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw

if ($cmake -notlike '*Qt6Sql.dll*') {
    throw "Native deployment must copy Qt6Sql.dll so colosseum.exe launches without a developer PATH."
}

Write-Host "Native runtime deployment checks passed."
