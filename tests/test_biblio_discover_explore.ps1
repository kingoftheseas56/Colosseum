$ErrorActionPreference = "Stop"

# =============================================================================
# Biblio Discover/Explore - FOCUSED FEATURE GATE (plan 2026-08-01-biblio-discover-explore.md,
# Task 9, Step 3). Runs the three native BiblioCatalog harnesses, the four Biblio QML harnesses
# offscreen, and a set of static contract guards - prints BIBLIO_DISCOVER_EXPLORE_OK only after
# every child is both a clean exit(0) AND carries its own unique OK marker.
#
# Children:
#   (1) native\build-msvc\biblio_catalog_logic_harness.exe     (taxonomy/ranking oracle)
#   (2) native\build-msvc\biblio_catalog_store_harness.exe     (SQLite snapshot/paging oracle)
#   (3) native\build-msvc\biblio_catalog_service_harness.exe   (daily refresh/failure oracle)
#   (4) qml biblio_discover_api_harness.qml   (Task 5 adapter contract + Task 9 lifecycle)
#   (5) qml biblio_discover_page_harness.qml  (Task 5 page contract + Task 9 Part A explicit-flip)
#   (6) qml biblio_explore_harness.qml        (Task 6+7 rules/prefs/page + Task 9 Part A explicit-flip)
#   (7) qml biblio_world_harness.qml          (Task 8 world integration)
#
# Plus static contract guards (grep-based, mirroring tests\test_theatre_deep_catalogue.ps1's
# Stage-RoundedPoster convention): no `api_key` anywhere in the Biblio QML/JS surface, no
# award-style row label, no second hero/Continue widget inside the Discover/Explore tab pages
# (those live only in BiblioWorld.qml), and no row-blurb field/property anywhere.
# =============================================================================

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$qmlInc = Join-Path $root "qml"

function Invoke-Gate {
    param(
        [string]$Label,
        [scriptblock]$Action
    )
    Write-Host ""
    Write-Host "===== $Label ====="
    & $Action
    Write-Host "----- $($Label): OK -----"
}

