# Colosseum Tankoban Nyaa Volume Mode Design

**Date:** 2026-07-14

**Owner:** [Agent 1 (Codex), comics]

**Execution substrate:** Claude, after an implementation plan is approved

**Reference implementation:** `C:\Users\Suprabha\Desktop\Tankoban 2`

## Purpose

Add a manga-only **Tankoban Mode** to every Colosseum manga series. The mode is a volume-first acquisition and reading system, not a chapter/volume display filter. It presents the complete known volume run, lets the user choose a high-quality Nyaa release manually, retains WeebCentral volume compilation as a dependable fallback, and reads the result as one continuous local book.

Tankoban Mode defaults Off and is remembered independently for each stable manga series ID. Mode Off preserves the current WeebCentral chapter experience unchanged.

## Locked product decisions

1. Tankoban Mode appears only on manga series. Western comics are out of scope.
2. The control is named `Tankoban Mode: Off/On`; it is not labeled `Chapters/Volumes`.
3. It defaults Off and persists per series.
4. Mode On shows every known canonical volume, never only volumes found on Nyaa.
5. Every new Nyaa download requires manual source selection. Seed count never selects a release automatically because uploaders differ in scan and double-page-spread treatment.
6. WeebCentral compilation is always the final source card, even when Nyaa results exist.
7. The Tankoban 2 default manga experience is the behavioral reference. Colosseum receives a native adapter, not a wholesale Qt Widgets transplant.
8. Per-volume synopsis v1 uses Open Library first, Apple Books second, then an honest empty state. Publisher-specific scrapers are deferred.
9. Combined archives are rejected when the requested volume cannot be isolated safely.
10. Bulk whole-series download is deferred because v1 requires a deliberate source choice per volume.

## Experience and visual hierarchy

The mode control sits in the manga-series hero action area so it reads as a series-level identity. Off is visually quiet. On changes the lower page from the current chapter-oriented surface into a collected-edition library.

```text
TANKOBAN MODE                                      [ OFF  O ]
Chapters from the current online source

                  -- switch On --

YOUR TANKOBAN LIBRARY                              12 VOLUMES
| Vol. 01  Downloaded                         Open          |
| Vol. 02  Available                          Choose source |
| Vol. 03  Downloading 64%                    Cancel        |
| Vol. 04  Available                          Choose source |
```

The signature visual is a thin gold bookbinding rule that runs from the enabled mode control down the left edge of the volume library. It binds the run without inventing a new theme. Existing Colosseum black, gold, glass, display, UI, and utility tokens remain authoritative.

Selecting an undownloaded volume expands its source chooser directly beneath that row. Each Nyaa card displays the unmodified release title, uploader, trust tier, size, seeders, and parsed coverage. Derived chips such as `DIGITAL`, `SINGLE VOLUME`, and `VOLS 1-12` explain the result but never claim that spreads are intact. The quieter final card is always `WeebCentral - Build from chapters`.

Downloaded volumes open immediately. They do not reopen source selection.

## Canonical data model

The existing MangaFire/MangaVolumes metadata decides which volumes exist. Nyaa, Open Library, and Apple only enrich or acquire canonical records.

Stable identities:

```text
seriesId = existing MangaSeries.seriesId
volumeId = tankoban:<seriesId>:volume:<normalizedVolumeNumber>
```

Volume numbers are normalized as strings so fractional and special volumes are never collapsed by floating-point conversion.

```qml
{
    id, seriesId, number, title, cover,
    chapterStart, chapterEnd, chapterIds,

    synopsis,
    synopsisSource,       // openlibrary | apple | ""
    synopsisSourceUrl,
    synopsisConfidence,   // exact-isbn | exact-title-volume | none

    state,                // none | searching | choosing | inspecting |
                          // queued | downloading | packing | validating |
                          // ready | failed | cancelled
    progress,
    localPagesPath,
    selectedSource
}
```

Mode preference uses `QSettings` under `manga/tankobanMode/<escaped-series-id>`. A missing key is Off. Switching modes does not cancel transfers or remove downloads.

Canonical volume assembly merges the existing cover/number/range information with mapped WeebCentral chapter IDs. Every known volume is emitted. An incomplete chapter map disables only the fallback card and explains the missing mapping; it does not remove the volume or its Nyaa path.

## Per-volume synopsis enrichment

Synopsis enrichment is lazy, cached, restart-safe, and never blocks series rendering, Nyaa search, or reading.

```text
render canonical volumes immediately
  -> merge cached synopsis records
  -> enqueue missing Open Library lookups
  -> enqueue strong Apple Books fallback searches
  -> update individual rows as results arrive
```

Open Library matching prefers an exact known English ISBN. When no ISBN is present, it may use a normalized title plus explicit volume number, rejecting edition qualifiers that do not match the canonical record.

