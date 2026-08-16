# Tankoban (manga + comics world) — subsystem guide

> **Hand-written. Keep it true.** If you change the Tankoban world, the manga lane, or volume
> mode, update this file in the same commit. The per-file index beside it
> ([`tankoban-index.md`](tankoban-index.md)) is generated — never edit that one.
>
> Verified against `master` (2026-08-07 state). Drafted via the encyclopedia arc, ground-truthed
> and adopted by Agent 0. Related guides: [`comics.md`](comics.md) owns comics ingest + the shared
> reader; [`shell.md`](shell.md) owns the world/session machinery.
>
> **Vocabulary:** per `CONTEXT.md` — Collection = the saved-series store, Library = the derived
> tab, Library row = a render-time join, **reading lane** = chapter lane (`Progress` kind `"manga"`)
> or volume lane (`Progress` kind `"tankoban"`). Use those terms exactly.

## 1. What this subsystem is for

Run the Tankoban world (manga + comics), get manga onto disk through one of two acquisition lanes
(chapters or tankōbon volumes), and read either through the one shared reader — with each lane's
progress tracked independently.

## 2. The flow — two acquisition lanes converging on one reader

**The world** (TankobanWorld.qml, 293 lines) — four tabs: Discover | Manga | Comics | Library.
Featured / Next Up / Continue stay **blended** above the tabs; only the browse rows split into the
Manga/Comics halves (TankobanWorld.qml:188–201). Discover and Library are retained (not
Loader-swapped) so their state survives tab switches; the Manga/Comics browse halves are
Loader-swapped and bound reactively (TankobanWorld.qml:230–283).

```
TankobanWorld.qml
  ├─ Discover → TankobanDiscoverPage (shared DiscoverBrowser + TankobanDiscoverApi.js adapter;
  │             manga card → seriesRequested(title), comics card → comicSeriesRequested)
  ├─ Manga   → TankobanMangaTab (Top-10 + GenreMosaic + Your-Collection row)   → MangaSeries
  ├─ Comics  → TankobanComicsTab (a door — ingest/reader internals live in comics.md)
  └─ Library → TankobanLibraryTab (mixed manga+comic wall; rows derived in TankobanLibraryApi.js)

MangaSeries.qml — the series page (1,015 lines)
  ├─ metadata ladder: WeebCentral (search/chapters/detail) + Comick volume DB + AniList→Kitsu art
  ├─ THE SURFACE IS DECIDED BY THE DATA (2026-07-29 ruling, no toggle):
  │     volumes.length > 0  → permanent volume shelf (tankobanMode), chapter table reduced
  │     else                → flat WeebCentral chapter list      (MangaSeries.qml:8–11, 64)
  │
  ├─ CHAPTER LANE:   download chapter → MangaDownloader → <appdata>/manga/<series>/<chapter>/
  │                  page_000.jpg … (loose pages) + index.json   → read
  └─ VOLUME LANE:    MangaTankobanSourcesPage (user picks, nothing auto-picks)
                        ├─ nyaa ranked rows   → MangaVolumeTorrentDownloader → CBZ → ingest
                        └─ "Build from chapters" fallback → MangaVolumePacker → ArchiveIngestor
                     → <appdata>/manga-volumes/archives/<series>/vol-<n>-<hash>.cbz (+ ledger)

READ (one reader, both lanes): MangaReader.qml (32 lines) = ComicReaderShell
  { objectName: "comicReaderShell" } — the 2026-07-25 cutover deleted the WeebCentral-HTML reader;
  reading internals belong to comics.md.
```

The two lanes meet in `MangaSeries.qml`: `openEntryKind` (`"manga"` for chapters, `"tankoban"`
for volumes, MangaSeries.qml:167–169) selects which Progress kind the reader writes — the reader
itself is the same `ComicReaderShell` for both.

## 3. The files that matter

