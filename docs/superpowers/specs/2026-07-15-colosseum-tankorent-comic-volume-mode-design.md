# Colosseum Tankorent Comic — Collected-Edition Volume-in-Pack Design

> Agent 0 (Claude), 2026-07-15. Tankorent enrichment lane. Mirrors the manga
> [Tankoban Nyaa Volume Mode](2026-07-14-colosseum-tankoban-nyaa-volume-mode-design.md)
> — same logic, comic vocabulary. Builds on the comics
> [Alternate Torrent Sources v2](2026-07-15-colosseum-comics-alternate-torrent-sources-v2-design.md).

## Purpose

Give a comics **collected edition** (Compendium, Omnibus, TPB, Deluxe, Absolute, Vol…) the same
automatic acquisition manga volumes already have: you tap a torrent, and the app pulls **exactly
that edition** out of a full-series pack — no second file-pick. Collected editions rarely have
standalone torrents; they live inside mega-packs (e.g. Nem's *"Invincible Collection (000-144,
Spin-offs, TPBs v01-v25, Compendiums v01-v03+Extras)"*), so we match the edition **by name/coverage
to its file or folder inside the pack**, exactly like Stremio wires an episode to a file inside a
season pack and like manga wires Vol N to its file.

## Locked product decisions (Hemanth, 2026-07-15)

1. **Extend, don't rebuild.** Evolve the existing comics alt-sources pieces (`ComicTorrentRanker`,
   `ComicTorrentFilePicker`, `ComicTorrentDownloader`) in place. Converge with manga into one shared
   Tankorent engine *later*, once comics is proven — do **not** touch manga's shipped code now.
2. **Full manga-Tankoban parity** — "in terms of logic and everything": automatic matching,
   coverage grammar, trusted uploaders, restart-safe ledger, one-pack-serves-many.
