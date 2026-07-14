# Comics Alternate Torrent Sources v2 Design

**Date:** 2026-07-15
**Owner:** [Agent 1 (Codex), comics]
**Status:** Approved by Hemanth

## Goal

Give every GCD collected edition an honest secondary acquisition path: a small alternate-sources action beside the verified GetComics action opens a Theatre-style full-screen picker containing the matching results from Tankorent's universal Comics search. The user always chooses the torrent. Once chosen, the existing `Comics` object downloads, extracts, indexes, reports progress, and opens the edition under the same catalog `chId` used by the ledger.

## Why this is v2

The v1 catalog deliberately kept unavailable editions visible while exposing a download control only for verified GetComics matches. That remains the primary path. Public torrent indexes add useful but noisy coverage, so they belong behind an explicit secondary action and a manual source picker rather than an automatic fallback.

The dormant native path already proves the hard half of the flow:

- `TankorentSearchService` has a `comics` media-type allowlist.
- `ComicTorrents` can search the indexers or download a selected infohash.
- `ComicTorrentDownloader` resolves metadata and downloads one CBR/CBZ/CB7/CBT.
- `ComicDownloader::ingestLocalArchive()` reuses extraction, `index.json`, reader pages, progress, and `finished(issueId)`.

This design turns that private automatic path into a user-visible, manual, edition-aware picker without creating a second reader/download identity.

## Locked product decisions

1. GetComics remains the trusted one-click primary source.
2. Every undownloaded collected edition receives a smaller alternate-sources action, whether or not GetComics is available.
3. The action opens a full-screen page visually equivalent to Theatre's Torrentio source picker.
4. Search uses Tankorent's universal filter: `mediaType="comics"`, `sourceFilter="all"`, and no per-site category.
5. Automatic search fans out three identity variants, merges the answers, and deduplicates by canonical infohash:
   - canonical edition title;
   - ISBN, when present;
   - series plus collected-volume/issue-range identity.
6. A manual query field sits at the top of the picker. Manual search resets the visible result set and searches the same universal Comics filter.
7. All returned universal-filter results remain visible. Ranking and confidence explain risk; weak matches are not silently hidden.
8. Manual torrent selection is authoritative. A weak title match requires explicit confirmation.
9. A torrent with exactly one comic archive, or one unique exact canonical-title archive, auto-selects that file. Ambiguous and multi-volume packs open a second archive picker.
10. The alternate action is unavailable during an active acquisition and disappears once the edition is locally readable. Replacing a source requires deleting the local edition first.
11. All acquisition state continues to surface through the global `Comics` object under the original edition `chId`.

## User experience

### Ledger actions

The edition row keeps its existing large primary behavior:

- downloaded: open the reader;
- active: show progress/cancel state;
- verified GetComics source: direct-download action;
- no GetComics source: bibliographic row with no primary download action.

A separate 36–40 px circular action using the existing `search.svg` appears beside the primary action for every undownloaded, idle edition. Its tooltip/copy is **Find alternate sources**. It has its own `MouseArea`; clicking it never triggers the row's direct GetComics action.

### Full-screen source picker

`ComicTorrentSourcesPage.qml` is instantiated inside `ComicSeriesPage.qml`, matching the way `SourcesSheet.qml` belongs to `TheatreSeries.qml`. It is lazy in practice because `ComicSeriesPage` itself is lazy-loaded. It never touches root startup.

The page uses Colosseum's existing house tokens rather than inventing another visual system:

- opaque black base;
- edition/series art across the top at reduced opacity with a black wash;
- Fraunces display title, Switzer UI copy, gold eyebrow and actions;
- glass result table, thin house borders, gold scrollbar;
- 56 px gold select/download button on each result row.

The comics-specific signature is an **edition identity rail** below the title/search field. It shows the canonical title, ISBN, and collected range. Result rows light the clues they matched (`TITLE`, `ISBN`, `ISSUES`, `ARCHIVE`) so the user can understand why a result ranks where it does. This is bibliographic evidence, not decorative scoring.

The top-to-bottom structure is:

