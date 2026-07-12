# Biblio — Book Torrents Shelf ("Torrentio for books")

> Spec · 2026-07-12 · Agent 2 (Biblio lane). Brings TB2's Tankorent federated
> torrent search home to Colosseum as a ranked torrent shelf on the book page,
> above LibGen. **No inbuilt torrent client** — search is HTTP-to-indexers, download
> rides the Stremio engine we already ship.

## One-sentence what-it-is

On a book's page, a **TORRENTS** shelf above EDITIONS lists live torrent results for
that title, ranked best-match first and — within the same match quality — **most
seeders at top, fewest at bottom**; clicking a row pulls the single best-matching
ebook file to disk via the Stremio engine, then opens it in the reader.

## Why now / the call

Hemanth's call (2026-07-12): we already solved this in Tankoban 2. TB2's
`TankorentSearchService` is a **UI-free, federated torrent search** — it fans out one
query to a bank of indexers and streams ranked results back. TB2 proved it on the
*hard* case (TV: episode vs season vs full-series packs). Books are the *simple* case.
Nothing here needs the libtorrent client TB2 carried — TB2's search never depended on
it; it's plain HTTP to indexer sites. Transport (bytes → disk) is already solved by the
Stremio engine the audiobook lane shipped 2026-07-12.

## Architecture — three parts, two of them ported

QML sees **one** small context property, `BookTorrents`. Internally it composes three
single-purpose pieces:

| Piece | Origin | Responsibility |
|---|---|---|
| `TankorentSearchService` + 4 indexers | **Ported** from TB2 `src/core/` | Fan out a query to the book indexers, return `QList<TorrentResult>` |
| `BookTorrentRanker` (pure) | **New**, tiny; shaped after TB2 `rerankBooks` | Dedup by infoHash + rank rows: match tier, then seeders desc |
| `BookTorrentDownloader` | **New**, sibling of `AudiobookDownloader` | Stremio-fed pull of the **single** best ebook file from the chosen torrent |

### Part 1 — Search (ported, native C++)

Port from `~/Desktop/Tankoban 2/src/core/`:

- `TankorentSearchService.{h,cpp}` — the headless fan-out. `startSearch(mediaType,
  sourceFilter, query, limit)` → per-indexer `resultsReady` / `indexerError` /
  `searchFinished(handle)`. Media-type router already knows **books**; we trim its
  allowlist (`kMediaTypeIndexers`) to the three keyless, CF-free indexers `{ piratebay,
  exttorrents, torrentscsv }`.
- **Three** book indexers from `src/core/indexers/`: `PirateBayIndexer`,
  `ExtTorrentsIndexer`, `TorrentsCsvIndexer` (+ the `TorrentIndexer` base). PirateBay
  uses apibay.org JSON; the others parse HTML.
- **`X1337xIndexer` is deliberately NOT ported.** It `#include`s
  `CloudflareCookieHarvester.h`, which is `QWebEngineView`-backed — hidden-browser CF
  harvesting, banned by house doctrine and not linked by the colosseum target. It cannot
  be ported without importing a banned WebEngine path. Park it. (Pre-flight review, 2026-07-13.)
- `TorrentResult` (`src/core/TorrentResult.h`) — carries `title`, `seeders`,
  `leechers`, `sizeBytes`, `infoHash`, `magnetUri`, `sourceName`, `detailsUrl`,
  `category`, plus `humanSize()` and `buildMagnet()` helpers. Ports verbatim.

**Doctrine the ported HTTP must inherit.** Search rides a **dedicated `CachingNam`
built with `useCache = false`** (a new backward-compatible flag) — reusing the app's
proven pinning NAM but without its image cache:
- **IPv4-pinned** (the 21-second IPv6 black-hole stall fix) — the 3 indexer hosts
  (apibay.org, extto.org, torrents-csv.com) are added to `pinnedHosts` so `CachingNam`
  rewrites them to IPv4 (Host header + `peerVerifyName`, TLS intact).
- **Real User-Agent** — `CachingNam` already stamps a browser UA when a request carries
  none (search stays **native C++**, not QML XHR, so the UA is honored — A1's 2026-07-12
  finding that QML XHR silently drops `setRequestHeader` is exactly why).
- **Uncached / live** — `useCache = false` skips the disk cache and the `PreferCache`
  attribute, so seeder counts are never served stale (the image NAM's `PreferCache` would
  freeze them). Torrent bytes use the separate uncached `dlNam`. (Pre-flight review, 2026-07-13.)

### Part 2 — Rank (new, pure, testable)

`BookTorrentRanker` — one pure function, no I/O:

```
QList<TorrentResult> rank(query{title,author}, QList<TorrentResult> raw)
```

1. **Dedup** by canonical `infoHash` (same torrent surfaces from multiple indexers);
   keep the copy with the **highest seeders**.
