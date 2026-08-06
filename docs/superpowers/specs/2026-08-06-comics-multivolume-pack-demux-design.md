# Comics multi-volume pack demux — design specification

**Date:** 2026-08-06
**Status:** Approved brainstorm → spec for review
**Lane:** Comics (Tankoban), download/ingest pipeline
**Origin:** Hemanth hit it live — "Chew Vol. 1–8 + Extras" (GetComics post 1284, 1.46 GB) fails
with "archive contained no pages". Diagnosed same wake: the download is an archive-of-archives —
a ZIP whose top folder holds 12 complete nested comic files (11 CBR, 1 CBZ), not loose page
images. Not a regression; the ingest correctly refused to guess. The source survived via
`failPreservingSource` (preserved at `<AppData>/Brotherhood/Colosseum/comics/dl_c5c1573258.archive`
+ extracted tree `dl_c5c1573258.archive.x/`).

---

## 1. Experience promise and scope

When a comic download turns out to be a whole series packed in one file, the app unpacks it into
a proper series shelf instead of failing. For the live case: the failed Chew download becomes a
Chew shelf with 12 readable volumes without re-downloading a byte.

In scope: the comics (Tankoban western) download/ingest lane only — HTTP-downloaded packs and the
preserved-on-disk retry of a previously failed pack.

Out of scope (see §7): manga and Biblio lanes, packs nested deeper than one level, catalogue
changes.

## 2. Decision ledger

### Locked (Hemanth)

- **One grouped series, not N tiles.** All 12 Chew volumes live inside one "Chew" series shelf —
  never 12 top-level tiles. (Decided pre-handoff, reconfirmed.)
- **Reader crossing is main-story only.** "Next" at the end of a volume flows v1 → v8 and stops
  after v8. The 3 Bonus volumes and the Script Book form a separate labeled **Extras** group on
  the series shelf — one tap to open, never in the next-chain. (Decided 2026-08-06.)
