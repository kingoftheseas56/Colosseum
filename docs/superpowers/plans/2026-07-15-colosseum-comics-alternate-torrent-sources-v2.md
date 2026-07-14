# Comics Alternate Torrent Sources v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a manual Theatre-style Tankorent source picker to every undownloaded GCD collected edition while preserving GetComics as the primary source and `Comics` as the only reader/download identity.

**Architecture:** Pure C++ query/ranking/file-decision helpers feed a private `ComicTorrents` search-session coordinator. `ComicDownloader` forwards QML-friendly search and archive-choice signals, while a new full-screen QML page lets the user search, inspect, confirm, and select a torrent before the existing downloader/extractor takes over under the edition's original `chId`.

**Tech Stack:** Qt 6.11 QML/JavaScript, C++17, `QNetworkAccessManager`, libtorrent `TorrentEngine`, CMake/Ninja/MSVC 2022, PowerShell and headless `qml.exe` harnesses.

## Global Constraints

- GetComics remains the trusted one-click primary source.
- Every idle, undownloaded collected edition exposes **Find alternate sources**.
- Search must call `startSearch("comics", "all", query, 80)` and retain Colosseum's current Pirate Bay / ExtraTorrents / Torrents-CSV allowlist.
- All returned canonical results remain visible; weak matches require confirmation rather than being hidden.
- User-visible torrent acquisition must never call the automatic `downloadIssueTorrent()` route.
- All acquisition progress, status, failure, completion, local pages, and reader opening must remain on the global `Comics` object under the original edition `chId`.
- Auto-select an archive only when it is the lone eligible comic file or the unique exact canonical-title file; ambiguous packs require manual choice.
- Do not modify `qml/Main.qml`, manga/Tankoban Mode files, Biblio files, Theatre files, catalog artifacts, or Python catalog builders.
- Preserve unrelated dirty files and stage surgically.

---

### Task 1: Plan edition identity queries with pure logic

**Files:**
- Create: `native/torrent/ComicTorrentQueryPlanner.h`
- Create: `native/torrent/ComicTorrentQueryPlanner.cpp`
- Create: `tests/comic_torrent_query_planner_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `ComicTorrentQueryPlanner::automaticQueries(seriesTitle, editionTitle, isbn, collects) -> QStringList`.
- Produces: `ComicTorrentQueryPlanner::manualQuery(query) -> QStringList`.
- Consumed by: `ComicTorrents::searchSources()` in Task 3.

- [ ] **Step 1: Write the failing query-planner harness**

Create a small `require()`-based executable that asserts:

```cpp
const QStringList saga = ComicTorrentQueryPlanner::automaticQueries(
    "Saga", "Saga: Book One", "9781632150783", "Saga #1-18");
require(saga == QStringList{"Saga: Book One", "9781632150783", "Saga #1-18"},
        "Saga identity cascade is canonical title, ISBN, then range");

const QStringList noDuplicate = ComicTorrentQueryPlanner::automaticQueries(
    "Batman", "Batman: I Am Gotham", "", "Batman #1-6");
require(noDuplicate == QStringList{"Batman: I Am Gotham", "Batman #1-6"},
        "missing ISBN is omitted and collection already owns the series name");

require(ComicTorrentQueryPlanner::manualQuery("  Saga Compendium One  ")
            == QStringList{"Saga Compendium One"},
        "manual query is trimmed and remains a single search");
require(ComicTorrentQueryPlanner::manualQuery("   ").isEmpty(),
        "blank manual query starts no search");
```

- [ ] **Step 2: Register and run the failing harness**

Add `comic_torrent_query_planner_harness` beside the existing comic torrent harnesses in `native/CMakeLists.txt`, linking `Qt6::Core`. Run:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build native\build-msvc --target comic_torrent_query_planner_harness
```

Expected: compilation fails because `ComicTorrentQueryPlanner` does not exist.

- [ ] **Step 3: Implement the minimal planner**

Declare:

