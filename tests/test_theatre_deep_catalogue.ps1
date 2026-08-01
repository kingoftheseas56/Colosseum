param([string]$Stage = "All")

# Focused acceptance runner for the Theatre Deep Catalogue (spec 2026-08-01).
# One entry point, many -Stage slices; -Stage All runs every wired slice and prints
# THEATRE_DEEP_CATALOGUE_OK only when all pass. Each offscreen QML harness is gated on
# BOTH its unique OK marker AND a clean exit(0). qml.exe is a GUI-subsystem binary, so
# QT_FORCE_STDERR_LOGGING pushes console.log to the merged stream we capture.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
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

function Stage-Rules {
    Invoke-Harness "tests\theatre_catalog_rules_harness.qml" "THEATRE_CATALOG_RULES_OK"
}
function Stage-ApiRows {
    Invoke-Harness "tests\theatre_api_rows_harness.qml" "THEATRE_API_ROWS_OK"
}
function Stage-Cards {
    Invoke-Harness "tests\catalogue_poster_card_harness.qml" "CATALOGUE_POSTER_CARD_OK"
}
function Stage-SeeAll {
    Invoke-Harness "tests\theatre_see_all_harness.qml" "THEATRE_SEE_ALL_OK"
}
function Stage-DiscoverRegression {
    # The deep catalogue reuses Discover's addon/card layer — prove Discover's observable
    # behaviour is unchanged by the shared-identity + shared-card refactors.
    Invoke-Harness "tests\discover_api_harness.qml"    "discover_api_harness: ALL PASS"
    Invoke-Harness "tests\discover_page_harness.qml"   "discover_page_harness: ALL PASS"
    Invoke-Harness "tests\discover_picker_harness.qml" "discover_picker_harness: ALL PASS"
    Invoke-Harness "tests\discover_browser_harness.qml" "DISCOVER_BROWSER_OK"
}

# Additional stages (Cards, SeeAll, Preferences, Page) are wired by their owning tasks.
# -Stage All composes every wired slice.
$wired = @("Rules", "ApiRows", "Cards", "SeeAll", "DiscoverRegression")

switch ($Stage) {
    "Rules"              { Stage-Rules }
    "ApiRows"            { Stage-ApiRows }
    "Cards"              { Stage-Cards }
    "SeeAll"             { Stage-SeeAll }
    "DiscoverRegression" { Stage-DiscoverRegression }
    "All" {
        Stage-Rules
        Stage-ApiRows
        Stage-Cards
        Stage-SeeAll
        Stage-DiscoverRegression
        Write-Host "THEATRE_DEEP_CATALOGUE_OK"
    }
    default { throw "unknown -Stage '$Stage' (wired: $($wired -join ', '), All)" }
}
