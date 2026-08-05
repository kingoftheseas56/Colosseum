# Colosseum Download Grouping + Torrent Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this
> plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work inline in the existing
> checkout; do not create a branch, worktree, or subagent workspace (Rule 28).

**Goal (Hemanth's option B, ratified 2026-08-05):** Multi-file downloads display as ONE grouped
row with a fold — the shipped season pattern, extended to manga volume batches and multi-part
comics — plus the two real defects fixed: Biblio torrents are invisible on the Downloads page, and
book torrents vanish on restart. **No new UI furniture. No Transfers table.**

**Why this plan exists instead of the Transfers plan:** Hemanth challenged the scope of
`2026-08-05-colosseum-transfers.md` — showing multi-file downloads clearly and controlling torrent
transport are two different problems, and the first is already solved by shipped machinery. The
Transfers plan is **parked**, not deleted; it must earn its place separately on transport control
(peers, speeds, pause/resume, seeding). See the amendment note in
`docs/superpowers/specs/2026-08-05-colosseum-transfers-design.md`.

**Architecture:** `groupJobs()` in `DownloadsPage.qml` is already generic — it collapses any rows
sharing a `groupKey` into one header with aggregate progress, a collapsible fold of member rows,
and a group-level cancel. Only Theatre emits `groupKey` today. This plan emits it for manga volume
batches (natural key: the shared infoHash from `downloadNyaaBatch`) and multi-part comics (natural
key: the GetComics post id), and generalizes the group's title/label so a non-Theatre group does
not read "— Season 0".

**Tech Stack:** Qt 6 C++ (`MangaTankobanService`, `ComicDownloader`, `LocalDownloads`,
`BookTorrents`, `BookTorrentDownloader`), QML (`DownloadsPage.qml`), existing ledger pattern
(`MangaVolumeRequestLedger` as the shape donor for books).

## Ground truth (verified 2026-08-05, master)

- `qml/DownloadsPage.qml:315` `groupJobs()` keys on `(j.world) + "|" + (j.groupKey || j.id)` —
  **generic**, no Theatre assumption in the grouping itself.
- Group UI already shipping: aggregate progress bar, collapsible fold of member rows,
  group-level cancel (the cancel path was crash-fixed tonight, `00ae741`).
- `native/engine/LocalDownloads.cpp:431` — **only** the `m_videos` (Theatre) branch passes
  `groupKey` through. Manga chapters, manga volumes, and comics branches do not.
- Consequence today: a real 10-volume manga batch (`downloadNyaaBatch(volumeIds, infoHash)`,
  `MangaTankobanService.h:133`) renders as **10 flat rows**. The batch download works; only its
  display is ungrouped.
- `groupJobs()` title derivation hardcodes season phrasing:
  `title = single ? first.title : (seriesTitle || first.title) + " — Season " + season` — a
  comics or volume group would render "— Season 0".
- `canCancel` is universal across worlds (`LocalDownloads.cpp:470`); `canPause`/`canResume` are
  Theatre-only **by honesty** — the other engines have no pause, so no dead buttons are drawn.
  This plan does NOT add pause; that belongs to the parked Transfers arc.
- Books: `BookTorrents` is not passed to `LocalDownloads` (`main.cpp:924` receives the HTTP
  `books`), and `BookTorrentDownloader` keeps no active-request ledger — finished copies survive
  via `books-torrent/index.json`, live jobs do not.

## Global Constraints

- No new UI surface, no new page, no Transfers table. This plan adds ONE field to existing rows
  and generalizes ONE label.
- Do not add pause/resume for non-Theatre worlds — the engines cannot honor it; absent beats dead.
- Do not touch media pickers, rankers, extraction, publication, or ledger formats (except the new
  book ledger). Existing lane harnesses must stay green untouched.
- Grouping must be inert where no group exists: a single-job "group" keeps today's exact single-row
  rendering (`g2.single === true` path), byte-for-byte.
- Shared-file discipline: `qml/DownloadsPage.qml`, `native/engine/LocalDownloads.{h,cpp}`,
  `native/main.cpp` are shared — DECLARE each touch on `Brotherhood/agents/chat.md` before editing,
  additive only.
- Commit per task by explicit pathspec, push with the commit; verify with
  `git diff --cached --name-only` before every commit (brothers hold WIP in this checkout).
- Build via `native/build-target.bat colosseum`; grep the log for
  `error C|error LNK|ninja: build stopped` before believing it. Qt runtime warnings ARE results.
- Reported green is not Hemanth's eyes. The grouping is a visual change — his eyes are the gate.

## File Map

**Modify**

- `native/engine/MangaTankobanService.cpp` — emit `groupKey` (shared infoHash) + `batchSize` on
  `activeVolumeJobs()` rows.
- `native/engine/ComicDownloader.cpp` — emit `groupKey` (post id) on `activeIssueJobs()` rows for
  multi-part jobs (inert until A1's parts exist; single-archive jobs stay single).
- `native/engine/LocalDownloads.cpp` — pass `groupKey` through for the manga-volume and comics
  branches (mirroring the existing Theatre line at 431).
- `qml/DownloadsPage.qml` — generalize the group title/label so non-Theatre groups read by member
  count ("10 volumes", "7 parts") instead of "— Season N"; group cancel wording follows suit.
- `native/torrent/BookTorrents.{h,cpp}` — `activeJobs()`, `downloadedBooks()`, public
  `cancelDownload()`.
- `native/engine/LocalDownloads.{h,cpp}` — receive `BookTorrents`, merge its rows, route its cancels.
- `native/main.cpp` — pass `bookTorrents` into `LocalDownloads`.
- `native/torrent/BookTorrentDownloader.{h,cpp}` — ledger hooks + `replayActive()` on construction.
- `native/CMakeLists.txt` — new ledger + harness sources.

**Create**

- `native/torrent/BookTorrentRequestLedger.{h,cpp}` — durable active book-torrent intents.
- `tests/download_grouping_test.mjs` — pure `groupJobs()` logic tests (grouping, title, single-row
  invariance).
- `tests/book_torrent_ledger_harness.cpp` — ledger write/replay/prune.
- `tests/test_download_grouping_p0.ps1` — the arc's P0 gate.

---

### Task 1: Group multi-file downloads — manga batches and multi-part comics together

**Files:** modify `native/engine/MangaTankobanService.cpp`, `native/engine/ComicDownloader.cpp`,
`native/engine/LocalDownloads.cpp`, `qml/DownloadsPage.qml`; create
`tests/download_grouping_test.mjs`, `tests/test_download_grouping_p0.ps1`.

Both halves land in one pass (Hemanth, 2026-08-05) — they touch the same three files and share one
title/label generalization, so splitting them would mean two passes over identical ground. The
manga half is **immediately visible** (10-volume batches group the moment this lands); the comics
half is **inert pre-wiring** until A1's multi-part fix emits one job per part, at which point Chew
groups with no further UI work.

**Interfaces:**
- `activeVolumeJobs()` rows gain `groupKey` (the shared infoHash from `downloadNyaaBatch`; absent
  for a solo volume) and `batchSize`.
- `activeIssueJobs()` rows gain `groupKey` (the GetComics post id) for jobs that declare a part
  set; single-archive jobs emit none.
- `LocalDownloads::activeJobs()` passes `groupKey` through for both branches, mirroring the
  existing Theatre line.
- `groupJobs()` gains a world-aware label: Theatre keeps "— Season N"; others use the series/post
  title plus a member count ("10 volumes", "7 parts"). Group cancel wording follows ("Cancel
  season" only for Theatre; "Cancel all" elsewhere).

- [ ] Write `tests/download_grouping_test.mjs` RED (pure JS over the `groupJobs` logic):
      10 volume rows sharing a groupKey collapse to one group with summed progress; N comic part
      rows sharing a post id collapse and title by count, never "Season 0"; Theatre season titling
      is unchanged; **and the negative controls** — a solo volume and a single-archive comic each
      stay a single row rendering identically to today (the `single` path must not regress).
- [ ] Emit `groupKey`/`batchSize` from `activeVolumeJobs()`; emit `groupKey` from
      `activeIssueJobs()` for part-set jobs; pass both through `LocalDownloads::activeJobs()`.
- [ ] Generalize the `groupJobs()` title/label derivation and the group cancel wording.
- [ ] Verify: JS test green; build green by log grep; boot smoke clean; **eyes-on** — a real
      10-volume batch shows one collapsible row with working group cancel, and a single-archive
      comic download still renders as one plain row. Commit + push.
- [ ] Post on `Brotherhood/agents/chat.md` that the grouping seam is live for A1: emit one job per
      part sharing the post id and the grouped row appears with no further UI work.

### Task 2: Biblio torrents visible in Downloads

**Files:** modify `native/torrent/BookTorrents.{h,cpp}`, `native/engine/LocalDownloads.{h,cpp}`,
`native/main.cpp` (declared).

- [ ] Harness RED: an active book torrent appears in `activeJobs()` (world biblio) and cancels via
      `LocalDownloads::cancel("biblio", id)`; a completed one appears in the Biblio vault rows;
      HTTP and torrent copies of one book never collide (explicit transport ids).
- [ ] Add `activeJobs()/downloadedBooks()/cancelDownload()` to `BookTorrents`; inject the pointer
      into `LocalDownloads` (ctor arg + `main.cpp` wiring); merge and route.
- [ ] Verify: harness green, build green, boot smoke clean; **eyes-on** — a real book torrent
      appears under "Now arriving". Commit + push.

### Task 3: Book torrents survive restart

**Files:** create `native/torrent/BookTorrentRequestLedger.{h,cpp}`,
`tests/book_torrent_ledger_harness.cpp`; modify `native/torrent/BookTorrentDownloader.{h,cpp}`,
`native/CMakeLists.txt` (declared).

- [ ] Harness RED: intent written on start, pruned on finish/cancel; `replayActive()` re-adds via
      the same public download path (never a shortcut) and tolerates missing engine resume data.
- [ ] Ledger at `<AppData>/books-torrent/book-requests.json`, shape-mirroring
      `MangaVolumeRequestLedger`; replay on construction exactly as manga does.
- [ ] Verify: harness green; **kill-and-relaunch smoke** — a live book torrent returns after a
      hard kill. Commit + push.

### Task 4: Sweep + eyes-on

- [ ] Re-run every harness + `test_download_grouping_p0.ps1` fresh; append a results table here.
- [ ] Negative controls stated explicitly: single-job downloads render exactly as before (all three
      worlds), and no pause/resume affordance appeared anywhere it cannot work.
- [ ] Eyes-on with Hemanth: a 10-volume manga batch as one grouped row; a book torrent visible and
      surviving a restart. His eyes close this, not the harness table.
- [ ] Close: append results, recap on chat, final commit + push.

---

## Execution notes

- **Task 1 carries the whole visible win** — the manga half needs nobody; land it first and get it
  in front of Hemanth before continuing.
- Tasks 2–3 are lifted from the parked Transfers plan (its Tasks 2–3) unchanged in substance.
- **Deliberately excluded** (belongs to the parked Transfers arc, and only if it earns its place):
  the transfer table, peers/seeds/speeds, pause/resume for non-Theatre worlds, seeding policy,
  auto-yield, tracker/peer management, generic magnet add.
- A1's multi-part comics fix stays A1's lane on A1's clock; Task 2 only pre-wires the seam.