```cpp
class ComicTorrentQueryPlanner
{
public:
    static QStringList automaticQueries(const QString& seriesTitle,
                                        const QString& editionTitle,
                                        const QString& isbn,
                                        const QString& collects);
    static QStringList manualQuery(const QString& query);
};
```

Implement ordered insertion with normalized case/whitespace deduplication. Use the supplied `collects` string directly when it already starts with the normalized series title; otherwise prefix the series title once.

- [ ] **Step 4: Run the harness to green**

```powershell
& '.\native\build-msvc\comic_torrent_query_planner_harness.exe'
```

Expected: `comic_torrent_query_planner_harness PASS`, exit 0.

- [ ] **Step 5: Commit the pure query slice**

```powershell
git add native/torrent/ComicTorrentQueryPlanner.h native/torrent/ComicTorrentQueryPlanner.cpp tests/comic_torrent_query_planner_harness.cpp native/CMakeLists.txt
git commit -m "[Agent 1 (Claude), comics] Plan comic torrent identity queries"
```

### Task 2: Rank every universal-filter result with visible evidence

**Files:**
- Modify: `native/torrent/ComicTorrentRanker.h`
- Modify: `native/torrent/ComicTorrentRanker.cpp`
- Modify: `tests/comic_torrent_ranker_harness.cpp`

**Interfaces:**
- Consumes: edition title, ISBN, collected range, all query-result slices.
- Produces: ranked `RankedComicTorrent` values with `confidence`, `evidence`, and canonical hash.
- Preserves: existing `rank(query, raw)` and `best(query, raw)` compatibility for the dormant automatic/self-test path.

- [ ] **Step 1: Extend the harness with failing manual-picker cases**

Add cases proving hash deduplication across queries, ISBN/title/range evidence, weak-result retention, and safe ordering:

```cpp
const QList<RankedComicTorrent> picker = ComicTorrentRanker::rankForEdition(
    "Saga", "Saga: Book One", "9781632150783", "Saga #1-18",
    {
        row("Annihilation Saga Issue 1 CBR", 900, hashA),
        row("Saga Book One 1-18 CBZ", 8, hashB),
        row("Saga 9781632150783 Digital", 2, hashC)
    });
require(picker.size() == 3, "manual picker retains weak universal results");
require(picker[0].src.infoHash == hashC && picker[0].confidence == "strong",
        "exact ISBN is strongest evidence");
require(picker[1].src.infoHash == hashB && picker[1].evidence.contains("ISSUES"),
        "canonical title/range beats unrelated seed count");
require(picker[2].src.infoHash == hashA && picker[2].confidence == "weak",
        "unrelated result remains visible but weak");
```

Add a duplicate-hash case where two source rows collapse to one and retain the higher seed count.

- [ ] **Step 2: Run the existing ranker harness and observe failure**

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build native\build-msvc --target comic_torrent_ranker_harness
```

Expected: compilation fails on `rankForEdition`, `confidence`, and `evidence`.

- [ ] **Step 3: Add the ranked evidence contract**

Extend the struct without changing its existing fields:

```cpp
struct RankedComicTorrent {
    TorrentResult src;
    int matchTier = 0;
    bool archiveHint = false;
    int identityScore = 0;
    QString confidence;       // strong | possible | weak
    QStringList evidence;     // TITLE, ISBN, ISSUES, ARCHIVE
};
```

Add:

```cpp
static QList<RankedComicTorrent> rankForEdition(
    const QString& seriesTitle, const QString& editionTitle,
    const QString& isbn, const QString& collects,
    const QList<TorrentResult>& raw);
