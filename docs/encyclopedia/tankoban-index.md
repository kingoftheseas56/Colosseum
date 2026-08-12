# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/tankoban-state.json`

## Summary

- Total files: **40**
- Documented: **29**
- Undocumented: **11**
- Drifted: **0**

<a id="file-native-mangaengine-h"></a>
## `native/MangaEngine.h`

- Status: **CURRENT**
- Accepted blob: `3cd9daf883ab55f2a863f3210594d088e4385a0b`
- Current blob: `3cd9daf883ab55f2a863f3210594d088e4385a0b`
- Source: [`native/MangaEngine.h`](../../native/MangaEngine.h)

```text
// QML bridge over the native WeebCentral scraper (lifted from Tankoban 2's proven
// engine). QML calls the Q_INVOKABLEs and receives results as plain JS arrays/objects
// via the signals — so the QML side never sees a C++ struct. Uses its OWN fresh
// QNetworkAccessManager (no PreferCache) so scrape responses are never served stale.
```

<a id="file-native-engine-mangadownloader-cpp"></a>
## `native/engine/MangaDownloader.cpp`

- Status: **CURRENT**
- Accepted blob: `8d1ee8d954aeb76ca5bdc8d8ad21058a21ee7108`
- Current blob: `8d1ee8d954aeb76ca5bdc8d8ad21058a21ee7108`
- Source: [`native/engine/MangaDownloader.cpp`](../../native/engine/MangaDownloader.cpp)

```text
// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
```

<a id="file-native-engine-mangadownloader-h"></a>
## `native/engine/MangaDownloader.h`

- Status: **CURRENT**
- Accepted blob: `d4968dcb23554d313cbc7b001f938e6bed6fbb0b`
- Current blob: `d4968dcb23554d313cbc7b001f938e6bed6fbb0b`
- Source: [`native/engine/MangaDownloader.h`](../../native/engine/MangaDownloader.h)

```text
// MangaDownloader.h
//
// The download-fed backbone: reading is NEVER a live stream. A chapter is
// downloaded once — its page images land as loose files on disk — and the
// reader then reads those local files, offline, forever. This recreates
// Tankoban 2 / the Electron app's proven downloader in Colosseum-lean form:
// the irreducible core (fetch page URLs -> download images -> JSON index ->
// localPages flip) is kept; TB2's CBZ packing / followed-library / history-cap
// are deferred to a later pass (justified up only when needed).
//
// Pipeline (mirrors TB2 + mangaDownloads.js):
//   1. WeebCentralScraper::fetchPages(chapterId)  -> [{index, imageUrl}]
//   2. for each page: GET image -> write <dir>/page_NNN.<ext>  (3 retries,
//      2/4/8s backoff; resume skips existing files > 1 KB; bounded concurrency)
//   3. write an index entry {chapterId -> dir, files[], pageCount, bytes}
//   4. reader calls localPages(chapterId) -> file:/// URLs for the saved pages
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/manga/<series>/<chapter>/page_000.jpg ...
//   <appdata>/manga/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.
```

