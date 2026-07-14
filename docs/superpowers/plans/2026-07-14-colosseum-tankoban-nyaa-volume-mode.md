# Colosseum Tankoban Nyaa Volume Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a manga-only, per-series Tankoban Mode that presents every known volume, lets the user manually choose a Nyaa release or WeebCentral compilation, enriches strong per-volume synopses, and reads each downloaded volume as one continuous local book.

**Architecture:** A single `MangaTankobanService` QML facade composes a port of Tankoban 2's Nyaa runtime search, a pure volume matcher, a metadata-aware torrent downloader, a WeebCentral volume packer, a restart-safe local volume index, and a lazy Open Library/Apple synopsis enricher. `MangaSeries.qml` supplies the already-loaded canonical volume/chapter snapshot; `MangaReader.qml` receives a generic page store so chapter and Tankoban progress remain separate.

**Tech Stack:** Qt 6.11.1, C++17, QML, QNetworkAccessManager, QSettings/QSaveFile, embedded libtorrent through the existing `TorrentEngine`, PowerShell/QML offscreen harnesses, Ninja/MSVC 2022.

## Global Constraints

- Execute in an isolated worktree created with `superpowers:using-git-worktrees`; the live Colosseum tree contains unrelated A2/A5 changes.
- Read the approved spec first: `docs/superpowers/specs/2026-07-14-colosseum-tankoban-nyaa-volume-mode-design.md`.
- Read the Tankoban 2 references before each corresponding task; copy behavior, not Qt Widgets UI.
- Tankoban Mode is manga-only, defaults Off, and persists per stable series ID.
- Mode Off must preserve the existing `Downloads` chapter path byte-for-byte except for additive bindings needed to switch stores.
- Every known canonical volume remains visible regardless of source availability.
- Nyaa is always manually selected; WeebCentral is always the final fallback card.
- Do not add Nyaa to Colosseum's generic `TankorentSearchService`; port `NyaaRuntimeSource` into the manga feature because the generic result type lacks uploader data.
- Use the one shared `TorrentEngine`; never create a second libtorrent session.
- Open Library -> Apple Books -> honest empty is the complete v1 synopsis cascade.
- No publisher scrapers, bulk whole-series action, split-spread reconstruction, or inseparable combined-volume archives in v1.
- Network enrichment and Nyaa search begin only after a lazy `MangaSeries` page exists.
- Use TDD for pure logic. Record network fixtures; unit tests never depend on live Nyaa, Open Library, Apple, or WeebCentral.
- Kill any running `colosseum.exe` by PID before the final build. Run `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat` directly, never through `cmd /c`.
- Stage only the exact task files. Never stage the whole dirty tree.

## Reference map

Tankoban 2 behavioral sources:

- `C:\Users\Suprabha\Desktop\Tankoban 2\src\core\manga\NyaaRuntimeSource.{h,cpp}`
- `C:\Users\Suprabha\Desktop\Tankoban 2\resources\manga_uploader_trust.json`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\core\manga\TorrentVolumeProvider.{h,cpp}`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\core\manga\TorrentRequestLedger.{h,cpp}`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\core\manga\WeebCentralVolumePacker.{h,cpp}`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\core\manga\MangaDownloadIndex.{h,cpp}`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\ui\pages\comics\ComicsSourcesPanel.cpp:186-378`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\ui\pages\comics\ComicsSourceCard.cpp:135-415`
- `C:\Users\Suprabha\Desktop\Tankoban 2\src\ui\pages\comics\ComicsSeriesView.cpp:2539-2699`

Colosseum integration sources:

- `native/torrent/engine/TorrentEngine.{h,cpp}`
- `native/torrent/ComicTorrentDownloader.{h,cpp}`
- `native/torrent/ComicTorrentFilePicker.{h,cpp}`
- `native/engine/ComicDownloader.cpp:211-320,592-818`
- `native/engine/WeebCentralScraper.{h,cpp}`
- `native/engine/MangaDownloader.{h,cpp}`
- `native/main.cpp:332-435`
- `qml/MangaSeries.qml`
- `qml/MangaReader.qml:20-267`

---

### Task 1: Canonical volume model and per-series preference keys

**Files:**
- Create: `native/engine/MangaTankobanTypes.h`
- Create: `native/engine/MangaTankobanLogic.h`
- Create: `native/engine/MangaTankobanLogic.cpp`
- Create: `tests/manga_tankoban_logic_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: QML-style `QVariantMap` series/volume/chapter snapshots.
- Produces: `MangaTankoban::SeriesSnapshot`, `VolumeRecord`, `normalizeVolumeNumber`, `volumeId`, `settingsKey`, and `prepareSeries`.

- [ ] **Step 1: Write the failing canonical-identity tests**

Create assertions that prove string-safe identity and complete-volume assembly:

```cpp
require(normalizeVolumeNumber(QStringLiteral("10.5")) == QStringLiteral("10.5"),
        "fractional volume survives");