static QVariantList toVariantRows(const QList<RankedComicTorrent>& ranked);
```

Normalize punctuation and number separators, match ISBN digits exactly, derive significant title tokens, detect the collection number/range, append evidence labels once, then sort by `identityScore`, archive hint, and seed count. `rankForEdition()` must return tier-zero rows; only compatibility `best()` retains the old fail-closed threshold.

- [ ] **Step 4: Run the ranker harness to green**

```powershell
& '.\native\build-msvc\comic_torrent_ranker_harness.exe'
```

Expected: `comic_torrent_ranker_harness PASS`, exit 0.

- [ ] **Step 5: Commit ranking and evidence**

```powershell
git add native/torrent/ComicTorrentRanker.h native/torrent/ComicTorrentRanker.cpp tests/comic_torrent_ranker_harness.cpp
git commit -m "[Agent 1 (Claude), comics] Rank alternate comic sources"
```

### Task 3: Expose cancellable multi-query search through `Comics`

**Files:**
- Modify: `native/torrent/ComicTorrents.h`
- Modify: `native/torrent/ComicTorrents.cpp`
- Modify: `native/engine/ComicDownloader.h`
- Modify: `native/engine/ComicDownloader.cpp`
- Modify: `native/CMakeLists.txt`
- Create: `tests/comic_torrents_search_harness.cpp`

**Interfaces:**
- Consumes: Task 1 queries and Task 2 ranked variant rows.
- Produces: `torrentSourcesUpdated(issueId, rows, complete)` and `torrentSourceSearchFailed(issueId, reason)` from the global `Comics` facade.
- Preserves: search-only activity does not appear in `activeIssueJobs()` and does not emit terminal acquisition failure.

- [ ] **Step 1: Write a failing asynchronous search harness**

Add a production-delegating constructor seam:

```cpp
ComicTorrents(TankorentSearchService* search,
              ComicTorrentDownloader* downloader,
              QObject* parent = nullptr);
```

In the harness, provide a `TestableSearchService` whose overridden `buildIndexersFor()` returns mock indexers. Assert that an automatic search starts three `comics/all` queries, partial updates are cumulative, duplicate hashes collapse, final completion fires once, and a replacement/manual search ignores cancelled handles.

The terminal assertions must include:

```cpp
require(lastRows.size() == 3, "merged search exposes all canonical results");
require(finalCount == 1, "all query handles settle into one final update");
require(!facadeFailureEmitted, "partial indexer failure is not an acquisition failure");
```

- [ ] **Step 2: Register and run the failing search harness**

Register the harness with the same Qt Core/Network and torrent source files used by `comic_downloader_ingest_harness`. Run its build target and confirm the new constructor/search methods are missing.

- [ ] **Step 3: Implement `ComicTorrents` search sessions**

Add:

```cpp
void searchSources(const QString& issueId, const QString& seriesTitle,
                   const QString& editionTitle, const QString& isbn,
                   const QString& collects);
