# Colosseum Transfers — the unified Downloads drawer + BitTorrent table (Design Spec)

- **Date:** 2026-08-05
- **Owner:** brainstormed by a scoped helper (Claude) with Hemanth; lane assignment happens at
  planning (the surface is cross-lane: Downloads page UX, comics, biblio, theatre all touch it)
- **Status:** Design locked (Hemanth, 2026-08-05) — spec self-reviewed, awaiting Hemanth's final read
- **Arc:** TRANSFERS (deliberately **not** named Tankorent — Agent 4's *Tankorent 2.0* is the video
  streaming challenger, a different arc; this arc must never blur with it)
- **Companions:** `TANKORENT_BITTORRENT_UI_UX_CODE_MAP.md` (donor map, TB2) and
  `COLOSSEUM_TANKORENT_CODE_MAP.md` (receiver map) — Hemanth-supplied source archaeology, claims
  ground-truthed against master 2026-08-05
- **Ratified mock:** `agents/colosseum-downloads-transfers-table-mock.html` (v2 — disposable, the
  written decisions here are authoritative). v1 (`colosseum-downloads-tankorent-drawer-mock.html`)
  was rejected on sight and is kept only as the record of why.

---

## 0. One-sentence promise

> One Downloads page where nothing downloads invisibly and nothing misbehaves silently — media
> rows stay media, and below them a real transfer table gives every torrent and multi-part
> download the full BitTorrent truth: one row, honest columns, working controls.

The arc was born from a real night: a Downloads cancel that did nothing, printed nothing, and left
no evidence (2026-08-05, the Chew incident). The design's standard is that such a night becomes
impossible to have blind.

## 1. Why this exists

TB2's Tankorent proved Hemanth wants manager-grade torrent control. Colosseum already imported the
entire engine — pause, resume, force-start, queue, speed limits, per-file priorities, trackers,
peers, bans all exist in `native/torrent/engine/TorrentEngine.*` and ship his comics, manga, and
books daily. What never came across is the window and the steering wheel: no surface shows a
single live torrent's true state, no torrent can be paused, a Biblio book torrent is invisible on
the Downloads page, and a book torrent mid-download silently vanishes on restart.

The transplant is therefore **projection and presentation, not engine work**. TB2's monolith
(3,289-line page, double queue authority, dead controls) is the anti-pattern; its capability set
is the inheritance.

## 2. Ground truth (inspected 2026-08-05, master)

- One shared `TorrentEngine`, one session, born dormant, `<AppData>/torrent-engine`. Global
  seeding hardcoded `setGlobalSeedingRules(0.f, 1)` in `main.cpp` (download-and-stop).
- Three independent media downloaders wrap it: books (`BookTorrentDownloader`), comics
  (`ComicTorrents`/`ComicTorrentDownloader`), manga volumes (`MangaVolumeTorrentDownloader`).
  Comics and manga journal intents and replay on restart; **books do not** (active book jobs die
  with the process; finished copies survive via `books-torrent/index.json`).
- `LocalDownloads` (the Downloads page read-model) receives the HTTP `BookDownloader`, **not**
  `BookTorrents` — torrent books never reach the page.
- Manga's shipped cancel semantics: one infohash can feed several volume intents; cancelling one
  recomputes the priority union and keeps the torrent if siblings live; teardown only on the last
  intent. Comic packs deliberately stay registered/seeding for the session after completion.
- `TorrentRepository` (SQLite) is compiled but dormant — constructed by nothing. It is **not**
  current truth and stays dormant this arc.
- The GetComics HTTP lane assumes one archive per post; multi-part posts download the first part
  and report success (the Chew defect, observed live 2026-08-05: 4.9 GB post arrived as 565 MB).

## 3. Scope

### In scope
- The **Transfers** section on the existing Downloads page: toolbar, table, properties pane.
- Engine settings sheet (speed limits, queue size, seeding, throttle) with honest readback.
- Auto-yield during playback/reading + manual throttle.
- Session seeding on by default, off switch, per-transfer stop.
- Restart survival for **all** torrent lanes (closes the book-torrent gap).
- Biblio torrent visibility in the Downloads read-model.
- The transport-agnostic **composite transfer contract** (multi-part HTTP rides the same table).
- Pause/resume for torrent transfers (today most lanes can only cancel).

