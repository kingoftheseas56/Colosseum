# Tankoban Top Comics Explore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Tankoban Top Comics Explore label into a lazy, searchable ranked wall for all 688 resolved comics while trimming the home strip to the top ten.

**Architecture:** Keep the generated catalog owned by lazy `TankobanWorld.qml`, pass its lightweight ranked rows into a new catalog Loader, and route catalog cards through the existing `ComicSeriesPage` layer. Put deterministic rank/search/availability projection in a small JavaScript library so the catalog behavior is headlessly testable without creating 688 visual delegates.

**Tech Stack:** Qt 6 / QML, QML JavaScript libraries, PowerShell test runner, Qt `qml.exe` offscreen harness, MSVC 2022 native build.

## Global Constraints

- The home Top Comics widget displays exactly the first 10 resolved rows; Explore receives the complete array.
- Canonical order is fixed and display ranks are clean sequential ordinals 1 through 688.
- Downloadable means at least one edition has both `available` and a non-empty `getcomics_post`.
- The root `Main.qml` must not import or ingest `comics_db.gen.js`; catalog loading remains behind `TankobanWorld.qml`.
- The wall must use a virtualized `GridView`, asynchronous cover images, and integer `font.pixelSize` values.
- The existing GetComics Archives taxonomy, torrent code, reader/Biblio work, universe work, SQLite runtime, and full 24K browse remain untouched.
- Stage only files named by this plan; preserve all unrelated dirty files.

---

### Task 1: Pure catalog projection and filters

**Files:**
- Create: `qml/ComicCatalogModel.js`
- Modify: `qml/ComicsDb.js`
- Modify: `tests/comics_catalog_logic_harness.qml`
- Modify: `tests/test_comics_catalog_v1.ps1`

**Interfaces:**
- Consumes: `ComicsDb.rankedSeries()`, `ComicsDb.editions(locgId)`.
- Produces: `ComicsDb.hasDownloadableEdition(locgId) -> bool`, `ComicCatalogModel.prepare(rows, availabilityFn) -> array`, and `ComicCatalogModel.filter(rows, query, downloadableOnly) -> array`.

- [ ] **Step 1: Write the failing headless assertions**

Extend `tests/comics_catalog_logic_harness.qml` to import `ComicCatalogModel.js`, prepare all ranked rows, and assert sequential `displayRank`, route integrity, case-insensitive title/publisher search, canonical-order preservation, and honest availability:

```qml
var prepared = CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)
if (prepared.length !== 688 || prepared[0].displayRank !== 1
        || prepared[687].displayRank !== 688)
    throw new Error("display ranks are not sequential")
var titleHit = CatalogModel.filter(prepared, rows[0].title.toUpperCase(), false)
if (!titleHit.length || titleHit[0].locgId !== rows[0].locgId)
    throw new Error("title search is not case-insensitive")
var downloadable = CatalogModel.filter(prepared, "", true)
for (var d = 0; d < downloadable.length; d++)
    if (!downloadable[d].downloadable)
        throw new Error("downloadable filter admitted an unavailable series")
```

- [ ] **Step 2: Run the harness and verify RED**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comics_catalog_v1.ps1`

Expected: FAIL because `ComicCatalogModel.js` and `ComicsDb.hasDownloadableEdition` do not exist.

- [ ] **Step 3: Implement the minimal pure model**

Create `qml/ComicCatalogModel.js` as a `.pragma library` with copies of input rows annotated once:

```javascript
.pragma library

function prepare(rows, availabilityFn) {
    var source = Array.isArray(rows) ? rows : []
    return source.map(function(row, index) {
        var out = {}
        for (var key in row) out[key] = row[key]
        out.displayRank = index + 1
        out.downloadable = !!availabilityFn(row.locgId)
        return out
    })
}

function filter(rows, query, downloadableOnly) {
    var needle = String(query || "").trim().toLowerCase()
    return (Array.isArray(rows) ? rows : []).filter(function(row) {
        if (downloadableOnly && !row.downloadable) return false
        if (!needle.length) return true
        return String(row.title || row.caption || "").toLowerCase().indexOf(needle) >= 0
            || String(row.publisher || "").toLowerCase().indexOf(needle) >= 0
    })
}
```

Add `ComicsDb.hasDownloadableEdition(locgId)` by scanning the already-indexed editions and returning true only when `downloadPost(edition)` is non-null.

- [ ] **Step 4: Run the harness and verify GREEN**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comics_catalog_v1.ps1`