void searchSourcesQuery(const QString& issueId, const QString& query);
void cancelSourceSearch(const QString& issueId);
```

Use one `SearchSession` per `issueId`, a `handle -> issueId` map, and a pending-handle set. Each query calls:

```cpp
m_search->startSearch(QStringLiteral("comics"), QStringLiteral("all"), query, 80);
```

After each result slice, merge raw rows and emit ranked partial rows with `complete=false`. After the final handle settles, emit `complete=true` and remove the session. Record indexer errors in the session and emit `torrentSourceSearchFailed` only when no result survives; otherwise final rows remain usable.

- [ ] **Step 4: Forward the facade invokables and signals**

Add the exact public `ComicDownloader` invokables and signals from the design spec. Connect `ComicTorrents` search signals in the constructor. Implement `downloadTorrentSource()` as a validated call to existing `downloadInfoHash()`; do not route it through `downloadIssueTorrent()`.

Before beginning the selected download, cancel the live source-search session for that `issueId`. Pass `issueLabel` as `downloadInfoHash()`'s `pickerTitle`; keep `releaseTitle` only for diagnostics so torrent naming can never replace canonical edition identity.

- [ ] **Step 5: Run the search and existing ingest harnesses**

```powershell
& '.\native\build-msvc\comic_torrents_search_harness.exe'
& '.\native\build-msvc\comic_downloader_ingest_harness.exe'
```

Expected: both print `PASS`/their existing success verdict and exit 0.

- [ ] **Step 6: Commit the search facade**

```powershell
git add native/torrent/ComicTorrents.h native/torrent/ComicTorrents.cpp native/engine/ComicDownloader.h native/engine/ComicDownloader.cpp tests/comic_torrents_search_harness.cpp native/CMakeLists.txt
git commit -m "[Agent 1 (Claude), comics] Expose manual comic source search"
```

### Task 4: Pause ambiguous torrents for archive selection

**Files:**
- Modify: `native/torrent/ComicTorrentFilePicker.h`
- Modify: `native/torrent/ComicTorrentFilePicker.cpp`
- Modify: `native/torrent/ComicTorrentDownloader.h`
- Modify: `native/torrent/ComicTorrentDownloader.cpp`
- Modify: `native/torrent/ComicTorrents.h`
- Modify: `native/torrent/ComicTorrents.cpp`
- Modify: `native/engine/ComicDownloader.h`
- Modify: `native/engine/ComicDownloader.cpp`
- Modify: `tests/comic_torrent_filepicker_harness.cpp`

**Interfaces:**
- Produces: `ComicTorrentFilePicker::decide(title, manifest) -> ComicArchiveDecision`.
- Produces: `fileSelectionRequired(issueId, files)` and `fileSelected(issueId, fileName, automatic)`.
- Consumes: `chooseFile(issueId, fileIndex)` from QML through `ComicDownloader::chooseTorrentArchive()`.

- [ ] **Step 1: Replace unsafe tie-breaking expectations with failing decision tests**

Keep extension acceptance tests, then add:

```cpp
const ComicArchiveDecision lone = ComicTorrentFilePicker::decide(
    "Saga Book One", {file(0, "Saga Book One.cbz")});
require(!lone.requiresChoice && lone.selected.idx == 0,
        "one comic archive auto-selects");

const ComicArchiveDecision exact = ComicTorrentFilePicker::decide(
    "Batman I Am Gotham", {
        file(0, "Batman I Am Suicide.cbr"),
        file(1, "Batman I Am Gotham.cbz")});
require(!exact.requiresChoice && exact.selected.idx == 1,
        "one unique exact title auto-selects inside a pack");

const ComicArchiveDecision ambiguous = ComicTorrentFilePicker::decide(
    "Saga Book One", {
        file(0, "Saga v01.cbz"), file(1, "Saga v02.cbz")});
require(ambiguous.requiresChoice && ambiguous.candidates.size() == 2,
        "multi-volume pack pauses for manual choice");
```

Remove the old expectation that CBZ format alone silently wins an otherwise ambiguous tie.

- [ ] **Step 2: Build the file-picker harness and verify failure**

Expected: compilation fails because `ComicArchiveDecision` and `decide()` do not exist.

- [ ] **Step 3: Implement the narrow auto-selection decision**

Add the `ComicArchiveCandidate` and `ComicArchiveDecision` structs from the spec. `decide()` filters to CBR/CBZ/CB7/CBT, computes exact normalized-stem equality and token coverage, and returns `requiresChoice=true` unless the lone/unique-exact rule succeeds. Keep `pick()` as a compatibility wrapper that returns `decide().selected` only when no choice is required.

- [ ] **Step 4: Add downloader choosing state**

Extend `Job` with the eligible manifest/candidate list and `choosing` flag. In `applyMetadata()`:

```cpp
const ComicArchiveDecision decision = ComicTorrentFilePicker::decide(job->title, manifest);
if (decision.candidates.isEmpty()) {
    m_engine->removeTorrent(job->infoHash, true);
    failJob(job, QStringLiteral("this torrent has no CBR/CBZ/CB7/CBT file (%1 file(s))")
                     .arg(manifest.size()));
    return;
}
if (decision.requiresChoice) {
    m_engine->pauseTorrent(job->infoHash);
    m_engine->setFilePriorities(job->infoHash, QVector<int>(manifest.size(), 0));
    job->choosing = true;
    emit fileSelectionRequired(job->issueId, toVariantFiles(decision.candidates));
    return;
}
applyPickedFile(job, decision.selected, manifest, true);
```

Implement `chooseFile(issueId, fileIndex)` to reject unknown/non-comic indices, apply one-file priorities, resume, set state to downloading, and emit `fileSelected(..., false)`. Report `choosing` from `statusOf()` and `activeJobs()`.

- [ ] **Step 5: Forward archive-choice signals through `ComicTorrents` and `ComicDownloader`**

Wire `ComicTorrentDownloader::fileSelectionRequired/fileSelected` through `ComicTorrents`. In `ComicDownloader`, map them to the QML-facing `torrentArchiveSelectionRequired/torrentArchiveSelected` signals from the spec. Implement `ComicDownloader::chooseTorrentArchive()` as the sole QML entry point.

- [ ] **Step 6: Run native regression harnesses**

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build native\build-msvc --target comic_torrent_filepicker_harness comic_torrent_ranker_harness comic_downloader_ingest_harness
& '.\native\build-msvc\comic_torrent_filepicker_harness.exe'
& '.\native\build-msvc\comic_torrent_ranker_harness.exe'
& '.\native\build-msvc\comic_downloader_ingest_harness.exe'
```

