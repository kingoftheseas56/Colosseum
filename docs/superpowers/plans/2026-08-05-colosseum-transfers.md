# Colosseum Transfers Implementation Plan — unified Downloads drawer + BitTorrent table

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this
> plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work inline in the existing
> checkout; do not create a branch, worktree, or subagent workspace (Rule 28).

**Design authority:** `docs/superpowers/specs/2026-08-05-colosseum-transfers-design.md` (locked by
Hemanth 2026-08-05). Ratified look: `agents/colosseum-downloads-transfers-table-mock.html` (v2).
This plan implements the spec; where the two disagree, the spec wins and the plan gets amended.

**Goal:** The Downloads page gains a Transfers section — one row per transfer with a "For" column,
classic properties pane (General/Files/Trackers/Peers), engine toolbar with auto-yield — plus
session seeding on by default, restart survival for all torrent lanes, Biblio torrent visibility,
and a transport-agnostic composite contract for multi-part directs. Projection over the existing
engine; zero engine-ownership changes.

**Architecture:** One new C++ facade (`TransfersManager`) *observes* the shared `TorrentEngine`
and the three media downloaders, projects an incremental transfer list model + per-transfer detail
adapters to QML, and routes commands: transport actions → engine handle, item semantics → owning
downloader. An owner registry (ownerType/ownerIds/safeDeletePolicy) feeds the "For" column, cancel
routing, and the shared-pack deletion guard. Media facades keep sole ownership of job identity,
file selection, ledgers, and publication.

**Tech Stack:** Qt 6 C++ (QAbstractListModel, QSettings), QML on the existing DownloadsPage,
existing `TorrentEngine` API (`allStatuses`, `torrentFiles`, `torrentFileProgress`,
`torrentDetails`, tracker/peer APIs, pause/resume/recheck/reannounce, speed/queue/seeding rules),
existing ledger patterns (`MangaVolumeRequestLedger` as the shape donor for books).

## Global Constraints

- One engine, one session, **one queue authority** — never construct a second `TorrentEngine`;
  never add an app-level queue beside the engine's (`TB2`'s double-queue scar).
- `TransfersManager` owns projection and commands only: no job identity, no file selection, no
  publication, no duplicate ledgers. `TorrentRepository` stays dormant — do not construct it.
- Do not modify media pickers, rankers, ledger formats (except the new book ledger), extraction,
  or publication paths. Existing harnesses for those lanes must stay green untouched.
- Incremental updates only: dataChanged by role, stable row identity by canonical infohash; never
  a rebuild-the-table timer. Only the visible properties tab polls detail.
- Rendering follows mock v2: gray/black/white + gold `#c9a15a` accents, SVG only, mono numerals,
  no color, no emoji. The Transfers section renders only when at least one transfer exists.
- File priorities are READ-ONLY this arc; needed files show "chosen for <item>" from the owner
  registry, never from filename guessing.
- Shared-file discipline: `qml/DownloadsPage.qml`, `qml/Main.qml`, `native/main.cpp`,
  `native/CMakeLists.txt` are shared — DECLARE each touch on `Brotherhood/agents/chat.md` before
  editing, additive edits only.
- Commit per task by explicit pathspec, push with the commit; verify staged set with
  `git diff --cached --name-only` before every commit (brothers have WIP in this checkout).
