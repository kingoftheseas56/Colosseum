$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# =============================================================================
# Tankoban Discover — FULL ACCEPTANCE RUNNER (Task 10 Step 1).
# Runs every focused gate for the Tankoban Discover arc and prints
# TANKOBAN_DISCOVER_ACCEPTANCE_OK only after every child exits 0.
#
# Children:
#   (1)  tests\test_explicit_content_policy.ps1      (Task 1 + 9 cross-world boundary)
#   (2)  tests\test_content_preferences.ps1          (Task 2 preference plumbing)
#   (3)  tests\test_discover_shared_shell.ps1        (Task 3 shared shell + Theatre regression)
#   (4)  tests\test_tankoban_tabs.ps1                (Task 7+8 tabs/routing/pins)
#   (5)  tests\test_mal_genre_catalog_p0.ps1         (Task 4 baked MAL catalogue)
#   (6)  tests\test_comics_catalog_db.ps1            (Task 4 comics catalogue)
#   (7)  native\build-msvc\mal_catalog_discover_harness.exe   (native discover contract)
#   (8)  native\build-msvc\comics_catalog_engine_harness.exe  (native catalogue engine)
#   (9)  qml tankoban_discover_api_harness.qml       (Task 6 adapter contract)
#   (10) qml tankoban_discover_page_harness.qml      (Task 7+8 page/pin contract)
#
# Plus this file retains its Task 6 static contract guards on TankobanDiscoverApi.js
# (no Comic Vine / Metron runtime, no download action, Policy.visible in both paths).
# =============================================================================

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$qmlInc = Join-Path $root "qml"

function Invoke-Gate {
    param(
        [string]$Label,            # short human label
        [scriptblock]$Action       # runs the child; throws on failure
    )
    Write-Host ""
    Write-Host "===== $Label ====="
    & $Action
    Write-Host "----- $($Label): OK -----"
}

# (1) Explicit content policy + Task 9 cross-world boundary
Invoke-Gate "explicit content policy (Task 1 + 9)" {
    $p = Join-Path $root "tests\test_explicit_content_policy.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_explicit_content_policy exit $LASTEXITCODE" }
}

# (2) Content preferences plumbing
Invoke-Gate "content preferences (Task 2)" {
    $p = Join-Path $root "tests\test_content_preferences.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_content_preferences exit $LASTEXITCODE" }
}

# (3) Shared Discover shell + Theatre regression
Invoke-Gate "discover shared shell (Task 3)" {
    $p = Join-Path $root "tests\test_discover_shared_shell.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_discover_shared_shell exit $LASTEXITCODE" }
}

# (4) Tankoban tabs / routing / pins
Invoke-Gate "tankoban tabs (Task 7 + 8)" {
    $p = Join-Path $root "tests\test_tankoban_tabs.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_tankoban_tabs exit $LASTEXITCODE" }
}

# (5) MAL genre catalogue (baked DB + live ladder)
Invoke-Gate "mal genre catalogue p0 (Task 4)" {
    $p = Join-Path $root "tests\test_mal_genre_catalog_p0.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_mal_genre_catalog_p0 exit $LASTEXITCODE" }
}

# (6) Comics catalogue DB
Invoke-Gate "comics catalogue db (Task 4)" {
    $p = Join-Path $root "tests\test_comics_catalog_db.ps1"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) { throw "test_comics_catalog_db exit $LASTEXITCODE" }
}

# (7) Native discover contract harness
Invoke-Gate "mal_catalog_discover_harness (native)" {
    $exe = Join-Path $root "native\build-msvc\mal_catalog_discover_harness.exe"
    if (!(Test-Path -LiteralPath $exe)) { throw "native harness missing: $exe" }
    # Route through cmd /c so benign Qt SQL teardown warnings on stderr don't trip
    # $ErrorActionPreference = Stop. The gate is the exit code + OK marker, not stderr silence.
    $out = cmd /c "`"$exe`" 2>&1" | Out-String
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "mal_catalog_discover_harness exit $LASTEXITCODE" }
    if ($out -notlike "*MAL_CATALOG_DISCOVER_OK*") {
        throw "mal_catalog_discover_harness missing OK marker"
    }
}

# (8) Native comics catalogue engine harness
Invoke-Gate "comics_catalog_engine_harness (native)" {
    $exe = Join-Path $root "native\build-msvc\comics_catalog_engine_harness.exe"
    if (!(Test-Path -LiteralPath $exe)) { throw "native harness missing: $exe" }
    $out = cmd /c "`"$exe`" 2>&1" | Out-String
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "comics_catalog_engine_harness exit $LASTEXITCODE" }
    if ($out -notlike "*COMICS-CATALOG-ENGINE OK*") {
        throw "comics_catalog_engine_harness missing OK marker"
    }
}

