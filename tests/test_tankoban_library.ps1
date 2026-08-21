# Contract + offscreen-logic gate for the Tankoban Library tab. TB-001 pinned the mixed
# manga+comic wall, Details routing, and retained tab state; TB-002 added the manga-chapter
# progress join (canonical id → legacy title fallback), the manga identity fix (Collection
# key by seriesId + one-shot legacy re-file), and the resume-vs-Details tap branch; TB-003
# adds the volume-lane (kind:"tankoban") + comic-lane (kind:"comic") joins and picks the
# most-recent lane when a manga series holds both a chapter and a volume record; TB-004
# adds the download badge — a row whose resume-target chapter is on disk is badged, with
# the volume-lane deliberately left unbadged (its on-disk state lives in TankobanVolumes,
# not Downloads, so a chapter-id lookup there would be dishonest); TB-005 adds search +
# 3 filters (All / In Progress / Downloaded) + 3 sorts (Last Read / Recently Added / A-Z)
# + the card ⋮ menu with Remove from Library.
#
# Two layers, both cheap and CI-safe:
#   1. GREP SHAPE — the QML/JS files must carry the load-bearing wiring strings (a green
#      grep proves the string is PRESENT, never that it behaves; the offscreen harnesses
#      are what prove behaviour).
#   2. OFFSCREEN LOGIC — qml.exe -platform offscreen drives TankobanLibraryApi's pure row
#      derivation and TankobanLibraryTab's construction, and must print both sentinels.
#
# The final pixels are Hemanth's eyes-on; this file only pins shape + logic.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        Write-Host "FAIL: $message"
        exit 1
    }
}
function Assert-Absent([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) {
        Write-Host "FAIL: $message"
        exit 1
    }
}

$api    = Read-RepoFile "qml/TankobanLibraryApi.js"
$tab    = Read-RepoFile "qml/TankobanLibraryTab.qml"
$world  = Read-RepoFile "qml/TankobanWorld.qml"
$series = Read-RepoFile "qml/MangaSeries.qml"

# --- TB-001: the pure module has no context-property, network, or routing access ---
Assert-Absent $api "Collection." "TankobanLibraryApi must not touch the Collection singleton (pure module)"
Assert-Absent $api "Progress." "TankobanLibraryApi must not touch the Progress singleton (pure module)"
Assert-Absent $api "LocalDownloads." "TankobanLibraryApi must not touch LocalDownloads (pure module)"
Assert-Absent $api "XMLHttpRequest" "TankobanLibraryApi must not make network calls (pure module)"
Assert-Contains $api "function buildRows(" "buildRows is the module's row-derivation entry point"

# --- TB-001: the page is typeof-guarded so it constructs offscreen, and exposes the
#     minimal contract (allRows, detailRequested) TB-002+ builds on ---
Assert-Contains $tab "typeof Collection" "page must typeof-guard Collection so it constructs offscreen"
Assert-Contains $tab "signal detailRequested(var entry)" "page must emit detailRequested for unstarted/Details taps"
Assert-Contains $tab "GridView" "page must render the wall as a GridView"
Assert-Contains $tab "Your library is empty" "page must show the empty-Collection state"

# --- TB-002: the manga-chapter join lives in the pure module, and the page feeds it the
#     real manga Progress list (typeof-guarded, same pattern as Collection) ---
Assert-Contains $api "resumeTarget" "buildRows must carry the resumeTarget field (manga-chapter join)"
Assert-Contains $api "resumeLane" "buildRows must carry the resumeLane field"
Assert-Contains $api "normalizedTitle" "buildRows reuses the module's normalizedTitle helper for the legacy fallback"
Assert-Contains $tab "typeof Progress" "page must typeof-guard Progress so it constructs offscreen"
Assert-Contains $tab "Progress.recent(`"manga`", 0)" "page must feed the manga Progress list into buildRows"
Assert-Contains $tab "signal resumeRequested(var record)" "page must expose a resumeRequested signal for started rows"
Assert-Contains $tab "handleCardTap" "page must route card taps through handleCardTap (started->resume, else details)"

# --- TB-003: the volume lane + comic lane feeds, and the multi-lane matcher picks the
#     most-recent lane when a manga series holds both chapter and volume progress ---
Assert-Contains $tab "Progress.recent(`"tankoban`", 0)" "page must feed the volume-lane Progress list into buildRows"
Assert-Contains $tab "Progress.recent(`"comic`", 0)" "page must feed the comic-lane Progress list into buildRows"
Assert-Contains $api "_matchProgressOfKind" "buildRows must use the kind-filtered matcher for all three lanes"
Assert-Contains $api '"tankoban"' "buildRows must recognize the tankoban (volume) lane"
Assert-Contains $api '"comic"' "buildRows must recognize the comic lane"
Assert-Contains $api "_newerOf" "buildRows must pick the newer of two lane records (most-recent lane rule)"

# --- TB-002: a started Library row routes through the world's existing
#     continueResumeRequested door (already wired in Main.qml). No Main.qml edit. ---
Assert-Contains $world "onResumeRequested: function(record) { tanko.continueResumeRequested(record) }" `
    "Library resume taps must hop through the existing continueResumeRequested door"