function Invoke-NativeHarness($exeRel, $marker) {
    $exe = Join-Path $root $exeRel
    if (!(Test-Path -LiteralPath $exe)) { throw "native harness missing: $exe (build it first with cmake --build native/build-msvc --config Release --target $([IO.Path]::GetFileNameWithoutExtension($exeRel)))" }
    # Route through cmd /c so benign Qt SQL teardown warnings on stderr don't trip
    # $ErrorActionPreference = Stop. The gate is the exit code + OK marker, not stderr silence.
    $out = cmd /c "`"$exe`" 2>&1" | Out-String
    Write-Host $out
    if ($LASTEXITCODE -ne 0) { throw "$exeRel failed (exit $LASTEXITCODE)" }
    if ($out -notlike "*$marker*") { throw "$exeRel missing OK marker '$marker'" }
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

# (1)-(3) native BiblioCatalog harnesses
Invoke-Gate "biblio_catalog_logic_harness (native)" {
    Invoke-NativeHarness "native\build-msvc\biblio_catalog_logic_harness.exe" "BIBLIO_CATALOG_LOGIC_OK"
}
Invoke-Gate "biblio_catalog_store_harness (native)" {
    Invoke-NativeHarness "native\build-msvc\biblio_catalog_store_harness.exe" "BIBLIO_CATALOG_STORE_OK"
}
Invoke-Gate "biblio_catalog_service_harness (native)" {
    Invoke-NativeHarness "native\build-msvc\biblio_catalog_service_harness.exe" "BIBLIO_CATALOG_SERVICE_OK"
}

# (4)-(7) the four Biblio QML harnesses
Invoke-Gate "biblio_discover_api_harness (Task 5 + 9)" {
    Invoke-QmlHarness "tests\biblio_discover_api_harness.qml" "BIBLIO_DISCOVER_API_OK"
}
Invoke-Gate "biblio_discover_page_harness (Task 5 + 9 Part A)" {
    Invoke-QmlHarness "tests\biblio_discover_page_harness.qml" "BIBLIO_DISCOVER_PAGE_OK"
}
Invoke-Gate "biblio_explore_harness (Task 6 + 7 + 9 Part A)" {
    Invoke-QmlHarness "tests\biblio_explore_harness.qml" "BIBLIO_EXPLORE_PAGE_OK"
}
Invoke-Gate "biblio_world_harness (Task 8)" {
    Invoke-QmlHarness "tests\biblio_world_harness.qml" "BIBLIO_WORLD_OK"
}

# ── static contract guards ──
Invoke-Gate "static: no api_key anywhere in the Biblio QML/JS surface" {
    $biblioFiles = Get-ChildItem (Join-Path $root "qml") -Filter "Biblio*.qml" -File
    $biblioFiles += Get-ChildItem (Join-Path $root "qml") -Filter "Biblio*.js" -File
    if ($biblioFiles.Count -eq 0) { throw "no Biblio*.qml/js files found under qml/ - guard would be vacuous" }
    foreach ($f in $biblioFiles) {
        $text = Get-Content $f.FullName -Raw
        if ($text -match "api_key") { throw "$($f.Name) must stay keyless (no API keys) - found 'api_key'" }
    }
}

Invoke-Gate "static: no award-style row label in the Biblio Explore surface" {
    $explorePage = Read-File "qml/BiblioExplorePage.qml"
    $exploreRules = Read-File "qml/BiblioExploreRules.js"
    $bookRail = Read-File "qml/BiblioBookRail.qml"
    foreach ($pair in @(@{name="BiblioExplorePage.qml"; text=$explorePage},
                         @{name="BiblioExploreRules.js"; text=$exploreRules},
                         @{name="BiblioBookRail.qml"; text=$bookRail})) {
        if ($pair.text -match "(?i)award") {
            throw "$($pair.name) must carry no award-based discovery concept (plan global constraint) - found 'award'"
        }
    }
}

Invoke-Gate "static: no second hero/Continue widget inside the Discover/Explore tab pages" {
    $discoverPage = Read-File "qml/BiblioDiscoverPage.qml"
    $explorePage = Read-File "qml/BiblioExplorePage.qml"
    foreach ($pair in @(@{name="BiblioDiscoverPage.qml"; text=$discoverPage},
                         @{name="BiblioExplorePage.qml"; text=$explorePage})) {
        if ($pair.text -match "FeaturedCarousel\s*\{") {
            throw "$($pair.name) must not host its own FeaturedCarousel - that widget lives ONLY in BiblioWorld.qml"
        }
        if ($pair.text -match "ContinueRow\s*\{") {
            throw "$($pair.name) must not host its own ContinueRow - that widget lives ONLY in BiblioWorld.qml"
        }
    }
}

Invoke-Gate "static: no row-blurb field/property in the Discover/Explore row surface" {
    # Scoped to Discover/Explore's OWN row/card surface - NOT BiblioWorld.qml, whose shared
    # FeaturedCarousel legitimately carries a `blurb` field on its slides (the SAME established
    # cross-world FeaturedCarousel convention Tankoban/Theatre already use); this guard is about
    # the plan's "no generated row blurbs" constraint for Discover/Explore SHELF rows specifically.
    # Match actual FIELD/PROPERTY usage only (an object-literal key, a property access, or a QML
    # property declaration) so a documenting comment that names the very thing it forbids (e.g.
    # "NO row blurb is ever rendered here") can't trip the guard -- the same discipline
    # tests\test_theatre_deep_catalogue.ps1's MultiEffect-instantiation guard uses.
    $files = @("qml/BiblioDiscoverApi.js", "qml/BiblioExplorePage.qml", "qml/BiblioBookRail.qml",
               "qml/BiblioExploreRules.js", "qml/BiblioDiscoverPage.qml")
    foreach ($rel in $files) {
        $lines = Get-Content (Join-Path $root $rel)
        foreach ($line in $lines) {
            $code = $line -replace "//.*$", ""   # strip a trailing line comment before matching
            if ($code -match "(?i)\bblurb\s*:" -or $code -match "(?i)\.\s*blurb\b" `
                -or $code -match "(?i)property\s+\S+\s+blurb\b") {
                throw "$rel must carry no generated row/item blurb field (plan global constraint) - found: $($line.Trim())"
            }
        }
    }
}

Write-Host ""
Write-Host "BIBLIO_DISCOVER_EXPLORE_OK"
exit 0
