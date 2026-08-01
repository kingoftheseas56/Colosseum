$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Task 3 gate: the world-neutral DiscoverBrowser shell PLUS the three Theatre regression
# harnesses it was carved out of — Theatre's observable behaviour must not change.
# qml.exe is a GUI-subsystem exe: its console.log never reaches redirected stdout unless
# QT_FORCE_STDERR_LOGGING forces it to stderr, so we set that and capture the merged stream,
# gating each harness on BOTH its OK marker AND a clean exit (0).
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$qmlInc = Join-Path $root "qml"

function Invoke-Harness($relPath, $marker) {
    $harness = Join-Path $root $relPath
    if (!(Test-Path -LiteralPath $harness)) { throw "harness not found: $harness" }
    $out  = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$harness`" 2>&1" | Out-String
    $code = $LASTEXITCODE
    Write-Host "----- $relPath (exit $code) -----"
    Write-Host $out
    if ($code -ne 0) { throw "$relPath failed (exit $code)" }
    if ($marker -and $out -notlike "*$marker*") { throw "$relPath missing OK marker '$marker' (exit $code)" }
}

# (1) shared-shell contract
Invoke-Harness "tests\discover_browser_harness.qml" "DISCOVER_BROWSER_OK"

# (2) Theatre regression coverage — MUST stay green under the refactor
Invoke-Harness "tests\discover_api_harness.qml"    "discover_api_harness: ALL PASS"
Invoke-Harness "tests\discover_page_harness.qml"   "discover_page_harness: ALL PASS"
Invoke-Harness "tests\discover_picker_harness.qml" "discover_picker_harness: ALL PASS"

Write-Host "discover shared shell + Theatre regression OK"
exit 0
