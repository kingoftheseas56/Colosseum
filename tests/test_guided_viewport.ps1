# Offscreen-logic gate for the Guided Reader viewport (Task 9).
#
# Drives qml/guided/GuidedViewport.qml through tests/guided_viewport_harness.qml under
# qml.exe -platform offscreen and requires the GUIDED_VIEWPORT_OK sentinel. The final
# pixels (the glide itself) are Hemanth's eyes-on; this only pins the load-bearing logic:
# intact source images, one wide spread canvas, no crop substitution, and interruption.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "guided_viewport_harness.qml"

# qml.exe emits benign warnings (font dir) on stderr; don't let ErrorActionPreference=Stop
# turn a native-command stderr line into a terminating error before we read the verdict.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "GUIDED_VIEWPORT_OK")) {
    Write-Host "FAIL: guided viewport offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "guided viewport: OK"