| File | Role |
|---|---|
| `qml/TankobanWorld.qml` | the world: 4 tabs, blended personal rows, Next Up derivation, comics data computed once |
| `qml/TankobanMangaTab.qml` / `TankobanComicsTab.qml` | the browse halves (Comics tab = door to `comics.md`) |
| `qml/TankobanDiscoverPage.qml` + `TankobanDiscoverApi.js` | the Discover shell wrapper + adapter (MalCatalog/ComicsCatalog injected, null-safe) |
| `qml/TankobanLibraryTab.qml` + `TankobanLibraryApi.js` | the Library tab: pure row derivation (Collection × Progress × Downloads join), filters/sorts/⋮ menu |
| `qml/MangaSeries.qml` | the series page: metadata ladder, data-decided surface, BOTH lanes' doors, legacy re-file |
| `qml/MangaReader.qml` | 32-line delegation to `ComicReaderShell` — the one reader (see comics.md) |
| `qml/MangaTankobanLibrary.qml` | the volume shelf (Theatre-episode anatomy; "ownership drawn as a spine") |
| `qml/MangaTankobanSourcesPage.qml` | per-volume source picker: ranked Nyaa rows + WeebCentral "Build from chapters" pinned last |
| `native/MangaEngine.h` | the QML bridge (`Manga`): WeebCentral scrape + Comick volume DB (completeness-gated) + AniList→Kitsu art ladder |
| `native/engine/MangaDownloader.{h,cpp}` | chapter-lane backbone: fetch page URLs → loose image files → JSON index (`Downloads`) |
| `native/engine/MangaTankobanService.{h,cpp}` | the volume-mode façade (`TankobanVolumes`): search → choose → download/pack → ingest → terminal state |
| `native/engine/MangaTankobanLogic.{h,cpp}` + `MangaTankobanTypes.h` | pure volume identity: string-safe number normalization, escaped `volumeId`, SeriesSnapshot |
| `native/engine/MangaVolumeIndex.{h,cpp}` + `MangaVolumePacker.{h,cpp}` + `MangaVolumeArchiveIngestor.{h,cpp}` | durable CBZ ledger, WeebCentral chapter packer, lossless archive ingest |
| `native/torrent/MangaNyaaSource.{h,cpp}` + `MangaVolumeTorrentDownloader.{h,cpp}` | Nyaa discovery (trust tiers, rejection filters) + restart-safe torrent transport |

## 4. Where state lives

- **Progress — two lanes per series, keyed by `seriesId`** (CONTEXT.md:18–23). Chapter lane:
  `Progress` kind `"manga"`. Volume lane: kind `"tankoban"`. **A single series can have both.**
  The reader (via `ProgressStore`) writes whichever lane `openEntryKind` selected.
- **Chapter downloads — `<appdata>/manga/<series>/<chapter>/page_000.jpg …` + `index.json`**
  (`chapterId → {dir, files[], pageCount, bytes}`), owned by `MangaDownloader` (MangaDownloader.h:
  18–21). Loose page folders — not archives.
- **Volume downloads — `<appdata>/manga-volumes/volume-index.json` (QSaveFile ledger) +
  `archives/<series>/vol-<n>-<hash>.cbz` (+ `.cbz.json` sidecar)**, owned by `MangaVolumeIndex`
  (MangaVolumeIndex.h). Canonical CBZ — the volume lane *is* archive-shaped, unlike the chapter
  lane. In-flight torrent intents journaled in `MangaVolumeRequestLedger` for restart replay.
- **Collection — QSettings `collection/entries`, world `"tankoban"`**, entries keyed by `seriesId`
  with `type: "manga" | "comic"` (ADR 0001; legacy title-keyed manga entries are re-filed on open,
  MangaSeries.qml:79–120).
- **Torrent session — `<appdata>/torrent-engine/`** (shared TorrentEngine).
- **What does NOT persist:** the active tab (every fresh load starts at Discover,
  TankobanWorld.qml:142), `tankobanMode` (data-decided each open, no setting), Library rows
  (derived at render, never stored).

## 5. Traps

1. **"tankoban" names two different things — the subsystem's sharpest trap.** The Progress-kind
   string `"tankoban"` is the **volume lane**, not the Tankoban world/mode (CONTEXT.md:22–23). A
   series can carry progress in BOTH lanes at once. Misread this and every progress call in the
   subsystem reads wrong.