require(volumeId("mangafire:berserk", "10.5") ==
        "tankoban:mangafire%3Aberserk:volume:10.5", "stable escaped id");
require(settingsKey("mangafire:berserk") ==
        "manga/tankobanMode/mangafire%3Aberserk", "per-series settings key");

const QVariantList volumes{{QVariantMap{{"number", "1"}, {"cover", "a.jpg"}}},
                           {QVariantMap{{"number", "2"}, {"cover", "b.jpg"}}}};
const QVariantList chapters{{QVariantMap{{"id", "c1"}, {"volume", "1"}}},
                            {QVariantMap{{"id", "c2"}, {"volume", "1"}}}};
const auto snap = prepareSeries({{"seriesId", "s1"}, {"title", "Series"}}, volumes, chapters);
require(snap.volumes.size() == 2, "source-less volume remains canonical");
require(snap.volumes[0].chapterIds == QStringList{"c1", "c2"}, "chapter mapping retained");
```

- [ ] **Step 2: Add the harness target and verify RED**

Add `manga_tankoban_logic_harness` to `native/CMakeLists.txt`, linked to `Qt6::Core`. Run the absolute `native\build-msvc.bat`; expect compilation failure because the new headers/functions do not exist.

- [ ] **Step 3: Implement the minimal canonical model**

Define focused value types:

```cpp
namespace MangaTankoban {
struct VolumeRecord {
    QString id, seriesId, number, title, cover;
    QString chapterStart, chapterEnd;
    QStringList chapterIds;
};
struct SeriesSnapshot {
    QString seriesId, title, author;
    QStringList aliases;
    QList<VolumeRecord> volumes;
};
QString normalizeVolumeNumber(const QVariant& raw);
QString volumeId(const QString& seriesId, const QString& volumeNumber);
QString settingsKey(const QString& seriesId);
SeriesSnapshot prepareSeries(const QVariantMap& descriptor,
                             const QVariantList& volumeRows,
                             const QVariantList& chapterRows);
}
```

Normalize whitespace and leading zeroes while preserving non-integral suffixes. Match chapter rows by explicit volume first, then by the existing `chapterStart/chapterEnd` range. Never drop a volume because its mapped chapter list is empty.

- [ ] **Step 4: Verify GREEN**

Run `native\build-msvc.bat`, then `native\build-msvc\manga_tankoban_logic_harness.exe`. Expect exit 0 and `MANGA_TANKOBAN_LOGIC_OK`.

- [ ] **Step 5: Commit surgically**

```powershell
git add -- native/engine/MangaTankobanTypes.h native/engine/MangaTankobanLogic.h native/engine/MangaTankobanLogic.cpp tests/manga_tankoban_logic_harness.cpp native/CMakeLists.txt
git commit -m "feat(manga): add canonical Tankoban volume model"
```

### Task 2: Port Tankoban 2 Nyaa RSS search, trust, filtering, and ordering

**Files:**
- Create: `native/torrent/MangaNyaaSource.h`
- Create: `native/torrent/MangaNyaaSource.cpp`
- Create: `native/torrent/manga_uploader_trust.json`
- Create: `tests/fixtures/tankoban/nyaa_volume_results.xml`
- Extend: `tests/manga_tankoban_logic_harness.cpp`
- Modify: `native/app_resources.qrc`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `SeriesSnapshot`, target volume number, shared search `QNetworkAccessManager`.
- Produces: `MangaNyaaCandidate`, `queryVariants`, `parseRss`, `filterAndRank`, plus correlated `searchSucceeded(volumeId, rows)` / `searchFailed(volumeId, reason)`.

- [ ] **Step 1: Capture deterministic RSS and write failing tests**

The fixture must contain: exact Volume 2 digital, Volumes 1-12 pack, Chapter 2, Volume 3, a blocked uploader, a raw, and duplicate infohash entries. Assert:

```cpp
const auto parsed = MangaNyaaSource::parseRss(fixture("nyaa_volume_results.xml"));
const auto ranked = MangaNyaaSource::filterAndRank(
    {"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}}, "2", parsed, trust);
require(ranked.size() == 2, "only exact volume and inclusive pack survive");
require(ranked[0].coverageLo == "2" && ranked[0].coverageHi == "2",
        "standalone volume ranks before pack");
require(ranked[1].coverageLo == "1" && ranked[1].coverageHi == "12",
        "inclusive pack retained");
require(ranked[0].uploader == "1r0n", "uploader preserved");
require(queryVariants("Grand Blue Dreaming", "2").contains("Grand Blue Dreaming Vol 2"),
        "Tankoban 2 query family retained");
```

- [ ] **Step 2: Verify RED**

Add the new `.cpp` to the existing logic harness target and run `native\build-msvc.bat`. Expect unresolved `MangaNyaaSource` symbols.

- [ ] **Step 3: Port the proven runtime source**

Port parsing/query behavior from Tankoban 2, retaining Nyaa category `3_1`, seed-desc RSS ordering, batched query variants, infohash dedupe, and trust tiers. Extend the candidate only with derived coverage fields/chips needed by QML:

```cpp
struct MangaNyaaCandidate {
    QString title, uploader, magnetUri, infoHash;
    QString coverageLo, coverageHi;
    qint64 sizeBytes = 0;
    int seeders = 0, leechers = 0, tier = 99;
    bool standalone = false, digitalHint = false;
};
```

Reject chapter packs, wrong target volumes, blocked uploaders, raw markers, weak series matches, and missing hashes. Order by tier, standalone, digital hint, seeders, then case-folded title. Do not expose an auto-pick method.

- [ ] **Step 4: Embed trust data and verify GREEN**

Copy Tankoban 2's trust JSON as data, add it to `app_resources.qrc` under `:/tankoban/manga_uploader_trust.json`, and load once in the constructor. Run the build and logic harness; expect `MANGA_TANKOBAN_LOGIC_OK`.

- [ ] **Step 5: Commit surgically**

```powershell
git add -- native/torrent/MangaNyaaSource.h native/torrent/MangaNyaaSource.cpp native/torrent/manga_uploader_trust.json native/app_resources.qrc native/CMakeLists.txt tests/manga_tankoban_logic_harness.cpp tests/fixtures/tankoban/nyaa_volume_results.xml
git commit -m "feat(manga): port trusted Nyaa volume discovery"
```

### Task 3: Lazy Open Library and Apple Books synopsis enrichment

**Files:**
- Create: `native/engine/MangaSynopsisEnricher.h`
- Create: `native/engine/MangaSynopsisEnricher.cpp`
- Create: `tests/fixtures/tankoban/openlibrary_volume.json`
- Create: `tests/fixtures/tankoban/apple_books_volume.json`
- Create: `tests/fixtures/tankoban/apple_books_ambiguous.json`
- Extend: `tests/manga_tankoban_logic_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `SeriesSnapshot`, `VolumeRecord`, shared search NAM, cache path.
- Produces: `SynopsisRecord`, pure `matchOpenLibrary` / `matchApple`, `enrichSeries`, and `synopsisReady(volumeId, record)`.

- [ ] **Step 1: Write failing match-honesty tests**

```cpp
const auto ol = MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olFixture);
require(ol.accepted && ol.source == "openlibrary", "strong OL result accepted");
const auto apple = MangaSynopsisEnricher::matchApple(series, vol4, appleFixture);
require(apple.accepted && apple.sourceUrl.contains("books.apple"), "exact Apple volume accepted");
const auto ambiguous = MangaSynopsisEnricher::matchApple(series, vol4, ambiguousFixture);
require(!ambiguous.accepted, "equal strong candidates remain empty");
require(!MangaSynopsisEnricher::acceptDistinctVolumeText(seriesSynopsis, seriesSynopsis),
        "series synopsis is never repeated as volume synopsis");
```

- [ ] **Step 2: Verify RED**

Add the `.cpp` and `Qt6::Network` to the logic harness target; run the build and expect missing enricher types.

- [ ] **Step 3: Implement pure matching and cache schema**

Use this durable record:

```cpp
struct SynopsisRecord {
    QString volumeId, text, source, sourceUrl, confidence, fetchedAt;
    bool accepted = false;
};
```

Normalize punctuation, apostrophes, `Vol`/`Volume`, edition qualifiers, and creator names. Require target-volume evidence and a strong series token score; reject equal top scores. Store cache JSON with schema version 1 using `QSaveFile`, one record per `volumeId`.

- [ ] **Step 4: Implement sequential network cascade**

For each uncached volume, request Open Library first. If it yields no strong record, enqueue:

```text
https://itunes.apple.com/search?term=<series+Volume+N>&entity=ebook&limit=8&country=us
```

Use one Apple request at a time with at least 3200 ms between starts. A failed request records no negative forever; cache misses for 24 hours and accepted records for 30 days. Emit results incrementally. Never delay canonical volume rendering.

- [ ] **Step 5: Verify GREEN and persistence**

Extend the harness with a temporary cache round-trip. Build, run the harness, and require that accepted provenance survives reload while ambiguous results do not become accepted.

- [ ] **Step 6: Commit surgically**

```powershell
git add -- native/engine/MangaSynopsisEnricher.h native/engine/MangaSynopsisEnricher.cpp native/CMakeLists.txt tests/manga_tankoban_logic_harness.cpp tests/fixtures/tankoban/openlibrary_volume.json tests/fixtures/tankoban/apple_books_volume.json tests/fixtures/tankoban/apple_books_ambiguous.json
git commit -m "feat(manga): add honest volume synopsis enrichment"
```

### Task 4: Metadata-aware manga volume file picker

**Files:**
- Create: `native/torrent/MangaVolumeFilePicker.h`
- Create: `native/torrent/MangaVolumeFilePicker.cpp`
- Create: `tests/manga_volume_filepicker_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: requested volume string and `QJsonArray` metadata files from `TorrentEngine::metadataReady`.
- Produces: `MangaVolumePick pick(target, files)` and `QVector<int> unionPriorities(picks, fileCount)`.

- [ ] **Step 1: Write failing picker tests**

```cpp
require(pick("2", files({"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"})).index == 1,
        "target file selected from pack");
require(pick("2", files({"Series Volumes 1-3.cbz"})).index == -1,
        "inseparable combined archive rejected");
require(pick("2", files({"Series v02.cbz", "Series Volume 02.cbr"})).index == -1,
        "equal exact candidates require another source");
require(unionPriorities({0, 2}, 4) == QVector<int>({7, 0, 7, 0}),
        "shared torrent priorities are a union");
```

- [ ] **Step 2: Verify RED**

Add a `manga_volume_filepicker_harness` target linked to `Qt6::Core`; run the build and expect missing picker symbols.

- [ ] **Step 3: Implement exact file selection**

Accept `.cbz`, `.cbr`, `.cb7`, and `.cbt`. Parse coverage from the base filename and parent directories. Score exact target filename above directory-only evidence. Return an explicit reason enum:

```cpp
enum class PickFailure { None, NoArchive, TargetMissing, Ambiguous, CombinedArchive };
struct MangaVolumePick { int index = -1; QString path; qint64 size = 0; PickFailure failure; };
```

- [ ] **Step 4: Verify GREEN and commit**

Build, run `native\build-msvc\manga_volume_filepicker_harness.exe`, expect `MANGA_VOLUME_FILEPICKER_OK`, then commit only the four task files plus CMake.

### Task 5: Atomic local volume index and archive ingestion

**Files:**
- Create: `native/engine/MangaVolumeIndex.h`
- Create: `native/engine/MangaVolumeIndex.cpp`
- Create: `native/engine/MangaVolumeArchiveIngestor.h`
- Create: `native/engine/MangaVolumeArchiveIngestor.cpp`
- Create: `tests/manga_volume_index_harness.cpp`
- Create: `tests/fixtures/tankoban/tiny-volume.cbz`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: canonical `volumeId`, series metadata, source provenance, and local archive or prepared page directory.
- Produces: `localPages`, `statusOf`, `remove`, atomic `publish`, and `ingestArchive`.

- [ ] **Step 1: Write failing index/ingestion tests**

Test a temporary AppData root:

```cpp
ingestor.ingestArchive(record, fixture("tiny-volume.cbz"));
waitFor(ingestor, &MangaVolumeArchiveIngestor::finished);
require(index.statusOf(record.id).value("state") == "ready", "published only after extraction");
require(index.localPages(record.id).size() == 3, "three naturally ordered pages");
index.reload();
require(index.localPages(record.id).size() == 3, "atomic index survives restart");
QFile::remove(pagePath);
index.heal();
require(index.statusOf(record.id).value("state") == "none", "missing payload is pruned");
```

- [ ] **Step 2: Verify RED**

Add `manga_volume_index_harness` linked to `Qt6::Core` and the ingestor sources. Build and expect missing classes.

- [ ] **Step 3: Implement the durable index**

Store `<AppData>/manga-volumes/volume-index.json` via `QSaveFile`. Each entry records volume ID, series ID/title, volume number, final directory, naturally ordered filenames, byte count, source kind, release title, uploader, infohash, chapter IDs, and added time. `localPages` must return the existing reader shape:

```cpp
{{"index", 0}, {"url", QUrl::fromLocalFile(path)}, {"group", chapterGroup}}
```

- [ ] **Step 4: Implement lossless archive ingestion**

Lift the proven extraction lifecycle from `ComicDownloader.cpp:592-818` into the focused ingestor: extract to a staging directory, validate at least one decodable image, natural-sort image files, write the per-volume `index.json`, atomically rename staging to the final directory, publish the global index, then delete the staging archive. Never recompress images.

- [ ] **Step 5: Verify GREEN and commit**

Build, run `manga_volume_index_harness.exe`, expect `MANGA_VOLUME_INDEX_OK`, and commit only the task files and CMake.

### Task 6: Restart-safe torrent transport with shared-infohash priorities

**Files:**
- Create: `native/torrent/MangaVolumeTorrentDownloader.h`
- Create: `native/torrent/MangaVolumeTorrentDownloader.cpp`
- Create: `native/torrent/MangaVolumeRequestLedger.h`
- Create: `native/torrent/MangaVolumeRequestLedger.cpp`
- Create: `tests/manga_volume_torrent_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: selected `MangaNyaaCandidate`, target `VolumeRecord`, shared `TorrentEngine`, `MangaVolumeFilePicker`.
- Produces: volume-keyed resolving/progress/finished/failed signals and replayable request rows.

- [ ] **Step 1: Write failing ledger and union-job tests**

Use a fake engine seam that records `addMagnet`, `setFilePriorities`, and `startTorrent` calls. Prove paused add, target selection, two-volume union, cancellation, and ledger reload:

```cpp
downloader.download(v2, candidate);
require(engine.lastPaused, "metadata is inspected before payload download");
engine.emitMetadata(hash, packFiles);
require(engine.priorities == QVector<int>({0, 7, 0}), "only v2 starts");
downloader.download(v3, candidate);
require(engine.priorities == QVector<int>({0, 7, 7}), "same hash unions requested files");
ledger.reload();
require(ledger.active().size() == 2, "both intents survive restart");
```

- [ ] **Step 2: Verify RED**

Add the harness target linked to Qt Core/Network and the new sources. Build and expect missing downloader/ledger types.

- [ ] **Step 3: Implement transport and ledger**

Mirror `ComicTorrentDownloader` signal wiring but key public state by `volumeId` and internal jobs by infohash. Always call `addMagnet(..., paused=true)`. On metadata, resolve every requested volume, reject ambiguous/combined files before starting, union priorities, persist selected indices, then call `startTorrent`. On completion, emit each requested archive path independently for ingestion.

Persist rows with states `awaiting_metadata`, `downloading`, `validating`, `completed`, `failed`, and `cancelled` using `QSaveFile`. Replay active rows at service construction against the existing engine resume state.

- [ ] **Step 4: Verify GREEN and commit**

Build, run `manga_volume_torrent_harness.exe`, expect `MANGA_VOLUME_TORRENT_OK`, then commit only this task's files and CMake.

### Task 7: WeebCentral volume fallback packer

**Files:**
- Create: `native/engine/MangaVolumePacker.h`
- Create: `native/engine/MangaVolumePacker.cpp`
- Create: `tests/manga_volume_packer_harness.cpp`
- Create: `tests/fixtures/tankoban/weeb-pages.json`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `VolumeRecord.chapterIds`, `WeebCentralScraper`, download NAM, `MangaVolumeIndex` publication shape.
- Produces: `progress(volumeId, done, total)`, `finished(volumeId, preparedDirectory)`, `failed(volumeId, reason)`.

- [ ] **Step 1: Write failing completeness/order tests**

Inject recorded page lists and local image responses. Assert chapter order followed by natural page order, stable chapter `group`, and no publication on a missing chapter/page:

```cpp
packer.pack(v2);
require(savedNames == QStringList{"c010_001.jpg", "c010_002.jpg", "c011_001.jpg"},
        "chapter then natural page order");
require(groups == QList<int>{0, 0, 1}, "chapter boundaries retained");
require(!packer.complete(incompleteV2), "partial fallback never becomes ready");
```

- [ ] **Step 2: Verify RED**

Add the harness with `WeebCentralScraper.cpp`, `MangaScraper.cpp`, and Qt Network. Build and expect missing packer symbols.

- [ ] **Step 3: Implement direct fallback compilation**

Port the request lifecycle from Tankoban 2's `WeebCentralVolumePacker`, but publish Colosseum's extracted directory rather than creating a CBZ that would immediately be extracted. Fetch each chapter's pages, download images into a staging directory, validate content using MangaDownloader's real-image checks, write chapter group metadata, and atomically hand the prepared directory to `MangaVolumeIndex`. Cancellation aborts all replies and removes staging.

- [ ] **Step 4: Verify GREEN and commit**

Build, run `manga_volume_packer_harness.exe`, expect `MANGA_VOLUME_PACKER_OK`, then commit the task files and CMake.

### Task 8: Compose the single `TankobanVolumes` façade

**Files:**
- Create: `native/engine/MangaTankobanService.h`
- Create: `native/engine/MangaTankobanService.cpp`
- Create: `tests/manga_tankoban_service_harness.cpp`
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: shared search NAM, download NAM, shared `TorrentEngine`, dynamic `prepareSeries` snapshots, Tasks 1-7 components.
- Produces: the complete spec facade exposed as QML context property `TankobanVolumes`.

- [ ] **Step 1: Write the failing façade state-machine test**

Prove Off-default settings, snapshot preparation, Nyaa/Weeb source ordering, stable progress routing, and terminal ingestion:

```cpp
service.prepareSeries(descriptor, volumes, chapters);
require(!service.modeEnabled("s1"), "missing preference is Off");
service.setModeEnabled("s1", true);
require(service.modeEnabled("s1"), "preference persists per series");
service.searchSources(volumeId);
require(lastSources.last().value("kind") == "weebcentral", "fallback always last");
service.downloadNyaa(volumeId, hash);
transport.emitFinished(volumeId, archivePath);
require(service.statusOf(volumeId).value("state") == "ready", "one facade owns terminal state");
```

- [ ] **Step 2: Verify RED**

Add the service harness and main target sources. Build and expect missing service API.

- [ ] **Step 3: Implement the exact QML API**

Expose:

```cpp
Q_INVOKABLE void prepareSeries(QVariantMap descriptor, QVariantList volumes, QVariantList chapters);
Q_INVOKABLE QVariantList volumesForSeries(QString seriesId) const;
Q_INVOKABLE bool modeEnabled(QString seriesId) const;
Q_INVOKABLE void setModeEnabled(QString seriesId, bool enabled);
Q_INVOKABLE void searchSources(QString volumeId);
Q_INVOKABLE void downloadNyaa(QString volumeId, QString infoHash);
Q_INVOKABLE void compileWeebCentral(QString volumeId);
Q_INVOKABLE void cancel(QString volumeId);
Q_INVOKABLE void remove(QString volumeId);
Q_INVOKABLE QVariantMap statusOf(QString volumeId) const;
Q_INVOKABLE QVariantList localPages(QString volumeId) const;
```

Emit `volumesChanged`, `sourcesReady`, `progress`, `finished`, `failed`, `removed`, and `synopsisReady` with the same `volumeId`. Search results live in a per-volume cache so `downloadNyaa(volumeId, infoHash)` can recover the selected full candidate without accepting arbitrary QML magnet data.

Always append a WeebCentral source map after the ranked Nyaa maps. When `chapterIds` is empty or the canonical range is incomplete, keep the card visible with `enabled:false` and a concrete `reason` such as `Chapter mapping unavailable`; never hide the fallback or pretend it can build an incomplete book.

- [ ] **Step 4: Wire shared runtime dependencies**

In `native/main.cpp`, after `torrentEngine` and `searchNam` exist, construct one service with `searchNam`, `dlNam`, and `torrentEngine`; expose it as `TankobanVolumes`. Add `COLOSSEUM_TANKOBAN_DLTEST` parsing but do not run it unless the environment variable exists.

- [ ] **Step 5: Verify GREEN and commit**

Build, run `manga_tankoban_service_harness.exe`, expect `MANGA_TANKOBAN_SERVICE_OK`, then stage only the new service files, `main.cpp`, and precise CMake additions.

### Task 9: Build the Tankoban Mode volume library and manual source cards

**Files:**
- Modify: `qml/MangaSeries.qml`
- Create: `qml/MangaTankobanLibrary.qml`
- Create: `qml/MangaTankobanSourceCard.qml`
- Create: `tests/manga_tankoban_page_harness.qml`
- Create: `tests/test_manga_tankoban_mode.ps1`

**Interfaces:**
- Consumes: `TankobanVolumes`, existing `volumes`, `chaptersModel`, series identity/art.
- Produces: hero mode control, bound volume library, inline source chooser, and volume reader handoff.

- [ ] **Step 1: Write the failing QML contract test**

The PowerShell test must assert these exact contracts and then run the offscreen harness:

```powershell
Assert-Contains $series 'text: "TANKOBAN MODE"' "series-level mode label missing"
Assert-Contains $series 'TankobanVolumes.prepareSeries' "dynamic snapshot is not handed off"
Assert-Contains $series 'MangaTankobanLibrary {' "volume-first surface missing"
Assert-Contains $library 'model: root.volumeRows' "all canonical volumes must render"
Assert-Contains $library 'TankobanVolumes.searchSources' "volume click must open sources"
Assert-Contains $card 'modelData.uploader' "uploader evidence must remain visible"
Assert-Contains $card 'modelData.seeders' "seed evidence must remain visible"
Assert-Contains $card 'Build from chapters' "WeebCentral fallback copy missing"
```

The QML harness supplies a fake `TankobanVolumes`, toggles two series, and proves Off default, independent persistence, every volume visible, Nyaa order unchanged, fallback last, and progress attached to the selected volume.

- [ ] **Step 2: Run RED**

Run `powershell -ExecutionPolicy Bypass -File tests\test_manga_tankoban_mode.ps1`; expect a missing component/contract failure.

- [ ] **Step 3: Implement the hero control and lazy snapshot handoff**

Add a `Settings` JSON map keyed by stable series ID or bind directly to the native `modeEnabled/setModeEnabled` pair. Once chapters, art, and volumes are ready, call:

```qml
TankobanVolumes.prepareSeries({
    seriesId: page.seriesId, title: page.seriesTitle,
    author: page.author, aliases: []
}, page.volumes, page.chaptersModel)
```

Off keeps the existing shelf/chapter table visible. On hides only those lower sections and displays `MangaTankobanLibrary`.

- [ ] **Step 4: Implement the bound library visual system**

Use the existing black/gold/glass theme. Draw one thin gold binding rule from the enabled control through the left edge of the list. Each row shows cover, `Vol. N`, title, synopsis when present, state, progress, and one action. Expanding a row shows Nyaa cards followed by the quieter WeebCentral card. Apple synopsis attribution opens `synopsisSourceUrl` only for Apple records.

- [ ] **Step 5: Run GREEN and commit**

Run the PowerShell/QML test and require `MANGA_TANKOBAN_PAGE_OK`, then commit only the five task files.

### Task 10: Generalize `MangaReader` for volume entries without regressing chapters

**Files:**
- Modify: `qml/MangaReader.qml`
- Modify: `qml/MangaSeries.qml`
- Modify: `qml/Main.qml`
- Extend: `tests/manga_tankoban_page_harness.qml`
- Extend: `tests/test_manga_tankoban_mode.ps1`

**Interfaces:**
- Consumes: `pageStore`, `entryKind`, ordered chapter or volume entries.
- Produces: separate `manga`/`tankoban` progress namespaces and next-volume behavior.

- [ ] **Step 1: Add failing reader tests**

Assert the generic inputs and progress split:

```qml
property var pageStore: null
property string entryKind: "manga"
readonly property var store: pageStore ? pageStore : existingStore
readonly property string progressKind: entryKind
```

The harness must open a ready Volume 1, verify `localPages(volumeId)` is called, record progress under `tankoban`, then switch Off and verify chapter progress under `manga` remains unchanged.

- [ ] **Step 2: Run RED**

Run `tests\test_manga_tankoban_mode.ps1`; expect missing generic reader contract.

- [ ] **Step 3: Make the smallest reader generalization**

Keep the existing public chapter property names for compatibility, but add `pageStore`, `entryKind`, `entryLabelPrefix`, and `signal sourceRequested(string entryId)`. Replace hard-coded `Downloads/Comics` selection with the injected store when present. In Tankoban Mode pass canonical volumes transformed to `{id, number, name}` entries, `pageStore: TankobanVolumes`, and `entryKind: "tankoban"`.

The visible library remains ascending, but its reader model is a separate descending copy (`highest volume -> lowest volume`). This preserves MangaReader's existing newest-first crossing law: `curIndex - 1` is the next higher volume and `curIndex + 1` is the previous volume. At the end of a ready volume, open the next ready volume. If it is not ready, emit `sourceRequested(nextVolumeId)` without changing `curChapterId`; `MangaSeries` closes the reader and expands that volume's source chooser. `startDownload()` also emits `sourceRequested(curChapterId)` in Tankoban mode rather than calling chapter download APIs.

Teach `Main.qml` and the series-layer resume handoff that a saved `tankoban` progress record routes to the same manga series with Tankoban Mode enabled before assigning the saved volume ID. A chapter `manga` record continues to restore Mode Off behavior. Preserve all reading styles, RTL behavior, bookmarks, thumbnails, and wide-image settings.

- [ ] **Step 4: Run GREEN and existing reader regressions**

Run `test_manga_tankoban_mode.ps1` plus the existing reader-related P0 harnesses selected by `rg -l "MangaReader" tests/test_*`. Require all exit 0.

- [ ] **Step 5: Commit surgically**

Stage only `MangaReader.qml`, `MangaSeries.qml`, `Main.qml`, and the two Tankoban test files; commit `feat(manga): read Tankoban volumes through shared reader`.

### Task 11: Real download, fallback, restart, build, and self-review gates

**Files:**
- Modify: `native/engine/MangaTankobanService.cpp`
- Modify: `native/engine/MangaTankobanService.h`
- Modify: `native/main.cpp`
- Create: `tests/test_manga_tankoban_native.ps1`
- Update: `docs/superpowers/plans/2026-07-14-colosseum-tankoban-nyaa-volume-mode.md` checkboxes/evidence only during execution

**Interfaces:**
- Consumes: `COLOSSEUM_TANKOBAN_DLTEST` and a real Nyaa magnet chosen by the executor.
- Produces: process exit 0 only after archive download -> ingestion -> `localPages` succeeds.

- [ ] **Step 1: Implement the honest DLTEST contract**

Use:

```text
COLOSSEUM_TANKOBAN_DLTEST=<magnet-or-infohash>|<seriesId>|<seriesTitle>|<volumeNumber>
```

The service constructs one canonical snapshot, downloads the selected target, waits for `finished(volumeId)`, asserts `localPages(volumeId).size() > 0`, prints `[tankoban-dltest] DONE`, and exits 0. Any search, metadata, picker, engine, extraction, or page failure prints `[tankoban-dltest] FAIL <reason>` and exits 2. Retain the 240-second hard backstop.

- [ ] **Step 2: Run all deterministic tests**

Run the native build, all four new native harness executables, `test_manga_tankoban_mode.ps1`, and `test_manga_tankoban_native.ps1`. Record exact exit codes in the plan execution notes.

- [ ] **Step 3: Prove one real Nyaa acquisition**

Choose a legal test release manually from the source cards, set `COLOSSEUM_TANKOBAN_DLTEST`, launch `native\build-msvc\colosseum.exe qml\Main.qml` with `QML_DISABLE_DISK_CACHE=1`, and require `[tankoban-dltest] DONE` plus a non-empty canonical page directory.

- [ ] **Step 4: Prove one real WeebCentral fallback**

Use a known mapped short volume, select `Build from chapters`, and require progress -> validation -> ready -> reader pages. Record the volume ID and page count.

- [ ] **Step 5: Prove restart recovery**

Start a real volume torrent, wait until payload progress is non-zero, kill only that `colosseum.exe` PID, restart with the same AppData, and require the ledger to reconnect and finish without a second canonical record.

- [ ] **Step 6: Run the final MSVC build exactly as required**

Find and kill any running Colosseum PID, then run `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat` directly. Require `BUILD_OK`, exit 0. Do not claim visual completion; hand Hemanth the manga series page for eyes-on verification.

- [ ] **Step 7: Review against the written Definition of Done**

Invoke `brotherhood-review` on the full implementation diff against the spec's Definition of Done. Every item must be `MET`; any `PARTIAL` or `NOT-MET` is fixed before the final commit.

- [ ] **Step 8: Final surgical commit and push**

Confirm `git status --short` contains no staged A2/A5 paths. Commit the DLTEST/test additions and any review fixes, then push together under Hemanth's standing rule. Sign the handoff `[Agent 1 (Claude), comics]` and include changed files, deterministic test results, real Nyaa evidence, fallback evidence, restart evidence, build evidence, and the remaining Hemanth visual smoke.

## Plan self-review

### Spec coverage

- Manga-only Off/On mode and per-series persistence: Tasks 1, 8, 9.
- Complete known-volume catalog independent of sources: Tasks 1, 8, 9.
- Manual trusted Nyaa discovery with uploader evidence: Tasks 2, 8, 9.
- WeebCentral-last fallback: Tasks 7-9.
- Metadata inspection, exact-file priorities, shared infohash union: Tasks 4 and 6.
- Canonical extracted volume identity and lossless page publication: Task 5.
- One QML facade and stable volume-keyed terminal signals: Task 8.
- Whole-volume reading and separate progress: Task 10.
- Open Library/Apple lazy synopsis cascade with honest empty: Task 3 and Task 9.
- Atomic persistence and crash recovery: Tasks 5, 6, 8, 11.
- No root-startup network work: Tasks 8 and 9.
- Pure, fixture, headless, real Nyaa, real fallback, restart, and build verification: Tasks 1-11.

### Type consistency

- `volumeId` is a string everywhere from `VolumeRecord` through transport, index, service, QML, reader, and progress.
- `prepareSeries(QVariantMap, QVariantList, QVariantList)` is defined in Task 8 and called with the same shape in Task 9.
- Search results remain `MangaNyaaCandidate`; QML downloads by cached `infoHash`, never a reconstructed candidate.
- Both ingestion paths terminate in `MangaVolumeIndex::localPages(volumeId)`.
- Reader entry IDs are canonical volume IDs and `progressKind` is exactly `tankoban`.

### Placeholder scan

The plan contains no deferred implementation markers or unspecified error-handling steps. Every network path has deterministic fixtures, every pure boundary has a named harness, and every live claim has an exit-code-bearing proof.