```text
Back     ALTERNATE SOURCES · COLLECTED EDITION
         Saga: Book One
         [ canonical title / ISBN / Saga #1–18 identity rail ]

         [ editable query                                    ] [Search]

         18 results · Pirate Bay / ExtraTorrents / Torrents-CSV
         ┌──────────────────────────────────────────────────────────────┐
         │ source │ release title                  confidence    ↓     │
         │        │ size · seeds · matched clues                      │
         ├──────────────────────────────────────────────────────────────┤
         │ ...                                                          │
         └──────────────────────────────────────────────────────────────┘
```

Empty and failure copy is directional:

- `Searching comic sources…`
- `No torrents matched this query. Try another title or ISBN.`
- `Some sources did not answer. Showing the results that arrived.`
- `This release does not closely match the collected edition.` followed by **Choose anyway** / **Go back**.

### Archive picker

After a torrent is chosen, the page enters a metadata-resolving state. It closes automatically when the downloader identifies one safe archive. If the manifest is ambiguous, `ComicTorrentArchivePicker.qml` replaces the results table with the eligible CBR/CBZ/CB7/CBT files, including path, extension, and size. The user chooses exactly one. Non-comic files never appear.

## Search and ranking model

### Query planning

`ComicTorrentQueryPlanner` is pure C++ and returns a normalized, deduplicated `QStringList`.

For an edition such as Saga: Book One:

1. `Saga: Book One`
2. `9781632150783`
3. `Saga #1-18`

The third query avoids duplicating the series when `collects` already begins with it. Missing ISBN or collection data simply removes that variant. A manual query produces one variant and does not append the automatic variants.

Each query starts `TankorentSearchService::startSearch("comics", "all", query, 80)`. The three searches may run concurrently. Partial results update the page as indexers answer; completion occurs only after every handle settles or is cancelled.

### Deduplication and evidence

Results are deduplicated by `canonicalizeInfoHash()`. When multiple sources report the same hash, the representative with the highest current seed count supplies volatile metadata while all matched-query evidence is retained.

`ComicTorrentRanker` ranks every canonical result. It does not discard match-tier zero rows in the manual picker.

Evidence order:

1. exact ISBN in the release title;
2. exact or prefix canonical-title match;
3. all significant canonical-title tokens;
4. matching collected number/range;
5. explicit CBR/CBZ/CB7/CBT hint;
6. seed count.

Confidence is user-facing and deterministic:

- **Strong:** ISBN match, exact canonical title, or canonical-title prefix plus matching collection identity.
- **Possible:** all significant title tokens match, or series plus collection identity matches.
- **Weak:** partial or no edition identity match. The row stays visible but requires confirmation.

Indexer brand is displayed but does not receive a hidden reliability bonus. The evidence rail and seed count are more honest than an undocumented provider preference.

## Native architecture

### Public `Comics` facade

`ComicDownloader` remains the only QML-visible object. It gains invokables that forward to its private `ComicTorrents` instance:

```cpp
Q_INVOKABLE void searchTorrentSources(
    const QString& issueId, const QString& seriesTitle,
    const QString& editionTitle, const QString& isbn,
    const QString& collects);
Q_INVOKABLE void searchTorrentSourcesQuery(
    const QString& issueId, const QString& query);
Q_INVOKABLE void cancelTorrentSourceSearch(const QString& issueId);
Q_INVOKABLE void downloadTorrentSource(
    const QString& issueId, const QString& seriesId,
    const QString& seriesTitle, const QString& issueLabel,
    const QString& infoHash, const QString& releaseTitle,
    const QString& magnetUri);
Q_INVOKABLE void chooseTorrentArchive(const QString& issueId, int fileIndex);
```

It forwards these QML-friendly signals:

```cpp
void torrentSourcesUpdated(const QString& issueId,
                           const QVariantList& rows, bool complete);
void torrentSourceSearchFailed(const QString& issueId,
                               const QString& reason);
void torrentArchiveSelectionRequired(const QString& issueId,
                                     const QVariantList& files);
void torrentArchiveSelected(const QString& issueId,
                            const QString& fileName, bool automatic);
```