Expected: all exit 0.

- [ ] **Step 7: Commit archive selection**

```powershell
git add native/torrent/ComicTorrentFilePicker.h native/torrent/ComicTorrentFilePicker.cpp native/torrent/ComicTorrentDownloader.h native/torrent/ComicTorrentDownloader.cpp native/torrent/ComicTorrents.h native/torrent/ComicTorrents.cpp native/engine/ComicDownloader.h native/engine/ComicDownloader.cpp tests/comic_torrent_filepicker_harness.cpp
git commit -m "[Agent 1 (Claude), comics] Require choice for ambiguous comic packs"
```

### Task 5: Build the Theatre-style comics source and archive pages

**Files:**
- Create: `qml/ComicTorrentSourcesPage.qml`
- Create: `qml/ComicTorrentArchivePicker.qml`
- Create: `tests/comic_torrent_sources_page_harness.qml`
- Create: `tests/test_comic_torrent_sources_v2.ps1`

**Interfaces:**
- `ComicTorrentSourcesPage.show(context)` consumes `{issueId, seriesId, seriesTitle, editionTitle, isbn, collects, year, cover}`.
- `property var comicsApi` defaults to global `Comics` and can be replaced by a fake in the headless harness.
- Emits no reader signal; acquisition completion continues through the ledger's existing `Comics.finished/statusOf/localPages` polling.

- [ ] **Step 1: Write the failing headless QML harness**

Create a fake API object recording calls to `searchTorrentSources`, `searchTorrentSourcesQuery`, `downloadTorrentSource`, `chooseTorrentArchive`, and cancellation. Instantiate the page with that fake and assert:

```qml
page.show({ issueId: "gc:saga:book-one", seriesId: "gc:saga",
            seriesTitle: "Saga", editionTitle: "Saga: Book One",
            isbn: "9781632150783", collects: "Saga #1-18",
            year: "2014", cover: "" })
verify(fake.autoSearchCount === 1)
verify(page.identityLine.indexOf("9781632150783") >= 0)

page.applySources("gc:saga:book-one", [strongRow, weakRow], true)
verify(page.visibleRows.length === 2)
page.selectRow(weakRow)
verify(page.confirmingWeak)
page.confirmWeakSelection()
verify(fake.downloadCount === 1)

page.applyArchiveChoices("gc:saga:book-one", [fileOne, fileTwo])
verify(page.archiveChoosing)
page.chooseArchive(1)
verify(fake.chosenFileIndex === 1)
```

- [ ] **Step 2: Run the harness and verify the component is missing**

```powershell
$env:QT_FORCE_STDERR_LOGGING='1'
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen tests\comic_torrent_sources_page_harness.qml
```