# the existing TB-001 detail wire must still be present verbatim (regression guard)
Assert-Contains $world 'onDetailRequested: function(entry) { tanko.collectionOpenRequested(entry) }' `
    "Library detail routing through collectionOpenRequested must remain unchanged"

# --- TB-002: manga Collection identity keys by seriesId (not seriesTitle), so a saved
#     manga matches its own Progress records. The legacy title-keyed shape is the bug. ---
Assert-Contains $series '"id": page.seriesId' "collectionEntry() must key by seriesId"
Assert-Absent $series '"id": page.seriesTitle' "collectionEntry() must NOT key by seriesTitle (the identity bug)"
Assert-Contains $series "_refileLegacyCollectionEntryIfNeeded" "MangaSeries must carry the one-shot legacy re-file"
Assert-Contains $series "Collection.has(`"tankoban`", sid)" "re-file must verify the canonical entry landed before removing the legacy one"

# --- TB-001: Library is a FOURTH tab, retained (not Loader-swapped), and Manga/Comics'
#     existing Loader-swap wiring is untouched ---
Assert-Contains $world '{ key: "library", label: "Library" }' "tab model must carry the library key"
Assert-Contains $world "TankobanLibraryTab {" "world must mount the retained TankobanLibraryTab"
Assert-Contains $world 'onDetailRequested: function(entry) { tanko.collectionOpenRequested(entry) }' `
    "Library detail taps must route through the existing collectionOpenRequested door"
Assert-Contains $world 'source: tanko.activeTab === "comics" ? "TankobanComicsTab.qml"' `
    "Manga/Comics Loader-swap wiring must remain unchanged"
Assert-Absent $world '"TankobanLibraryTab.qml"' "Library must NOT go through the Manga/Comics Loader (retained child, not Loader-swapped)"