Search errors use their dedicated signal and do not emit the terminal acquisition `failed(issueId, reason)` signal. Merely browsing sources must not create a Downloads-page job.

`issueLabel` is the canonical edition title and remains the archive file picker's matching title. `releaseTitle` is diagnostic/display metadata for the chosen torrent only; it must never replace the canonical picker title.

### `ComicTorrents`

`ComicTorrents` owns search sessions keyed by edition `issueId` and correlates every Tankorent handle back to that session. It accumulates all query/indexer slices, runs the pure ranking projection, and emits partial and final `QVariantList` rows.

Each row contains:

```text
infoHash, magnetUri, title, sizeBytes, sizeText,
seeders, leechers, sourceName, sourceKey,
confidence, matchTier, evidence[], archiveHint
```

Starting another search for the same `issueId` cancels and removes the prior handles. Closing the page also cancels them. Stale callbacks are ignored because their handles no longer belong to a live session.

The existing automatic `downloadIssueTorrent()` path may remain for its self-test lineage, but the ledger never invokes it. User-visible acquisition always calls `downloadTorrentSource()` with the selected infohash.

### Metadata and file selection

`ComicTorrentFilePicker` gains a decision API while retaining its existing `pick()` compatibility function:

```cpp
struct ComicArchiveCandidate {
    int index;
    QString name;
    QString extension;
    qint64 bytes;
    bool exactTitle;
    int tokenCoverage;
};

struct ComicArchiveDecision {
    PickedFile selected;
    QList<ComicArchiveCandidate> candidates;
    bool requiresChoice;
};
```

Auto-selection is intentionally narrow:

- one eligible comic archive; or
- one and only one eligible archive whose normalized stem exactly matches the canonical edition title.

Every other multi-archive manifest requires user choice. Format preference can order candidates but cannot silently decide an ambiguous pack.

When metadata is ambiguous, `ComicTorrentDownloader` pauses the torrent, sets all file priorities to zero, changes `statusOf(issueId).state` to `choosing`, and emits `fileSelectionRequired`. `chooseFile(issueId, index)` validates that the index is one of the eligible candidates, applies one-file priorities, resumes the torrent, and emits `fileSelected`.

### Reader and lifecycle contract

After the selected archive finishes, the existing chain remains unchanged:

```text
ComicTorrentDownloader::finished(issueId, archivePath)
  -> ComicTorrents::archiveReady(...)
  -> ComicDownloader::ingestLocalArchive(...)
  -> beginExtract / finalizeExtract
  -> index.json
  -> ComicDownloader::finished(issueId)
  -> Comics.localPages(issueId)
  -> reader
```

`Comics.statusOf(chId)` reports `resolving`, `choosing`, `downloading`, `extracting`, or `done`. `Comics.progress`, `Comics.failed`, and `Comics.finished` use the same `chId` already held by the ledger. No second QML downloader or alternate issue ID is permitted.

## State and safety rules

- Search is cancellable and does not create an active download record.
- Selection creates the acquisition job and disables both GetComics and alternate actions for that edition.
- A weak match requires a second user action before metadata resolution begins.
- A manifest with no comic archive fails honestly and deletes its temporary torrent files.
- Cancelling during resolving/choosing/downloading removes the torrent and temporary files through the existing `Comics.cancelDownload(chId)` route.
- A downloaded edition has no alternate-source action. The existing delete flow is the only way to choose a replacement.
- Partial indexer failure keeps successful results and reports which sources failed.
- No credentials, account, WebEngine cookie harvester, Nyaa addition, or 1337x addition is introduced. Colosseum's current universal Comics allowlist remains Pirate Bay, ExtraTorrents, and Torrents-CSV.

## Files

### New

- `native/torrent/ComicTorrentQueryPlanner.h`
- `native/torrent/ComicTorrentQueryPlanner.cpp`
- `qml/ComicTorrentSourcesPage.qml`
- `qml/ComicTorrentArchivePicker.qml`
- `tests/comic_torrent_query_planner_harness.cpp`
- `tests/comic_torrent_sources_page_harness.qml`
- `tests/test_comic_torrent_sources_v2.ps1`