Apple Books searches `normalized series title + Volume N`. Acceptance requires an explicit target volume and strong normalized-series agreement. Author agreement boosts confidence and breaks ambiguity. Equal strong candidates are rejected rather than guessed.

The enricher must never substitute the series synopsis. Exact repeated text across several supposedly different volumes is flagged and withheld. Cache records store source, source URL, match basis, confidence, and retrieval date. Apple-sourced text exposes a restrained `Apple Books` attribution link when expanded. Requests remain below Apple's approximate 20 calls per minute and are cached as Apple recommends.

V1 ends at honest empty when neither source produces a strong match. VIZ, Kodansha, Yen Press, and Seven Seas adapters are a measured follow-up after miss coverage is known.

## Nyaa discovery and ranking

Colosseum retains one shared `TorrentEngine`, but it does **not** currently have a Nyaa indexer: `TankorentSearchService` explicitly dropped Nyaa during its keyless book-only port. The parity adapter therefore ports Tankoban 2's proven `NyaaRuntimeSource` RSS/search behavior into `MangaNyaaSearchAdapter`, using the shared search `QNetworkAccessManager`. This is necessary both for Nyaa category `3_1` and for uploader metadata, which Colosseum's generic `TorrentResult` does not carry.

The search adapter is manga-specific and does not add Nyaa back to the general `TankorentSearchService` allowlist in v1. Torrent payloads still use Colosseum's one shared native `TorrentEngine`.

Query family:

```text
<series> <volume>
<series> <zero-padded-volume>
<series> Vol <volume>
<series> Volume <volume>
<series>                         # broad final pass
```

English and romaji aliases may be queried. Results merge by infohash.

A candidate survives only when it strongly matches a known title or alias, advertises the exact target or an inclusive range, is not from a blocked uploader, is plausibly archive-backed, and is not clearly a raw Japanese release for an English catalog record. `Vol. 2`, `Volume 02`, `v002`, and ranges normalize into one coverage model. Chapter packs cannot masquerade as volumes.

The manual list is ordered by:

1. trusted uploader tier;
2. exact standalone volume before multi-volume pack;
3. explicit digital or official-volume indicators;
4. seed count;
5. stable title ordering.

This order is advisory. No candidate is auto-selected.

## Source selection and acquisition

### Nyaa path

```text
choose release
  -> add magnet paused
  -> wait for metadata
  -> inspect archive files
  -> resolve exact requested-volume candidate
  -> set file priorities to only that candidate
  -> start transfer
  -> validate archive
  -> losslessly extract and index pages
  -> atomically publish canonical volume
  -> ready(volumeId)
```

When metadata is ambiguous, missing the target, or contains one inseparable combined archive, payload download does not begin. The UI explains the reason and returns to source selection.

Requests for multiple volumes from one infohash share one torrent job. Its file priorities become the union of the requested volume files.

### WeebCentral fallback

```text
choose Build from chapters
  -> resolve canonical chapter IDs
  -> download only missing chapters
  -> order groups by chapter, pages by natural page order
  -> validate expected chapter completeness
  -> publish the same canonical extracted-volume shape
  -> ready(volumeId)
```

A partial compilation is never marked ready.

## State machine

```text
none -> searching -> choosing
choosing -> inspecting -> queued -> downloading -> validating -> ready
choosing -> packing -> validating -> ready

active -> failed
failed -> searching
active payload transfer -> cancelled -> choosing
ready -> open
ready -> removed -> none
```

All progress, failure, completion, cancellation, deletion, and page lookup signals use the same canonical `volumeId`.

## Colosseum-native component boundary

One native facade is exposed to QML:

```text
MangaTankobanService  -> context property: TankobanVolumes
```

It composes:

```text
MangaVolumeCatalog
MangaSynopsisEnricher
MangaNyaaSearchAdapter
MangaVolumeTorrentJob
MangaVolumePacker
MangaVolumeIndex
```

QML-facing operations:

```text
prepareSeries(seriesDescriptor, volumeRows, chapterRows)
volumesForSeries(seriesId)
sourcesForVolume(seriesId, volumeNumber)
downloadNyaa(volumeId, infoHash)
compileWeebCentral(volumeId)
cancel(volumeId)
remove(volumeId)
statusOf(volumeId)
localPages(volumeId)
```

`MangaSeries.qml` calls `prepareSeries(...)` only after its existing art, volume, and chapter results have resolved. The service validates and normalizes this dynamic snapshot, derives canonical volume IDs, maps chapter IDs, merges cached acquisition/synopsis state, and emits `volumesChanged(seriesId)`. The QML page remains the handoff point for already-loaded catalog data; the native facade does not refetch MangaFire merely to reconstruct information already in memory.

Signals:

