# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/biblio-state.json`

## Summary

- Total files: **59**
- Documented: **45**
- Undocumented: **14**
- Drifted: **0**

<a id="file-native-engine-biblioartworkurl-h"></a>
## `native/engine/BiblioArtworkUrl.h`

- Status: **CURRENT**
- Accepted blob: `47c9bbc41c22ff214c0bbc0478e3b9a7f562ef8b`
- Current blob: `47c9bbc41c22ff214c0bbc0478e3b9a7f562ef8b`
- Source: [`native/engine/BiblioArtworkUrl.h`](../../native/engine/BiblioArtworkUrl.h)

```text
// normalizedAppleArtworkUrl — Apple's mzstatic "thumb" CDN serves cover art at
// .../<file>.<ext>/<W>x<H>bb.<ext2>. Two real problems collapse into one fix here:
//
//  1. Apple's own top-ebooks RSS feed (BiblioProviders::parseAppleRss) ships im:image
//     labels shaped "0x<N>bb.png" — an auto-width placeholder the "bb" (bounding-box)
//     resize style cannot actually produce. Verified live against production, 2026-08-06:
//     requesting that exact URL returns HTTP 400 from Apple's CDN with the body
//     {"errorMessage":"Cannot produce 0x170 image with Resize Style: 'bb'"} — every
//     height variant (55/60/170) fails identically, so this is not a size problem, the
//     whole "0xN" shape is dead on arrival. This is why most Biblio Discover covers
//     rendered as blank placeholders rather than blurry thumbnails.
//  2. Apple's Search API (BiblioProviders::parseAppleSearch, artworkUrl100/60) returns
//     VALID but small (60-100px) two-dimension URLs on the same CDN endpoint, which
//     read blurry once enlarged into a grid card.
//
// Both are the same URL family, so one rewrite fixes both: replace whatever trailing
// size segment is present with a fixed, verified-working WxH. 600x600bb.jpg was chosen
// (not a larger size) because it comfortably covers a 148px logical gallery card even
// at 3x device pixel ratio (~444px) without paying for detail nothing on screen can
// show — confirmed at 92KB per cover vs ~350KB at 1400x1400 for the same asset.
//
// Fail-safe: a URL that doesn't match the expected trailing "/WxHbb.ext" shape (Open
// Library covers, a future Apple CDN change, anything malformed in an unrecognized way)
// is returned completely untouched — this function only ever narrows a known, verified
// pattern, it never guesses at or invents a provider URL shape.
```

<a id="file-native-engine-bibliocanonicalizer-cpp"></a>
## `native/engine/BiblioCanonicalizer.cpp`

- Status: **CURRENT**
- Accepted blob: `792875942e5c40860d300bdcaf8205fc9944d45a`
- Current blob: `792875942e5c40860d300bdcaf8205fc9944d45a`
- Source: [`native/engine/BiblioCanonicalizer.cpp`](../../native/engine/BiblioCanonicalizer.cpp)

```text
// Layered identity resolution (spec 6.2). Records are unioned when they share a
// strong identity key — an Open Library work key, an ISBN, or a normalized
// title+author (never title alone). Each resulting group becomes one canonical
// work; ordinary format variants nest as editions. Ownership (spec 6.1) is
// honored when a field is filled: Apple governs rating/chart/artwork, Open
// Library governs work identity + earliest-publication evidence, and every field
// keeps the source/sourceId/observedAt it came from.
```

<a id="file-native-engine-bibliocanonicalizer-h"></a>
## `native/engine/BiblioCanonicalizer.h`

- Status: **CURRENT**
- Accepted blob: `949624e7cf3fa4d85ab86672c42dbc6a1c036eea`
- Current blob: `949624e7cf3fa4d85ab86672c42dbc6a1c036eea`
- Source: [`native/engine/BiblioCanonicalizer.h`](../../native/engine/BiblioCanonicalizer.h)

```text
// Canonicalization (spec 6.2): reconcile per-source BiblioSourceRecord evidence
// into canonical BiblioWork values with nested editions and per-field
// provenance.
//
// Identity is resolved by LAYERED evidence, strongest first:
//   1. Open Library work key,
//   2. ISBN / other authority identifiers,
//   3. normalized title + author,
//   4. original & edition publication dates,
//   5. language & translator evidence,
//   6. publisher & edition notes.
// Title-only equality can NEVER silently merge two works: the same title by two
// different authors stays two works. Ordinary format changes (ebook/print/audio)
// remain editions nested under ONE canonical work; a materially different
// translation keeps its own edition metadata but still routes through the
// canonical work with the original language retained.
//
// Source responsibilities are honored on merge (spec 6.1): Open Library governs
// work identity and earliest-publication evidence; Apple governs the Apple
// chart, Apple ratings, and Apple artwork / current storefront activity. Every
// merged field keeps the source, sourceId and observedAt it came from.

// Provenance for one canonical field: which source set it, that source's record
// id, and when it was observed.
```