2. **There is one reader, and it is comics'.** `MangaReader.qml` is a 32-line shell —
   `ComicReaderShell { objectName: "comicReaderShell" }` (MangaReader.qml:32). The file's own
   doctrine: *nothing belongs in here* — a reader fix goes to `qml/comicreader/`, never to a
   property on MangaReader. The old WeebCentral-HTML reader and the Guided/ONNX arc are gone
   (archived 2026-07-24, `archive/onnx-readalong-guided-2026-07-24`).
3. **On-disk layout differs by lane — never assume.** Chapter lane = loose page folders
   (`<appdata>/manga/<series>/<chapter>/page_000.jpg`); volume lane = canonical CBZ
   (`<appdata>/manga-volumes/archives/...`); comics = its own canonical CBZ (see comics.md).
   A download-state check must know which lane it is in.
4. **The shared TorrentEngine is born asleep.** Built dormant; **every** consumer must lazily
   `start()` it or downloads hang at "resolving" forever — `if (!m_engine->isRunning())
   m_engine->start();` is the pattern (BookTorrentDownloader.cpp:65, ComicTorrentDownloader.cpp:
   118/404/502; "born-asleep, 4fbb1c2", main.cpp:830–831). New consumers of the shared engine must
   do the same.
5. **Volume mode is data-decided, no toggle (2026-07-29 ruling).** `volumes.length > 0` IS the
   verdict — the Comick gate emits a complete list or nothing; an estimated volume boundary is
   never shown (MangaSeries.qml:8–11, 61–64). Don't add a toggle or per-series persistence.
6. **Volume numbers are strings on purpose.** `"10.5"`, `"Extra"` must never collapse through a
   float round-trip (MangaTankobanTypes.h:6–7). `volumeId` percent-escapes the seriesId's `:` to
   `%3A` — don't join raw ids.
7. **The completeness gate is load-bearing.** `chapterMapComplete` decides whether a volume can be
   built at all — a volume with 1 of 10 chapters is not offered. The WeebCentral packer NEVER
   publishes a partial fallback: any missing/failed chapter tears the staging dir down and
   `complete()` stays false (MangaVolumePacker.h, MangaVolumeArchiveIngestor.h).
8. **A source failure must not read as a page failure.** WeebCentral rate-limits with 429s; once
   the volumes are in, the shelf below is complete and unaffected — `errorText` degrades the
   message instead of blanking the page (MangaSeries.qml:51–58).
9. **Manga Collection entries key by seriesId now; legacy ones keyed by title.** The re-file
   runs in add-verify-remove order so an interruption NEVER deletes the user's save before the
   canonical entry lands (MangaSeries.qml:79–120). Don't "simplify" that order.
10. **Nyaa is a deliberate manga-side fork.** `TankorentSearchService` explicitly dropped Nyaa;
    `MangaNyaaSource` re-ports it with uploader-trust tiers + rejection filters (chapter-pack /
    raw / weak-match / hash-less). Don't try to fold volume discovery back into the generic
    torrent service (MangaNyaaSource.h).
11. **`MangaVolumeTorrentDownloader.h:30` is stale**: "The concrete adapter over the real
    TorrentEngine is wired in a later task" — the `IMangaTorrentEngine` concrete adapter already
    lives in `MangaTankobanService.h:23–25`, `HAS_LIBTORRENT`-gated.
12. **Manga magnets must carry trackers — bare-DHT metadata stalls here.** Nyaa's RSS publishes
    only `nyaa:infoHash` (its `<link>` is the .torrent file URL), so both `MangaNyaaSource::
    parseRss` AND `MangaTorrentEngineAdapter::addMagnet` build tracker-bearing magnets
    (`BookTorrentMagnet::buildMagnet`); the adapter seam also heals pre-fix bare ledger rows on
    replay. Don't strip the `tr=` params or route bare magnets to the engine — on this network
    DHT is unreliable and downloads sit at "resolving" forever (2026-08-16).
13. **The ingestor's source-open is retried on purpose.** `torrentFinished` fires while the
    engine/AV still holds the freshly written archive; miniz's fopen fails once on a valid zip.
    `validateAndAdoptCbz` retries 250 ms × 48 (the ComicTorrentDownloader flush-race twin).
    Don't collapse it back to a single open.

## 6. How to test it

