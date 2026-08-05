# Comics CBZ-in-Place Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this
> plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work inline in the existing
> checkout; do not create a branch, worktree, or subagent workspace (Rule 28).

**Goal (Hemanth, 2026-08-06):** Western comics stop extracting downloads into loose page folders
and adopt the CBZ-in-place model manga already ships (2026-07-29,
`docs/superpowers/specs/2026-07-28-tankoban-cbz-recovery-reader-design.md`): keep the canonical
`.cbz` on disk, decode pages directly from the archive at read time. "No comic reader worth its
salt extracts cbr/cbz" — Hemanth, verbatim. This also retires the crash-safety gap that lost a
completed 1.4 GB download tonight (extraction has no durable checkpoint; the source archive is
deleted before the extracted result is ever verified usable).

**Why this plan exists:** `ComicReaderCore::parsePages()` (shared by manga and comics) already
accepts `{archive, entry}` page descriptors and decodes them via `CbzArchive::readEntry` — zero
reader-side changes needed, confirmed by direct source read. The gap is entirely on the comics
download/index side (`native/engine/ComicDownloader.{h,cpp}`), which still extracts to
`page_NNN.ext` folders and deletes the source archive immediately.

**Design pressure-tested twice by an Opus 5 advisor consult** (skill:advisor, `--model
claude-opus-5 --effort high`) before any code — once on the overall design, once implicitly
corrected via a second pass after every claim in the first review was independently verified
against source. The review caught defects that would have made the "fix" itself a new, more
silent data-loss class; every one below is confirmed against real line numbers, not taken on
faith:

- `CbzArchive::imageEntries()` proves a file is a well-formed ZIP, **not that its pages are
  decodable** — miniz only inflates store/deflate; an LZMA-compressed CBZ (7-Zip default) lists
  perfectly and then fails at every `readEntry()`. Needs a `probe()` that also checks compression
  method, encryption flag, duplicate entry names, and byte-sniffs a sample.
- **`loadIndex()`'s prune condition would silently drop every new archive-shaped row on the very
  first restart** (`!e.dir.isEmpty()` is false for an archive row → never inserted). This is a
  library-eating bug if landed out of order with the writer — the fix must ship in the same
  commit as the first writer that can produce an archive row.
- **`publishAssembledEdition()` is a second, completely independent `Entry` writer** (torrent-pack
  editions) that still does `QDir().rename(root, dirPath)` into a loose folder. Missing this means
  the migration never converges — fresh legacy rows keep appearing forever.
- Today's `finalizeExtract()` deletes the source archive **before** saving the index
  (`ComicDownloader.cpp` — confirmed both lines directly). That inversion is the exact shape of
  tonight's loss. Manga's own ingestor gets this right (save index, then delete source) and is the
  reference order.
- `ComicDownloader::saveIndex()` is a plain truncating write, not atomic — `MangaDownloader.cpp`
  and `ExtensionsStore.cpp` both already use `QSaveFile`; comics is the odd one out.
- Repacking a multi-GB CBR synchronously (`writeImagesAtomic`) would block the GUI thread for
  minutes — recreating tonight's incident with better ordering but the same freeze-and-get-killed
  shape. Must run off the GUI thread.
- `isDownloaded()`/`deleteIssue()` need an explicit branch — otherwise an archive row false-passes
  `isDownloaded` (empty-`dir` `QDir("").exists()` resolves true) and `deleteIssue` calls
  `removeTree("")`, a destructive empty-path call.
- No existing mechanism serves a cover thumbnail without a loose page file. A stateless
  `image://comiccover/<b64 archive>/<b64 entry>` provider is the answer — never touches
  `ComicDownloader`'s `m_index` from a provider thread (a real GUI-thread/pool-thread race, not
  theoretical, since Quick can call `requestImageResponse` off-thread).

## Ground truth (verified 2026-08-06, master, every line read directly)