### Out of scope (non-goals)
- Generic magnet add (deferred to its own arc; the contract accommodates it without rework).
- Discovery: indexer search UI, source-health panel, credentials (Colosseum's own source pages
  already own discovery; TB2's scraping stack does not make the trip).
- A standalone manager page — there is no new navigation door anywhere in this design.
- Replacing any media acquisition flow, picker, ledger, or publication path.
- Editable file priorities on media-owned torrents (arrives with generic add).
- Anything in Tankorent 2.0's streaming lane (video stays Stremio-served).
- Implementing the comics multi-part fix (A1's lane, separate work, plugs into this contract).

## 4. The decision ledger (as locked)

1. **One unified Downloads surface.** No standalone manager page, ever, this arc.
2. **The full BitTorrent look, Colosseum-skinned.** A classic transfer table + fixed properties
   pane — the proven genre, not an invented hybrid. (v1's inline item-row expansion was built,
   mocked, and rejected by Hemanth as confusing; the discipline of "one row per transfer,
   scannable columns, properties in a fixed place" is what makes the genre legible.)
3. **One row per transfer, with a "For" column.** The Colosseum invention that survives: every
   transfer names the library items it exists for ("Vol. 3 + Vol. 4"). Now-arriving item rows
   keep their simple item-level cancel and gain a quiet "in transfers below" pointer.
4. **Engine controls live in the Transfers toolbar.** Pause all · Resume all · yield chip ·
   live ↓/↑ totals · Settings. (The earlier "engine line in the page header" was superseded by
   the table having a natural toolbar home.)
5. **Auto-yield.** When a film is playing or a reader is open, transfers drop to a trickle and
   the gold "Yielding" chip says so; leaving restores full speed. Manual throttle exists in the
   settings sheet. Colosseum knows when you're watching; qBittorrent never could.
6. **Multi-part HTTP joins by contract.** The same table hosts multi-part direct downloads
   ("part 3 of 7", "—" in torrent-only columns). A1 implements the comics fix against this
   contract on its own clock.
7. **File priorities are read-only this arc**, each needed file locked with its reason
   ("chosen for Vol. 3"); skipped files dimmed ("skipped · not requested").
8. **Seeding on by default, session-scoped.** Finished torrents give back while the app runs;
   closing the app ends the obligation. Off switch in settings; per-transfer "Stop seeding".
   This deliberately **reverses** the shipped download-and-stop constant (Hemanth, 2026-08-05)
   and generalizes the comic-pack session-seeding precedent.
9. **Generic magnet add deferred** to its own arc.
10. **The TB2 kill list** (§10) is ratified — eight pieces do not make the trip.

### Constraints every later choice must respect
- One engine, one session, **one queue authority** — TB2's double-queue scar must not recur.
- The Downloads page stays media-first; the table lives inside it, below "Now arriving".
- Shared packs are deletion-safe (§7.3).
- Every live torrent job survives restart, all three lanes.
- Gray/black/white + gold accent, SVG only, no emoji; parity-grade, not MVP.
- Incremental row updates — never a once-a-second rebuild; inactive tabs don't poll.
- QML paints, C++ decides (house architecture law).

## 5. The surface

### 5.1 Residency rule — what appears where

| Job | Now arriving | Transfers table |
|---|---|---|
| Single-file HTTP (film, epub, audiobook part, single-archive comic) | yes (today's row, unchanged) | no |
| Torrent, any lane (book, comic pack, manga pack) | yes — one row **per library item** | yes — one row **per transfer** |
| Multi-part HTTP (Chew-class, once A1 lands) | yes — one row per item | yes — one composite row |
| Seeding (post-completion) | no | yes — Seeding state until app close or stop |

The same underlying job appearing in both places is deliberate: up top it speaks library
("One Piece — Vol. 3, 62%"), below it speaks transport ("[Judas] v01–v24 · 18 peers · 4.8 MB/s ·
For: Vol. 3 + Vol. 4"). The item row carries "in transfers below" so the link is discoverable.

### 5.2 The Transfers section (ratified mock v2)

- **Toolbar:** Pause all · Resume all · gold "Yielding — film playing" chip (visible only while
  yielding) · live aggregate ↓/↑ · Settings.
- **Table columns:** Name · **For** · Size ("612 MB of 14.2 GB") · Progress · Status · Seeds ·
  Peers · Down · Up · ETA. Mono numerals; selected row carries a gold left edge; seeding rows
  fill their progress bar gold. Non-torrent composites show "—" in Seeds/Peers/Up.
- **Status vocabulary:** Downloading · Part N of M (composite direct) · Queued · Paused ·
  Seeding · ratio · Failed — reason. A transfer that fails **stays in the table** with its
  reason; disappearance is never an error state.
- **Properties pane** (fixed, below the table, for the selected row):
  - **General** — name, hash, save path, pieces, availability, share ratio, added/completed
    times, open-folder.
  - **Files** — per-file progress; needed files show the lock glyph + "chosen for <item>";
    skipped files dimmed; long tails collapse ("+ 65 more files, skipped").
  - **Trackers** — inspect, add, edit, remove, force reannounce.
  - **Peers** — inspect (endpoint, client, flags, rates, progress), add peer, ban (with
    confirmation). No country column (killed — TB2's never worked).
  - Pane actions: Pause/Resume · Recheck · Reannounce · Open folder — plus the honest note
    when the transfer is shared ("Pausing pauses this transfer — Vol. 3 and Vol. 4 both wait").
- Only the visible tab refreshes; the table updates incrementally by role.

### 5.3 The settings sheet

Opened from the toolbar. Every value **reads back the live truth** on open — TB2's
blank-defaults dialogs are the named anti-pattern.

- Global download / upload speed limits (0 = unlimited).
- Queue size (how many transfers download at once; the rest show Queued).
- **Seeding:** on (default) / off. On = seed while the app is open; the obligation ends at app
  close. Per-transfer "Stop seeding" lives on the row.
- Manual throttle (the yield trickle, engageable by hand); auto-yield on/off.
- Applied to the engine at startup and on change; persisted app-side. Replaces the hardcoded
  `setGlobalSeedingRules(0.f, 1)` with settings-owned policy.

## 6. Flows

### 6.1 First encounter
Downloads page looks identical until a torrent or composite job exists — then the Transfers
section appears below Now arriving with its one row. No empty table furniture on a quiet day:
no transfers, no section.

### 6.2 Normal use (the primary journey)
Queue Vol. 3 + Vol. 4 → two item rows up top, one pack row below ("For: Vol. 3 + Vol. 4") →
select it, Files tab shows 2 pulling / 66 skipped with reasons → start a film → yield chip
lights, trickle → film ends, full speed → volumes finish and publish exactly as today → row
turns gold: Seeding · ratio climbing → close the app → obligation ends.

### 6.3 Interruption & recovery
- **Pause/resume** per transfer and pause-all; paused state survives restart.
- **Hard kill or crash mid-download:** every live torrent job replays on next boot — comics and
  manga already do; books gain a request ledger to match. Engine resume data continues to make
  replay cheap.
- **Failed transfer:** stays visible with reason; Recheck and Reannounce are the recovery tools;
  cancel is always available.
- **Externally deleted payload:** shown honestly (TB2's careful distinction survives: an absent
  *volume* is not a deleted *file* — a transfer on an unavailable disk restores paused, never
  silently re-downloads, never falsely reports done).
- **Multi-part direct:** a dead part fails the part by name; the job never reports success at
  the wrong size (the Chew defect made impossible at the contract level).

### 6.4 Cancel semantics (two cancels, both honest)
- **Item cancel** (Now arriving): drops that library item's intent only — today's shipped manga
  meaning, now uniform. Siblings keep the transfer alive; priorities recompute.
- **Transfer cancel** (table): ends the whole transfer. If deletion of payload files would
  orphan a sibling item still feeding, the action is refused with the sibling named.

## 7. Contracts (technical shape — enough for planning, no more)

### 7.1 The Transfers read-model
One C++ facade (working name `TransfersManager`) **observes** the existing engine and the three
media downloaders; exposed to QML as an incremental list model plus per-transfer detail
adapters (general/files/trackers/peers as QVariant shapes at the boundary only). It owns
projection and user commands; it owns **no** job identity, no file selection, no publication.
Commands route: transport actions → engine handle; item semantics → owning downloader. It never
creates a second session, thread-polls against the engine's own cadence, or duplicates ledgers.

### 7.2 Owner registry
Every transfer row carries: `ownerType` (biblio-book / comic-issue / comic-edition /
manga-volume / direct-composite), `ownerIds` (the library items it feeds → the "For" column),
and `safeDeletePolicy` (route-to-owner / forbid-while-shared). This is the single source for
"For", for cancel routing, and for the deletion guard.

### 7.3 Shared-pack safety
Delete-with-files is refused while any other live intent feeds from the same infohash; the
refusal names the sibling. Intent-level cancel never deletes shared payload.

### 7.4 Composite transfer contract (transport-agnostic)
A composite job publishes: parts list (name, size, state, progress), aggregate expected size,
current part index, per-part failure reason. Torrent transfers fill it from the engine;
multi-part HTTP fills it from its part plan (A1's implementation). The table and pane render
either without knowing the transport.

### 7.5 Restart
Books gain an active-request ledger (mirroring manga's `volume-requests.json` shape) and replay
on construction, as comics and manga already do. `LocalDownloads` gains the book-torrent
facade so Biblio torrent jobs appear in both Now arriving and Transfers.

### 7.6 Session seeding
On completion, handles stay registered and seeding (generalizing the comic-pack precedent);
engine teardown at shutdown ends it. Seeding off = today's behavior (handle removed on
completion). Per-transfer stop removes the handle without touching files.

### 7.7 Auto-yield
Keyed off the app's own playback/reading state (the same signals the GUI already owns), applied
as a global engine rate limit while active; released on exit. Chip visible whenever the limit
is engaged, whether automatic or manual.

## 8. Acceptance criteria (all observable)

1. A Biblio book torrent appears in Now arriving **and** Transfers while active, and its live
   job survives an app restart.
2. Pausing a shared pack from the pane visibly pauses both fed volumes; resume completes and
   publishes both.
3. Delete-with-files on a pack feeding a second live item is refused, with the sibling named.
4. Item-cancel of one volume leaves the sibling downloading (priorities recomputed) — verified
   in the table, not just the log.
5. On completion a torrent enters Seeding (gold bar, live ratio); closing the app ends it;
   the settings off-switch prevents it; per-row Stop seeding ends one.
6. Yield engages within seconds of playback start (chip visible, rates trickle) and releases
   after playback ends.
7. Every settings value re-opens showing the value actually in force.
8. A transfer failure remains visible with its reason; Recheck/Reannounce act on it.
9. Trackers can be added/edited/removed and a peer can be banned from the pane.
10. The table updates without flicker or selection loss; inactive pane tabs generate no
    refresh traffic (probe-verified, not eyeballed).
11. No second engine/session exists; queue size in settings is the only queue authority.

## 9. Discarded alternatives (with reasons)

- **Standalone Engine Room page** — rejected for the unified drawer (Hemanth's reframe: depth
  belongs where the payload is complex, not in a destination you must remember).
- **Item rows with inline pack expansion** (v1 mock) — built, seen, rejected: interleaving
  library rows and transport guts in one card read as confusing; the proven table genre won.
- **Pack-as-the-row in Now arriving** — breaks the page's promise that rows are library items.
- **Ratio-capped (1:1) or perpetual seeding defaults** — session seeding chosen: no config
  debt, nothing runs behind Hemanth's back, matches the shipped comic-pack posture.
- **Guarded priority editing this arc** — nearly nothing legitimate to edit on media-owned
  packs; machinery deferred to generic add.
- **Wiring the dormant `TorrentRepository`** — stays dormant; the live ledgers are truth. It
  becomes interesting only with generic add.

## 10. The TB2 kill list (ratified 2026-08-05)

1. The double queue authority (TransferQueue vs engine limits) — one authority here.
2. The indexer/search UI + plaintext Cloudflare/EZTV credentials — discovery isn't this arc.
3. The country column (never worked).
4. The rename-before-add field (wired to nothing).
5. The magnets-only "Add URL" dialog (returns honest with generic add, or not at all).
6. The separate History dialog — the Downloads page's per-world shelves are the history.
7. The once-a-second full-table rebuild and all-tabs polling.
8. TB2's two conflicting priority vocabularies — one scale, one set of words.

## 11. Deferred

- Generic magnet add (own brainstorm; the contract here accommodates it without rework).
- A1's multi-part comics implementation (plugs into §7.4).
- Editable file priorities (with generic add).
- Any revisit of discovery/indexers.

## 12. Lineage

Brainstormed Hemanth + scoped helper (Claude), 2026-08-05, from Hemanth's two prepared code
maps. Key reframes on the record: Hemanth — unified drawer, power scales with payload
complexity, "full BitTorrent look" after seeing v1; Claude — payload-count split sharpened by
the same-night Chew evidence, the "For" column, auto-yield, session-seeding default, the
restart and Biblio-visibility constraints. The Chew incident (silent partial download + cancel
that did nothing + no log to read) is this arc's origin story; the always-on rolling log
(Colosseum `10cfb0c`) was landed the same night as its first repair.