### Modified

- `native/torrent/ComicTorrentRanker.h/.cpp`
- `native/torrent/ComicTorrentFilePicker.h/.cpp`
- `native/torrent/ComicTorrentDownloader.h/.cpp`
- `native/torrent/ComicTorrents.h/.cpp`
- `native/engine/ComicDownloader.h/.cpp`
- `native/CMakeLists.txt`
- `qml/ComicDbLedger.qml`
- `qml/ComicSeriesPage.qml`
- `tests/comic_torrent_ranker_harness.cpp`
- `tests/comic_torrent_filepicker_harness.cpp`
- `tests/test_comics_catalog_v1.ps1`

`qml/Main.qml`, catalog artifacts, Python catalog builders, manga/Tankoban Mode files, Biblio files, and Theatre files are out of scope.

## Verification

### Pure logic

- Query planner emits canonical title, ISBN, and collection-range variants without duplicates.
- Ranker retains weak results, deduplicates hashes, grades evidence deterministically, and keeps a relevant low-seed edition above an unrelated high-seed pack.
- File decision auto-selects only a lone archive or unique exact-title archive and requests choice for ambiguous packs.

### QML contract

- Every idle, undownloaded ledger edition exposes the alternate action.
- Direct GetComics remains conditional on `available && postUrl`.
- Alternate clicks do not trigger direct downloads.
- Search page loads headlessly, accepts partial/final rows, handles manual query, shows weak confirmation, and transitions to archive choice.
- The v1 catalog test is updated narrowly: it continues to forbid automatic `downloadIssueTorrent()` wiring while allowing the manual v2 facade.

### Build and live gate

- Kill any running `colosseum.exe` by PID.
- Run `native\build-msvc.bat` directly; require exit 0 and `BUILD_OK`.
- Run `tests/comic_torrent_seed_harness.cpp` to create and seed its deterministic legal two-page `Loopback_Comic.cbz`, feed the emitted magnet to `COLOSSEUM_TORRENT_DLTEST="$magnet|Loopback Comic|Loopback Comic"`, and require `[comic-torrent-dl] DONE`, extracted pages, and exit 0.
- Run the app with `QML_DISABLE_DISK_CACHE=1` and hand Hemanth the picker and ambiguous-pack flow for eyes-on verification.

## Definition of Done

- Every idle, undownloaded GCD edition has a separate **Find alternate sources** action; verified GetComics remains the only primary one-click source.
- The alternate action opens a lazy full-screen comics picker with edition identity, automatic results, an editable manual query, partial/final search state, and Theatre's established visual language.
- Automatic search uses `comics/all` for canonical title, ISBN, and collection range; results merge and deduplicate by canonical infohash.
- Every universal-filter result remains visible, with deterministic confidence/evidence; weak selections require confirmation.
- Selecting a torrent uses the canonical edition title for file matching, not the torrent release title.
- Lone or unique-exact comic archives auto-select; ambiguous packs pause with zero file priorities until the user selects one eligible archive.
- Search browsing does not create a Downloads job, is cancellable, ignores stale callbacks, and partial indexer failure preserves successful results.
- Torrent acquisition, progress, cancellation, extraction, completion, local pages, and reader opening all remain on `Comics` under the original ledger `chId`.
- Pure query/ranking/file-decision harnesses, async search harness, headless QML harness, and existing comics regressions exit 0.
- `native\build-msvc.bat` exits 0 with `BUILD_OK`, and the deterministic loopback DLTEST reports `[comic-torrent-dl] DONE` with a positive page count.
- The implementation changes only the files named in this spec and does not modify `Main.qml`, manga/Tankoban Mode, Biblio, Theatre, catalog artifacts, or Python builders.

## Non-goals

- Automatic torrent fallback when GetComics fails.
- Automatic selection of the “best” torrent for the user.
- Hiding weak universal-filter results.
- Adding new indexers or bypassing Cloudflare.
- Downloading an entire series or multiple archives into one edition.
- Altering catalog matching, rebuilding `comics_db.gen.js`, or deleting existing local downloads.