Expected: nonzero exit because `ComicTorrentSourcesPage.qml` does not exist.

- [ ] **Step 3: Implement the source picker visual/state contract**

Mirror `SourcesSheet.qml`'s black hero, typography, glass table, spacing, row hover, and gold action button without copying Theatre's AddonClient logic. Implement these explicit properties/functions:

```qml
property var comicsApi: typeof Comics !== "undefined" ? Comics : null
property Item backdrop: null
property var context: ({})
property var rows: []
property var errors: []
property var archiveFiles: []
property string queryText: ""
property bool open: false
property bool loading: false
property bool complete: false
property bool confirmingWeak: false
property bool archiveChoosing: false
property var pendingRow: null
readonly property var visibleRows: rows
readonly property string identityLine: buildIdentityLine()

function show(contextObject) {
    context = contextObject; rows = []; errors = []; loading = true; complete = false
    open = true; comicsApi.searchTorrentSources(context.issueId, context.seriesTitle,
        context.editionTitle, context.isbn, context.collects)
}
function hide() {
    if (comicsApi && context.issueId) comicsApi.cancelTorrentSourceSearch(context.issueId)
    open = false; rows = []; pendingRow = null; confirmingWeak = false
}
function submitManualQuery() {
    rows = []; errors = []; loading = true; complete = false
    comicsApi.searchTorrentSourcesQuery(context.issueId, queryText)
}
function applySources(issueId, newRows, isComplete) {
    if (issueId !== context.issueId) return
    rows = newRows; complete = isComplete; loading = !isComplete
}
function selectRow(row) {
    pendingRow = row
    if (row.confidence === "weak") { confirmingWeak = true; return }
    confirmWeakSelection()
}
function confirmWeakSelection() {
    confirmingWeak = false
    comicsApi.cancelTorrentSourceSearch(context.issueId)
    comicsApi.downloadTorrentSource(context.issueId, context.seriesId,
        context.seriesTitle, context.editionTitle, pendingRow.infoHash,
        pendingRow.title, pendingRow.magnetUri)
}
function applyArchiveChoices(issueId, files) {
    if (issueId !== context.issueId) return
    archiveFiles = files; archiveChoosing = true
}
```

Use confidence colors only as restrained evidence: gold for strong, muted blue-grey for possible, muted red for weak. Keep every row visible.

- [ ] **Step 4: Implement the archive picker**

`ComicTorrentArchivePicker.qml` receives `files`, displays only backend-validated comic candidates, and emits `archiveChosen(int fileIndex)`. It uses the same hero/table shell and clearly labels multi-volume paths so the user can avoid split or wrong-volume releases.

- [ ] **Step 5: Connect facade signals with stale-ID guards**

Use `Connections { target: page.comicsApi }` and handlers for the four signals. A result or archive callback whose `issueId` differs from `context.issueId` must be ignored. On `torrentArchiveSelected`, hide the page; progress then belongs to the ledger.

- [ ] **Step 6: Run the headless harness to green**

Expected: `COMIC_TORRENT_SOURCES_PAGE_OK`, exit 0.

- [ ] **Step 7: Add the PowerShell contract test**

Assert that the new page contains the universal-search facade verbs, manual query, confidence warning, and archive picker, while containing no `AddonClient`, `Torrentio`, or direct `TorrentEngine` reference.

- [ ] **Step 8: Commit the two pages and tests**

```powershell
git add qml/ComicTorrentSourcesPage.qml qml/ComicTorrentArchivePicker.qml tests/comic_torrent_sources_page_harness.qml tests/test_comic_torrent_sources_v2.ps1
git commit -m "[Agent 1 (Claude), comics] Add alternate comic source picker"
```

### Task 6: Wire every collected edition without weakening GetComics honesty

**Files:**
- Modify: `qml/ComicDbLedger.qml`
- Modify: `qml/ComicSeriesPage.qml`
- Modify: `tests/test_comics_catalog_v1.ps1`
- Modify: `tests/test_comic_torrent_sources_v2.ps1`

