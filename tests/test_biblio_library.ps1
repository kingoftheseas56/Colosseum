$ErrorActionPreference = "Stop"

# =============================================================================
# Biblio Library tab — FOCUSED FEATURE GATE (plan 2026-08-06-biblio-library-tab-theatre-parity.md).
# Slice 1: runs the pure-JS derivation harness offscreen and a static guard that the new API
# module imports nothing video-specific from LibraryApi.js. Prints
# BIBLIO_LIBRARY_OK only after the harness is both a clean exit(0) AND carries its OK marker.
#
# Children:
#   (1) qml biblio_library_api_harness.qml  (Slice 1 pure-derivation contract)
#
# Slices 2-4 add the page harness, the world-harness extension, and the Lanista scenario; this
# runner grows with them. Mirrors tests/test_biblio_discover_explore.ps1's shape.
# =============================================================================

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$qmlInc = Join-Path $root "qml"

function Invoke-Gate {
    param([string]$Label, [scriptblock]$Action)
    Write-Host ""
    Write-Host "===== $Label ====="
    & $Action
    Write-Host "----- $($Label): OK -----"
}

function Invoke-QmlHarness($relPath, $marker) {
    $harness = Join-Path $root $relPath
    if (!(Test-Path -LiteralPath $harness)) { throw "harness not found: $harness" }
    $out = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$harness`" 2>&1" | Out-String
    Write-Host $out
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "$relPath failed (exit $code)" }
    if ($marker -and $out -notlike "*$marker*") { throw "$relPath missing OK marker '$marker' (exit $code)" }
}

function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}

# (1) the pure-JS derivation harness
Invoke-Gate "biblio_library_api_harness (Slice 1)" {
    Invoke-QmlHarness "tests\biblio_library_api_harness.qml" "BIBLIO_LIBRARY_API_OK"
}

# (2) the offscreen page construct + signal-contract harness (Slice 2)
Invoke-Gate "biblio_library_page_harness (Slice 2)" {
    Invoke-QmlHarness "tests\biblio_library_page_harness.qml" "BIBLIO_LIBRARY_PAGE_OK"
}

# ── static contract guard: the biblio API module carries no video concepts in CODE ──
# (strip JS comments first so the guard isn't tripped by the comments that DOCUMENT the absence)
Invoke-Gate "static: BiblioLibraryApi carries no video concepts in code (watched/airing/finale/newEpisode)" {
    $api = Read-File "qml\BiblioLibraryApi.js"
    # drop // line comments and /* */ block comments, leaving only executable code
    $code = $api -replace '/\*[\s\S]*?\*/', '' -replace '//.*', ''
    foreach ($forbidden in @("watched", "airing", "finale", "newEpisode", "episode", "isSeries", "markWatched")) {
        if ($code -imatch $forbidden) { throw "BiblioLibraryApi.js references video concept in code: $forbidden" }
    }
}

Write-Host ""
Write-Host "BIBLIO_LIBRARY_OK"
