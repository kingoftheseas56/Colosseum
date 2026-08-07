# Comics — subsystem guide

> **Hand-written. Keep it true.** If you change how comics ingest, store, or read works,
> update this file in the same commit. The per-file index beside it
> ([`comics-index.md`](comics-index.md)) is generated — never edit that one.
>
> Verified against `master@a40333d` + the CBZ-in-place arc. Drafted via Preflight-Architect
> issue #3, ground-truthed and adopted by Agent 0.

## 1. What this subsystem is for

Turn a comic the user picked into **one canonical CBZ owned by the app**, and render its pages
in the native comic reader — without ever streaming, and without losing reading state.

Reading is download-fed. There is no live page streaming anywhere in this path.

## 2. The flow

```
ComicSeries.qml (the GetComics shelf — a tag IS the series)
  → user picks a release  →  Comics (ComicDownloader)
      1. resolve   parse the release post for the FULL signed download href
      2. stream    write <file>.part in chunks, retry, atomic rename
      3. INGEST    ← the part that surprises people; see below
      4. index     write the row into comics/index.json
  → MangaReader.qml → ComicReaderShell.qml
      → ComicReaderCore (parsePages) → provider/response → single|double|strip surface
```

**Step 3 is the one to understand.** Since 2026-08-06 ingest has **two paths**:

- a **natively-readable CBZ is MOVED into the library as-is** — no extraction, no repack;
- **anything else** (RAR/`.cbr`, `.cb7`, `.cbt`, or a CBZ the reader can't open) is extracted
  and **repacked into one canonical CBZ**.

On success the source archive is consumed. **On failure the source is preserved, not deleted**
(repair-before-prune). Older downloads that landed as loose page folders are migrated on boot.

## 3. The files that matter

Full per-file descriptions: [`comics-index.md`](comics-index.md).

| File | Role |
|---|---|
| `qml/ComicSeries.qml` | the live series page — the GetComics shelf |
| `native/engine/ComicDownloader.{h,cpp}` | resolve → stream → **two-path ingest** → index. The subsystem's spine. |
| `native/engine/CbzArchive.{h,cpp}` | probe/read/write CBZ. `probe()` decides which ingest path is taken. |
| `native/comicreader/ComicReaderCore.{h,cpp}` | the one app-facing reader backend; `parsePages`, generation counter |
| `native/comicreader/ComicReaderProvider.h`, `ComicReaderImageResponse.h` | async page delivery |
| `qml/comicreader/ComicReaderShell.qml` | reader chrome; hands entry + state to the core |
| `qml/comicreader/ComicReaderState.js` | pure layout/navigation decisions — no persistence, no async |
| `native/torrent/ComicRequestLedger.{h,cpp}` | versioned-JSON + `QSaveFile` + quarantine pattern (worth copying) |

## 4. Where state lives

- **`<appdata>/comics/index.json`** — the catalog rows. A row carries either `archive` (canonical
  CBZ) **or** `dir` (legacy loose pages). **`archive` wins whenever both are set** —
  `isDownloaded()`, `deleteIssue()`, `downloadedIssues()`, and `localPages()` all check
  `usesArchive()` FIRST.
- **`<appdata>/comics/<series>/<issue>-<hash10>.cbz`** — canonical storage. Not the purgeable cache.
- **Reader generation counter** (in `ComicReaderCore`) — bumped per entry; invalidates in-flight
  async page work so a stale decode can't publish into a newer issue.
- **Reading position / preferences** — supplied to the shell by its store, not owned here.

## 5. Traps

1. **A row mid-migration carries BOTH `archive` and `dir`.** Task 7's first boot deliberately
   leaves `dir` set for one boot and reclaims the loose files on the next. Never assume `dir`
   empty means "archive row," or that both set means corruption.
2. **`probe()` samples only 3 entries** (first/middle/last). It cannot see truncation in the
   middle of a long book. Per-page size verification (`archiveMatchesSourceExactly`) is what
   catches that — don't treat a passing probe as proof the whole archive is intact.
3. **Path separators.** Entries are stored via `QDir::fromNativeSeparators`; comparing raw
   backslash strings against normalized entry names silently never matches, which once caused an
   infinite repack on every boot.
4. **A failed ingest must preserve the source.** Deleting on failure loses the user's download.
5. **Loose images are not a comic session.** Only archives are registered.
6. **`qml/ComicSeriesPage.qml` contradicts itself** — its header says "PARKED… no door routes
   here," yet `qml/Main.qml` references it (`source: "ComicSeriesPage.qml"`). Resolve with the
   lane owner before trusting either statement. Flagged 2026-08-07, not yet settled.

## 6. How to test it

Existing harnesses (run via `ctest -L unit`):

- `comic_downloader_ingest_harness` — the ordinary ingest path
- `comic_downloader_archive_ingest_harness` — archive-in-place ingest
- the legacy-migration harness — 8 scenarios incl. page-order/byte-exactness, off-sample
  corruption, stale-dir `deleteIssue`
- `cbz_archive_harness`, `comicreader_cache_harness`

**A green suite is not a working reader.** Offscreen harnesses never resize, never click, never
render — page rendering and reader chrome still need eyes on the running app.

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/comics.paths \
  --output docs/encyclopedia/comics-index.md --state docs/encyclopedia/comics-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