**Interfaces:**
- Produces: `ComicDbLedger.alternateSourcesRequested(var edition, string chId)`.
- Consumes: `ComicTorrentSourcesPage.show(context)`.
- Preserves: primary GetComics gate `available && postUrl.length > 0`.

- [ ] **Step 1: Make the contract tests fail on the unwired ledger**

Update the v1 assertion from “no torrent fallback exists” to the narrower invariant:

```powershell
Assert-NotContains $ledger 'downloadIssueTorrent' `
    "The ledger must never auto-pick a torrent source."
Assert-Contains $ledger 'hasSource: !!ed.modelData.available && postUrl.length > 0' `
    "Verified GetComics remains the only primary action."
```

In the v2 test require `alternateSourcesRequested`, `Find alternate sources`, and the new source page instance. Run both scripts and confirm v2 fails.

- [ ] **Step 2: Split primary and alternate action state in the ledger**

Add `alternateSourcesRequested(var edition, string chId)`. Derive:

```qml
readonly property bool downloaded: dlState === "done"
readonly property bool inFlight: dlState === "resolving"
                              || dlState === "choosing"
                              || dlState === "downloading"
                              || dlState === "extracting"
readonly property bool canDirect: chId.length > 0 && hasSource && !inFlight && !downloaded
readonly property bool canAlternate: chId.length > 0 && !inFlight && !downloaded
```

Keep the row's existing read/direct action. Add a separate small circular `search.svg` button beside it with tooltip/accessibility text `Find alternate sources`; its `MouseArea` emits only `alternateSourcesRequested(ed.modelData, chId)`.

- [ ] **Step 3: Instantiate the page inside `ComicSeriesPage.qml`**

Add `ComicTorrentSourcesPage { id: torrentSources; anchors.fill: parent; z: 70; backdrop: page.backdrop }` beside the reader overlay, not in `Main.qml`. Handle the ledger signal with:

```qml
onAlternateSourcesRequested: (edition, chId) => torrentSources.show({
    issueId: chId,
    seriesId: ledger.gcTag,
    seriesTitle: page.seriesTitle,
    editionTitle: String(edition.display_title || edition.title || ""),
    isbn: String(edition.isbn || ""),
    collects: String(edition.collects || ""),
    year: String(edition.published || ""),
    cover: String(edition.cover || page.cover || "")
})
```

Use the actual `id` assigned to the ledger (`ledger`) so `seriesId` remains exactly the existing `gc:` identity and is not double-prefixed.