- **Native harnesses (build targets, run directly — none of the manga/tankoban set is
  ctest-registered):** `manga_tankoban_logic_harness` (pure volume identity, fixtures under
  `tests/fixtures/tankoban`), `manga_tankoban_service_harness` (the full search→choose→download→
  ingest→ready pipeline over injected fakes — provable offline without libtorrent),
  `manga_volume_index_harness`, `manga_volume_packer_harness`, `manga_volume_filepicker_harness`,
  `manga_volume_torrent_harness`.
- **QML + JS:** `manga_tankoban_page_harness.qml`, `manga_volume_batch_harness.qml`,
  `manga_volume_cover_harness.qml`, `tankoban_discover_api_harness.qml`,
  `tankoban_discover_page_harness.qml`, `tankoban_library_api_harness.qml`,
  `tankoban_library_page_harness.qml`, `manga_volume_batch_test.mjs`, plus the shared-reader
  harnesses (`comicreader_*` — see comics.md; the reader is the same one).
- **Gates:** `test_manga_tankoban_mode.ps1`, `test_manga_tankoban_native.ps1`,
  `test_manga_volume_batch.ps1`, `test_manga_volume_covers.ps1`, `test_manga_genre_ladder_p0.ps1`,
  `test_tankoban_discover.ps1`, `test_tankoban_library.ps1`, `test_tankoban_migration_p0.ps1`,
  `test_tankoban_series_background.ps1`, `test_tankoban_tabs.ps1`.
- **What it cannot cover:** live WeebCentral scrape behavior, real Nyaa/torrent network flows,
  and how a synthesized volume *reads* — the pack proves bytes and ordering, not reading feel;
  those stay eyes-on with the running app.

## Reading Room UI invariants

The Reading Room is a vertical, reader-derived surface (2026-08-14 bookshelf rebuild):

- `Main.qml` suppresses the Colosseum taskbar while a series detail is active.
- `MangaReadingRoom.qml` compresses the series context into a slim glass masthead over the
  wallpaper: title, author/status/year/score, `N volumes`/`N chapters`, an "ON THIS DEVICE" count
  with a progress bar, and the primary action. The synopsis sits behind a tap (clamped to one
  line, "more"/"less" expands/collapses) instead of a fixed rail column — the masthead grows only
  for that line.
- `MangaTankobanLibrary.qml` renders every canonical volume as a card in a vertical, virtualized
  `GridView` (`volumeShelfGrid`) that opens the page — not a horizontal Pages flow. Each card shows
  its cover (or a "NO COVER" placeholder), a state chip (`Owned` / `Downloading` / `Failed`, no
  chip for a plain unowned volume), and a `VOL n` / title / chapter-span caption (`shelfRangeFor`;
  "chapters not mapped yet" when the volume has none mapped).
- Chapters past the last mapped volume surface as a persistent `LATEST CHAPTERS` tail below the
  grid (the `GridView` footer), never a separate tab — the old Volumes/Chapters tab pair is gone.
- Cover fetching is a bounded window that follows the scroll position (`visibleGridRows()`,
  debounced 120 ms via `_coverPrefetchTimer`), not the whole shelf — this guards the 2026-07-31
  WeebCentral-throttle bug: opening a 115-volume series must never fire a thumbnail scrape for
  every volume at once.
- Batch download (`Select` / `Download next 10` / `Download selected`) is kept alive behind a
  plain text toggle in the grid header — the approved mock has no other entry point for it — and
  the floating "N selected" bar still lands the selection.
- `focusIndex`/`focusToken` and their `focusAtNumber`/`focusAtIndex`/`jumpToNumber` API survive the
  rebuild as the headless cover-prefetch cursor only — nothing highlights or centers on them
  visually any more.

The offscreen behavior is covered by `tests/manga_reading_room_harness.qml`,
`tests/manga_volume_cover_harness.qml`, and the existing volume-batch harnesses
(`tests/manga_volume_batch_harness.qml`, `tests/test_manga_volume_batch.ps1`,
`tests/test_manga_volume_covers.ps1`, `tests/test_manga_reading_room.ps1`).

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/tankoban.paths \
  --output docs/encyclopedia/tankoban-index.md --state docs/encyclopedia/tankoban-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