Expected: `COMICS_CATALOG_OK 688` and `comics catalog v1: OK`, exit 0.

- [ ] **Step 5: Commit the pure logic slice**

```powershell
git add -- qml/ComicCatalogModel.js qml/ComicsDb.js tests/comics_catalog_logic_harness.qml tests/test_comics_catalog_v1.ps1
git commit -m "[Agent 1 (Codex), comics] Add catalog ranking and filters"
```

### Task 2: Home Top 10 and Explore event

**Files:**
- Modify: `qml/TrendingTop10.qml`
- Modify: `qml/TankobanWorld.qml`
- Create: `tests/test_top_comics_explore.ps1`

**Interfaces:**
- Consumes: `tankoban.comicRows` containing all resolved ranked rows.
- Produces: `TrendingTop10.exploreClicked()`, `TankobanWorld.comicCatalogRequested(var rows)`.

- [ ] **Step 1: Write static contract tests**

Create `tests/test_top_comics_explore.ps1` with exact assertions that:

```powershell
$top = Get-Content -Raw qml/TrendingTop10.qml
$world = Get-Content -Raw qml/TankobanWorld.qml
if (!$top.Contains('signal exploreClicked()')) { throw 'missing Explore signal' }
if (!$top.Contains('onMoreClicked: top10.exploreClicked()')) { throw 'Explore label is not wired' }
if (!$world.Contains('signal comicCatalogRequested(var rows)')) { throw 'missing catalog route' }
if (!$world.Contains('items: tanko.comicRows.slice(0, 10)')) { throw 'home is not a Top 10' }
if (!$world.Contains('tanko.comicCatalogRequested(tanko.comicRows)')) { throw 'Explore does not receive the full catalog' }
```

- [ ] **Step 2: Run the contract test and verify RED**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1`

Expected: FAIL at the missing Explore signal.

- [ ] **Step 3: Wire the additive event and sliced model**

Add `signal exploreClicked()` and `WidgetHeader.onMoreClicked` in `TrendingTop10.qml`. In `TankobanWorld.qml`, add `comicCatalogRequested(var rows)`, bind the comics widget to `comicRows.slice(0, 10)`, resolve card clicks against the same slice, and forward Explore with the unsliced `comicRows`.

- [ ] **Step 4: Run the contract test and verify GREEN**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1`

Expected: `top comics explore contracts: OK`, exit 0.

- [ ] **Step 5: Commit the home navigation slice**

```powershell
git add -- qml/TrendingTop10.qml qml/TankobanWorld.qml tests/test_top_comics_explore.ps1
git commit -m "[Agent 1 (Codex), comics] Open full catalog from Top Comics"
```

### Task 3: Ranked library wall

**Files:**
- Create: `qml/ComicCatalogPage.qml`
- Modify: `tests/test_top_comics_explore.ps1`

**Interfaces:**
- Consumes: `rows`, `backdrop`, `ComicCatalogModel.prepare`, `ComicCatalogModel.filter`.
- Produces: `backRequested()`, `minimizeRequested()`, `closeRequested()`, `searchClicked()`, `seriesRequested(var series)`.

- [ ] **Step 1: Add page-structure tests**

Extend `tests/test_top_comics_explore.ps1` to require the page's `GridView`, sticky header, search text, All/Downloadable controls, rank text sourced from `displayRank`, asynchronous cover loading, titled failure fallback, empty-state actions, and scroll save/restore functions. Explicitly reject a 688-card `Repeater`.

- [ ] **Step 2: Run the contract test and verify RED**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1`

Expected: FAIL because `qml/ComicCatalogPage.qml` is absent.

- [ ] **Step 3: Build `ComicCatalogPage.qml`**

Implement an `Item` layer that:

```qml
property var rows: []
property var catalogRows: CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)
property string query: ""
property bool downloadableOnly: false
property var visibleRows: CatalogModel.filter(catalogRows, query, downloadableOnly)
property real savedAllContentY: 0
property bool filterViewActive: query.trim().length > 0 || downloadableOnly

function beginFilteredView() {
    if (!filterViewActive) savedAllContentY = wall.contentY
}
function restoreAllView() {
    Qt.callLater(function() { wall.contentY = Math.max(0, savedAllContentY) })
}
```

Use the approved fixed chrome and fixed header over a `GridView`. The delegate shows a large low-opacity canonical rank, portrait cover, two-line title, optional publisher, restrained gold acquisition mark, hover border/scale, and a whole-card click that emits `{id: locgId, title, cover}`. Cover errors reveal a stable gradient/title fallback. Empty source and zero-result states expose Back or Clear actions respectively.

- [ ] **Step 4: Run static and offscreen component checks**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1`