- Reader is ready today: `ComicReaderCore::parsePages()` (`native/comicreader/ComicReaderCore.cpp`)
  accepts `{archive, entry}` alongside `{url}`; `PageSourceKind::CbzEntry` already exists.
- `CbzArchive` (`native/engine/CbzArchive.{h,cpp}`, miniz-backed): `imageEntries()`,
  `readEntry()`, `writeImagesAtomic()` — all reusable as-is; `writeImagesAtomic` already stores
  with `MZ_NO_COMPRESSION` and refuses to overwrite an existing path ("refusing to replace an
  existing CBZ").
- Precedent to mirror: `MangaVolumeIndex::migrateLegacy()` (repair-before-prune: verify → pack →
  round-trip-verify → only then clear the legacy pointer) and
  `MangaVolumeArchiveIngestor::looksDecodable()` (magic-byte sniff, currently `QFile`-path-based,
  Qt::Core-only by design).
- `Entry` struct (`ComicDownloader.h:201-210`): `seriesId, seriesTitle, label, dir, files, groups,
  bytes, addedAt` — no `archive` field yet.
- Three producers of `Entry` today, all loose-folder: `finalizeExtract()` (HTTP download),
  `publishAssembledEdition()` (torrent-pack editions), `ingestLocalArchive()` (user-imported
  files, via the same `beginExtract` path). All three must converge on the archive model.
- Four consumers keyed on `dir`/`files`: `loadIndex`/`saveIndex`, `isDownloaded`, `deleteIssue`,
  `downloadedIssues` (cover thumbnail), `localPages`.
- Exactly one legacy comic exists on this machine today (`gcd_119237`, confirmed via `index.json`)
  — migration blast radius is tiny, but the code must be general (in-flight jobs during the
  transition could still produce a legacy row on the last unconverted writer if tasks land
  out of order).
- Existing self-tests exercise this exact surface across a process boundary and will catch
  regressions here: `runPackSelfTest`'s restart scenario asserts `localPages(id).size() > 0` and a
  `downloadedIssues()` record count post-restart.

## Global Constraints

- **`loadIndex()`'s keep-condition fix and the first archive-producing writer land in the SAME
  commit.** Never ship a writer that can produce an archive row before the reader that keeps it
  across a restart.
- Never delete a source archive (downloaded, or the loose extract temp dir) until the replacement
  has been independently re-verified openable AND `saveIndex()` has returned successfully. Save
  index, then delete source — never the reverse.
- Any `writeImagesAtomic` call triggered from a **live, in-app job** (download-finish repack,
  assembled-edition publish) runs off the GUI thread (`QtConcurrent::run`/`QThreadPool`), signalled
  back. The one-time **boot-time** legacy migration may run synchronously — it is rare (one row
  today), one-shot per row, and a short startup delay is a materially different cost than freezing
  a live session the user might kill.
- `CbzArchive::probe()` is NOT gated by file extension — GetComics mislabels suffixes; probe by
  content, always.
- No persisted `StorageKind` enum. `Entry::usesArchive()` (`!archive.isEmpty()`) is the sole
  discriminator, mirroring manga's own `dir`-emptiness pattern. Document the precedence rule
  (archive wins if both are set) directly in the header comment beside the fields.
- The new image provider is fully stateless — decodes from a self-contained URL
  (base64url-encoded archive path + entry name) via `CbzArchive::readEntry` only. It never holds a
  pointer to `ComicDownloader` or touches `m_index`.
- Do not touch `MangaVolumeArchiveIngestor`/`MangaVolumeIndex` beyond the narrow, additive lift of
  shared logic into `CbzArchive` (the `probe()`/sniff primitive). Do not refactor manga's ingestor
  into a shared class this arc — comics already owns an equivalent state machine
  (`beginExtract`/`runExtractor`/`onExtractDone`) and doesn't need a second one.
- `ingestLocalArchive()` keeps its existing, deliberate ownership-transfer contract (source file
  deleted on success) — that is this function's own documented design, not a defect; only its
  ingest mechanism converges on the archive model.
- Existing lane harnesses (`cbz_archive_harness`, `comic_torrent_pack_seed_harness`, and the
  self-tests named above) must stay green throughout.
- Shared-file discipline: `native/main.cpp` (new provider registration), `native/CMakeLists.txt`
  are shared — DECLARE before touching, additive only.
- Commit per task by explicit pathspec; verify with `git diff --cached --name-only` before every
  commit. Build via `native/build-target.bat colosseum`; grep the log for
  `error C|error LNK|ninja: build stopped`. Kill any locking `colosseum.exe` only after confirming
  with Hemanth it isn't his live session (tonight's precedent).