<a id="file-native-engine-mangaresult-h"></a>
## `native/engine/MangaResult.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `797b652dbfa6407d13d020a5da5acd025191299a`
- Current blob: `797b652dbfa6407d13d020a5da5acd025191299a`
- Source: [`native/engine/MangaResult.h`](../../native/engine/MangaResult.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangascraper-h"></a>
## `native/engine/MangaScraper.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `9557969a67627a180191ed76602ea5e1c475edd9`
- Current blob: `9557969a67627a180191ed76602ea5e1c475edd9`
- Source: [`native/engine/MangaScraper.h`](../../native/engine/MangaScraper.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangaseriesdetail-h"></a>
## `native/engine/MangaSeriesDetail.h`

- Status: **CURRENT**
- Accepted blob: `8309dbb5d34dcd319974ac002616d5022b0c0f33`
- Current blob: `8309dbb5d34dcd319974ac002616d5022b0c0f33`
- Source: [`native/engine/MangaSeriesDetail.h`](../../native/engine/MangaSeriesDetail.h)

```text
// Detail-page payload returned by MangaScraper::fetchDetail.
// Decoupled from MangaResult (the search preview) so that
// preview cards stay cheap while detail-page hero gets the full
// metadata. Per brainstorm-md §12 (Codex pass).
```

<a id="file-native-engine-mangasynopsisenricher-cpp"></a>
## `native/engine/MangaSynopsisEnricher.cpp`

- Status: **CURRENT**
- Accepted blob: `9c94a5ba1b86cf5ade5addf025cbeff8c35411b6`
- Current blob: `9c94a5ba1b86cf5ade5addf025cbeff8c35411b6`
- Source: [`native/engine/MangaSynopsisEnricher.cpp`](../../native/engine/MangaSynopsisEnricher.cpp)

```text
// native/engine/MangaSynopsisEnricher.cpp
```

<a id="file-native-engine-mangasynopsisenricher-h"></a>
## `native/engine/MangaSynopsisEnricher.h`

- Status: **CURRENT**
- Accepted blob: `7e85df92e9e2ae4ef0abacecaaa25954f6e5da08`
- Current blob: `7e85df92e9e2ae4ef0abacecaaa25954f6e5da08`
- Source: [`native/engine/MangaSynopsisEnricher.h`](../../native/engine/MangaSynopsisEnricher.h)

```text
// Lazy, source-honest per-volume synopsis enrichment for Tankoban "volume mode".
//
// Canonical volume rendering NEVER waits on this: every fetch is async/lazy and
// the enricher only augments a volume with a synopsis once it has genuine,
// target-volume evidence. Two providers, tried in order per uncached volume:
//   1. Open Library  — search by series+volume; a doc is accepted only on a
//      normalized series-title + explicit target-volume match. A matched edition
//      carrying a valid English-registration-group ISBN is stamped "exact-isbn";
//      a title+volume-only match is "exact-title-volume".
//   2. Apple Books   — iTunes Search (entity=ebook); a result is accepted only on
//      strong series agreement + an explicit target volume. Author agreement
//      breaks a near-tie; two equally-strong candidates with no distinguishing
//      author signal are LEFT EMPTY (never guessed).
//
// The enricher must never emit the SERIES synopsis (or another volume's text) as
// a volume synopsis — every accepted text is gated through acceptDistinctVolumeText.
//
// Concern split (so the matching is harness-testable with no I/O):
//   * matchOpenLibrary / matchApple / acceptDistinctVolumeText are pure static
//     functions over (series, volume, json) or two strings. They do all the
//     honesty work and are the pinned contract.
//   * enrichSeries owns the async cascade: cache lookup, Open Library first, a
//     throttled single-flight Apple fallback, cache writes and synopsisReady.
//
// Cache: JSON schema version 1, one SynopsisRecord per volumeId, persisted
// atomically with QSaveFile. A cache MISS (a valid response that yielded nothing
// acceptable) is retained 24h so we don't re-hammer; an ACCEPTED record 30 days.
// A network FAILURE records no permanent negative.
```

<a id="file-native-engine-mangatankobanlogic-cpp"></a>
## `native/engine/MangaTankobanLogic.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `1a4d873f7d4978fba8251d1b55e2f5bae9f65863`
- Current blob: `1a4d873f7d4978fba8251d1b55e2f5bae9f65863`
- Source: [`native/engine/MangaTankobanLogic.cpp`](../../native/engine/MangaTankobanLogic.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangatankobanlogic-h"></a>
## `native/engine/MangaTankobanLogic.h`

- Status: **CURRENT**
- Accepted blob: `f2ceae88046c04b3d8987d3060495f0135149d2b`
- Current blob: `f2ceae88046c04b3d8987d3060495f0135149d2b`
- Source: [`native/engine/MangaTankobanLogic.h`](../../native/engine/MangaTankobanLogic.h)

```text
// Pure logic for Tankoban volume mode: string-safe volume-number normalization,
// stable escaped identity (volume id + per-series settings key), and assembly of
// a SeriesSnapshot from QML-style QVariant snapshots. No Qt Quick / no I/O — this
// header is harness-testable on its own (see tests/manga_tankoban_logic_harness.cpp).
```

<a id="file-native-engine-mangatankobanservice-cpp"></a>
## `native/engine/MangaTankobanService.cpp`

- Status: **CURRENT**
- Accepted blob: `37594e9c08cc91d38ce0650d5b8e12ac3fcacf18`
- Current blob: `37594e9c08cc91d38ce0650d5b8e12ac3fcacf18`
- Source: [`native/engine/MangaTankobanService.cpp`](../../native/engine/MangaTankobanService.cpp)

```text
// native/engine/MangaTankobanService.cpp
```

<a id="file-native-engine-mangatankobanservice-h"></a>
## `native/engine/MangaTankobanService.h`

- Status: **CURRENT**
- Accepted blob: `f507253c60b69fba3dd51b589a468d1a1e79a076`
- Current blob: `f507253c60b69fba3dd51b589a468d1a1e79a076`
- Source: [`native/engine/MangaTankobanService.h`](../../native/engine/MangaTankobanService.h)

```text
// The single Tankoban "volume mode" façade the QML page talks to (`TankobanVolumes`).
//
// Tasks 1–7 built the organs; this composes them into ONE object that owns the
// whole lifecycle of a tankōbon volume: it turns a MangaFire snapshot into
// canonical volume records (Task 1), searches Nyaa for a per-volume source
// (Task 2), lazily enriches each volume's synopsis (Task 3), downloads a chosen
// candidate through the restart-safe torrent transport (Task 6) or synthesizes
// the volume from WeebCentral chapters (Task 7), and ingests the result into the
// durable local index (Task 5) so it reads through MangaReader like any other
// download. ONE object owns terminal ("ready") state, so QML never has to reason
// about which collaborator finished.
//
// Testability by dependency injection: the DI constructor takes already-built
// collaborators (a fake Nyaa search, the real transport over a fake torrent
// engine, and real index/ingestor/enricher over a temp dir), so the whole
// search→choose→download→ingest→ready pipeline is provable offline without
// libtorrent (see tests/manga_tankoban_service_harness.cpp). The production
// constructor builds the same collaborators over the shared runtime NAMs and the
// real TorrentEngine.
//
// The concrete IMangaTorrentEngine adapter over the real (non-virtual)
// TorrentEngine lives here too, gated on HAS_LIBTORRENT so the harness (which
// never links libtorrent) compiles the façade without the engine.
```

<a id="file-native-engine-mangatankobantypes-h"></a>
## `native/engine/MangaTankobanTypes.h`

- Status: **CURRENT**
- Accepted blob: `4888002d5a4a40fb09b7673f2cb830d0006b5b41`
- Current blob: `4888002d5a4a40fb09b7673f2cb830d0006b5b41`
- Source: [`native/engine/MangaTankobanTypes.h`](../../native/engine/MangaTankobanTypes.h)

```text
// Canonical value types for Tankoban "volume mode". A VolumeRecord is the app's
// single source of truth for one tankobon volume: a stable escaped id, the
// display fields lifted from the source row, and the ordered chapter ids that
// fall under it. Volume numbers are STRINGS on purpose — fractional (10.5) and
// special/named volumes must never be collapsed by a float round-trip.
```

<a id="file-native-engine-mangavolumearchiveingestor-cpp"></a>
## `native/engine/MangaVolumeArchiveIngestor.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `d0775437f0188455cc403e227c17e7b201ce106c`
- Current blob: `d0775437f0188455cc403e227c17e7b201ce106c`
- Source: [`native/engine/MangaVolumeArchiveIngestor.cpp`](../../native/engine/MangaVolumeArchiveIngestor.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangavolumearchiveingestor-h"></a>
## `native/engine/MangaVolumeArchiveIngestor.h`

- Status: **CURRENT**
- Accepted blob: `233ce8c8abd995dd012fa18e5f462ed548ceabbe`
- Current blob: `233ce8c8abd995dd012fa18e5f462ed548ceabbe`
- Source: [`native/engine/MangaVolumeArchiveIngestor.h`](../../native/engine/MangaVolumeArchiveIngestor.h)

```text
// Lossless archive → volume ingestion for Tankoban volume mode.
//
// A downloaded CBZ/ZIP is validated, copied atomically into canonical storage,
// and published without extraction. Other supported comic archives use the OS
// archiver only as a conversion step, then become a validated canonical CBZ.
// Images are never recompressed. A partial or failed conversion never publishes
// a ready volume.
//
// This is a focused fork of ComicDownloader's proven extraction lifecycle
// (bsdtar-first, 7-Zip fallback, recursive QCollator natural sort, page_%03d
// naming). ComicDownloader stays untouched; the manga path needs the volume
// provenance + the two-phase staging→final atomic rename, so the lifecycle is
// lifted here rather than shared through the western-comics object.
//
// publish() is the WeebCentral packer's entry: its temporary downloaded images
// are naturally ordered and packed into one canonical CBZ before publication.
```

<a id="file-native-engine-mangavolumeindex-cpp"></a>
## `native/engine/MangaVolumeIndex.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `8fd8915e315307c9db4406632109fbafecbe6ee2`
- Current blob: `8fd8915e315307c9db4406632109fbafecbe6ee2`
- Source: [`native/engine/MangaVolumeIndex.cpp`](../../native/engine/MangaVolumeIndex.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangavolumeindex-h"></a>
## `native/engine/MangaVolumeIndex.h`

- Status: **CURRENT**
- Accepted blob: `c113d61d4cf1cf05ddb6d44f4163f7dba0839e10`
- Current blob: `c113d61d4cf1cf05ddb6d44f4163f7dba0839e10`
- Source: [`native/engine/MangaVolumeIndex.h`](../../native/engine/MangaVolumeIndex.h)

```text
// Durable, atomic on-disk index of downloaded Tankoban volumes.
//
// One volume = one canonical CBZ plus an atomic recovery sidecar and one row in
// a single JSON ledger written with QSaveFile, so a crash mid-write can never
// corrupt the ledger (the temp file is renamed into place or discarded whole).
// The ledger row carries the volume's canonical id, series metadata, source
// provenance (nyaa infohash / weebcentral chapter ids, uploader, release title),
// the naturally-ordered page filenames, per-page chapter-group ordinals, the
// payload byte count and the added-time.
//
// localPages() returns direct archive descriptors:
//   [{index, archive, entry, group}]
// The comic reader decodes these entries without extracting them.
//
// Layout (root is injected — the app passes AppDataLocation, tests a temp dir):
//   <root>/manga-volumes/volume-index.json          (the ledger)
//   <root>/manga-volumes/archives/<series>/vol-<n>-<hash>.cbz
//   <root>/manga-volumes/archives/<series>/vol-<n>-<hash>.cbz.json
//
// Self-heal doctrine (repair-then-prune): sidecars and legacy per-volume
// manifests repair the ledger first. Valid loose legacy payloads migrate to CBZ;
// only an unrecoverable lookup row is pruned, never unvalidated payload bytes.
```

<a id="file-native-engine-mangavolumepacker-cpp"></a>
## `native/engine/MangaVolumePacker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f0d1c6407c47fc2c2464cc1add064a008801ded4`
- Current blob: `f0d1c6407c47fc2c2464cc1add064a008801ded4`
- Source: [`native/engine/MangaVolumePacker.cpp`](../../native/engine/MangaVolumePacker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-mangavolumepacker-h"></a>
## `native/engine/MangaVolumePacker.h`

- Status: **CURRENT**
- Accepted blob: `5923506144119ca30f3918457c9f9a66dca3fde0`
- Current blob: `5923506144119ca30f3918457c9f9a66dca3fde0`
- Source: [`native/engine/MangaVolumePacker.h`](../../native/engine/MangaVolumePacker.h)

```text
// WeebCentral volume fallback packer for Tankoban volume mode.
//
// When a tankobon has no single-archive (nyaa) source, this packer synthesizes
// the volume from its constituent WeebCentral chapters. For each chapterId in the
// VolumeRecord (in order) it asks a MangaScraper for that chapter's page image
// URLs, downloads every image through a QNetworkAccessManager, validates each as a
// real image (magic-byte sniff), and lays them out in a staging directory named
//
//     c<chapterNumber:03d>_<pageInChapter:03d>.<ext>
//
// (chapter order, then natural page order). Each page is tagged with its chapter's
// 0-based ordinal within the volume (its "group"). Once EVERY chapter and page is
// present and valid, the prepared directory is handed to the shared
// MangaVolumeArchiveIngestor::publish() path together with the per-page group
// vector, so a WeebCentral volume lands in MangaVolumeIndex with the SAME
// canonical "ready" shape a nyaa volume does. A missing/failed chapter or page, or
// a cancel(), tears down the staging directory and NEVER publishes a ready volume
// (complete() stays false): a partial fallback is never presented as complete.
//
// Chapter number: WeebCentral chapter ids are opaque ULIDs. The number is parsed
// from the id only when it cleanly carries a trailing numeric run at a
// non-alphanumeric (or start) boundary ("wc-chapter-10" -> 10); an opaque id falls
// back to the chapter's 1-based volume ordinal, so pages stay ordered and uniquely
// named regardless. Reading order and chapter-group boundaries come from the
// chapter order + the group vector, never from the label — so a ULID-only volume
// still reads correctly.
```

<a id="file-native-engine-weebcentralscraper-cpp"></a>
## `native/engine/WeebCentralScraper.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `c04f29aa3171f4a70e2ee5e80f2b946ed1fb4080`
- Current blob: `c04f29aa3171f4a70e2ee5e80f2b946ed1fb4080`
- Source: [`native/engine/WeebCentralScraper.cpp`](../../native/engine/WeebCentralScraper.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-weebcentralscraper-h"></a>
## `native/engine/WeebCentralScraper.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `926c25d89520ac66ef3a03db50ad1f127e509a61`
- Current blob: `926c25d89520ac66ef3a03db50ad1f127e509a61`
- Source: [`native/engine/WeebCentralScraper.h`](../../native/engine/WeebCentralScraper.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-manganyaasource-cpp"></a>
## `native/torrent/MangaNyaaSource.cpp`

- Status: **CURRENT**
- Accepted blob: `15f518415b05d77c39efaa9a0117325030e9984c`
- Current blob: `15f518415b05d77c39efaa9a0117325030e9984c`
- Source: [`native/torrent/MangaNyaaSource.cpp`](../../native/torrent/MangaNyaaSource.cpp)

```text
// native/torrent/MangaNyaaSource.cpp
```

<a id="file-native-torrent-manganyaasource-h"></a>
## `native/torrent/MangaNyaaSource.h`

- Status: **CURRENT**
- Accepted blob: `0375fb06064662b496f6d98696376e887d44e65c`
- Current blob: `0375fb06064662b496f6d98696376e887d44e65c`
- Source: [`native/torrent/MangaNyaaSource.h`](../../native/torrent/MangaNyaaSource.h)

```text
// Manga-specific Nyaa RSS volume discovery for Tankoban "volume mode".
//
// PORTED from Tankoban 2's proven core/manga/NyaaRuntimeSource: the query
// family, the namespace-aware RSS parse, the uploader-trust tiers, the
// volume-coverage matcher and the stable tier/seeder ranking are retained
// faithfully. It is EXTENDED with the derived fields the volume-mode QML needs
// (string coverage-range bounds, standalone/digital hints) and the rejection
// filters TB2 lacked (chapter-pack, wrong-target, raw/untranslated, weak
// series-title match, hash-less rows).
//
// This is a DELIBERATE fork, not a reuse of Colosseum's generic
// torrent/TankorentSearchService. That service explicitly DROPPED Nyaa and its
// generic result type carries no uploader metadata, so trust-ranked volume
// discovery cannot live there. The Nyaa behaviour is kept manga-side, here,
// leaving the generic torrent path untouched. Torrent payloads are later handed
// to Colosseum's one shared TorrentEngine — out of scope for this source.
//
// Concern split (intentional, so the pure logic is harness-testable):
//   * queryVariants / parseRss are pure and trust-agnostic. parseRss takes only
//     the RSS bytes (the pinned harness contract) and therefore does NO trust
//     tagging and NO target filtering — it just extracts fields and derives the
//     coverage/standalone/digital data straight from each title.
//   * filterAndRank is where every trust-dependent and volume-target decision
//     happens: uploader inference, tier tagging, blocked drop, coverage/target
//     match, chapter-pack / raw / weak-match / hash-less rejection, dedup by
//     infohash, and the advisory ordering. TB2 did target filtering inside the
//     batched-reply merge; we lift it into one pure function instead.
```

<a id="file-native-torrent-mangavolumefilepicker-cpp"></a>
## `native/torrent/MangaVolumeFilePicker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `ba7e31106e0f65e3b9e841e061c3d1a71868406a`
- Current blob: `ba7e31106e0f65e3b9e841e061c3d1a71868406a`
- Source: [`native/torrent/MangaVolumeFilePicker.cpp`](../../native/torrent/MangaVolumeFilePicker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-mangavolumefilepicker-h"></a>
## `native/torrent/MangaVolumeFilePicker.h`

- Status: **CURRENT**
- Accepted blob: `81d13d3cb2d41dfc0ca933442fb8f6d4de9f651f`
- Current blob: `81d13d3cb2d41dfc0ca933442fb8f6d4de9f651f`
- Source: [`native/torrent/MangaVolumeFilePicker.h`](../../native/torrent/MangaVolumeFilePicker.h)

```text
// Metadata-aware manga volume file picker. Given the requested volume and a
// torrent's REAL engine metadata (the QJsonArray shape emitted by
// TorrentEngine::metadataReady / TorrentEngine::torrentFiles — each element an
// object with "index", "name", "size"), isolate the single archive that IS that
// volume. It refuses when the volume cannot be honestly isolated: no comic
// archive present, the target is absent, two candidates match equally, or the
// only match is an inseparable multi-volume combined archive.
//
// Self-contained by design (spec: do NOT couple to MangaNyaaSource). The volume
// coverage grammar mirrors the Nyaa source's detectCoverage() — explicit
// v / vol / volume markers plus inclusive ranges — reimplemented locally so this
// picker owns its own parsing.
```

<a id="file-native-torrent-mangavolumerequestledger-cpp"></a>
## `native/torrent/MangaVolumeRequestLedger.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `bf15133ad3e525e4aeef7e05de888e629b0734f0`
- Current blob: `bf15133ad3e525e4aeef7e05de888e629b0734f0`
- Source: [`native/torrent/MangaVolumeRequestLedger.cpp`](../../native/torrent/MangaVolumeRequestLedger.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-mangavolumerequestledger-h"></a>
## `native/torrent/MangaVolumeRequestLedger.h`

- Status: **CURRENT**
- Accepted blob: `dde366f19e8842006b61071a5b6e5c7c5db13249`
- Current blob: `dde366f19e8842006b61071a5b6e5c7c5db13249`
- Source: [`native/torrent/MangaVolumeRequestLedger.h`](../../native/torrent/MangaVolumeRequestLedger.h)

```text
// Restart-safe intent ledger for Tankoban volume-mode torrent downloads.
//
// One ROW per requested volume (keyed by its stable volumeId). A row records
// everything the transport needs to resume a download after a process restart:
// the shared torrent infoHash + magnet, the series/volume identity, the save
// path, the picked file index once metadata resolves, and the current state.
//
// State machine (a row NEVER leaves the ledger; it just advances):
//   awaiting_metadata -> downloading -> validating -> completed
//                                    \-> failed
//   (any live state)                  \-> cancelled
// active() returns only the non-terminal rows (awaiting_metadata / downloading /
// validating) so a fresh downloader can REPLAY exactly the intents still in
// flight. Persistence is atomic via QSaveFile so a crash mid-write can never
// corrupt the ledger.
```

<a id="file-native-torrent-mangavolumetorrentdownloader-cpp"></a>
## `native/torrent/MangaVolumeTorrentDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f261d31a0c3ac4c8d9dc1a6c8b26d6e531844ca0`
- Current blob: `f261d31a0c3ac4c8d9dc1a6c8b26d6e531844ca0`
- Source: [`native/torrent/MangaVolumeTorrentDownloader.cpp`](../../native/torrent/MangaVolumeTorrentDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-mangavolumetorrentdownloader-h"></a>
## `native/torrent/MangaVolumeTorrentDownloader.h`

- Status: **CURRENT**
- Accepted blob: `fa6e9dcfcdb6560f7aa94e915ac78265887f2b3d`
- Current blob: `fa6e9dcfcdb6560f7aa94e915ac78265887f2b3d`
- Source: [`native/torrent/MangaVolumeTorrentDownloader.h`](../../native/torrent/MangaVolumeTorrentDownloader.h)

```text
// Restart-safe torrent transport for Tankoban volume mode.
//
// Mirrors ComicTorrentDownloader's proven engine wiring (metadataReady ->
// pick + priorities, progress throttle, finished-verify-then-emit) but with the
// two behaviours volume mode needs:
//
//   1. Metadata is inspected BEFORE any payload downloads. A candidate is added
//      PAUSED; only after its metadata arrives, the exact volume file is
//      resolved, priorities are set to that file, and the torrent is STARTED.
//      (ComicTorrent adds paused=false and lets everything trickle.)
//
//   2. One torrent can serve SEVERAL requested volumes. Jobs are keyed by
//      infoHash; each carries a set of requested volume intents. Adding a second
//      volume to a live torrent GROWS the file-priority union — it never re-adds
//      the magnet — and each volume finishes/fails/cancels independently.
//
// Every intent is journaled to a MangaVolumeRequestLedger so a restart can
// replay exactly the downloads still in flight.
//
// The engine is reached through the abstract IMangaTorrentEngine seam so this
// unit is harness-testable without libtorrent. The concrete adapter over the
// real TorrentEngine is wired in a later task.
```

<a id="file-qml-mangacatalogpage-qml"></a>
## `qml/MangaCatalogPage.qml`

- Status: **CURRENT**
- Accepted blob: `fb26bb786c2e929e7283de366f86174f3353ae9d`
- Current blob: `fb26bb786c2e929e7283de366f86174f3353ae9d`
- Source: [`qml/MangaCatalogPage.qml`](../../qml/MangaCatalogPage.qml)

```text
// Top Manga — the complete MAL score-ranked manga wall (topmanga.php), rebuilt in
// the house glass of the retired ~1,100 Top Comics catalogue (057e109^). Data is
// the BAKED MyAnimeList dump via MalCatalog.topManga() — offline, never a live
// Jikan call, ranked by weighted score with a vote floor (~1,254 titles).
```

<a id="file-qml-mangareader-qml"></a>
## `qml/MangaReader.qml`

- Status: **CURRENT**
- Accepted blob: `040671f1729375cdaba789cdbc262f43ba42d990`
- Current blob: `040671f1729375cdaba789cdbc262f43ba42d990`
- Source: [`qml/MangaReader.qml`](../../qml/MangaReader.qml)

```text
// The production reader (Task 13 cutover, 2026-07-25).
//
// This file used to BE the reader — ~2000 lines of Electron-recreation paging driven by
// ReaderEngine.js. It is now a thin delegation to the from-scratch Comic Reader in qml/comicreader/,
// which decides in C++ and paints in QML (ComicReaderCore + the image://comicreader/ provider).
//
// The filename stays because the callers stay: qml/MangaSeries.qml, qml/ComicSeries.qml,
// qml/ComicSeriesPage.qml and qml/BakeoffStripHost.qml all instantiate `MangaReader { ... }` and are
// UNCHANGED by this cutover — that was the design constraint of the whole rebuild, and
// tests/test_comicreader_migration.ps1 enforces both halves of it: this file must stay thin (no
// state, no behaviour), and the reader behind it must still honour the full Task 1 caller contract.
//
// Nothing belongs in here. A property or workaround added to this file is a second implementation
// of the reader; put it in comicreader/ComicReaderShell.qml (orchestration) or in the surface that
// owns the behaviour.
//
// Guided is ARCHIVED, not frozen-in-tree. It was still on disk when this reader was cut over, but
// Agent 2's 1d79fee removed the ONNX/read-along/guided arc from master on Hemanth's call — sources,
// harnesses and its gate. The whole thing lives on `archive/onnx-readalong-guided-2026-07-24`, one
// command from restoration. Nothing here reaches it: the reader's mode identity is Manga/Comic/Strip
// only, and tests/test_comicreader_surfaces.ps1 still greps this tree for any guided reference.
```

<a id="file-qml-mangaseries-qml"></a>
## `qml/MangaSeries.qml`

- Status: **CURRENT**
- Accepted blob: `4a94b78320f21001b3884ced00c6ae5524f422dd`
- Current blob: `4a94b78320f21001b3884ced00c6ae5524f422dd`
- Source: [`qml/MangaSeries.qml`](../../qml/MangaSeries.qml)

```text
// MangaSeries — the manga detail page. Colosseum series-view design (mock:
// agents/colosseum-series-mock.html, approved 2026-06-27). Floats over the wallpaper; metadata is
// inline (no glass pills); gold stays a sparing accent. Data is LIVE from the native engine via the
// `Manga` bridge:
//   title → WeebCentral search → (chapters + detail)
//                              → volumes(wcId, title) → the Comick volume DB / live scrape, gated
//         → AniList art()      → banner / cover / synopsis / genres / year / score
// THE SURFACE IS DECIDED BY THE DATA (2026-07-29 ruling, no toggle): a series whose volume list
// passes the completeness gate gets the permanent tankoban volume library, with the glass chapter
// table below reduced to the loose tail ("Latest chapters"); a series that does not qualify gets
// the plain flat WeebCentral chapter list. An estimated volume boundary is never shown.
// Opened from a Top-10 manga tile.
```

<a id="file-qml-mangatankobanlibrary-qml"></a>
## `qml/MangaTankobanLibrary.qml`

- Status: **CURRENT**
- Accepted blob: `4274c26fa82dd9e851efdbf595e7d0499698f016`
- Current blob: `4274c26fa82dd9e851efdbf595e7d0499698f016`
- Source: [`qml/MangaTankobanLibrary.qml`](../../qml/MangaTankobanLibrary.qml)

```text
// MangaTankobanLibrary - the reader-derived Tankoban Pages flow.
//
// Every canonical volume belongs to one virtualized ListView model. The focused
// volume is centered through the reader's double-pass geometry operation; the
// surrounding covers shrink monotonically into one consistent recession line.
```

<a id="file-qml-mangatankobansourcespage-qml"></a>
## `qml/MangaTankobanSourcesPage.qml`

- Status: **CURRENT**
- Accepted blob: `afabe31cf40d201091cfc87a49b2f44de4e218d5`
- Current blob: `afabe31cf40d201091cfc87a49b2f44de4e218d5`
- Source: [`qml/MangaTankobanSourcesPage.qml`](../../qml/MangaTankobanSourcesPage.qml)

```text
// MangaTankobanSourcesPage — the full-screen "Choose source" picker for ONE
// tankōbon volume, in the Colosseum house language (mirrors ComicTorrentSourcesPage:
// black base + wallpaper backdrop, volume key-art hero washing down, gold eyebrow +
// Fraunces title + identity line, a glass result table). Manga-specific: the ranked
// Nyaa releases (uploader trust → STRONG/POSSIBLE/WEAK, evidence chips explaining
// coverage) then the quieter WeebCentral "Build from chapters" fallback pinned LAST.
// The user always chooses; nothing auto-picks.
//
// Belongs to MangaSeries (a sibling of the reader; mutually-exclusive overlays). All
// acquisition rides the native TankobanVolumes service under the original volumeId —
// this page emits NO reader signal; it only kicks a native download/compile then hides.
```

<a id="file-qml-tankobancomicstab-qml"></a>
## `qml/TankobanComicsTab.qml`

- Status: **CURRENT**
- Accepted blob: `33c0d3dc4f93d16590089b9aa9a725bc25b19ced`
- Current blob: `33c0d3dc4f93d16590089b9aa9a725bc25b19ced`
- Source: [`qml/TankobanComicsTab.qml`](../../qml/TankobanComicsTab.qml)

```text
// TankobanComicsTab — the Comics half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the comics rows. Data is passed IN from TankobanWorld (which owns the
// one-time ComicsCatalog.shelf compute + GcApi.explore fetch) so switching tabs never
// re-fetches — the Loader may rebuild this view, but the data is cached upstream and bound
// in reactively. Emits the comics signals the world forwards to the host. No manga knowledge.
```

<a id="file-qml-tankobandiscoverpage-qml"></a>
## `qml/TankobanDiscoverPage.qml`

- Status: **CURRENT**
- Accepted blob: `b250db2ebfece18b75af4e43d67c8fee66ea148e`
- Current blob: `b250db2ebfece18b75af4e43d67c8fee66ea148e`
- Source: [`qml/TankobanDiscoverPage.qml`](../../qml/TankobanDiscoverPage.qml)

```text
// TankobanDiscoverPage — the TANKOBAN wrapper around the shared Discover shell
// (Task 7, arc 2026-08-01).
//
// The browsing surface lives in the world-neutral DiscoverBrowser.qml. This file is the
// thin Tankoban-side wrapper: it builds the Tankoban adapter (TankobanDiscoverApi.js) from
// injected dependencies (MalCatalog / ComicsCatalog context objects, the extension
// registry, the global showExplicitContent preference, and an XMLHttpRequest factory),
// binds it to the shell, and routes a normalized card by type into TankobanWorld's
// existing series doors — Manga cards to seriesRequested(title), Comics cards to
// comicSeriesRequested(item). It owns NO download action and NO series-page UI; the
// existing detail routes are unchanged.
//
// Dependencies are PROPERTIES (not context reads) because the adapter factory needs real
// objects and the page harness builds this bare — a missing MalCatalog/ComicsCatalog is
// null-safe (the adapter returns empty catalogues and the shell shows the empty state).
// showExplicitContent is threaded from WorldPage (Task 7 Step 4) and later from the
// global ContentPreferences (Task 9); the adapter is rebuilt when it changes so the wall
// reflects the live preference without a manual reload.
//
// Public surface for TankobanWorld + the page harness: applyPin(pin),
// mangaSeriesRequested(item), comicSeriesRequested(item), currentType, keyboardMode,
// catalogMenuOpen, catalogMenuModel, items, loading.
```

<a id="file-qml-tankobanlibraryapi-js"></a>
## `qml/TankobanLibraryApi.js`

- Status: **CURRENT**
- Accepted blob: `6f59c06d32922a071faa785fd681f0ee69b5f3ef`
- Current blob: `6f59c06d32922a071faa785fd681f0ee69b5f3ef`
- Source: [`qml/TankobanLibraryApi.js`](../../qml/TankobanLibraryApi.js)

```text
// TankobanLibraryApi — pure row derivation for the Tankoban Library tab (spec:
// Brotherhood#1). Mirrors LibraryApi.js's shape (pure joins, no context-property or
// network access — a .pragma library script can't see those anyway) but carries
// Tankoban's own vocabulary: manga/comic rows, chapter/volume reading lanes, no
// airing/watched/new-episode concepts. See CONTEXT.md for "Library row", "Collection",
// "reading lane".
//
// TB-002 slice: manga-chapter progress join added. A manga Collection entry joins the
// kind:"manga" progress list — canonical first (record.id === entry.id), then a legacy
// title fallback (a still-title-keyed entry finding its seriesId-keyed progress). Two
// Collection entries that resolve to the SAME progress record (the brief re-file window
// where both a canonical and a legacy row exist) collapse to one canonical row.
//
// TB-003 slice: the volume lane (kind:"tankoban") joins manga entries alongside the
// chapter lane, and the newer of the two wins (a series with both a chapter read and a
// volume read resumes into whichever happened last). Comic entries (type:"comic") join
// kind:"comic" progress only — they never match manga/tankoban records. Comics use the
// same canonical-then-title matcher shape but a strict kind filter, so a saved western
// comic and a manga with the same title cannot cross-resume.
//
// TB-004 slice: a row whose resume target is on disk shows a download badge. The badge
// is a pure function of the resume target's chapter id + a per-chapter-id "is on disk"
// map the page feeds in (the page owns the native download-index seam; this pure module
// never touches it). No new native API and no series-level scan: one lookup per resume
// target, honest for the resume action. A row resuming via the VOLUME lane has no honest
// per-volume on-disk state without a new seam (volumes live in TankobanVolumes, not the
// chapter download index), so volume-lane rows are deliberately left unbadged — an
// explicit, scoped gap, not a silent bug. Filters and sorts still stay at their TB-001
// defaults; TB-005 owns them.
```

<a id="file-qml-tankobanlibrarytab-qml"></a>
## `qml/TankobanLibraryTab.qml`

- Status: **CURRENT**
- Accepted blob: `0da3e9e9939e31967d0d044c1c07af609e075805`
- Current blob: `0da3e9e9939e31967d0d044c1c07af609e075805`
- Source: [`qml/TankobanLibraryTab.qml`](../../qml/TankobanLibraryTab.qml)

```text
// TankobanLibraryTab — Tankoban's Library tab. TB-001 shipped the mixed manga+comic wall
// + Details routing; TB-002 added the manga-chapter progress join + resume-vs-Details tap
// branch + the Collection identity fix; TB-003 added the volume-lane + comic joins and the
// most-recent-lane rule; TB-004 added the download badge (resume-target chapter on disk);
// TB-005 adds search + 3 filters (All / In Progress / Downloaded) + 3 sorts (Last Read /
// Recently Added / A-Z) + the card ⋮ menu with Remove from Library. The pure row-derivation
// lives in TankobanLibraryApi.js; this page owns the live singletons (Collection / Progress /
// Downloads), the toolbar state, and the menu. Mirrors LibraryPage.qml's construction
// discipline (every singleton typeof-guarded so this constructs offscreen for the harness)
// but carries Tankoban's own vocabulary. Renders as tab content inside TankobanWorld (no
// standalone chrome), retained across tab switches like TankobanDiscoverPage and Theatre's
// LibraryPage — same fixed-viewport-height + self-scrolling GridView pattern both ship.
```

<a id="file-qml-tankobanmangatab-qml"></a>
## `qml/TankobanMangaTab.qml`

- Status: **CURRENT**
- Accepted blob: `4e16853585c62c3590bf5f3475d4e4161feeed53`
- Current blob: `4e16853585c62c3590bf5f3475d4e4161feeed53`
- Source: [`qml/TankobanMangaTab.qml`](../../qml/TankobanMangaTab.qml)

```text
// TankobanMangaTab — the Manga half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the manga rows, sourcing static manga data from Catalog.js and
// emitting the same signals the world forwards to the host. No comics knowledge.
```

<a id="file-qml-tankobanworld-qml"></a>
## `qml/TankobanWorld.qml`

- Status: **CURRENT**
- Accepted blob: `4b48a81a3188d1320d589fb4549fa0520235f597`
- Current blob: `4b48a81a3188d1320d589fb4549fa0520235f597`
- Source: [`qml/TankobanWorld.qml`](../../qml/TankobanWorld.qml)

```text
// TankobanWorld — the REAL instantiation of the world-page template for the Tankoban mode
// (comics + manga / sequential art). Owner: A1. Data lives in Catalog.js (one source, also used by
// the boot prefetch). Ported from our shipped Tankoban Electron catalog (manga = AniList/WeebCentral
// · comics = RCO "rcostation"); live sources come LATER (Hemanth: "apis can come later").
//
// The board (Hemanth-locked 2026-06-25) — personal surfaces BLENDED, discovery surfaces SPLIT:
//   1. Featured (blended) · 2. Continue (blended) · 3. Top in Tankoban — Manga
//   4. Top in Tankoban — Comics (curated) · 5. Explore Genre — Manga
//   6. Explore Comics (GetComics' own tag taxonomy, inline)
// The catalogue's needs override the doctrine's ~2-row cap: comics and manga are two real
// sub-catalogues, so the split IS the need (not a lazy row-wall).
```