```text
volumesChanged(seriesId)
sourcesReady(volumeId, results)
progress(volumeId, done, total)
finished(volumeId)
failed(volumeId, reason)
removed(volumeId)
synopsisReady(volumeId)
```

QML never coordinates the torrent engine and chapter downloader itself.

## Reader integration

`MangaReader` receives a generic page-store input.

```text
Mode Off: pageStore=Downloads, entries=chapters, progress kind=manga
Mode On:  pageStore=TankobanVolumes, entries=volumes, progress kind=tankoban
```

All existing reading behavior remains: RTL, continuous and paged modes, reader-side wide-image handling, bookmarks, thumbnails, and page progress. Chapter and Tankoban progress remain separate.

At the end of a volume, the next downloaded volume may open. A known but undownloaded next volume returns the user to `Choose source for Volume N`. There is no claim that the app can reconstruct double-page spreads already split by an uploader.

## Durable local representation

Colosseum v1 publishes extracted pages because the QML reader consumes local URLs:

```text
<AppData>/manga-volumes/<series-key>/<volume-key>/
    index.json
    page_0001.<ext>
    page_0002.<ext>
    ...
```

Nyaa images are extracted without recompression. WeebCentral pages enter the identical shape. A staging archive is removed only after atomic publication succeeds, avoiding duplicate long-term storage. The index retains release title, uploader, infohash or chapter IDs, byte count, source, and provenance.

## Recovery and deletion

`volume-index.json` is atomic. Torrent requests persist infohash, selected file, canonical ID, and destination. Libtorrent resume data remains authoritative for payload recovery. WeebCentral pack jobs record expected chapters and completed groups.

On restart, the service heals missing-file records, reconnects resumable torrents, restarts only missing pack work, and prunes aged orphan staging directories. No partial target becomes ready. Cancellation removes temporary payloads but keeps synopsis and source-search caches. Removing a volume deletes its pages and acquisition record, not its catalog row or mode preference.

## Startup and ownership boundaries

- The facade receives the shared torrent engine and a search network manager from `native/main.cpp`; it never creates a second libtorrent session. Its Nyaa RSS adapter is the ported Tankoban 2 runtime source, not `TankorentSearchService`.
- Manga series data and enrichment load only after the lazy manga-series page exists.
- Mode Off keeps the existing `Downloads` path unchanged.
- Western comics and the dormant comics-ledger torrent path are untouched.
- Shared build and main wiring must be edited surgically around other agents' uncommitted work.

## Testing strategy

Pure TDD covers volume identity, preference keys, Nyaa query generation, aliases, coverage parsing, hard filters, ranking, synopsis matching and ambiguity, repeated-synopsis rejection, torrent file selection and union priorities, WeebCentral ordering/completeness, state transitions, and index recovery.

Recorded fixtures cover Nyaa RSS, Open Library JSON, Apple Books JSON, multi-file torrent metadata, misleading result names, and complete/incomplete WeebCentral mappings.

Headless QML tests prove Off-default behavior, per-series persistence, unchanged chapter mode, complete volume rendering, WeebCentral-last ordering, manual selection, stable row progress, volume reader handoff, and separate progress namespaces.

Integration proof requires:

1. one real Nyaa magnet resolving metadata, downloading the requested archive, validating, extracting, producing `localPages(volumeId)`, and opening in `MangaReader`;
2. one real WeebCentral volume compilation reaching the same reader contract;
3. one kill/restart torrent recovery proof;
4. `native\build-msvc.bat` producing `BUILD_OK` with exit code 0;
5. Hemanth's final visual smoke.

## Explicit v1 exclusions

- western comics;
- automatic source selection;
- split-spread detection or reconstruction;
- inseparable combined-volume archives;
- bulk whole-series acquisition;
- publisher-specific synopsis scrapers;
- full-catalog eager synopsis enrichment;
- root-window startup work.

## Definition of Done

- Every manga series exposes Tankoban Mode with Off as the default and per-series persistence.
- Off preserves the current chapter experience.
- On shows every canonical volume, including undownloaded volumes.
- Each volume presents manually selectable ranked Nyaa cards and a final WeebCentral fallback.
- Multi-file torrents download only selected volume files and reuse shared infohash jobs.
- Nyaa and WeebCentral publish one canonical volume identity and page-store contract.
- Status, progress, cancellation, failure, completion, deletion, and pages use the same `volumeId`.
- Downloaded volumes read as continuous books with volume-aware navigation.
- Chapter and Tankoban reading progress do not overwrite one another.
- Strong Open Library or Apple results enrich volume synopses lazily with provenance; weak matches remain empty.
- Interrupted transfers recover without publishing partial volumes.
- Pure tests, headless QML tests, real Nyaa proof, real WeebCentral proof, restart proof, and MSVC build all pass.