- Reported green is not Hemanth's eyes for anything visual (cover thumbnails, reader opening a
  migrated comic) — his eyes are the gate for those specifically.

## File Map

**Modify**

- `native/engine/CbzArchive.{h,cpp}` — add `probe()` (method/encryption/duplicate-name check +
  sampled byte-sniff), lift a `QByteArray`-based sniff helper.
- `native/engine/ComicDownloader.{h,cpp}` — `Entry::archive` + `usesArchive()`; rewired
  `onFinished()`/`finalizeExtract()`/`publishAssembledEdition()`/`ingestLocalArchive()`;
  `loadIndex()`/`saveIndex()` (atomic + fixed prune condition); `isDownloaded`/`deleteIssue`/
  `downloadedIssues`/`localPages` branched; safe cross-volume move helper; background-thread
  repack; boot-time legacy migration.
- `native/main.cpp` — register the new `image://comiccover` provider (declared).
- `native/CMakeLists.txt` — new provider source files, new harness targets (declared).

**Create**

- `native/engine/ComicCoverProvider.{h,cpp}` — stateless `QQuickImageProvider`.
- `tests/cbz_archive_probe_harness.cpp` — `probe()` against store/deflate/LZMA/encrypted/
  duplicate-name fixtures.
- `tests/comic_downloader_archive_ingest_harness.cpp` — the two-path ingest, safe-move ordering,
  atomic saveIndex, loadIndex prune condition (both branches), isDownloaded/deleteIssue/
  downloadedIssues/localPages branching, adopt-existing-orphan-cbz recovery.
- `tests/comic_downloader_legacy_migration_harness.cpp` — repair-before-prune migration: verify,
  pack, round-trip, persist-with-dir-left-alone, later-boot reclaim, and every documented failure
  path leaves source data untouched.

---

### Task 1: `CbzArchive::probe()` — the primitive everything else depends on

**Files:** modify `native/engine/CbzArchive.{h,cpp}`; create `tests/cbz_archive_probe_harness.cpp`;
modify `native/CMakeLists.txt` (declared).

**Interfaces:**
```cpp
struct CbzProbeResult { QVector<CbzPageEntry> entries; bool nativelyReadable = false; };
static CbzProbeResult probe(const QString& archivePath, QString* error = nullptr);
```
Not gated by extension. Rejects (`nativelyReadable = false`, `entries` still populated for
diagnostics) on: open failure, empty entry list, any entry with `m_method` not in {0, 8}, any
entry with `m_bit_flag & 1` (encrypted), any duplicate entry name (case-insensitive), or a failed
byte-sniff on the first/middle/last sampled entries (lift `looksDecodable`'s magic-byte table as a
`QByteArray`-based free function shared between this and — later, optionally — manga's own
ingestor).

- [x] Write the harness RED: a plain store-mode CBZ probes `nativelyReadable == true`; an
      LZMA-compressed archive (method != 0/8) probes `false`; an encrypted-flag entry probes
      `false`; a CBZ with two entries sharing one name probes `false`; a CBZ containing a
      non-image blob under an image-looking name (fails the sniff) probes `false`; a truncated/
      corrupt zip probes `false` with `entries` empty.
