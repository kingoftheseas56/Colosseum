# Your Collection arc — wiring contracts (SHAPE, not behavior; behavior = harness).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) {
    if ($hay -notlike "*$needle*") { Write-Host "FAIL: $msg"; exit 1 }
}

$main = Read-File "native/main.cpp"
Assert-Contains $main '#include "CollectionStore.h"' "main.cpp includes CollectionStore.h"
Assert-Contains $main 'setContextProperty(QStringLiteral("Collection")' "Collection registered as context property"

# The behavioral harness must exist and pass.
$harness = Join-Path $root "native\build-msvc\collection_store_harness.exe"
if (-not (Test-Path $harness)) { Write-Host "FAIL: build collection_store_harness first"; exit 1 }
& $harness | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: collection_store_harness red"; exit 1 }

Write-Host "test_collection_p0 PASSED"