# --- TB-004: download badge — the page computes a per-chapter-id on-disk map (typeof-guarded
#     Downloads + finished/removed Connections that bump dlRev) and feeds it to buildRows,
#     which badges a row whose resume-target chapter id is in the map. Volume-lane rows are
#     deliberately unbadged (no honest per-volume on-disk state without a new seam). ---
Assert-Contains $api 'row.resumeTarget && row.resumeLane !== "tankoban"' `
    "buildRows must only badge a row that has a resume target on a non-volume lane"
Assert-Contains $api "onDisk[chId] === true" "buildRows must look up the resume chapter id in the on-disk map"
Assert-Contains $api "row.downloadSeriesKey = chId" "buildRows must set downloadSeriesKey to the resume chapter id when badged"
Assert-Contains $tab "typeof Downloads" "page must typeof-guard Downloads so it constructs offscreen"
Assert-Contains $tab "Downloads.isDownloaded" "page must probe Downloads.isDownloaded per distinct resume chapter id"
Assert-Contains $tab "function onFinished(cid)" "page must bump dlRev on Downloads.finished (badge refresh)"
Assert-Contains $tab "function onRemoved(cid)" "page must bump dlRev on Downloads.removed (badge refresh)"
Assert-Contains $tab "Api.buildRows(entries, mp, vp, cp, onDisk)" "page must feed the on-disk map as buildRows' 5th arg"
# the pure module still must not touch Downloads directly (the page owns the seam)
Assert-Absent $api "Downloads." "TankobanLibraryApi must not touch the Downloads singleton (pure module; page owns the seam)"

# --- TB-005: search + 3 filters + 3 sorts live as pure functions in the api module; the
#     page owns the toolbar state (filter/sortMode/query) + the visibleRows pipeline +
#     the ⋮ card menu (Remove from Library) via a removeRequested signal the world wires
#     to Collection.remove. Filter labels: All/In Progress/Downloaded; sort labels:
#     Last Read/Recently Added/A-Z (the page's chosen enumeration — not specced elsewhere). ---
Assert-Contains $api "function applyFilters(" "api module must expose applyFilters (search + filter)"
Assert-Contains $api "function sortRows(" "api module must expose sortRows (3 sort modes)"
Assert-Contains $api '"inprogress"' "applyFilters must recognize the 'inProgress' filter"
Assert-Contains $api '"downloaded"' "applyFilters must recognize the 'downloaded' filter"
Assert-Contains $api '"lastRead"' "sortRows must recognize the 'lastRead' sort (default)"
Assert-Contains $api '"added"' "sortRows must recognize the 'added' sort"
Assert-Contains $api '"az"' "sortRows must recognize the 'az' sort"
# applyFilters/sortRows stay pure: no singleton access (mirrors the TB-001 module-wide guard)
Assert-Contains $api "r.entry && r.entry.title" "applyFilters/sortRows read the row contract's entry.title, not a singleton"
Assert-Contains $tab "property string filter" "page must hold the active filter chip"
Assert-Contains $tab "property string sortMode" "page must hold the active sort mode"
Assert-Contains $tab "property string query" "page must hold the search needle"
Assert-Contains $tab 'Api.sortRows(' "page must pipe visibleRows through sortRows"
Assert-Contains $tab 'Api.applyFilters(allRows' "page must pipe visibleRows through applyFilters (filter + query)"
Assert-Contains $tab 'signal removeRequested(var entry)' "page must expose removeRequested for the ⋮ menu's Remove action"
Assert-Contains $tab '"All"' "page toolbar must carry the 'All' filter label"
Assert-Contains $tab '"In Progress"' "page toolbar must carry the 'In Progress' filter label"
Assert-Contains $tab '"Downloaded"' "page toolbar must carry the 'Downloaded' filter label"
Assert-Contains $tab '"Last Read"' "page toolbar must carry the 'Last Read' sort label"
Assert-Contains $tab '"Recently Added"' "page toolbar must carry the 'Recently Added' sort label"
Assert-Contains $tab '"A-Z"' "page toolbar must carry the 'A-Z' sort label"
Assert-Contains $tab "Remove from Library" "page ⋮ menu must carry the Remove from Library action"
Assert-Contains $tab "No matches" "page must show a no-match empty state when filters narrow the wall to zero"
# the world wires the ⋮ menu's Remove to Collection.remove (mirrors TheatreWorld's shape)
Assert-Contains $world 'onRemoveRequested: function(entry) { if (typeof Collection !== "undefined") Collection.remove("tankoban", String(entry.id)) }' `
    "Library Remove must route through Collection.remove with the String() id cast"

$env:QT_FORCE_STDERR_LOGGING = "1"
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"

$apiHarness = Join-Path $PSScriptRoot "tankoban_library_api_harness.qml"
$apiOutput = & $qmlExe -platform offscreen $apiHarness 2>&1 | Out-String
$apiCode = $LASTEXITCODE
if ($apiCode -ne 0 -or ($apiOutput -notmatch "tankoban_library_api_harness: ALL PASS")) {
    $ErrorActionPreference = $prevEAP
    Write-Host "FAIL: api harness (exit $apiCode)"
    Write-Host $apiOutput
    exit 1
}

$pageHarness = Join-Path $PSScriptRoot "tankoban_library_page_harness.qml"
$pageOutput = & $qmlExe -platform offscreen $pageHarness 2>&1 | Out-String
$pageCode = $LASTEXITCODE
if ($pageCode -ne 0 -or ($pageOutput -notmatch "tankoban_library_page_harness: ALL PASS")) {
    $ErrorActionPreference = $prevEAP
    Write-Host "FAIL: page harness (exit $pageCode)"
    Write-Host $pageOutput
    exit 1
}
$ErrorActionPreference = $prevEAP

Write-Host "tankoban library (TB-005): OK"