Run: `C:\Qt\6.11.1\msvc2022_64\bin\qml.exe -platform offscreen tests/comic_catalog_page_harness.qml`

Expected: contract test exits 0; harness creates the page with sample rows and prints `COMIC_CATALOG_PAGE_OK` without QML errors.

- [ ] **Step 5: Commit the page slice**

```powershell
git add -- qml/ComicCatalogPage.qml tests/test_top_comics_explore.ps1 tests/comic_catalog_page_harness.qml
git commit -m "[Agent 1 (Codex), comics] Build ranked comics library wall"
```

### Task 4: Lazy Main route and end-to-end verification

**Files:**
- Modify: `qml/Main.qml`
- Modify: `tests/test_top_comics_explore.ps1`

**Interfaces:**
- Consumes: `TankobanWorld.comicCatalogRequested(rows)`, `ComicCatalogPage.seriesRequested(series)`.
- Produces: `openComicCatalog(rows)`, `closeComicCatalog()`, a z-49 keep-alive catalog Loader below `comicSeriesLayer`.

- [ ] **Step 1: Add route/laziness tests**

Extend `tests/test_top_comics_explore.ps1` to assert the optional world signal connects to `openComicCatalog`, the Loader source is `ComicCatalogPage.qml`, card selection connects to `openComicSeries`, Escape closes series before catalog and catalog before the world, and `Main.qml` still contains neither the generated-data import nor `ComicsDb.setData`.

- [ ] **Step 2: Run the route test and verify RED**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1`

Expected: FAIL because the Main route does not exist.

- [ ] **Step 3: Implement the lazy route**

Add:

```qml
function openComicCatalog(rows) {
    comicCatalogLayer.rows = rows || []
    comicCatalogLayer.active = true
}
function closeComicCatalog() { comicCatalogLayer.active = false }
```

Connect the guarded `comicCatalogRequested` signal in the world Loader. Add a z-49 `Loader` for `ComicCatalogPage.qml`, inject backdrop and rows on load, connect back/system/search signals, and connect `seriesRequested` to the existing `openComicSeries`. Place the catalog Escape branch immediately after `comicSeriesLayer` so series Back returns to the live wall and the next Back returns to Tankoban.

- [ ] **Step 4: Run deterministic verification**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comics_catalog_v1.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_top_comics_explore.ps1
```

Expected: both scripts exit 0; catalog harness reports 688 sequential rows.

- [ ] **Step 5: Build the app**

Find `colosseum.exe` processes, stop each by PID, then run `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat` directly.

Expected: exit 0 with final `BUILD_OK`.

- [ ] **Step 6: Run startup and Tankoban smokes**

Launch `native\build-msvc\colosseum.exe qml\Main.qml` with `QML_DISABLE_DISK_CACHE=1`. Confirm root boot does not log `ComicsDb: loaded`; navigate to Tankoban and confirm `ComicsDb: loaded 688 series`; open Top Comics Explore, open a card, Back to the preserved wall, and confirm no QML creation/property errors. Leave the app on the catalog wall for Hemanth's eyes-on check.

- [ ] **Step 7: Review the scoped diff against the Definition of Done**

Run `git diff --check`, `git status --short`, and a scoped diff limited to the files in this plan. Mark each design Definition-of-Done item MET/PARTIAL/NOT-MET and correct every code-verifiable gap before shipping.

- [ ] **Step 8: Commit and push surgically**

```powershell
git add -- qml/ComicCatalogModel.js qml/ComicCatalogPage.qml qml/ComicsDb.js qml/TrendingTop10.qml qml/TankobanWorld.qml qml/Main.qml tests/comics_catalog_logic_harness.qml tests/comic_catalog_page_harness.qml tests/test_comics_catalog_v1.ps1 tests/test_top_comics_explore.ps1 docs/superpowers/plans/2026-07-14-colosseum-top-comics-explore.md
git commit -m "[Agent 1 (Codex), comics] Ship Top Comics catalog explore"
git push origin master
```

Expected: only the named comics/catalog files are committed; unrelated A2/A5 and local probe changes remain untouched.