- [x] Implement `probe()` + the shared sniff helper.
- [x] Verify: harness green; `build-target.bat cbz_archive_probe_harness` green by log grep;
      existing `cbz_archive_harness` still green (no regression to `imageEntries`/`readEntry`/
      `writeImagesAtomic`). Commit + push.
      **DONE 2026-08-06, commit `981d705`.**

### Task 2: `Entry::archive` + atomic `saveIndex()` + fixed `loadIndex()` prune

**Files:** modify `native/engine/ComicDownloader.{h,cpp}`.

This is deliberately its own task, landing BEFORE any writer can produce an archive row — the
constraint that prevents the "every new row silently drops on restart" defect.

- [x] Harness RED (extend `comic_downloader_archive_ingest_harness.cpp`, created this task):
      loading an index containing one archive-shaped row (constructed directly as a fixture, no
      writer involved yet) survives a simulated reload; a `saveIndex()` write, killed mid-write via
      a fault-injected short write, never corrupts the previously-saved file (the whole point of
      `QSaveFile`); `deleteIssue`/`isDownloaded` on an archive row with no writer yet still branch
      correctly (both are safe to implement now since they only read `Entry`, not produce it).
- [x] Add `QString archive;` to `Entry`, `bool usesArchive() const`, header comment documenting the
      precedence rule (archive wins if both set). `loadIndex()`/`saveIndex()` serialize `archive`;
      `saveIndex()` becomes `QSaveFile`-based (mirror `MangaDownloader.cpp:163` exactly). Fixed
      keep-condition: `(usesArchive() && QFileInfo::exists(archive) && !files.isEmpty()) ||
      (!dir.isEmpty() && QDir(dir).exists() && !files.isEmpty())`.
      `isDownloaded`/`deleteIssue` (with an empty-path guard inside the remove helpers themselves,
      belt-and-suspenders) branch on `usesArchive()`.