3. **Automatic.** Search Compendium #1 → tap the torrent → download Compendium #1. The manual file
   picker survives only as a **fallback** for genuinely ambiguous packs (manga's `Ambiguous`).
4. **Name/coverage match is the PRIMARY path** (collected editions appear by name in packs);
   issue-range selection is the **fallback** for issue-only packs.
5. **Format-aware.** A mega-pack carries issues *and* TPBs *and* Compendiums at once. "Compendium #1"
   must lock onto the Compendium file — never TPB v01 or issue #1. Matching is scoped by collection
   format + number, not number alone.
6. **Trusted uploaders**, mirroring Nyaa. First trusted comics uploader: **Nem**.

## Canonical data model

Source of truth is the GCD catalog edition (`comics_db.gen.js` / `ComicsApi.js`): a series carries
`editions[]`, each `{ title, format, isbn, collects, locg_comic_id, slug, … }`. From an edition we
derive a **match target**:

```
ComicEditionTarget {
  seriesTitle   // "Invincible"
  format        // Compendium | Omnibus | TPB | Deluxe | Absolute | Volume | HC | Collection
  number        // 1            (parsed from title/format; may be absent for one-shots)
  isbn          // 9781607064114 (strongest identity signal when present)
  collects      // parsed issue set: {0, 14,15,16, 18..36, 38, 40..47}  (issue-range fallback)
}
```

`format` + `number` come from the edition title (`"Invincible Compendium #1"` → `Compendium`, `1`)
cross-checked against the `format` field. `collects` is parsed from the edition's collected-issue
string into a canonical integer set once, reused by both ranking and the issue-range fallback.

## Discovery and ranking

Search rides the existing `TankorentSearchService` (piratebay / exttorrents / torrentscsv) via
`ComicTorrents`; queries come from `ComicTorrentQueryPlanner` (edition title, ISBN, collected-range).
`ComicTorrentRanker.rankForEdition(...)` gains two new signals on top of its current
ISBN/title/issue-run grade:

- **Coverage grade (new).** Parse the torrent NAME for format-scoped ranges (see grammar below). If a
  range of the target's format covers the target number, add a strong coverage score and mark
  evidence `COVERAGE`. This is the fix for the *"Invincible Collection (…Compendiums v01-v03…)"*
  pack currently scored **WEAK** despite containing Compendium 1 — it becomes **STRONG**.
- **Uploader trust (new).** `comics_uploader_trust.json` (schema mirrors `manga_uploader_trust.json`:
  `tier1` / `tier2` / `blocked`) parsed from the release tag (`(- Nem -)`, `[group]`). Trust lifts
  ranking exactly like Nyaa's tiers. Seed `tier1: ["Nem"]`.

Ranking stays **advisory** (stable-sort, never silent auto-pick of a *torrent*) — the automatic step
is the file-in-pack isolation *after* the user taps a result. Size is a weak signal for comics
(ExtraTorrents reports `0 B`), so coverage + trust carry confidence.

## The coverage grammar (the new brain) — `ComicCoverage`

New pure-logic native module (analogue of manga's `detectCoverage` / `coverageIncludesTarget`,
`MangaVolumeFilePicker.cpp:38-57,159-170`), but **format-aware**. Two entry points, both pure and
headless-testable:

- `detectComicCoverage(text) -> [{ format, lo, hi }]` — scan a string (torrent name OR a filename OR
  a directory segment) for collection markers and their number ranges:
  - Format tokens: `Compendium(s)`, `Omnibus`, `Deluxe`, `Absolute`, `TPB` / `Trade Paperback`,
    `Vol` / `Volume` / `v`, `Book`, `HC` / `Hardcover`, `Collection`. Normalized to canonical format.
  - Range forms: `v01-v03`, `1-3`, `01`, `#1`, `Book One`/`Book Two` (worded), zero-stripped to
    canonical integers. A range wins over a single, mirroring manga.
  - Multiple formats coexist in one string (`"TPBs v01-v25, Compendiums v01-v03"` → two entries).
- `coverageCovers(coverages, target) -> bool` — true iff some entry has `format == target.format`
  **and** `lo <= target.number <= hi`. Format equality is required — the core comics rule.

Bare issue numbers (`Invincible 025`) are **not** collection coverage — they feed the issue-range
fallback, never the format-coverage match (keeps loose issues out of "Compendium" matching, the
comic analogue of manga keeping "Chapter 2" out of volume coverage).

## Source selection and acquisition

### Coverage path (primary — automatic)

Mirrors `MangaVolumeTorrentDownloader` discipline (add **paused** → inspect metadata → set
priorities → start; `MangaVolumeTorrentDownloader.h:8-14`):

1. Tap a result → `ComicTorrents.downloadTorrentSource(editionId, infoHash)` adds the torrent
   **paused**, journals the intent, waits for metadata.
2. On metadata, `ComicTorrentFilePicker` (extended) runs `ComicCoverage` over the real file list:
   - **Base filename wins**, then parent directory segments deepest-first (so
     `Compendiums/Invincible Compendium v01/…` resolves to `Compendium 1`), mirroring
     `MangaVolumeFilePicker::resolveCandidate` (`:72-100`).
   - **Isolation tiers:** exact format+number in a filename → pick; exact format+number declared only
     by a directory (a folder of loose pages/issues) → pick that subtree; two equal candidates →
     `Ambiguous`; an inclusive combined archive (single omnibus file covering the target) →
     `CombinedArchive` (download whole, cannot split); none → `TargetMissing` → try fallback.
3. `unionPriorities` sets libtorrent priority **7** on the picked file(s)/subtree, **0** elsewhere
   (union so one pack serving several requested editions grabs all their files) — same as manga
   (`MangaVolumeFilePicker.cpp:185-192`).
4. `TorrentEngine::setFilePriorities` → `startTorrent`. On finish, ingest the archive/subtree into
   the comics loose-page store like `ComicDownloader` does today.

### Issue-range path (fallback — automatic)

When coverage yields `TargetMissing` but the pack contains loose issue files: match the edition's
parsed `collects` set against issue-numbered files, select **all** matching issue files (multi-file
union, priority 7), and ingest them as one reading unit in `collects` order. This is the comics-only
capability manga never needed (manga volume = one file).

### Manual fallback (safety net only)

`Ambiguous` or `CombinedArchive`-with-choice surfaces the existing `ComicTorrentSourcesPage` /
`ComicTorrentArchivePicker` for a human pick (`chooseFile`), unchanged. Automatic is the default;
the picker only appears when the grammar honestly cannot isolate.

## State machine and durable memory — `ComicRequestLedger`

Comics has **no** restart resume today (`ComicTorrentDownloader` is in-memory `Job*` only). Port
`MangaVolumeRequestLedger` (`MangaVolumeRequestLedger.h`):

- One `EditionRequestRow` per requested edition: `{ editionId, infoHash, magnetUri, seriesId,
  format, number, savePath, pickedFileIndices[], state }`.
- States: `awaiting_metadata → downloading → validating → completed | failed | cancelled`, atomic via
  `QSaveFile`.
- **Restart replay:** on ctor, re-add every active row's torrent **paused**, forget the prior pick,
  re-resolve from re-emitted metadata (honest re-derivation), then resume. `markDownloading` records
  the resolved file indices durably.
- **One pack → many editions:** an already-live torrent **joins** the existing job and grows its
  intent set; priorities re-union; cancel re-narrows to survivors (manga's model exactly).

`pickedFileIndices` is a **list** (not manga's single index) because the issue-range path selects
several files per edition — the one structural widening over the manga ledger.

## Colosseum-native component boundary ("QML paints, C++ decides")

All matching, coverage, priorities, ledger, and transport are **C++** in `native/torrent/`
(the machine). QML only renders the results list + progress and forwards taps. Extended/new files:

| File | Change |
|---|---|
| `native/torrent/ComicCoverage.{h,cpp}` | **NEW** — pure format-aware coverage grammar. |
| `native/torrent/comics_uploader_trust.json` | **NEW** — `{tier1:["Nem"],tier2:[],blocked:[]}`. |
| `native/torrent/ComicRequestLedger.{h,cpp}` | **NEW** — restart-safe edition-request ledger. |
| `native/torrent/ComicTorrentRanker.{h,cpp}` | Extend — coverage grade + uploader trust. |
| `native/torrent/ComicTorrentFilePicker.{h,cpp}` | Extend — coverage auto-isolation + issue-range + tiers. |
| `native/torrent/ComicTorrentDownloader.{h,cpp}` | Extend — add-paused→inspect→isolate→priorities→start; ledger; intents. |
| `qml/ComicTorrentSourcesPage.qml` | Minor — coverage/trust badges; picker demoted to fallback. |

## Reader integration & durable representation

Ingested edition lands in the comics loose-page store exactly as `ComicDownloader` produces today
(one indexed page dir per edition), so the comic reader opens it with no reader change. The
issue-range path stitches its N issue files into a single page sequence in `collects` order.

## Testing strategy (house patterns)

- **Headless logic harnesses** (`tests/*.qml` or C++ GoogleTest, offscreen, exit-code verdict):
  - `ComicCoverage`: `"Compendiums v01-v03"` covers `(Compendium,1)` but NOT `(TPB,1)`; `"TPBs
    v01-v25"` covers `(TPB,7)`; bare `"Invincible 025"` yields no coverage; worded `Book One`.
  - `ComicTorrentFilePicker`: filename pick, directory pick, Ambiguous, CombinedArchive,
    TargetMissing→issue-range, union priorities.
  - `collects` parser: `"#0, #14-16, #18-36, #38, #40-47"` → correct integer set.
- **Grep contracts:** trust file wired into ranker; coverage grade present; downloader adds paused.
- **Build** (`./build-msvc.bat` → `BUILD_OK`) + qmllint.
- **Real eyes-on smoke** (Hemanth): search "Invincible Compendium #1" → Nem pack ranks STRONG → tap →
  Compendium 1 downloads (only its file) → opens in the reader. A TPB and an issue-only pack each too.

## Build order (full feature; sequenced)

- **Phase 1** — `ComicCoverage` + coverage grade in ranker + `comics_uploader_trust.json` (Nem) +
  automatic file-isolation in the picker/downloader. This *is* "click Compendium 1, get Compendium 1."
- **Phase 2** — issue-range fallback (`collects` parser + multi-file selection + stitched ingest).
- **Phase 3** — `ComicRequestLedger` (restart-safe replay) + one-pack-serves-many intents.

## Explicit exclusions (v1)

- No shared cross-media Tankorent engine yet (comics stays its own pieces; converge later).
- No new indexers (piratebay/exttorrents/torrentscsv only).
- No manga-code changes.
- No auto-pick of a *torrent* from search (user still taps the source; only the file-in-pack is auto).

## Definition of Done

- Search Compendium #1 → tap → Compendium #1 (only) downloads and opens, no file-pick.
- A pack advertising a matching-format range ranks STRONG (not WEAK); trusted uploaders lift ranking.
- Format-scoped: a Compendium target never picks a TPB/issue file of the same series.
- Issue-only packs resolve via the `collects` issue-range.
- A download resumes after an app restart; one pack serves multiple requested editions.
- Genuinely ambiguous packs fall back to the manual picker.
- Logic harnesses + grep contracts green; `BUILD_OK`; qmllint clean; eyes-on smoke passes.