- Opening an extra opens **just that item** — no chaining through other extras; back returns to
  the shelf. (Claude's default, accepted with the lock.)

### Constraints

- **Source-safety:** the pack archive is reclaimed only after **all** its volumes verify readable
  in the library index. Any failure or cancellation keeps the pack on disk. Never delete source
  before `saveIndex()` has returned for the entry that supersedes it (the standing CBZ-in-place
  ordering rule).
- **Stable identity from creation:** every volume's issue id is fixed deterministically the moment
  demux begins — no late-resolving identity (the exact trap behind the reader-resume bug fixed in
  Colosseum `df003eb`).
- **Single lane:** volumes ingest one at a time through the existing `m_queue`/`startNextQueued()`
  lane. No concurrent packers, no second ingest mechanism.
- **Detection by content, never filename:** a pack is recognized by what its extracted tree
  actually contains; nested archives are probed by content (the existing `probe()` discipline).
- **Idempotence:** re-running demux for the same pack produces no duplicates — already-indexed
  volumes are skipped.
- No color, no emoji on any surface; existing gray/black/white + SVG language only.

### Deferred (explicitly not this build)

- Cancel-then-retry race on deterministic per-id worker paths (`pathsInUseByLiveJobs()` guard).
- Torrent-source orphan sweep (`comics-torrent/<infohash>/`).
- Inactive long-strip surface `pageInView` emission gate (Preflight #2 "Option A").

## 3. Primary user journey

### 3a. The live retro-fix (Chew, first use)

1. Hemanth opens Downloads and presses **retry** on the failed Chew row — or, if that row did not
   survive an app restart, simply downloads the same Chew post again; both actions resolve to the
   same issue id and take the same path.
2. The app finds the preserved 1.46 GB pack already on disk and **does not re-download** — it goes
   straight to unpacking.
3. The queue shows **one folded line: "Chew — 12 volumes"** with an aggregate progress bar,
   filling as volumes land one by one. Expanding the fold shows per-volume rows.
4. On completion the comics shelf shows **one Chew series**: main run Vol. 1–Vol. 8, and an
   **Extras** group (Vol. 1–3 Bonus, Script Book) beneath it. The pack file is deleted; ~1.46 GB
   is reclaimed automatically.
5. He opens Vol. 1, reads, presses next → Vol. 2 → … → Vol. 8 → the run ends. Extras never
   interrupt. Opening an extra shows just that item.

### 3b. A fresh pack (normal use, steady state)

A future pack download flows through the same pipeline with **no failure moment at all**: download
completes → extraction finds nested comics → demux runs automatically → the folded group appears →
the series shelf materializes. The "archive contained no pages" failure no longer exists for true
packs; it remains for genuinely empty archives.

## 4. States, interruption, recovery, edge cases

- **Per-volume isolation.** Each volume succeeds or fails on its own. A failed volume shows its
  own row with retry; the other volumes are readable immediately. Any failure → the pack stays on
  disk.
- **Crash mid-demux.** A persisted **pack manifest** (see §6) records the pack and its expected
  volume ids before the first volume ingests. On next launch, an incomplete active manifest
  re-enqueues only the missing volumes from the preserved pack — unpacking resumes by itself, no
  user action, no duplicates.
- **Cancel.** Cancelling the folded group stops at the current volume boundary: landed volumes
  stay readable, queued volumes are dropped, the manifest is cleared, and the pack archive is
  preserved. Re-initiating the download later re-uses the preserved pack (3a step 2) and skips the
  already-landed volumes.
- **Empty or bogus pack.** Extracted tree contains neither page images nor nested comic archives →
  exactly today's behavior: `failPreservingSource("archive contained no pages")`.
- **Mixed tree** (images AND nested archives in one pack): accepted page images anywhere in the
  tree win — the archive ingests as a single issue exactly as today; demux only triggers when the
  tree yields **zero** accepted page images. (Keeps today's single-issue behavior bit-identical:
  the demux scan lives inside the branch that only runs at zero images.)
- **Nested pack inside a pack** (depth 2): the inner pack fails per-volume with source preserved,
  as today. One level only.
- **Unparseable volume filename:** classified **main**, ordered after the parsed mains by natural
  filename sort — always readable, never hidden.
- **Duplicate landing:** if a volume's canonical id already exists in the index at demux time, it
  is skipped (idempotence), never overwritten.

## 5. Controls, feedback, integration

No new surfaces and nothing new to learn — the feature rides existing affordances:

- **Downloads queue:** the folded multi-part group and its aggregate bar are the existing
  `groupJobs()` fold (`qml/DownloadsPage.qml`); demux children populate the pre-plumbed
  `groupKey`/`groupUnit` fields so the fold reads "Chew — 12 volumes". Expand, per-row cancel,
  and retry behave as for any grouped download.
- **Downloads shelf / series lane:** the finished volumes group into one series row via the
  existing `downloadsApi.series()` seriesId/seriesTitle grouping — free, no changes.
- **Series page:** lists the downloaded volumes with the main run ordered v1 → v8 and extras in a
  labeled Extras group. Reading progress per volume, per-volume delete, and series delete all
  behave as for any downloaded comic. Deleting the series removes all volumes.
- **Reader:** receives a mains-only chapter chain (ordered per the locked decision); extras open
  with a single-entry chain. Crossing UI is unchanged.

## 6. Technical contracts (enough to plan; not more)

### Detection seam

`ComicDownloader::finalizeExtract()` (`native/engine/ComicDownloader.cpp`, the
`rel.isEmpty()` branch, ~:1760). Before `failPreservingSource("archive contained no pages")`:
scan `f.extractTmp` recursively for nested comic archives, **by content probe, not suffix**
(suffix set `.cbr/.cbz/.cb7/.cbt` is a pre-filter only). Zero images + ≥1 nested archive → demux.
Zero images + zero archives → fail exactly as today.

### Demux = N × the existing primitive

For each nested archive, enqueue one child job through `m_queue` (single lane) that runs the
shared `ingestArchiveByProbe(InFlight&)` two-path ingest (~:1301): natively-readable CBZ →
`finalizeSafeMove` (move into place, no extraction); anything else → `beginExtract` →
extract-and-repack via the existing `bsdtar → 7z` fallback chain (`runExtractor`). **No second
ingest mechanism.** Child jobs are `localArchive`-style ingests whose `archivePath` points at the
nested file inside the pack's extract tree; the child ingest may consume that extracted copy —
the pack `.archive` itself is the protected source.

### Identity and grouping

- **Child issue id:** a deterministic, filesystem-safe pure function of
  (parent issue id, nested archive relative path) — distinct per nested file, stable across
  re-runs. Exact format chosen at planning to match existing id conventions
  (ids feed `issueArchivePath()`).
- **Series identity:** children inherit the parent's `f.seriesId`/`f.seriesTitle` verbatim
  (set at creation — constraint above). Shelf grouping then falls out of the existing series
  lane with zero changes.
- **Queue fold:** children share a `partGroupKey` (the parent id) and carry
  `groupUnit: "volumes"`. `activeIssueJobs()` already emits both fields (~:2287–2291, currently
  empty/hardcoded — the noted "future multi-part fix with zero further plumbing").

### Volume labels and roles

A label parser maps each nested filename → `{displayLabel, role, order}`:

- `v(\d+)` with zero-pad normalization → "Vol. N", role **main**, order N.
- A "Bonus" token → role **extra**, label "Vol. N — Bonus".
- "Script Book" (and unmatched named specials) → role **extra**, label from the cleaned name.
- Extras order after all mains, stable (natural sort).
- Non-ASCII in source names (e.g. `´`) must round-trip safely through extraction and paths.

`role` and `order` persist on the index `Entry` (new optional fields; absent = ordinary single
issue, all existing rows unaffected). They are the only inputs the series page needs to build the
mains-only crossing chain and the Extras group.

### Pack manifest (crash recovery)

A small persisted map in the comics index JSON, written **before** the first child ingests:
`packId → { packArchivePath, extractTmp, expectedChildIds, active }`. Cleared when all children
verify (then pack + extract tree are deleted — reclamation) or when the user cancels (pack file
kept). `loadIndex()` re-enqueues missing children of any active manifest. This is the entire
recovery mechanism; no other state.

### Retry re-uses the preserved pack

For a pack whose completed `.archive` staging file already exists on disk (the
`failPreservingSource` artifact), the retry/download path for that id goes straight to ingest —
no network. (A partial `.part` never qualifies; only a fully-renamed `.archive`.)

### Series page / reader wiring

The downloads-only series-page path (no catalogue rows behind the series) already exists
(`qml/Main.qml` ~:636 baked-injection mirror); planning extends it to order mains by `order`,
group extras by `role`, and hand the reader a mains-only `chapters` array (newest-first
convention — crossing advances toward index 0). Extras pass a single-entry array.

### RAR5 is proven, not assumed

`bsdtar` reading these specific RAR5 CBRs is verified twice: in the harness (fixture) and live
against the real Chew files before "done" is claimed.

## 7. Non-goals and discarded alternatives

**Non-goals**

- No recursive demux (one nesting level only).
- No changes to manga or Biblio ingest.
- No per-issue naming intelligence for loose-issue packs beyond the label parser above.
- No new UI surfaces, settings, or affordances.
- No streaming/reading from inside the pack — reading stays download-fed from canonical CBZs.

**Discarded alternatives (with reasons)**

- *Publish the pack as one giant assembled edition* (via `publishAssembledEdition`): breaks
  per-volume progress, resume, and delete; welds extras into the read; produces a multi-GB
  monolith. Rejected.
- *12 top-level tiles:* rejected by Hemanth (locked: one grouped series).
- *Parallel per-volume workers:* disk thrash and violates the single-lane design that the whole
  CBZ-in-place arc's safety analysis assumes. Rejected.
- *Read nested archives in place without demux:* fragile double-decompression on the read path
  and violates the canonical-CBZ library architecture. Rejected.
- *Flat 12-volume crossing chain / per-volume-then-bonus interleave:* rejected by Hemanth for
  main-story-only crossing (locked).

## 8. Acceptance criteria (observable)

**Harness (RED first — new `tests/comic_downloader_*_harness.cpp`, mirroring the
ingest-harness fixture/isolation pattern; additive CMake target, `native/CMakeLists.txt` edit
declared on `agents/chat.md` first):**

1. Nested-pack fixture (ZIP containing 2–3 small real CBZs via `CbzArchive::writeImagesAtomic`
   plus one tar-renamed-`.cbr`) ingests into N index entries sharing seriesId/seriesTitle, with
   parsed labels, correct main/extra roles, and correct order.
2. Pack archive and extract tree are deleted only after the final child verifies; a
   failure-injected run preserves the pack.
3. Re-running demux over the same pack creates zero duplicates.
4. A manifest with missing children at `loadIndex()` re-enqueues exactly the missing ones.
5. Empty-archive and mixed-tree cases behave exactly as today (no-pages fail; single-issue
   ingest).
6. Existing single-comic ingest harnesses stay green.
7. Harness run 3–4× for flake.

**Live (Hemanth's eyes are the gate):**

8. Retry on the failed Chew row re-uses the preserved 1.46 GB pack (no re-download), shows one
   folded "Chew — 12 volumes" progress line, and lands 12 readable volumes under one Chew shelf.
9. Reader next-chain walks Vol. 1 → Vol. 8 only; extras sit in the Extras group and open solo.
10. The pack file is gone afterward (~1.46 GB reclaimed) and every volume opens and looks right
    on screen.

**Process:** Fable-advisor consult before commit (main seat is Opus this arc); every advisor claim
re-verified against source. Commits by explicit pathspec inside the nested Colosseum repo; push
after commit. Any breakage routes through `brotherhood-systematic-debugging`.