- [x] Verify: harness green; build green. Commit + push.
      **DONE 2026-08-06.** Opus-advisor pass (`--model claude-opus-5 --effort high`) caught three
      real gaps before commit, all fixed and re-verified: (1) `deleteIssue()` order reversed to
      remove `dir` before `archive` (a partial failure now leaves the row valid off the archive,
      not orphaned); (2) `loadIndex()` demotes a row back to a plain legacy row (`archive.clear()`)
      when `archive` is stale but `dir` is what actually kept it — otherwise archive-first readers
      and dir-first readers (pre-Task-3/4) would disagree about the same row; (3) tightened
      `QFileInfo::exists` to `QFileInfo(...).isFile()` (a directory shouldn't pass as an archive).
      Also added a `qWarning()` on a failed `saveIndex()` commit and a 5th harness scenario
      covering the new demotion path directly. Final harness: 5/5 green; existing
      `comic_downloader_ingest_harness` unaffected (3/3 green); full `colosseum` app builds clean.

### Task 3: `image://comiccover` — stateless cover provider

**Files:** create `native/engine/ComicCoverProvider.{h,cpp}`; modify `native/main.cpp` (declared),
`native/CMakeLists.txt` (declared), `ComicDownloader::downloadedIssues()` (art URL for archive
rows).

**Interfaces:** URL shape `image://comiccover/<base64url(archivePath)>/<base64url(entryName)>`.
`requestImage()` (sync — occasional library-grid load, not the reader's high-frequency paging
path that justified the existing async `comicreader` provider) decodes the id, calls
`CbzArchive::readEntry`, decodes via `QImageReader` over a `QBuffer` with `setScaledSize()` so a
200px grid tile doesn't pay for a full 4000px decode. Legacy `dir` rows keep emitting `file://`
unchanged — no UX gap mid-migration.

- [x] Harness RED: a provider unit test (or QML harness, matching whatever this codebase's image
      providers are normally tested with — check `ComicReaderProvider`'s own test pattern first)
      proves a valid `(archive, entry)` id decodes to a non-null scaled image; a missing entry
      resolves to null without throwing; the id round-trips through the exact base64url encoding
      `downloadedIssues()` will produce.
- [x] Implement the provider; register beside the existing `comicreader` provider in `main.cpp`;
      wire `downloadedIssues()`'s `art`/`missing` fields to branch on `usesArchive()`.
- [x] Verify: harness green; build green; boot smoke shows the existing (still-legacy) Descender
      cover unaffected (file:// path untouched pre-migration).
      **DONE 2026-08-06.** Fixture-building gotcha caught before the harness could pass honestly:
      a `tar.exe`-built CBZ (the technique other comic harnesses use for extraction-subprocess
      tests) is NOT reliably `miniz`-readable — this provider's whole job is `readEntry()`, so
      fixtures switched to `CbzArchive::writeImagesAtomic` (the same writer `readEntry()`'s own
      family already uses in `cbz_archive_probe_harness`).

      Opus-advisor pass (`--model claude-opus-5 --effort high`) on the finished diff caught three
      real gaps, all fixed and re-verified: (1) a partial `requestedSize` (QML's
      `sourceSize.width` alone, e.g. `QSize(296, 0)`) was silently DROPPED to the default box
      instead of honored via the one dimension given; (2) the decode could UPSCALE a source
      smaller than the target box, contradicting its own "scale DOWN" comment; (3) `buildId()`
      living in the `QQuickImageProvider` translation unit dragged `Qt6::Gui`/`Qt6::Quick` and
      `CbzArchive.cpp`/`miniz.c` into four Core/Network-only harness targets that compile
      `ComicDownloader.cpp` for unrelated reasons — split into a new Core-only
      `native/engine/ComicCoverId.{h,cpp}` (`buildComicCoverId`/`parseComicCoverId`), which both
      `ComicCoverProvider` and `ComicDownloader::downloadedIssues()` now call; the four harness
      targets' `Qt6::Gui`/`Qt6::Quick` link additions and `CbzArchive.cpp`/`miniz.c` source
      additions were reverted back out. Also flagged, NOT fixed here (pre-existing, Task 1's file,
      out of Task 3's scope — see Execution notes): `CbzArchive.cpp`'s `nativePath()` uses
      `QFile::encodeName` while `miniz.c`'s Windows `mz_fopen` decodes with `CP_UTF8` explicitly
      (`third_party/miniz/miniz.c:3067-3084`, confirmed by direct read) — a real encoding-mismatch
      risk for any non-ASCII archive/series path, unverified on this machine only because every
      path here happens to be ASCII.

      Final harness (8 scenarios, 2 added on the advisor's findings): green. Task 2's harness
      needed one assertion deliberately flipped (`missing==true` → `missing==false` +
      `image://comiccover/` art) now that this task wires it — still 5/5 green. The 4 sibling
      comic-family harnesses that compile `ComicDownloader.cpp` rebuild clean (2 re-run green, 2
      link-verified). Full `colosseum` app builds clean. Two stray (non-Hemanth, confirmed before
      closing) `colosseum.exe` processes blocked link steps mid-task — closed after confirming.

### Task 4: Two-path ingest — the download-finish rewrite (the core fix)

**Files:** modify `native/engine/ComicDownloader.{h,cpp}`; extend
`tests/comic_downloader_archive_ingest_harness.cpp`.

**The safe move sequence** (rename source → `<canonical>.part`; on cross-volume rename failure,
copy → `.part` instead; re-probe the `.part`; rename `.part` → canonical; delete the original
source ONLY if the operation was a copy, and only after `saveIndex()` has returned):

- [ ] Harness RED: a downloaded file that `probe()`s `nativelyReadable` is moved into the library
      as-is with **no extractor subprocess ever spawned** (assert on a call-count fixture); its
      `Entry.files` is populated directly from the probe's entry names (no later zip re-open); the
      original source is gone only after the canonical path is proven to exist AND the index was
      saved. A CBR-shaped source falls through to the existing extract-then-repack path
      unchanged in its extraction half. A crash simulated between repack-verified and
      `saveIndex()` (kill the fixture mid-sequence) leaves BOTH the loose temp dir and the
      original source archive intact — repair-before-prune, not merely "less bad."
- [ ] Rewrite `onFinished()`'s post-download branch: probe first; fast path (safe move, no
      extraction) or fallback (existing `beginExtract`/`runExtractor`/`onExtractDone`, but
      `finalizeExtract()` now packs via `writeImagesAtomic` — off the GUI thread — instead of
      flattening to `page_NNN.ext`, verifies round-trip via `probe()`, saves the index, and ONLY
      THEN removes the extract temp dir and the original archive, in that order).
- [ ] Crash-recovery adoption: if the canonical archive path already exists with no index row
      (an interrupted prior attempt), probe it; adopt if valid (skip repack entirely), discard and
      re-ingest if not — never the manga precedent's hard "already exists" dead end.
- [ ] `localPages()` rewritten for archive rows: pure in-memory map from the stored `files`/
      `groups` lists (`{index, archive, entry: files[i], group}`) — no zip open per call.
- [ ] Verify: harness green; build green; **live smoke required, not optional** — download one
      real small CBZ comic end-to-end and confirm zero extraction subprocess spawns and the comic
      opens in the reader; download or synthesize one CBR-shaped source and confirm the fallback
      path still produces an openable archive. Commit + push.

### Task 5: `publishAssembledEdition()` convergence

**Files:** modify `native/engine/ComicDownloader.cpp`.

Without this, torrent-pack editions keep producing fresh legacy `dir` rows forever and the
migration in Task 6 never converges.

- [ ] Extend the ingest harness RED: an assembled edition (fixture staging dir + ordered file
      list) publishes as an `Entry` with `archive` set, `dir` empty; the existing traversal-safety
      and duplicate-page checks (`:1139-1160`, unchanged) still reject an escaping/missing/
      duplicate page BEFORE any archive write is attempted.
- [ ] Replace `QDir().rename(root, dirPath)` with `CbzArchive::writeImagesAtomic(finalCbz, root,
      f.assembledOrderedFiles)` off the GUI thread (same background-thread treatment as Task 4);
      `groups` stays index-parallel.
- [ ] Verify: harness green; build green; existing `comic_torrent_pack_seed_harness` /
      `runPackSelfTest`'s restart scenario (`localPages(id).size() > 0`, record count) still pass
      unmodified — this is the self-test the review flagged as exercising exactly this surface.
      Commit + push.

### Task 6: `ingestLocalArchive()` convergence

**Files:** modify `native/engine/ComicDownloader.cpp`.

- [ ] Extend the harness RED: a user-imported CBZ ingests via the fast path with no extraction;
      a user-imported CBR still extracts-then-repacks; in both cases the user's own source file is
      still deleted on success, matching this function's existing documented ownership-transfer
      contract (not changed by this task — only its ingest mechanism now converges on the archive
      model).
- [ ] Route through the same two-path ingest as Task 4 (shared helper, not a duplicate
      implementation).
- [ ] Verify: harness green; build green. Commit + push.

### Task 7: Legacy migration — repair-before-prune, two-boot reclaim

**Files:** create `tests/comic_downloader_legacy_migration_harness.cpp`; modify
`native/engine/ComicDownloader.cpp` (migration runs from `loadIndex()`).

- [ ] Harness RED: a legacy `dir`-shaped row with all listed files present migrates — pack via
      `writeImagesAtomic`, round-trip-verify via `probe()`, persist with `archive` SET and `dir`
      LEFT ALONE (not cleared in the same pass — the amendment past the manga precedent: don't
      reclaim loose files until a LATER boot re-verifies the archive independently openable).
      A SECOND simulated boot, given a row with both `archive` (valid) and `dir` set, clears `dir`
      and removes the loose files. A row with a missing listed file migrates not at all — `dir`
      and `files` untouched, warning logged, nothing deleted. A row whose canonical archive path
      already exists (crash recovery) adopts it directly (no repack) if valid.
- [ ] Implement the two-pass migration (pack-and-verify this boot; reclaim-loose-files next boot)
      as a synchronous step inside `loadIndex()` — deliberately NOT backgrounded, since it is
      one-shot per row, rare (one row on this machine today), and runs before the app is
      interactive, a materially different cost than the live-session freeze Task 4/5 avoid.
- [ ] Verify: harness green; build green; **live migration of the actual existing Descender
      entry** on this machine — confirm it opens in the reader post-migration exactly as before
      (eyes-on, matching manga's own verified-live bar from the precedent commit). Commit + push.

### Task 8: Sweep + eyes-on

- [ ] Re-run every harness from Tasks 1-7 plus `cbz_archive_harness`,
      `comic_torrent_pack_seed_harness`, and the `runPackSelfTest`/`COLOSSEUM_COMIC_PACK_DLTEST`
      self-test fresh.
- [ ] Confirm the crash-safety property directly: start a real comic download, kill the app
      process mid-transfer (before completion) — expect a clean failed/resumable state, not data
      loss; if feasible, also verify that once `probe()`-confirmed CBZ is on disk, there is no
      window at all where killing the app can lose it (the whole point of tonight's fix).
- [ ] Eyes-on with Hemanth: a fresh comic download completes and opens with NO visible extraction
      step; the migrated Descender comic opens correctly; a cover thumbnail renders in the
      Downloads/library grid for both a freshly-downloaded and a migrated comic.
- [ ] Close: append results here, recap on `Brotherhood/agents/chat.md`, final commit + push.

---

## Execution notes

- **Order is deliberate and load-bearing, not just risk-gradient:** Task 2 (the `loadIndex` fix)
  MUST land before Task 4 (the first writer that can emit an archive row) or the exact
  library-eating bug the review caught reappears. Task 5/6 must land before Task 7 or the
  migration chases a moving target.
- **Model routing:** plan authored and executed on Sonnet per Hemanth's routing tonight; design
  pressure-tested via `skill:advisor` with Opus 5 in the advisor seat (`--model claude-opus-5
  --effort high`), every claim independently re-verified against source before being adopted —
  not taken on the advisor's word alone.
- Every shared-file touch (`native/main.cpp`, `native/CMakeLists.txt`) gets its chat declaration
  before the edit, additive only, per task.
- This plan does NOT touch `MangaVolumeArchiveIngestor`/`MangaVolumeIndex`. `CbzArchive::probe()`
  becomes available for manga's own extension-gating hole (confirmed to exist,
  `MangaVolumeArchiveIngestor.cpp:122-123`) as a handoff note on chat, not a change made here
  uninvited into another lane's file.
- **Known gap, flagged not fixed (found during Task 3's advisor pass, 2026-08-06):**
  `CbzArchive.cpp`'s `nativePath()` (Task 1) encodes via `QFile::encodeName` (Windows: the
  locale's Local8Bit codec), but `third_party/miniz/miniz.c`'s Windows `mz_fopen` decodes the
  bytes it receives with `CP_UTF8` explicitly (`miniz.c:3067-3084`, confirmed by direct read) —
  a real mismatch for any non-ASCII archive or series path (accented names, CJK titles GetComics
  does post). Every path CbzArchive touches today is ASCII (this machine's username, existing
  fixtures), so it has not manifested, but Task 3 makes every future cover thumbnail depend on
  this same path-encoding round-trip, and Task 4+ makes every future comic. Likely one-line fix
  (`path.toUtf8()` instead of `QFile::encodeName`) but needs its own non-ASCII-path harness case
  to land safely — left out of Task 3's commit on scope discipline (pre-existing, not this task's
  file map). Worth a short, separate follow-up before Task 4 makes the blast radius bigger.