- Build with `native/build-target.bat colosseum` (+ the task's harness target); grep the log for
  `error C|error LNK|ninja: build stopped` before believing it; boot smoke after QML tasks. Qt
  runtime warnings ARE test results.
- Never claim a criterion from a green harness alone where the spec's acceptance (§8) names
  visible behavior — final sweep is eyes-on with Hemanth.

## File Map

**Create**

- `native/engine/TransfersManager.h` / `.cpp` — facade: projection model, owner registry, detail
  adapters, command routing, yield, settings application.
- `native/torrent/BookTorrentRequestLedger.h` / `.cpp` — durable active book-torrent intents
  (shape-mirrors `MangaVolumeRequestLedger`).
- `qml/TransfersSection.qml` — toolbar + table + properties pane (the whole mock-v2 surface).
- `tests/transfers_manager_harness.cpp` — projection, owner mapping, shared-hash single-row,
  command routing, deletion guard.
- `tests/book_torrent_ledger_harness.cpp` — ledger write/replay/prune.
- `tests/transfers_settings_harness.cpp` — persistence, readback, engine application order.
- `tests/transfers_section_harness.qml` — QML surface: residency, columns, states, pane tabs.
- `tests/test_transfers_p0.ps1` — drives the harnesses + boot smoke; the arc's P0 gate.

**Modify**

- `native/torrent/BookTorrents.h` / `.cpp` — `activeJobs()`, `downloadedBooks()`, public
  `cancelDownload()`; ledger integration + replay.
- `native/torrent/BookTorrentDownloader.h` / `.cpp` — ledger hooks; policy-aware completion
  (seeding keeps the handle); replay entry point.
- `native/engine/LocalDownloads.h` / `.cpp` — receive `BookTorrents`; merge its rows; route its
  cancels (closes the Biblio invisibility defect).
- `native/engine/ComicDownloader.cpp` + `native/torrent/ComicTorrents.cpp` — owner-registry
  registration only (expose existing intent facts; no behavior change).
- `native/engine/MangaTankobanService.cpp` — owner-registry registration only.
- `native/torrent/engine/TorrentEngine.h` / `.cpp` — small const getters where readback needs
  them (current global limits/queue caps) and a completion-policy seam; no behavioral rework.
- `native/main.cpp` — construct `TransfersManager`, expose as `Transfers`; apply persisted
  policy at startup (replaces hardcoded `setGlobalSeedingRules(0.f, 1)`).
- `native/CMakeLists.txt` — new sources + harness targets.
- `qml/DownloadsPage.qml` — mount `TransfersSection` below Now arriving; "in transfers below"
  pointer on torrent-backed item rows.
- `qml/Main.qml` — engagement signal for auto-yield (player/reader active → `Transfers.setYield`).

---

### Task 1: TransfersManager projection + owner registry (read-only core)

**Files:** create `native/engine/TransfersManager.{h,cpp}`, `tests/transfers_manager_harness.cpp`;
modify `native/CMakeLists.txt` (declare on chat first).

**Interfaces:** `QAbstractListModel` roles: name, forText, ownerIds, sizeDone/sizeTotal,
progress, status (downloading/queued/paused/seeding/failed+reason/partNofM), seeds, peers,
downRate, upRate, eta, ratio, infoHash, transport (torrent/direct), shared (bool).
`registerOwner(hash, ownerType, ownerId, label, neededFiles)` / `unregisterOwner(...)` called by
the media facades. Engine signals drive row updates; no polling timer of its own.

- [ ] Write the harness RED first: fake engine statuses → rows project correctly; two owners on
      one hash → ONE row, `forText == "Vol. 3 + Vol. 4"`; owner unregister updates the row;
      failed status carries its reason; direct-composite transport renders "—" fields as null.
- [ ] Implement the model + registry; wire engine progress/completion/error signals to
      role-level `dataChanged`; stable ordering (active first, then queued, then seeding).
- [ ] Register owners from comics, manga, and books facades (additive one-liners at their
      existing add/complete/cancel seams).
- [ ] Verify: harness green; `build-target.bat colosseum` green by log grep; commit
      `[<agent>] feat(transfers): projection core + owner registry` (pathspec), push.

### Task 2: Biblio visibility — BookTorrents into LocalDownloads

**Files:** modify `native/torrent/BookTorrents.{h,cpp}`, `native/engine/LocalDownloads.{h,cpp}`,
`native/main.cpp` (declared).

- [ ] Extend the existing `local_downloads`-family harness coverage RED: an active book torrent
      appears in `activeJobs()` rows (world biblio, transport torrent) and cancels through
      `LocalDownloads::cancel("biblio", id)`; a completed one appears in the vault rows.
- [ ] Add `activeJobs()/downloadedBooks()/cancelDownload()` to `BookTorrents`; inject the pointer
      into `LocalDownloads` (new ctor arg, main.cpp wiring); merge + route with explicit
      transport IDs so HTTP and torrent copies of one book never collide.
- [ ] Verify: harness green, build green, boot smoke clean; commit + push.

### Task 3: Book torrent restart survival

**Files:** create `native/torrent/BookTorrentRequestLedger.{h,cpp}`,
`tests/book_torrent_ledger_harness.cpp`; modify `BookTorrentDownloader.{h,cpp}`,
`native/CMakeLists.txt`.

- [ ] Harness RED: intent written on start, pruned on finish/cancel; `replayActive()` re-adds
      via the same public download path (never a shortcut), tolerates missing engine resume.
- [ ] Ledger at `<AppData>/books-torrent/book-requests.json`, shape-mirroring the manga ledger;
      replay on construction, exactly as manga does.
- [ ] Verify: harness green; kill-and-relaunch smoke shows the live book job return; commit + push.

### Task 4: The Transfers surface — toolbar + table, read-only

**Files:** create `qml/TransfersSection.qml`, `tests/transfers_section_harness.qml`,
`tests/test_transfers_p0.ps1`; modify `qml/DownloadsPage.qml` (declared, additive mount only).

- [ ] QML harness RED: section absent with zero transfers; appears with one; columns per mock v2;
      seeding rows gold; selection stable across model updates; direct rows show "—".
- [ ] Build `TransfersSection.qml` to mock v2: toolbar (Pause all · Resume all · yield chip ·
      live ↓/↑ · Settings — commands stubbed disabled until Task 6/8), table with gold-edge
      selection; mono numerals; house scrollbar.
- [ ] Mount below Now arriving in `DownloadsPage.qml`; torrent-backed item rows gain the quiet
      "in transfers below" meta suffix.
- [ ] Verify: harness + P0 script green; boot smoke; screenshot for the record; commit + push.

### Task 5: Properties pane — General / Files / Trackers / Peers, read-only

**Files:** modify `native/engine/TransfersManager.{h,cpp}`, `qml/TransfersSection.qml`,
harnesses.

**Interfaces:** `Q_INVOKABLE QVariantMap details(hash)`, `QVariantList files(hash)` (per-file:
name, progress, priority, lockedForLabel|skipped), `trackers(hash)`, `peers(hash)`; one
`setDetailFocus(hash, tab)` so ONLY the visible tab refreshes on the engine's cadence.

- [ ] Harness RED: files adapter marks needed files with their owner label from the registry and
      collapses the skipped tail count; detail refresh emits nothing when unfocused (probe with
      a counter, negative-controlled).
- [ ] Implement adapters over `torrentDetails/torrentFiles/torrentFileProgress/trackers/peers`;
      pane QML per mock v2 incl. the shared-transfer honesty note; no country column.
- [ ] Verify: harnesses green; eyes-on a real manga pack shows "chosen for Vol. N" locks;
      commit + push.

### Task 6: Transport commands + the two cancels + deletion guard

**Files:** modify `TransfersManager.{h,cpp}`, `qml/TransfersSection.qml`, harness.

- [ ] Harness RED: pause/resume flip engine state and row status; pause-all/resume-all cover
      every live handle; transfer-cancel with files refuses while a second live intent feeds the
      hash and names the sibling in the error; item-cancel still routes to the owning downloader
      untouched; paused state survives a simulated restart (ledger + resume replay restores
      paused).
- [ ] Implement command routing (engine for transport; owner facade for item semantics);
      enable toolbar/pane actions: Pause, Resume, Recheck, Reannounce, Open folder.
- [ ] Verify: harnesses green; live smoke: pause a real pack → both fed item rows visibly wait,
      resume completes and publishes; commit + push.

### Task 7: Tracker and peer management

**Files:** modify `TransfersManager.{h,cpp}`, `qml/TransfersSection.qml`, harness.

- [ ] Harness RED: tracker add/edit/remove round-trip through the engine snapshot; peer ban
      lands in the engine's persisted ban list; malformed URLs/endpoints rejected with reasons.
- [ ] Wire pane actions (add/edit/remove tracker, reannounce, add peer, ban with confirmation).
- [ ] Verify: harness green; live eyes-on against a real torrent; commit + push.

### Task 8: Settings sheet — one policy owner, honest readback

**Files:** create `tests/transfers_settings_harness.cpp`; modify `TransfersManager.{h,cpp}`,
`TorrentEngine.{h,cpp}` (const getters only), `qml/TransfersSection.qml`, `native/main.cpp`
(declared), `native/CMakeLists.txt`.

- [ ] Harness RED: every setting persists (QSettings `transfers/*`), re-opens showing the value
      in force (getter-backed, never constructor defaults — the named TB2 anti-pattern), and is
      applied to the engine at startup BEFORE any transfer starts.
- [ ] Sheet per spec §5.3: global ↓/↑ limits, queue size (the only queue authority), seeding
      on/off, manual throttle, auto-yield on/off. Replace `main.cpp`'s hardcoded
      `setGlobalSeedingRules(0.f, 1)` with policy application.
- [ ] Verify: harness green; restart smoke shows persisted values live; commit + push.

### Task 9: Session seeding

**Files:** modify `TransfersManager.{h,cpp}`, `BookTorrentDownloader.cpp`,
`TorrentEngine.{h,cpp}` (completion-policy seam), `qml/TransfersSection.qml`, harnesses.

- [ ] Harness RED: with seeding ON, completion keeps the handle (books stop removing it;
      comics/manga unchanged — they already keep); row enters Seeding with live ratio; per-row
      Stop seeding removes the handle, never files; seeding OFF restores today's remove-on-
      completion; app teardown ends all seeding.
- [ ] Implement behind the Task-8 policy; Seeding rows per mock (gold bar, ratio, Stop action).
- [ ] Verify: harness green; live smoke: finish a real download → Seeding appears, app close
      ends it; commit + push.

### Task 10: Auto-yield + manual throttle

**Files:** modify `TransfersManager.{h,cpp}`, `qml/Main.qml` (declared, additive),
`qml/TransfersSection.qml`, harness.

- [ ] Harness RED: `setYield(true)` applies the trickle as a global engine rate limit and raises
      `yielding`; release restores configured limits (not unlimited — the Task-8 values);
      manual throttle engages the same path; the two compose without fighting.
- [ ] Wire engagement from `Main.qml`'s existing player/reader activity state; gold chip
      "Yielding — film playing" / "Throttled" per source.
- [ ] Verify: harness green; live smoke: start a film → chip within seconds + rates trickle,
      stop → full speed returns; commit + push.

### Task 11: Composite contract for multi-part directs

**Files:** modify `TransfersManager.{h,cpp}`, `qml/TransfersSection.qml`, harness.

**Interfaces (the §7.4 contract, exactly):** a composite publisher provides parts
[{name, size, state, progress, failReason}], aggregate expected bytes, currentPart; the model
renders "Part N of M", fills torrent-only columns with "—", and the Files tab lists parts.

- [ ] Harness RED against a fixture publisher (A1's real feed lands later, this proves the
      seam): rows render, a failed part names itself, aggregate size never lies (expected vs
      received mismatch surfaces as state, never silent success — the Chew rule).
- [ ] Implement the contract adapter + registration API a lane downloader can call.
- [ ] Verify: harness green; commit + push; note on chat that the seam is live for A1.

### Task 12: Acceptance sweep + eyes-on

- [ ] Run every prior harness + `test_transfers_p0.ps1` fresh; map each green to spec §8's
      eleven criteria in a results table appended to this plan.
- [ ] Confirm perf criteria honestly: no-flicker/no-selection-loss under live updates, zero
      refresh traffic from unfocused tabs (counter probe, quiet machine).
- [ ] Eyes-on session with Hemanth: the §6.2 journey end-to-end on real downloads — his eyes
      are the gate, not the harness table.
- [ ] Close: append results + host-truth notes here, recap on chat, final commit + push.

---

## Execution notes

- **Order is the risk gradient:** Tasks 1–5 are read-only (nothing can break a download);
  commands arrive only in Task 6 after projection is trusted; policy (8–10) after commands;
  the contract seam (11) last before the sweep.
- **Model routing (Hemanth's standing doctrine):** plan authored on Fable; execute on Opus.
- **Checkpoint rule:** any QML/visual quirk that eats 3–4 screenshot cycles → commit the tested
  slice, switch to a deterministic signal, move on.
- **Every shared-file touch** (`DownloadsPage.qml`, `Main.qml`, `main.cpp`, `CMakeLists.txt`)
  gets its chat declaration BEFORE the edit, per task, additive only.