# (9) Tankoban adapter contract harness (offscreen QML)
Invoke-Gate "tankoban_discover_api_harness (Task 6)" {
    $h = Join-Path $root "tests\tankoban_discover_api_harness.qml"
    if (!(Test-Path -LiteralPath $h)) { throw "harness missing: $h" }
    $out = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$h`" 2>&1" | Out-String
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "tankoban_discover_api_harness exit $LASTEXITCODE" }
    if ($out -notlike "*TANKOBAN_DISCOVER_API_OK*") {
        throw "tankoban_discover_api_harness missing OK marker"
    }
}

# (10) Tankoban page + pin contract harness (offscreen QML)
Invoke-Gate "tankoban_discover_page_harness (Task 7 + 8)" {
    $h = Join-Path $root "tests\tankoban_discover_page_harness.qml"
    if (!(Test-Path -LiteralPath $h)) { throw "harness missing: $h" }
    $out = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$h`" 2>&1" | Out-String
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "tankoban_discover_page_harness exit $LASTEXITCODE" }
    if ($out -notlike "*TANKOBAN_DISCOVER_PAGE_OK*") {
        throw "tankoban_discover_page_harness missing OK marker"
    }
}

# ---------------------------------------------------------------------------
# Static contract guards on TankobanDiscoverApi.js (carried from the Task 6
# runner). These need no live DB and no Comic Vine / Metron runtime — they pin
# the source-level invariants that survive even if a harness regresses.
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "===== static contract: TankobanDiscoverApi.js ====="
$adapter = Join-Path $root "qml\TankobanDiscoverApi.js"
if (!(Test-Path -LiteralPath $adapter)) { throw "adapter not found: $adapter" }
$src = Get-Content -LiteralPath $adapter -Raw

if ($src -notmatch 'var SOURCE\s*=\s*"tankoban-discover-adapter"') {
    throw "Static contract: TankobanDiscoverApi.js must expose var SOURCE = `"tankoban-discover-adapter`""
}
$comicvineHits = [regex]::Matches($src, "(?i)comicvine|comic\.vine|api\.comicvine\.com")
if ($comicvineHits.Count -gt 0) {
    throw "Static contract: must not depend on Comic Vine (found $($comicvineHits.Count) match(es))"
}
# Metron: match real endpoints, not the prose word (comments documenting this invariant are fine).
$metronHits = [regex]::Matches($src, "(?i)api\.metron\.cloud|metron\.cloud|m\.metron|https?://[^""'\s]*metron")
if ($metronHits.Count -gt 0) {
    throw "Static contract: must not depend on Metron (found $($metronHits.Count) endpoint match(es))"
}
$downloadPatterns = @(
    '(?i)\bdownload\s*\(.',
    '(?i)\bfetch\s*\(.*download',
    '(?i)\bacquire\s*\(',
    '(?i)\binstallAddon\s*\(',
    '(?i)\bstartDownload\s*\(',
    '(?i)\btorrent:add\b',
    '(?i)\baddon:install\b'
)
foreach ($pat in $downloadPatterns) {
    $hits = [regex]::Matches($src, $pat)
    if ($hits.Count -gt 0) {
        throw "Static contract: must not perform download/acquisition (pattern $pat matched $($hits.Count) time(s))"
    }
}
if ($src -notmatch '\.import\s+"ExplicitContentPolicy\.js"\s+as\s+Policy') {
    throw "Static contract: must import ExplicitContentPolicy.js as Policy (sexually-explicit-only gate)"
}
$visibleUses = [regex]::Matches($src, 'Policy\.visible\s*\(')
if ($visibleUses.Count -lt 2) {
    throw "Static contract: must apply Policy.visible in BOTH manga and comics fetch paths (found $($visibleUses.Count))"
}
Write-Host "----- static contract: OK -----"

Write-Host ""
Write-Host "TANKOBAN_DISCOVER_ACCEPTANCE_OK"
exit 0