2. **Match tier** per row — article-stripped (drop leading "The"/"A" like TB2's
   `rerankBooks`), then: exact title (+author) > prefix > all-tokens-present > partial.
3. **Sort**: primary = match tier (best first); secondary = **seeders descending**.
   This is Hemanth's literal formula: *best-match-most-seeds → best-match-lowest-seeds.*
4. **Pack flag** per row (for the badge + single-file download): heuristic on title
   (`collection` / `books` / `pack` / `library` / a book-count number) and on size
   (≫ a single ebook). Packs are **not** rank-penalized — a well-seeded pack that
   matches the title may legitimately top the shelf; it just gets a badge and a
   single-file pull.
5. **Format guess** per row (for the pill): inferred from the torrent title's suffix
   when present (`epub`/`pdf`/`mobi`/`azw3`/`fb2`); `EBOOK` when unknown. The
   *authoritative* format is only known at download time from the manifest. (`djvu` is
   excluded from the downloadable ebook set — the foliate reader has no DJVU backend.)

### Part 3 — Download (new sibling, Stremio-fed)

`BookTorrentDownloader` — modeled on the proven `AudiobookDownloader` transport, but
**book-shaped**: pulls **one** file, not all.

**Why a sibling and not a generalization of `AudiobookDownloader`:** the audiobook
downloader was hardened under Hemanth's live eyes on 2026-07-12 (`69a39ba` watchdog,
per-row state, etc.). It is multi-file, natural-sorted, `pairKey`-keyed, audio-indexed.
Refactoring that hot, just-verified file to add a "single-file, infoHash-keyed, ebook"
mode is risk with no user-visible payoff. The reduction reflex says: the smallest safe
thing is a focused sibling that reuses the *pattern* (prefetch → manifest → stream to
disk, with the cold-engine watchdog) on its own state. If a second consumer ever needs
it, the shared engine-handshake can be extracted then — YAGNI until then.

Flow (mirrors `AudiobookDownloader` Transport steps 1–3):
1. `Stream.prefetch(infoHash, 0)` → adopt/start the Stremio engine → `fetchReady(url,
   infoHash, 0)` gives the engine base `http://127.0.0.1:<port>/<infoHash>`.
   Reuse the **`pollEngine` watchdog** (cold engine can lose `fetchReady`) + `/create`
   timeout + bounded retry — the exact hardening from the audiobook fixes.
2. `POST <base>/<infoHash>/create` → `{ files:[{path,name,length,offset}] }`.
3. **Best-file selection** (pure, testable — the "pull one file" logic):
   - Filter `files[]` to renderable ebook extensions (`epub, pdf, mobi, azw3, fb2` —
     **no `djvu`**, the reader can't open it).
   - Score each filename with **title tokens separated from author tokens** (a whole-title
     stem exact-match wins; author is only a final tie-break — so a same-author sequel
     doesn't outscore the exact title), tie-break by format (`epub` > `mobi`/`azw3` >
     `pdf` > `fb2`).
   - Pick the **single** highest-scoring file. Zero ebook files → **honest fail**
     (`failed(infoHash, "no ebook file in torrent")`), never a silent hang.
4. Stream **only that one** `fileIdx` complete (plain GET, no Range) to
   `<appdata>/books-torrent/<infoHash>/<name>.<ext>` via the `.part` → atomic-rename
   lineage. Index keyed by `infoHash` in `<appdata>/books-torrent/index.json`.

QML-facing surface on `BookTorrents` (one facade over the three parts):

```
Q_INVOKABLE void search(title, author)          // → resultsReady / searchFinished
Q_INVOKABLE void download(infoHash, title, author)
Q_INVOKABLE bool   isDownloaded(infoHash)
Q_INVOKABLE QString localFile(infoHash)          // path, or "" — reader opens this
Q_INVOKABLE QVariantMap statusOf(infoHash)
signals: resultsReady(QVariantList rankedRows); searchFinished()
         resolving(infoHash); progress(infoHash, rcv, tot)
         finished(infoHash, path); failed(infoHash, why)
```

The facade connects the search service's signals **once** (member slots, not per-search
lambdas) and guards results by the active handle — one shared `BookTorrents` instance
serves every book page, so opening book B supersedes book A's in-flight search cleanly.
Registered in `native/main.cpp` right after `Audiobooks` (the `stream` engine + the
uncached `dlNam` for bytes; the search half gets its own pinned **uncached** `CachingNam`).

## The shelf (QML — `BiblioBook.qml`)

A new **TORRENTS** block placed **directly above the EDITIONS block** (before the
current `// ── Editions ──` at ~line 353), mirroring the EDITIONS structure exactly so
it reads as one family:

- New `detail` properties: `property var torrents: []`, `property bool torLoading: false`.
- Header `Text`: `"TORRENTS" + (torLoading ? "  ·  SEARCHING…" : count | "  ·  NONE")`.
- `Glass` → `Column` → loading/empty `Item` ("Searching torrents…" / "No torrents
  found") + `Repeater { model: detail.torrents }`.
- Each row (52px, like an edition row): **format pill** · **seeders** (e.g. `▲ 128`) ·
  **size** (`humanSize`) · **pack badge** when `modelData.pack`. Download-state
  indicator on the right (`↓` / `…` / `NN%` / `✓` / `retry`) driven by `Connections`
  to `BookTorrents` signals keyed by `infoHash` — the same reactive pattern the
  EDITIONS rows use against `Books` (keyed by md5).
- Click: if done → `readRequested(BookTorrents.localFile(infoHash), book)`; else →
  `BookTorrents.download(...)`. Same download-fed doctrine as every Biblio row.
- **Defensive guard**: everything wrapped in `typeof BookTorrents !== 'undefined'`
  (matches the existing `Audiobooks`/`Books` guards) so the page still renders if the
  native object isn't registered.
- Wiring: a `loadTorrents()` run alongside `loadEditions()` in `onBookChanged` — fires
  `BookTorrents.search(title, author)`, sets `torrents` on `resultsReady` (already
  ranked by the native ranker — QML does **not** re-sort), clears `torLoading` on
  `searchFinished`.

Ordering on the page, top→bottom: synopsis → **TORRENTS** (new) → **EDITIONS (LibGen)**
→ **AUDIOBOOK**. LibGen stays the dependable primary ebook lane; torrents are the
complement above it for what LibGen misses.

## Testing

- **Ranker (pure)** — headless (`qml.exe -platform offscreen` logic harness *or*
  GoogleTest): synthetic `TorrentResult` lists → assert dedup-by-infoHash-keeps-max-
  seeders, match-tier ordering, seeders-desc within a tier, pack-flag heuristic. Prove
  the gate can fail.
- **Best-file selector (pure)** — synthetic manifest `files[]` → assert single pick,
  format-preference tie-break, honest empty on zero ebook files.
- **Indexer live smoke** — dev env-var `COLOSSEUM_TORRENT_SEARCHTEST="<title>"` runs the
  federated search headless and **logs per-indexer row counts**. This is how we catch a
  rotted indexer (dead domain / changed markup) — any indexer returning 0/erroring
  ships **disabled**, not silently broken.
- **Download live smoke** — `COLOSSEUM_TORRENT_DLTEST="<infoHash>|<title>"` resolves +
  pulls headless, logging the manifest, the picked file, and the final on-disk path
  (mirrors `AudiobookDownloader::selfTest` / `COLOSSEUM_ABB_DLTEST`).
- **Eyes-on (Hemanth)** — open a known book, watch the TORRENTS shelf populate ranked,
  click the top row, confirm it downloads and opens in the reader. Pixels are his eyes;
  the app is uncapturable headless.

## Honest risks (verify, don't hand-wave)

- **Indexer rot** — the three indexers were last touched in TB2 months ago; domains and
  markup drift. Each gets a live on-port smoke (on the production pinned NAM, so a slow
  host isn't falsely called dead) during the build. Expected outcome: some subset alive,
  ship those, disable the dead ones with a note.
- **Book swarms die** more than video/audiobook swarms — expect a higher "matched in the
  index but 0 seeders / dead" rate. Fast-fail machinery handles it; the user feels it as
  an honest "not available" on some rows, and LibGen below still delivers.
- **Cold-engine first-pull is slow** (empty Stremio DHT) — same as the first audiobook
  download; same mitigation (watchdog + pre-warm on panel open). A launch-time
  engine-warm (A0 lane) would fix it across every lane at once — flagged, not built here.

## Out of scope (v1)

- Private trackers / login / seed-ratio duty (the *only* future that would justify a
  real inbuilt client).
- Seeding after download; whole-pack ("download all 5,000 books") download.
- Any change to the audiobook lane or the LibGen lane.
- An in-app magnet/torrent client (Tankorent). Explicitly **not** needed.

## Ported-file manifest (for the plan)

From `~/Desktop/Tankoban 2/src/core/` → Colosseum `native/torrent/`:
`TorrentResult.h`, `TankorentSearchService.{h,cpp}`, `TorrentIndexer.*`,
`indexers/PirateBayIndexer.*`, `indexers/ExtTorrentsIndexer.*`,
`indexers/TorrentsCsvIndexer.*`. (**No `X1337xIndexer`** — CF-harvester/WebEngine, banned.)
New: `BookTorrentRanker.{h,cpp}` (pure), `BookTorrentDownloader.{h,cpp}`,
`BookTorrents` facade, QML shelf in `BiblioBook.qml`, `main.cpp` registration,
CMakeLists entries (grep-verify around brothers' hunks — shared-file collision rule).
