# Deterministic gate for Arc 19 Biblio consumption-first navigation.
$ErrorActionPreference = "Stop"
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$testDir = $PSScriptRoot
$candidateRoot = Split-Path -Parent $testDir
$localQmlDir = Join-Path $candidateRoot "qml"
$runHarness = Join-Path $testDir "biblio_consumption_intent_harness.qml"
$scratch = $null

# After adoption, tests/ sits beside the complete qml/ tree and runs directly.
# Inside Preflight, stage the current Colosseum qml/ tree and overlay the candidate.
if (!(Test-Path -LiteralPath (Join-Path $localQmlDir "Theme.qml"))) {
    $colosseumRoot = if ($env:COLOSSEUM_ROOT) { $env:COLOSSEUM_ROOT } else { "C:/Users/Suprabha/Desktop/Brotherhood/Colosseum" }
    $sourceQml = Join-Path $colosseumRoot "qml"
    if (!(Test-Path -LiteralPath (Join-Path $sourceQml "Theme.qml"))) {
        Write-Host "FAIL: set COLOSSEUM_ROOT to a current Colosseum checkout"
        exit 1
    }
    $scratch = Join-Path $env:TEMP ("arc19-biblio-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path (Join-Path $scratch "tests") -Force | Out-Null
    Copy-Item -LiteralPath $sourceQml -Destination (Join-Path $scratch "qml") -Recurse
    Copy-Item -LiteralPath (Join-Path $localQmlDir "BiblioBook.qml") -Destination (Join-Path $scratch "qml/BiblioBook.qml") -Force
    Copy-Item -LiteralPath $runHarness -Destination (Join-Path $scratch "tests/biblio_consumption_intent_harness.qml") -Force
    $runHarness = Join-Path $scratch "tests/biblio_consumption_intent_harness.qml"
}

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$previousForceStderr = $env:QT_FORCE_STDERR_LOGGING
$previousQmlRules = $env:QT_LOGGING_RULES
$previousStyle = $env:QT_QUICK_CONTROLS_STYLE
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_LOGGING_RULES = "qt.qml.*=false"
$env:QT_QUICK_CONTROLS_STYLE = "Basic"
$output = & $qmlExe -platform offscreen $runHarness 2>&1 | Out-String
$code = $LASTEXITCODE
$env:QT_FORCE_STDERR_LOGGING = $previousForceStderr
$env:QT_LOGGING_RULES = $previousQmlRules
$env:QT_QUICK_CONTROLS_STYLE = $previousStyle
$ErrorActionPreference = $previous

if ($scratch -and (Test-Path -LiteralPath $scratch)) {
    Remove-Item -LiteralPath $scratch -Recurse -Force
}

if ($code -ne 0 -or $output -notmatch "BIBLIO_CONSUMPTION_INTENT_OK") {
    Write-Host "FAIL: biblio consumption-intent harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "Biblio consumption-first intent: OK"