- [ ] **Step 4: Run all QML/PowerShell comics contracts**

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_comics_catalog_v1.ps1
powershell -ExecutionPolicy Bypass -File tests\test_comic_torrent_sources_v2.ps1
powershell -ExecutionPolicy Bypass -File tests\test_comics_sources_p0.ps1
```

Expected: all print their success verdict and exit 0.

- [ ] **Step 5: Commit ledger integration**

```powershell
git add qml/ComicDbLedger.qml qml/ComicSeriesPage.qml tests/test_comics_catalog_v1.ps1 tests/test_comic_torrent_sources_v2.ps1
git commit -m "[Agent 1 (Claude), comics] Wire alternate sources to collected editions"
```

### Task 7: Build, prove a real archive, and hand off for eyes-on

**Files:**
- Verify only; do not stage unrelated files.

**Interfaces:**
- Acceptance: search -> manual torrent selection -> metadata -> auto/manual archive choice -> download -> extract -> `Comics.localPages(chId)`.

- [ ] **Step 1: Run every focused native harness**

```powershell
& '.\native\build-msvc\comic_torrent_query_planner_harness.exe'
& '.\native\build-msvc\comic_torrent_ranker_harness.exe'
& '.\native\build-msvc\comic_torrent_filepicker_harness.exe'
& '.\native\build-msvc\comic_torrents_search_harness.exe'
& '.\native\build-msvc\comic_downloader_ingest_harness.exe'
```

Expected: every executable exits 0.

- [ ] **Step 2: Run the QML and catalog contracts**

Run the three PowerShell tests from Task 6 plus the direct offscreen page harness. Require exit 0 from each.

- [ ] **Step 3: Kill a running app by PID and build**

```powershell
$procs = Get-Process colosseum -ErrorAction SilentlyContinue
foreach ($proc in $procs) { Stop-Process -Id $proc.Id -Force }
& 'C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat'
if ($LASTEXITCODE -ne 0) { throw "Colosseum build failed: $LASTEXITCODE" }
```

Expected: output ends with `BUILD_OK`.

- [ ] **Step 4: Prove a real seeded comic archive end to end**

Use the repo's deterministic legal loopback CBZ seeder. It emits a real magnet with an explicit `127.0.0.1` peer and remains alive long enough for the app to fetch metadata and bytes:

```powershell
$seedRoot = Join-Path $env:TEMP ("colosseum-comic-dltest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $seedRoot | Out-Null
$seedOut = Join-Path $seedRoot 'seed.stdout.txt'
$seedErr = Join-Path $seedRoot 'seed.stderr.txt'
$seed = Start-Process -FilePath '.\native\build-msvc\comic_torrent_seed_harness.exe' `
    -ArgumentList @($seedRoot) -RedirectStandardOutput $seedOut `
    -RedirectStandardError $seedErr -WindowStyle Hidden -PassThru
$ready = $null
for ($i = 0; $i -lt 100 -and !$ready; $i++) {
    Start-Sleep -Milliseconds 100
    if (Test-Path $seedOut) { $ready = Get-Content $seedOut | Where-Object { $_ -like 'READY magnet:*' } | Select-Object -First 1 }
}
if (!$ready) { Stop-Process -Id $seed.Id -Force; throw "Loopback seed did not become ready: $(Get-Content $seedErr -Raw)" }
$magnet = $ready.Substring(6)
$env:COLOSSEUM_TORRENT_DLTEST="$magnet|Loopback Comic|Loopback Comic"
$env:QML_DISABLE_DISK_CACHE='1'
& '.\native\build-msvc\colosseum.exe' '.\qml\Main.qml'
$code = $LASTEXITCODE
Remove-Item Env:\COLOSSEUM_TORRENT_DLTEST
Stop-Process -Id $seed.Id -Force -ErrorAction SilentlyContinue
if ($code -ne 0) { throw "Torrent DLTEST failed: $code" }
```

Expected: `[comic-torrent-dl] DONE`, a positive page count, and exit 0. The loopback run is the deterministic engine/extraction gate; Hemanth's eyes-on picker smoke still exercises live public search separately.

- [ ] **Step 5: Inspect the surgical diff**

```powershell
git status --short
git diff -- native/torrent native/engine/ComicDownloader.h native/engine/ComicDownloader.cpp native/CMakeLists.txt qml/ComicDbLedger.qml qml/ComicSeriesPage.qml qml/ComicTorrentSourcesPage.qml qml/ComicTorrentArchivePicker.qml tests
```

Confirm `Main.qml`, manga/Tankoban Mode, Biblio, Theatre, catalog artifacts, and unrelated dirty files are absent.

- [ ] **Step 6: Launch for Hemanth's visual verification**

```powershell
$env:QML_DISABLE_DISK_CACHE='1'
Start-Process -FilePath '.\native\build-msvc\colosseum.exe' -ArgumentList '.\qml\Main.qml' -WindowStyle Hidden
```

Ask Hemanth to inspect one GetComics-backed edition, one no-GetComics edition, one weak-match confirmation, and one ambiguous archive pack.

- [ ] **Step 7: Push only after Hemanth's eyes-on approval**

If later fixes were required, rerun Steps 1–5. Then surgically stage the approved files, commit with `[Agent 1 (Claude), comics]` attribution, and push the current branch. Never use `git add -A` in the dirty shared tree.