<a id="file-native-engine-bibliocatalog-cpp"></a>
## `native/engine/BiblioCatalog.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f49057b1f9c36bb13313395cdc391185c55e53ab`
- Current blob: `f49057b1f9c36bb13313395cdc391185c55e53ab`
- Source: [`native/engine/BiblioCatalog.cpp`](../../native/engine/BiblioCatalog.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-bibliocatalog-h"></a>
## `native/engine/BiblioCatalog.h`

- Status: **CURRENT**
- Accepted blob: `875d6dd3576051c0dc835db3572dceb79dbce9a7`
- Current blob: `875d6dd3576051c0dc835db3572dceb79dbce9a7`
- Source: [`native/engine/BiblioCatalog.h`](../../native/engine/BiblioCatalog.h)

```text
// BiblioCatalog — the QML-facing daily keyless refresh coordinator for the
// Biblio Discover / Explore catalogue (spec 2026-08-01, plan 2026-08-03 Task
// 4). It owns the writable BiblioCatalogStore snapshot, fetches Apple Books
// RSS + Apple Search + Open Library records through an injectable transport
// seam, canonicalizes and ranks them (BiblioCanonicalizer / BiblioRanking),
// and publishes a validated snapshot at most once per local day. QML never
// computes rankings or talks to the network directly — it only reads the
// properties/invokables below.
//
// Threading: everything here runs on the GUI thread, event-driven (no worker
// threads). Requests are bounded to kMaxConcurrent in flight at a time;
// transient failures (network error, 429, 5xx) retry with a bounded linear
// backoff (kMaxRetries); a forced or day-due refresh that starts while a
// prior one is still running cancels that prior generation's outstanding
// requests first so results never straddle two refreshes.
//
// Failure posture (spec §8): publish() only ever runs against a COMPLETE
// normalized snapshot, and only once at least one provider produced usable
// records. A refresh that retrieves nothing (or whose snapshot fails
// BiblioCatalogStore::publish's own validation) leaves the previously
// published snapshot fully intact — `ready` only ever goes true→false when
// there was never a successful publish in the first place.
```

<a id="file-native-engine-bibliocatalogstore-cpp"></a>
## `native/engine/BiblioCatalogStore.cpp`

- Status: **CURRENT**
- Accepted blob: `0c753e22de3a9d8787ffd9a45512f5e0c2a1d671`
- Current blob: `0c753e22de3a9d8787ffd9a45512f5e0c2a1d671`
- Source: [`native/engine/BiblioCatalogStore.cpp`](../../native/engine/BiblioCatalogStore.cpp)

```text
// BiblioCatalogStore — implementation (spec 2026-08-01 §8, plan 2026-08-03 T3).
//
// Atomicity strategy: every data row carries the snapshot_id it belongs to.
// publish() opens ONE transaction, inserts a fresh snapshot_id row, writes all
// works/editions/sources/facets/rankings/history scoped to that id, validates
// invariants (unique canonical ids, controlled facet keys, FK integrity via the
// scoped staging set), and finally flips sync_meta.active_snapshot_id to the new
// id and prunes the previous snapshot's rows + ranking-history overflow. Any
// validation error -> rollback, prior active snapshot untouched (spec §8).
//
// Reads scope every query by the active_snapshot_id in sync_meta, so a reader
// never sees a half-published snapshot. Catalogue ids and facet axes are
// allowlisted; facetKey is always bound. The page() shape mirrors
// ComicsCatalog::discoverPage exactly so the QML adapter is shared.
```

<a id="file-native-engine-bibliocatalogstore-h"></a>
## `native/engine/BiblioCatalogStore.h`

- Status: **CURRENT**
- Accepted blob: `5b53591e663eda9f06adbcd6a6da085aab604aa2`
- Current blob: `5b53591e663eda9f06adbcd6a6da085aab604aa2`
- Source: [`native/engine/BiblioCatalogStore.h`](../../native/engine/BiblioCatalogStore.h)

```text
// BiblioCatalogStore — atomic SQLite snapshot store for the Biblio Discover /
// Explore catalogue (spec 2026-08-01 §8, plan 2026-08-03 Task 3).
//
// The store is the durable cache behind the daily refresh pipeline. Task 4's
// catalogue service produces a complete, validated candidate snapshot and hands
// it to publish(); this class writes it into staging tables inside ONE
// transaction, re-validates referential integrity and controlled-vocabulary
// facets, and atomically swaps the active snapshot id. On any validation
// failure it rolls back and leaves the prior active snapshot fully intact, so a
// partial or failed refresh NEVER replaces the last valid cache (spec §8).
//
// Reads are synchronous point queries (the catalogue is small and local): page,
// filterGroups, previewRows, top10. Catalogue ids and facet axes are
// ALLOWLISTED and all caller-supplied strings (catalogId, facetAxis, facetKey)
// are BOUND — never concatenated into SQL — mirroring ComicsCatalog::discoverPage.
// Paging matches that exact {items,nextOffset,exhausted,freshness,warning}
// contract so the QML adapter is identical across worlds.
```

<a id="file-native-engine-bibliocatalogtypes-h"></a>
## `native/engine/BiblioCatalogTypes.h`

- Status: **CURRENT**
- Accepted blob: `7fb48dd07f5bf68257634e0c9080f13a514150b9`
- Current blob: `7fb48dd07f5bf68257634e0c9080f13a514150b9`
- Source: [`native/engine/BiblioCatalogTypes.h`](../../native/engine/BiblioCatalogTypes.h)

```text
// Pure value types for the native BiblioCatalog engine (Discover / Explore).
//
// Task 1 foundation only: no networking, no SQL, no QML. These are plain,
// deterministic value structs that the controlled-vocabulary taxonomy mapper
// (BiblioTaxonomy) and the ranking functions (BiblioRanking) operate on. Later
// tasks add providers, a canonicalizer, a SQLite store and a QML-facing service
// on top of these types — QML must never compute rankings itself.

// Average rating + how many ratings stand behind it. Both signals are needed so
// Top Rated can weigh confidence, not just the raw average.
```

<a id="file-native-engine-biblioproviders-cpp"></a>
## `native/engine/BiblioProviders.cpp`

- Status: **CURRENT**
- Accepted blob: `e67341c8eaebdedd9f69453566d81c98c5825748`
- Current blob: `e67341c8eaebdedd9f69453566d81c98c5825748`
- Source: [`native/engine/BiblioProviders.cpp`](../../native/engine/BiblioProviders.cpp)

```text
// Keyless Apple Books + Open Library parsing (spec 6.1). Defensive throughout:
// every accessor tolerates a missing/wrong-typed field, and a record with no
// usable title (no identity to canonicalize on) is dropped rather than crashing.
```

<a id="file-native-engine-biblioproviders-h"></a>
## `native/engine/BiblioProviders.h`

- Status: **CURRENT**
- Accepted blob: `fbcef138cd3dc9a5fabff2f483721c31dde9b24c`
- Current blob: `fbcef138cd3dc9a5fabff2f483721c31dde9b24c`
- Source: [`native/engine/BiblioProviders.h`](../../native/engine/BiblioProviders.h)

```text
// Keyless provider parsing (spec 6.1) for the BiblioCatalog Discover/Explore
// engine. Two sources, no API keys, accounts, or tokens:
//
//   * Apple Books — the live ebook chart RSS (chart position, artwork, current
//     edition identity) and the iTunes Search API (ratings, descriptions,
//     genres, release activity, ebook/audiobook forms).
//   * Open Library — the search.json work/edition index (canonical work key,
//     ISBN/authority ids, first-publication year, authors, page counts,
//     language & English-edition evidence, publishers, subjects).
//
// Every parser is DEFENSIVE: missing artwork/rating, HTML descriptions, an RSS
// `entry` that is a single object OR an array, and malformed/partial records
// are all tolerated without crashing. Each parser produces per-source
// BiblioSourceRecord values carrying source/sourceId/observedAt provenance plus
// the raw evidence the canonicalizer (BiblioCanonicalizer) reconciles into
// canonical works. No networking lives here — the URL builders return the
// keyless request URLs a caller's transport fetches.

// One record as observed from a single provider before canonicalization. It
// carries its own provenance (source/sourceId/observedAt) and every field the
// layered identity resolver needs. This is a NEW Task-2 input type; the
// canonical output types (BiblioWork/BiblioEdition) live in BiblioCatalogTypes.h.
```

<a id="file-native-engine-biblioranking-cpp"></a>
## `native/engine/BiblioRanking.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `016b935ba44059563bb80e1f8a5009b35b9696c1`
- Current blob: `016b935ba44059563bb80e1f8a5009b35b9696c1`
- Source: [`native/engine/BiblioRanking.cpp`](../../native/engine/BiblioRanking.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-biblioranking-h"></a>
## `native/engine/BiblioRanking.h`

- Status: **CURRENT**
- Accepted blob: `7d80418072e6ec446ecda70192ac44c3af714bdc`
- Current blob: `7d80418072e6ec446ecda70192ac44c3af714bdc`
- Source: [`native/engine/BiblioRanking.h`](../../native/engine/BiblioRanking.h)

```text
// Deterministic, pure ranking functions for the Discover shelves (spec 7).
//
// These functions never consider acquisition availability, source-health,
// public-domain status, ownership, or local reading activity — those are out of
// scope for ranking by design. Missing evidence drops only the affected signal,
// never the work from an unrelated shelf. Everything is a pure function of the
// inputs so the ranking is reproducible and testable.
```

<a id="file-native-engine-bibliotaxonomy-cpp"></a>
## `native/engine/BiblioTaxonomy.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `b0e161e716530ceca7e42d6aa93148d27a443ae1`
- Current blob: `b0e161e716530ceca7e42d6aa93148d27a443ae1`
- Source: [`native/engine/BiblioTaxonomy.cpp`](../../native/engine/BiblioTaxonomy.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-bibliotaxonomy-h"></a>
## `native/engine/BiblioTaxonomy.h`

- Status: **CURRENT**
- Accepted blob: `9abfcc94ddd01e13f3a6149c5bc645d151a2af82`
- Current blob: `9abfcc94ddd01e13f3a6149c5bc645d151a2af82`
- Source: [`native/engine/BiblioTaxonomy.h`](../../native/engine/BiblioTaxonomy.h)

```text
// Versioned controlled-vocabulary mapper (spec 6.3) plus the pure facet-key
// helpers behind the Discover / Explore filters (spec 4.3).
//
// Every key produced here is a stable lowercase identifier. The mapper merges
// synonyms, case, spelling and singular/plural variants, folds imprints under
// their parent publisher, and NEVER turns an unknown provider string into a
// visible filter. The curated tables live in the .cpp so they are checked-in
// and testable — the harness is the oracle for these tables.
```

<a id="file-native-engine-bookdownloader-cpp"></a>
## `native/engine/BookDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `18a8253f06276dc9981e2b77a5317156d16f0d97`
- Current blob: `18a8253f06276dc9981e2b77a5317156d16f0d97`
- Source: [`native/engine/BookDownloader.cpp`](../../native/engine/BookDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-bookdownloader-h"></a>
## `native/engine/BookDownloader.h`

- Status: **CURRENT**
- Accepted blob: `c4ab5ddc8cce2dbf4aadf906e2f0b5d3350fe430`
- Current blob: `c4ab5ddc8cce2dbf4aadf906e2f0b5d3350fe430`
- Source: [`native/engine/BookDownloader.h`](../../native/engine/BookDownloader.h)

```text
// BookDownloader.h
//
// The book half of the download-fed backbone: reading is NEVER a live stream.
// A book is downloaded once — its .epub/.pdf lands as a loose file on disk —
// and the reader opens that local file, offline, forever. This ports Tankoban 2's
// proven BookDownloader (HTTP / LibGen path) into Colosseum-lean form: the
// irreducible core is kept; TB2's magnet/libtorrent transport, MD5-of-bytes
// verification, and cross-mirror failover beyond LibGen are dropped (Colosseum
// has no TorrentClient — its books come from LibGen over HTTP).
//
// Pipeline (mirrors TB2 BookDownloader + LibGenScraper::resolveDownload):
//   1. resolve: GET libgen.li/ads.php?md5=<md5> → parse <a href="get.php?...key=Y">
//      → the ephemeral direct-file URL(s). The key rotates ~60s, so resolve is
//      done immediately before streaming (fresh key = the safe pattern).
//   2. stream: GET the direct URL → write <dir>/<name>.part in chunks (readyRead,
//      NEVER readAll — books can be 100s of MB), stale-key detection on the first
//      chunk (text/html ⇒ key rotated ⇒ failover to next URL), retry 2/4/8s,
//      then atomic .part → final rename.
//   3. index: persist {md5 → path, title, bytes, addedAt} to index.json.
//   4. reader calls localBook(md5) → the on-disk file path, or "" (UI then shows
//      "go download it" — it NEVER falls back to streaming).
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/books/<name>.epub ...
//   <appdata>/books/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.
```

<a id="file-native-net-biblioimagediag-cpp"></a>
## `native/net/BiblioImageDiag.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `3e7c66246f323707dbf661a076962438b927f9dd`
- Current blob: `3e7c66246f323707dbf661a076962438b927f9dd`
- Source: [`native/net/BiblioImageDiag.cpp`](../../native/net/BiblioImageDiag.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-net-biblioimagediag-h"></a>
## `native/net/BiblioImageDiag.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `8d4d1f171e68868e51abfab805f7a0698a3fa6e6`
- Current blob: `8d4d1f171e68868e51abfab805f7a0698a3fa6e6`
- Source: [`native/net/BiblioImageDiag.h`](../../native/net/BiblioImageDiag.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-reader-bookstores-cpp"></a>
## `native/reader/BookStores.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `b466a45874ca88977c4b196ac25c8797bb9ae2d7`
- Current blob: `b466a45874ca88977c4b196ac25c8797bb9ae2d7`
- Source: [`native/reader/BookStores.cpp`](../../native/reader/BookStores.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-reader-bookstores-h"></a>
## `native/reader/BookStores.h`

- Status: **CURRENT**
- Accepted blob: `304f3203fc994657824afe92d63f265268f8943a`
- Current blob: `304f3203fc994657824afe92d63f265268f8943a`
- Source: [`native/reader/BookStores.h`](../../native/reader/BookStores.h)

```text
// BookStores.h
//
// Shared JSON store helpers, lifted out of BookBridge.cpp (the OLD reader's
// persistence) so the NEW reader (reader2) can read/write the EXACT SAME files
// under <appdata>/book_reader/ — progress.json, settings.json, bookmarks.json,
// annotations.json, display_names.json — with zero migration. Both readers call
// into this namespace; neither owns its own copy of the store logic.
//
// The directory resolves via QStandardPaths::writableLocation(AppDataLocation)
// (+ "/book_reader"), same as before. Under QStandardPaths::setTestModeEnabled(true)
// (set by test harnesses) that location is automatically redirected to a sandbox,
// so tests never touch a real user's stores.
```

<a id="file-native-reader2-reader2bridge-cpp"></a>
## `native/reader2/Reader2Bridge.cpp`

- Status: **CURRENT**
- Accepted blob: `5a1d1f0735f54345bf4016819568da2e24b8d072`
- Current blob: `5a1d1f0735f54345bf4016819568da2e24b8d072`
- Source: [`native/reader2/Reader2Bridge.cpp`](../../native/reader2/Reader2Bridge.cpp)

```text
// Normalize a book path to a stable identity for authorization compares: strip a file:///
// prefix, resolve to the canonical on-disk path when the file exists (collapses '..', native
// separators, and — on Windows — the real casing), else fall back to a cleaned path. Both the
// authorized book and every filesRead request pass through this, so a rigged book cannot slip a
// different file past the check with an alternate spelling of the same-or-other path.
```

<a id="file-native-reader2-reader2bridge-h"></a>
## `native/reader2/Reader2Bridge.h`

- Status: **CURRENT**
- Accepted blob: `9cb33f7498e54cc3d54d8086befd27d498aff125`
- Current blob: `9cb33f7498e54cc3d54d8086befd27d498aff125`
- Source: [`native/reader2/Reader2Bridge.h`](../../native/reader2/Reader2Bridge.h)

```text
// Reader2Bridge.h
//
// The fresh reader's native seam (TASK 4). Exposed to QML as context property
// "Reader2Bridge"; the paper's QWebChannel gets ONLY the nested Reader2PaperGate
// (registered as "bridge" by Paper.qml — least privilege, see the gate class
// below): the paper pulls book bytes (base64) and pushes events up through the
// gate; QML reads/writes the shared stores through the full bridge and receives
// the same paperEventReceived signal the paper's events raise. Networking
// (dictionary lookups) lives here, never in the paper's JS — house rule "QML
// paints, C++ decides": no raw XHR on the paper's web-content thread.
//
// Store methods delegate to BookStores (native/reader/BookStores.h) — the
// SAME files the OLD reader's BookBridge uses under
// <AppDataLocation>/book_reader/ (progress.json, settings.json,
// bookmarks.json, annotations.json) — so both readers share state
// byte-identically with zero migration.
//
// Slim on purpose: no audiobook/window-chrome methods here. Those stay on the
// old BookBridge until swap day (Task 16).
```

<a id="file-native-reader2-reader2-harness-main-cpp"></a>
## `native/reader2/reader2_harness_main.cpp`

- Status: **CURRENT**
- Accepted blob: `10ef8c41d731f3248413d3d9c440f72e2c758b69`
- Current blob: `10ef8c41d731f3248413d3d9c440f72e2c758b69`
- Source: [`native/reader2/reader2_harness_main.cpp`](../../native/reader2/reader2_harness_main.cpp)

```text
// reader2_harness_main.cpp — TASK 5: the standalone "first pixels" harness.
//
// Boots straight into the fresh reader (native QML chrome over the vendored Anx
// foliate "paper") with a book shelf — NO Colosseum shell. Click a book on the
// shelf and it renders in the paper; arrow keys turn pages; Esc returns to the
// shelf. This exe exists to prove the paper loads and reports position under a
// real Qt viewport, ahead of the swap into Biblio (Task 16).
//
// Stores are SANDBOXED by default (QStandardPaths test mode) so a harness run
// never mutates the real reader's progress/settings/bookmarks/annotations; pass
// --real-stores to read/write the live files instead.
//
// The book SHELF, however, always reads the REAL downloaded-books folder
// (<AppData>/Brotherhood/Colosseum/books — where BookDownloader lands .epub/.pdf,
// see native/engine/BookDownloader.cpp baseDir()). We compute that path with the
// shell's real identity BEFORE enabling test mode, then hand it to QML as the
// context property `booksDir`. Sandboxing only redirects the stores, not the shelf.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-native-torrent-booktorrentdownloader-cpp"></a>
## `native/torrent/BookTorrentDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `2a67f9896c5f899b79f2a877b9e0c35625ba982c`
- Current blob: `2a67f9896c5f899b79f2a877b9e0c35625ba982c`
- Source: [`native/torrent/BookTorrentDownloader.cpp`](../../native/torrent/BookTorrentDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrentdownloader-h"></a>
## `native/torrent/BookTorrentDownloader.h`

- Status: **CURRENT**
- Accepted blob: `ba4e2e13104d202c73bdb5f0622174d4fc836a07`
- Current blob: `ba4e2e13104d202c73bdb5f0622174d4fc836a07`
- Source: [`native/torrent/BookTorrentDownloader.h`](../../native/torrent/BookTorrentDownloader.h)

```text
// BookTorrentDownloader.h
//
// Engine-direct transport (Phase 2): pulls the SINGLE best ebook file
// (BookTorrentFilePicker) through the imported libtorrent TorrentEngine —
// addMagnet -> metadataReady -> setFilePriorities -> torrentFinished -> finalize
// from disk. Keys everything by infoHash; concurrent (QHash of jobs, each its own Job).
//
// On-disk: <appdata>/books-torrent/<infoHash>/<torrent-relative path> + .../index.json
// (by infoHash). Multi-file packs keep their torrent-relative subfolders under the hash dir.
```

<a id="file-native-torrent-booktorrentfilepicker-cpp"></a>
## `native/torrent/BookTorrentFilePicker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `86303f2a280d39aeaaa2ac50a84ec4bd178a03a6`
- Current blob: `86303f2a280d39aeaaa2ac50a84ec4bd178a03a6`
- Source: [`native/torrent/BookTorrentFilePicker.cpp`](../../native/torrent/BookTorrentFilePicker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrentfilepicker-h"></a>
## `native/torrent/BookTorrentFilePicker.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `021a08377a8be90a182dd613f2f512a420372620`
- Current blob: `021a08377a8be90a182dd613f2f512a420372620`
- Source: [`native/torrent/BookTorrentFilePicker.h`](../../native/torrent/BookTorrentFilePicker.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrentmagnet-h"></a>
## `native/torrent/BookTorrentMagnet.h`

- Status: **CURRENT**
- Accepted blob: `a8711e6fe9cab043f6f7cbcbd237da680aa92626`
- Current blob: `a8711e6fe9cab043f6f7cbcbd237da680aa92626`
- Source: [`native/torrent/BookTorrentMagnet.h`](../../native/torrent/BookTorrentMagnet.h)

```text
// Pure helpers for the engine-direct book torrent transport (Phase 2). No Qt
// GUI, no engine, no network — unit-tested by book_torrent_magnet_harness.
```

<a id="file-native-torrent-booktorrentranker-cpp"></a>
## `native/torrent/BookTorrentRanker.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `0f98e1a96db561601ad0c28a7b6590f5599d702c`
- Current blob: `0f98e1a96db561601ad0c28a7b6590f5599d702c`
- Source: [`native/torrent/BookTorrentRanker.cpp`](../../native/torrent/BookTorrentRanker.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrentranker-h"></a>
## `native/torrent/BookTorrentRanker.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `85fe789d5563c63a18d7780553b2d4a7b4cb9ec3`
- Current blob: `85fe789d5563c63a18d7780553b2d4a7b4cb9ec3`
- Source: [`native/torrent/BookTorrentRanker.h`](../../native/torrent/BookTorrentRanker.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrents-cpp"></a>
## `native/torrent/BookTorrents.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `9a90c991145f639ccc544827b23a0fd7b700ffba`
- Current blob: `9a90c991145f639ccc544827b23a0fd7b700ffba`
- Source: [`native/torrent/BookTorrents.cpp`](../../native/torrent/BookTorrents.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-torrent-booktorrents-h"></a>
## `native/torrent/BookTorrents.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `0ce3b52fa6d4f49882322e567e2b72ca16afeb2e`
- Current blob: `0ce3b52fa6d4f49882322e567e2b72ca16afeb2e`
- Source: [`native/torrent/BookTorrents.h`](../../native/torrent/BookTorrents.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-qml-bibliobook-qml"></a>
## `qml/BiblioBook.qml`

- Status: **CURRENT**
- Accepted blob: `a4f6110f49292b7239ce59d1f10d1cd72b6e17ec`
- Current blob: `a4f6110f49292b7239ce59d1f10d1cd72b6e17ec`
- Source: [`qml/BiblioBook.qml`](../../qml/BiblioBook.qml)

```text
// BiblioBook — the book "dust-jacket" detail page. Owner: A2. OUR OWN design (NOT the manga series
// view): the cover as a physical object · the tagline as the hero · a drop-capped synopsis · an
// "Editions" panel. Opens as a layer over the Biblio world (Main.qml bookLayer). `book` is a full
// Apple object from BiblioApi.fullBook.
//
// The Editions rows are a STUB until the libgen "delivery" layer is ported (TB2 had it; Colosseum
// doesn't yet). Metadata + layout are real; the download list is a preview.
```

<a id="file-qml-bibliobookrail-qml"></a>
## `qml/BiblioBookRail.qml`

- Status: **CURRENT**
- Accepted blob: `0f45d27781a9512f4857902ebdeee1ad80934c6c`
- Current blob: `0f45d27781a9512f4857902ebdeee1ad80934c6c`
- Source: [`qml/BiblioBookRail.qml`](../../qml/BiblioBookRail.qml)

```text
// BiblioBookRail — the book-specific horizontal shelf for the Biblio Explore page (plan
// `2026-08-01-biblio-discover-explore.md`, Task 7). Mirrors TheatreCatalogRow's shape (a
// Column: header + Flickable Row of cards) but owns book-specific card behaviour that the
// shared CataloguePosterCard does not: AUTHOR always visible at rest (no hover needed), and
// rating/source revealed on POINTER HOVER *OR* KEYBOARD FOCUS (CataloguePosterCard's rating is
// pointer-hover-only by contract — Biblio's is deliberately broader per this task's spec).
// "Ranked" mode (Top 10) shows ONLY the 1-10 numeral badge — no star/rating glyph competing
// with it. NO row blurb is ever rendered here — title only, per the plan's global constraint.
//
// Test hooks mirror CataloguePosterCard's `testHovered` convention: since a rail holds many
// cards, hover/focus are simulated per-index from the rail (`testHoveredIndex`, `keyboardMode`
// + `currentIndex`) rather than a single bool, so an offscreen harness can pin reveal-gating
// without a real pointer.
```

<a id="file-qml-bibliodiscoverpage-qml"></a>
## `qml/BiblioDiscoverPage.qml`

- Status: **CURRENT**
- Accepted blob: `68e26f43744e5faef36a6629e14a789ba1752a5c`
- Current blob: `68e26f43744e5faef36a6629e14a789ba1752a5c`
- Source: [`qml/BiblioDiscoverPage.qml`](../../qml/BiblioDiscoverPage.qml)

```text
// BiblioDiscoverPage — the BIBLIO wrapper around the shared Discover shell (Task 5, arc 2026-08-01).
//
// The browsing surface lives in the world-neutral DiscoverBrowser.qml. This file is the thin
// Biblio-side wrapper: it builds the Biblio adapter (BiblioDiscoverApi.js) from injected
// dependencies (the native BiblioCatalog context object, the extension registry, and the global
// Explicit Content preference), binds it to the shell, and re-emits a normalized card as
// itemOpenRequested for the book detail route. It owns NO acquisition and NO detail-page UI.
//
// Dependencies are PROPERTIES (not context reads) because the adapter factory needs a real
// BiblioCatalog-shaped object and the page harness builds this bare — a missing biblioCatalog is
// null-safe (the adapter returns an empty built-in wall and the shell shows the empty state).
// showExplicit is INJECTED, never read from Main's private contentPreferences id — whoever loads
// this page (a later task) binds it in from the outside, same as Tankoban's page does for
// showExplicitContent.
//
// applyPin(pin, returnToExplore) opens a built-in/extension catalogue exactly like every other
// world's applyPin, PLUS remembers whether the caller wants an Explore-return affordance. When
// armed, the shell's generic backRequested() (a plain "user pressed back" signal the shell itself
// never acts on) is re-emitted here as the Biblio-specific exploreReturnRequested() — the shell
// has no idea what "Explore" is; this wrapper is the one that does.
//
// Public surface for BiblioWorld + the page harness: applyPin(pin, returnToExplore),
// itemOpenRequested(item), exploreReturnRequested(), currentType, keyboardMode, catalogMenuOpen,
// catalogMenuModel, items, loading.
```

<a id="file-qml-biblioexplorepage-qml"></a>
## `qml/BiblioExplorePage.qml`

- Status: **CURRENT**
- Accepted blob: `203d77682f9e118d8289dc0e0e851634b531fd87`
- Current blob: `203d77682f9e118d8289dc0e0e851634b531fd87`
- Source: [`qml/BiblioExplorePage.qml`](../../qml/BiblioExplorePage.qml)

```text
// BiblioExplorePage — the deep Biblio "Explore" shelf page (plan
// `2026-08-01-biblio-discover-explore.md`, Task 7). Top 10 first, extension preview rows
// second, the four house rails (Popular/Top Rated/New Releases/Trending) next in fixed order,
// then the three fixed Fiction/Nonfiction/Audience mosaics LAST, always — the mosaics are never
// part of shelf customization (BiblioExploreRules never emits a mosaic key at all). Everything
// else (top-10 + extensions + house rails) is freely reorderable/hideable via
// BiblioExploreRules.applyCustomization + BiblioExplorePreferences, exactly like Task 6 proved.
//
// Injectable seams (mirrors this codebase's existing test-seam convention — BiblioCatalog's own
// IBiblioTransport, Task 5's injected catalogue/extension fakes): `catalogSource` stands in for
// the native `BiblioCatalog` context property, `extensionsSource` for the native `Extensions`
// context property, and `pageFetcher` for `DiscoverApi.loadPage` — an offscreen harness can
// construct this page with fakes for all three and never touch real native objects or the
// network. `preferences` is injectable the same way BiblioExplorePreferences itself supports an
// injected `settingsLocation`.
//
// "Top 10" reads BiblioCatalogStore's OWN documented equivalence (BiblioCatalogStore::top10's
// header comment: "rank-1..limit rows for the Popular catalogue") as `discoverPage("popular", ...,
// 0, top10Limit)` in ranked mode — a separate, smaller, numeral-badged slice of the exact same
// Popular ranking the "Popular" house rail also renders unranked at a larger count. Its See-All
// pin therefore resolves to the same Popular catalogue.
```

<a id="file-qml-biblioexplorepreferences-qml"></a>
## `qml/BiblioExplorePreferences.qml`

- Status: **CURRENT**
- Accepted blob: `22522624b2f10787ba2da017118370747d2a60b6`
- Current blob: `22522624b2f10787ba2da017118370747d2a60b6`
- Source: [`qml/BiblioExplorePreferences.qml`](../../qml/BiblioExplorePreferences.qml)

```text
// BiblioExplorePreferences — persisted shelf order/visibility for the Biblio Explore page
// (plan `2026-08-01-biblio-discover-explore.md`, Task 6). QtCore Settings under
// [biblioExplore] holds TWO compact JSON strings (order, hidden) — no `renamed` field, unlike
// Theatre's row preferences: Biblio shelves are not user-renamable per this task's interface.
// Reordering and hiding are keyed by STABLE row keys (see BiblioExploreRules.js), never
// display titles. `move(key, toIndex)` takes an ABSOLUTE index (not Theatre's relative delta)
// so keyboard reordering (index +/- 1) and pointer drag (an arbitrary drop index) both go
// through the same function; both are boundary-safe/clamped and silent on a genuine no-op.
// Production leaves settingsLocation unset (real QSettings store); the harness injects a
// temp INI url.
```

<a id="file-qml-bibliogenreindex-qml"></a>
## `qml/BiblioGenreIndex.qml`

- Status: **CURRENT**
- Accepted blob: `11f2975c241d5e532b9234ec334ed22c0404b450`
- Current blob: `11f2975c241d5e532b9234ec334ed22c0404b450`
- Source: [`qml/BiblioGenreIndex.qml`](../../qml/BiblioGenreIndex.qml)

```text
// BiblioGenreIndex — the "Explore" genre directory for the BIBLIO / books lane. Same MAL-index
// treatment as the manga and Theatre directories, fed entirely from Catalog.biblioGenres — the
// baked per-genre covers A2 already ships (download-once doctrine), so the page costs zero
// network. One flat section: Apple Books' genre space has no MAL-style groupings.
// A tile emits genrePicked(name); the host opens BiblioGenrePage over this index.
```

<a id="file-qml-bibliogenrepage-qml"></a>
## `qml/BiblioGenrePage.qml`

- Status: **CURRENT**
- Accepted blob: `6a36fcc85be68b2968247d7060148ca44b451f21`
- Current blob: `6a36fcc85be68b2968247d7060148ca44b451f21`
- Source: [`qml/BiblioGenrePage.qml`](../../qml/BiblioGenrePage.qml)

```text
// BiblioGenrePage — the genre BROWSE page for the Biblio / books lane.
// Faithful clone of GenrePage.qml's manga layout, wired to Apple Books and BiblioBook.qml.
//
// PROTOTYPE harness:  qml.exe qml/_genrecheck.qml   (loads this page with a live genre)
//
// Signature: the genre is its OWN art — its top covers wash behind the title (GenreMosaic doctrine),
// and the rank ordinal encodes the by-readers popularity sort (real info, not decoration).
```

<a id="file-qml-bibliolibrarypage-qml"></a>
## `qml/BiblioLibraryPage.qml`

- Status: **CURRENT**
- Accepted blob: `ec5217517fb0d9cf165ba9dc546288f22b228f7d`
- Current blob: `ec5217517fb0d9cf165ba9dc546288f22b228f7d`
- Source: [`qml/BiblioLibraryPage.qml`](../../qml/BiblioLibraryPage.qml)

```text
// BiblioLibraryPage — Biblio's Library tab (the Theatre-parity Library page, plan
// 2026-08-06-biblio-library-tab-theatre-parity.md, Slice 2). The book-domain mirror of
// LibraryPage.qml MINUS Theatre's video concepts: no watched/airing/finale/new-episode
// ledger — just search, a small filter (All | In Progress), sort (Recently added | Last read |
// A–Z), a wall of saved Collection entries, empty/no-match states, and a per-card ⋮ menu.
//
// One Collection entry → one card. Conservative Progress matching: a reliable match enables
// Resume; no match → the card's primary action is Details. Remove affects Collection
// membership only (never Progress, never files) — handled inline by the owner (BiblioWorld),
// not here. All derivations are BiblioLibraryApi (headless-proven in Slice 1); this page only
// paints and wires. Context properties (Collection/Progress) are typeof-guarded so it
// constructs offscreen for the harness.
```

<a id="file-qml-bibliosearch-qml"></a>
## `qml/BiblioSearch.qml`

- Status: **CURRENT**
- Accepted blob: `a88def36cfc2968ae418b37b23a81777dea21ac5`
- Current blob: `a88def36cfc2968ae418b37b23a81777dea21ac5`
- Source: [`qml/BiblioSearch.qml`](../../qml/BiblioSearch.qml)

```text
// BiblioSearch — Biblio's search overlay. Owner: A2. Harbor-adapted (function/feel, not a clone):
// the field leads (no chrome bar, Esc closes), the best hit blooms into a Top Match "mini dust-jacket",
// the rest fall into a cover grid, and an empty state offers Recent · Jump to · Browse-a-genre.
// Live as you type (180ms). Apple Books is the source; clicking any result opens its BiblioBook detail.
```

<a id="file-qml-biblioworld-qml"></a>
## `qml/BiblioWorld.qml`

- Status: **CURRENT**
- Accepted blob: `df77561b8095a82548e19a2f8fe4b2f50da4404a`
- Current blob: `df77561b8095a82548e19a2f8fe4b2f50da4404a`
- Source: [`qml/BiblioWorld.qml`](../../qml/BiblioWorld.qml)

```text
// BiblioWorld - the Colosseum world page for books. Owner: A2.
// Same spine as Tankoban/Theatre: Featured carousel + Continue rows stay shared ABOVE a
// Discover | Explore tab split (plan `2026-08-01-biblio-discover-explore.md`, Task 8) — the
// integration point where the native BiblioCatalog service (Task 4), BiblioDiscoverPage (Task 5),
// and BiblioExplorePage (Task 7) actually get wired into the app.
//
// Discover is the utilitarian catalogue grid (BiblioDiscoverPage -> the shared DiscoverBrowser
// shell) with no shelves of its own. Explore is the deep shelf page (Top 10 + house rails +
// extension previews + the three fixed mosaics). BOTH tabs are declared directly (mirrors
// TankobanDiscoverPage / TheatreCatalogPage) rather than Loader-swapped, so a tab switch never
// destroys either page — Explore's Flickable keeps its own scroll position alive the whole time,
// hidden or not, with zero extra restore plumbing (verified in tests/biblio_world_harness.qml).
//
// GenreMosaic/BiblioGenrePage/BiblioGenreIndex are RETIRED from this world (their native-backed
// replacement is Explore's three fixed mosaics) but left in place elsewhere for compatibility
// until a later cleanup — this file just stops reaching them.
//
// Delivery (search + download) stays libgen from TB2 - a separate layer, like Cinemeta vs the
// Theatre addon. BiblioApi.search/lookupBook/searchAudiobooks/pairing helpers are untouched.
```

<a id="file-qml-reader2-appearancepanel-qml"></a>
## `qml/reader2/AppearancePanel.qml`

- Status: **CURRENT**
- Accepted blob: `27850a7abf9f79742ed0e44b4df2b5925757ca49`
- Current blob: `27850a7abf9f79742ed0e44b4df2b5925757ca49`
- Source: [`qml/reader2/AppearancePanel.qml`](../../qml/reader2/AppearancePanel.qml)

```text
// AppearancePanel.qml — the reader's RIGHT GLASS PANEL (TASK 10): a slide-in column of
// reading controls that LIVE-APPLY to the paper as you touch them — theme swatches,
// typeface cards, a size stepper, line-spacing + margin sliders, a justify segment, and the
// reading-ruler CONTROLS (a toggle + band-height + dim sliders; the ruler's actual visual
// overlay is Task 11). 348px glass over the paper, sliding in from the RIGHT edge with the
// mock's ~.32s cubic. Pixel contract: the chrome mock's `.panel.right`, `.apphead`,
// `.grp`/`.glbl`, `.swatches`/`.swatch`, `.fontrow`/`.fontcard`, `.stepper`, `.sliderrow`,
// `.segment`, `.rulerrow`, `.switch` (agents/colosseum-book-reader-chrome-mock.html).
//
// Like the rest of the reader2 chrome this overlay is BRIDGE-FREE: it takes the CURRENT
// appearance via the `appearance` property and reports every edit up via a single
// changed(key, value) signal. ReaderShell owns the merge + persist (settings.json `reader2`
// sub-object) + the live push to the paper — so this stays instantiable headless (smoke).
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-audioglyph-qml"></a>
## `qml/reader2/AudioGlyph.qml`

- Status: **CURRENT**
- Accepted blob: `3d0452069bd6749f64dd2b5d84e5f70f6a3b9927`
- Current blob: `3d0452069bd6749f64dd2b5d84e5f70f6a3b9927`
- Source: [`qml/reader2/AudioGlyph.qml`](../../qml/reader2/AudioGlyph.qml)

```text
// AudioGlyph.qml — the reader's audiobook-transport glyphs, HAND-DRAWN in QML Canvas.
//
// Ported from the video player's IconGlyph (PlayerPage.qml — A4's "forged-line" family,
// mock-ratified 2026-07-08): one consistent heavy stroke across every glyph, drawn with
// the same coordinate system (fractions of the icon box around its center), so the
// reader's pill and the player's control bar speak one visual language. Hemanth's call
// 2026-07-18: the SVG-file skip icons never rendered right — "use the symbols from the
// video player." Two reader-only kinds (speed gauge, playlist) are drawn in the same
// family rules.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-bottomrail-qml"></a>
## `qml/reader2/BottomRail.qml`

- Status: **CURRENT**
- Accepted blob: `41566587a572dafdb390444a25d2900d1a54b943`
- Current blob: `41566587a572dafdb390444a25d2900d1a54b943`
- Source: [`qml/reader2/BottomRail.qml`](../../qml/reader2/BottomRail.qml)

```text
// BottomRail.qml — the reader's bottom chrome: a thin GOLD progress rail with a knob,
// chapter ticks (Reader2Logic.railTicks), drag-to-scrub, and a two-part meta row
// ("Page N of M in chapter" left / "X% of book" right). A glass "Return to page N"
// ghost chip appears after a jump. Pixel contract: the chrome mock's `.bottombar`,
// `.rail`, `.railmeta`, `.returnchip`. Glass over the paper; fades in with the reveal.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-dictcard-qml"></a>
## `qml/reader2/DictCard.qml`

- Status: **CURRENT**
- Accepted blob: `eb341a9768618a13cc59ecac96d8c7ca71671167`
- Current blob: `eb341a9768618a13cc59ecac96d8c7ca71671167`
- Source: [`qml/reader2/DictCard.qml`](../../qml/reader2/DictCard.qml)

```text
// DictCard.qml — the Define (dictionary) glass card (TASK 9 R2).
//
// Opened from the SelectionMenu's Define action: ReaderShell extracts the first word of the
// selection, calls Reader2Bridge.dictLookup(word) (Wiktionary REST, C++ side — house rule
// "no network on the paper"), and feeds the parsed result here. Word in the display serif
// (Fraunces), definitions in dim UI (Inter); a quiet empty state with an "Open in Wiktionary"
// affordance. Bridge-free like the rest of the chrome — data in via properties, actions out
// via signals — so it instantiates headless (chrome smoke).
//
// Positioned near the selection by the same pure clamp the SelectionMenu uses
// (Reader2Logic.selectionMenuPos); own click-swallow MouseArea (house doctrine); a backdrop
// below it dismisses on tap-outside. Esc is routed by ReaderShell.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-footnotecard-qml"></a>
## `qml/reader2/FootnoteCard.qml`

- Status: **CURRENT**
- Accepted blob: `cd61ee1ed48c0c08aac7fc593fc55bbefb24e0e5`
- Current blob: `cd61ee1ed48c0c08aac7fc593fc55bbefb24e0e5`
- Source: [`qml/reader2/FootnoteCard.qml`](../../qml/reader2/FootnoteCard.qml)

```text
// FootnoteCard.qml — the footnote/endnote peek card (TASK 9 R2).
//
// The glue detects a footnote/noteref link tap in the book iframe, extracts the note's text
// (paper_glue.js FootnoteHandler path), and emits 'footnote' { html, rect }; ReaderShell
// routes it here. We do NOT navigate the page to the note — the reader stays put and the note
// is shown in this glass card near the tap. v1 renders plain text (the glue strips tags).
//
// Serif body: the mock uses Literata, which is NOT bundled (only Fraunces + Inter are loaded
// by Main.qml / the harness), so we render in Theme.display (Fraunces — a real, loaded serif)
// rather than silently falling back to Tahoma. Bridge-free: data in via properties, dismiss
// out via signal; own click-swallow + a backdrop dismiss (house doctrine). Esc via ReaderShell.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-harness-qml"></a>
## `qml/reader2/Harness.qml`

- Status: **CURRENT**
- Accepted blob: `3437090b91797b3144975c524ee617d708a58e34`
- Current blob: `3437090b91797b3144975c524ee617d708a58e34`
- Source: [`qml/reader2/Harness.qml`](../../qml/reader2/Harness.qml)

```text
// Harness.qml — TASK 5 root window. Boots into the book shelf; clicking a book
// hides the shelf and reveals the ReaderShell (the paper). Esc in the shell
// returns to the shelf. No Colosseum chrome — this is the standalone "first
// pixels" harness that proves the paper loads and reports position.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-harnessshelf-qml"></a>
## `qml/reader2/HarnessShelf.qml`

- Status: **CURRENT**
- Accepted blob: `5b8f4f6398eca048278859c78ec8a1b34989845b`
- Current blob: `5b8f4f6398eca048278859c78ec8a1b34989845b`
- Source: [`qml/reader2/HarnessShelf.qml`](../../qml/reader2/HarnessShelf.qml)

```text
// HarnessShelf.qml — the harness book shelf. Lists the REAL downloaded books
// (booksDir context property → <AppData>/Brotherhood/Colosseum/books, read-only)
// via FolderListModel and emits bookChosen(filePath) on click. Not shipped chrome
// — just the entry point that lets a human open a book without a Colosseum shell.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-leftpanel-qml"></a>
## `qml/reader2/LeftPanel.qml`

- Status: **CURRENT**
- Accepted blob: `49957e841abe123d2884846a608a5c1fcbdb421f`
- Current blob: `49957e841abe123d2884846a608a5c1fcbdb421f`
- Source: [`qml/reader2/LeftPanel.qml`](../../qml/reader2/LeftPanel.qml)

```text
// LeftPanel.qml — the reader's LEFT GLASS PANEL (TASK 8): a slide-in tabbed column
// with Contents / Bookmarks / Highlights (+ a DISABLED Audio tab whose real content is
// Task 13). 348px glass over the paper, sliding in from the left edge with the mock's
// ~.32s cubic. Pixel contract: the chrome mock's `.panel.left`, `.tabs`, `.pane`,
// `.chrow`, `.mark`, `.hl` (agents/colosseum-book-reader-chrome-mock.html).
//
// Like TopBar/BottomRail this overlay is BRIDGE-FREE: it takes its data via properties
// and reports back via signals only, so ReaderShell keeps sole ownership of the paper +
// the native stores (bookmarks.json / annotations.json through Reader2Bridge). Row
// SHAPING is pure (Reader2Logic.tocRowState/bookmarkRow/highlightRow) so it renders BOTH
// reader2's write shape AND the old reader's records with zero migration.
//
// Dismissal: a transparent click-catcher over the paper to the RIGHT of the column
// (below the top bar, so the TopBar's right icons stay live) emits closeRequested — the
// familiar "tap the page to close the drawer". The Contents icon toggles it too, and Esc
// closes it (both wired in ReaderChrome / ReaderShell).
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-paper-qml"></a>
## `qml/reader2/Paper.qml`

- Status: **CURRENT**
- Accepted blob: `b0fc04c771a151ca2230c0fdd2a6915a5a99115d`
- Current blob: `b0fc04c771a151ca2230c0fdd2a6915a5a99115d`
- Source: [`qml/reader2/Paper.qml`](../../qml/reader2/Paper.qml)

```text
// Paper.qml — the web "paper" wrapper: a WebEngineView hosting the vendored Anx
// foliate fork + our thin glue (resources/reader2/paper.html + paper_glue.js).
// This is the WHOLE command/event surface between native QML and the paper:
//   commands DOWN  → window.paper.*  (runJavaScript)
//   events   UP    → Reader2Bridge.paperEventReceived → paperEvent(name, payload)
//
// The native Reader2Bridge is registered on this view's QWebChannel as "bridge";
// paper.html's <head> loads qwebchannel.js + bridge_boot.js (classic scripts) to
// build window.bridge from it — the proven in-repo pattern (the old reader wires
// its bridge the same way), so no userScripts injection is needed here.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-reader2logic-js"></a>
## `qml/reader2/Reader2Logic.js`

- Status: **CURRENT**
- Accepted blob: `f51254c6983695f2eec0b8e94f54c2cc6a37d7be`
- Current blob: `f51254c6983695f2eec0b8e94f54c2cc6a37d7be`
- Source: [`qml/reader2/Reader2Logic.js`](../../qml/reader2/Reader2Logic.js)

```text
// Reader2Logic.js — pure logic for the resume seam (TASK 6). No QML types, no
// network, no Date: just data-in / data-out, so a headless harness can prove it
// (tests/reader2_logic_harness.qml). ReaderShell.qml imports this as `L` and does
// the store I/O + timestamp stamping; this file only shapes the record.
//
// `.pragma library` = one shared, stateless singleton across every importer (no
// per-instance copy). A library JS cannot touch the QML engine's context or `Date`,
// so `updatedAt` is stamped by the CALLER and passed in via `relocated.updatedAt`.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-readerchrome-qml"></a>
## `qml/reader2/ReaderChrome.qml`

- Status: **CURRENT**
- Accepted blob: `3a99b06f14d6f096975a1e2685f0eae4732600e8`
- Current blob: `3a99b06f14d6f096975a1e2685f0eae4732600e8`
- Source: [`qml/reader2/ReaderChrome.qml`](../../qml/reader2/ReaderChrome.qml)

```text
// ReaderChrome.qml — the native glass chrome that floats OVER the paper (the web
// view). It owns the reveal (comic-reader doctrine, MangaReader.qml): the chrome stays
// hidden while you read and returns ONLY when you deliberately reach for it — the
// cursor enters the top/bottom edge band — or on the book-open beat / a double-click.
// A ~300ms Timer ticks it toward sleep after 3s idle; `awake` (= revealState.shown)
// fades the top scrim + TopBar and the bottom scrim + BottomRail in/out. Body movement,
// scroll, and keys NEVER wake it — there is NO "move" path into the reducer (that was
// the bug this doctrine fixes). Left/right edge zones turn pages. Keys are handled by
// ReaderShell and never routed here. Pixel contract: the chrome mock
// (.scrim/.topbar/.bottombar/.turn).
//
// This overlay is BRIDGE-FREE: it only emits signals up to ReaderShell, which owns
// the paper + the native stores. That keeps it instantiable headless (chrome smoke).
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-readershell-qml"></a>
## `qml/reader2/ReaderShell.qml`

- Status: **CURRENT**
- Accepted blob: `5fb86564cd223675c0fa46853c526d256c224bcb`
- Current blob: `5fb86564cd223675c0fa46853c526d256c224bcb`
- Source: [`qml/reader2/ReaderShell.qml`](../../qml/reader2/ReaderShell.qml)

```text
// ReaderShell.qml — the reader component Biblio embeds on swap day (Task 16).
//
// Composition: the web Paper on the bottom, the native ReaderChrome (glass over
// paper — TASK 7) on top. The chrome stays hidden while you read and returns only when
// you reach for the top/bottom edge (or double-click / the book-open orientation beat),
// turns pages at the edges, and scrubs the gold rail; ReaderShell owns the wiring to
// the paper + the native stores. Keyboard turns (Right/Space/PageDown → next,
// Left/PageUp → prev, Esc → back) are handled IN-PAGE by the glue — the web view owns
// focus + keyboard (old-reader model) — and arrive here as semantic paper events
// ('escape', 'selectionCleared'); they NEVER wake the chrome (the naked surface's point).
//
// The RESUME SEAM (Task 6) is unchanged: every 'relocated' persists position to the
// SAME progress.json the old reader uses, and reopening returns to where you left off.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-ruleroverlay-qml"></a>
## `qml/reader2/RulerOverlay.qml`

- Status: **CURRENT**
- Accepted blob: `70353901fa3dca9ac702fb325b37ef8bcfd0ae3e`
- Current blob: `70353901fa3dca9ac702fb325b37ef8bcfd0ae3e`
- Source: [`qml/reader2/RulerOverlay.qml`](../../qml/reader2/RulerOverlay.qml)

```text
// RulerOverlay.qml — the reading RULER (TASK 11): a horizontal focus band with dimmed
// regions above and below it, so the eye is drawn to the line you're reading. Driven by the
// Appearance panel's ruler CONTROLS (rulerOn / rulerHeightPx / rulerDimPct / rulerYPct).
// Pixel contract: the chrome mock's `.ruler` / `.band` (agents/colosseum-book-reader-chrome-mock.html).
//
// HARD CONSTRAINT — it must NEVER block text selection. This overlay sits OVER the
// WebEngineView (the paper), and a full-cover interactive MouseArea would re-block the
// press/drag the paper needs to select text — the exact bug Task 9 fixed. So this layer is
// PURE PAINT: plain Rectangles, ZERO MouseAreas / handlers anywhere. Every press/drag falls
// straight through to the paper. Reposition is done WITHOUT any page interaction — a "Band
// position" slider in the Appearance panel drives `yPct` (no grip over the reading column),
// which is why there is no interactive element here at all.
//
// Geometry is the pure Reader2Logic.rulerGeometry() (band-top + scrim heights from
// yPct/heightPx/overlayHeight), so it is proven headless and stays clamped on-screen.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-searchsheet-qml"></a>
## `qml/reader2/SearchSheet.qml`

- Status: **CURRENT**
- Accepted blob: `ecd333f2a004fefc60c70742e916c8c387ecd726`
- Current blob: `ecd333f2a004fefc60c70742e916c8c387ecd726`
- Source: [`qml/reader2/SearchSheet.qml`](../../qml/reader2/SearchSheet.qml)

```text
// SearchSheet.qml — the reader's SEARCH surface (TASK 11): a thin floating glass sheet
// that drops in under the top bar. An input ("Search this book") + a result-count label
// + a scrollable list of hits; each hit is a ghost-caps chapter label over a serif excerpt
// with the matched word marked in GOLD. Pixel contract: the chrome mock's `.search`,
// `.inrow`, `.count`, `.results`, `.res`, `.rwhere`, `.rtext`, `mark`
// (agents/colosseum-book-reader-chrome-mock.html).
//
// Like the rest of the reader2 chrome this overlay is BRIDGE-FREE: it takes results via
// properties and reports up via signals only. ReaderShell owns the paper.search / goTo /
// clearSearch. SEARCH IS SUBMIT-DRIVEN (Enter), not live-on-keystroke: searching the whole
// book on every keystroke would scan the book each time — the cap bounds the payload, not
// the scan. So `submitted(q)` fires on Return; results flow back in via `results`.
//
// The sheet is a small centered card — deliberately NO full-screen backdrop, so while it is
// open the paper underneath still reads/turns (and the overlay can never block selection).
// The card carries its OWN click-swallow (house doctrine) so taps on it don't fall through
// to the paper's double-click toggle. Esc (input focused) closes it; a row click jumps and
// the sheet STAYS OPEN so you can click through hits.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-selectionmenu-qml"></a>
## `qml/reader2/SelectionMenu.qml`

- Status: **CURRENT**
- Accepted blob: `0398de38fda698d06278fc83a734ed93be5ed7d8`
- Current blob: `0398de38fda698d06278fc83a734ed93be5ed7d8`
- Source: [`qml/reader2/SelectionMenu.qml`](../../qml/reader2/SelectionMenu.qml)

```text
// SelectionMenu.qml — the native glass popover for the paper's pen (TASK 9).
//
// Round 1 shipped: 3 highlight COLOR dots + Copy, on a live text selection. Round 2 adds
// the rest of the mock's set — Note + Define — plus a second MODE for tapping an EXISTING
// highlight (Delete, with optional re-color). Like the rest of the reader2 chrome it is
// BRIDGE-FREE: it takes its data via properties (the selection/highlight rect) and reports
// actions via signals only, so ReaderShell (which owns the paper + native stores) does the
// real work and this stays instantiable headless (chrome smoke).
//
// Two modes (menu.mode):
//   "select"   — a fresh text selection: color dots · Note · Define · Copy   (the mock)
//   "existing" — a tapped highlight:     color dots (re-color) · Delete
//
// The Note action expands the card in place into a small glass note editor (a native
// TextEdit — no QtQuick.Controls dependency); Save emits noteSaved(text). Esc / tap-outside
// cancels the editor (→ dismissed()).
//
// Geometry: an anchors.fill overlay; the CARD is positioned by the pure
// Reader2Logic.selectionMenuPos() (centered on the selection, clamped in-frame, above else
// below). A transparent backdrop below the card dismisses on tap-outside; the card carries
// its OWN click-swallow MouseArea (house doctrine) so taps on it never fall through to the
// paper's double-click-toggle beneath.
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-theme-qml"></a>
## `qml/reader2/Theme.qml`

- Status: **CURRENT**
- Accepted blob: `59e9006fb7a6bed9dcacfc0401d5ad21822b6fdf`
- Current blob: `59e9006fb7a6bed9dcacfc0401d5ad21822b6fdf`
- Source: [`qml/reader2/Theme.qml`](../../qml/reader2/Theme.qml)

```text
// Theme.qml — reader2 chrome design tokens (SINGLETON; the one source of truth for
// the fresh reader's glass-over-paper skin). Byte-for-byte the constants from the
// ratified chrome mock (agents/colosseum-book-reader-chrome-mock.html): GOLD is the
// sparing accent (progress fill / knob / active), the ink ramp is white-at-alpha, and
// the bars are the same smoked glass the video-player HUD uses.
//
// Declared `singleton` in qml/reader2/qmldir → referenced as `Theme.gold` from any
// sibling reader2 component (no per-instance copy).
//
// [Agent 2 (Claude), biblio]
```

<a id="file-qml-reader2-topbar-qml"></a>
## `qml/reader2/TopBar.qml`

- Status: **CURRENT**
- Accepted blob: `2e24f37ca882396c6f924772ca474499f9372abd`
- Current blob: `2e24f37ca882396c6f924772ca474499f9372abd`
- Source: [`qml/reader2/TopBar.qml`](../../qml/reader2/TopBar.qml)

```text
// TopBar.qml — the reader's ICON-ONLY top chrome (Hemanth's ratified amendment: no
// pills, no text buttons). Left: a back arrow. Center: title (Fraunces) + author
// (Inter, quiet). Right: the current chapter label + four line icons — search,
// contents, appearance, bookmark. Glass over the paper; it fades/slides in with the
// reveal (ReaderChrome drives `shown`). Pixel contract: the chrome mock's `.topbar`.
//
// Icons are white-stroke SVGs recolored purely by opacity (the ink ramp IS white at
// alpha), so `inkDim` = the icon at 0.62, hover = 1.0. No GraphicalEffects needed.
//
// [Agent 2 (Claude), biblio]
```
