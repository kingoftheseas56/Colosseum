# Comics multi-volume pack demux — implementation plan

**Date:** 2026-08-06
**Spec:** `docs/superpowers/specs/2026-08-06-comics-multivolume-pack-demux-design.md` (approved)
**Ledgers consulted:** `docs/colosseum-test-verification.md` + `docs/colosseum-lanista-verification.md`
(both read fresh 2026-08-06; every verification named below is drawn from their AVAILABLE
sections or the ratified human-witnessed lane)
**Lane:** Comics (Tankoban) download/ingest. Engine: `native/engine/ComicDownloader.{h,cpp}`.
**Executor model routing:** Opus main seat, **Fable** advisor (Hemanth's routing this arc —
consult before each commit; re-verify every advisor claim against source).

---

## Plan-level calls, named plainly

1. **No isolated Lanista session in this plan.** The composed runtime truth (folded progress
   line, series shelf, reader crossing) is a simple LOOK, and the live Chew case sits in
   Hemanth's daily AppData where the bridge is off-limits by ledger law. Engine/state/data truth
   is fully provable in the deterministic harness; the assembled-app proof is the ratified
   `human-witnessed:` lane (Slice 6) plus my own read-only disk/index verification. Ordering a
   bridge session would require inventing named Downloads-page automation surfaces this arc
   doesn't otherwise need — scope without proportionate evidence value.
2. **Mains/extras ordering computed in C++, painted by QML** (house doctrine: QML paints, C++
   decides). This moves the one genuinely new QML-adjacent logic into harness reach.
3. **Slices 1–4 are engine slices** whose user-visible composition lands in Slices 5–6; their
   per-slice proof is the deterministic layer. Slice 6 is the runtime gate for the whole arc —
   the plan does not claim Runtime-validated for anything before Slice 6 closes.
4. **Live network never enters a gate.** The demux harness feeds local fixture packs through
   `ingestLocalArchive()` / direct staging placement — the same lane HTTP downloads converge on.

## Shared-file discipline

`native/CMakeLists.txt` (new harness target) and `tests/CMakeLists.txt` (CTest registration)
are shared files: **declare the additive edits on `agents/chat.md` before making them.**
Colosseum is a nested git repo — `cd Colosseum` before any git command; commit by explicit
pathspec; push after commit.

## Ground-truth anchors (verified against source 2026-08-06)

- Detection seam: `finalizeExtract()` — the `rel.isEmpty()` branch at
  `ComicDownloader.cpp:1759-1762` (`failPreservingSource(f, "archive contained no pages")`).
- Per-volume primitive: `ingestArchiveByProbe()` (:1301) — probe → `finalizeSafeMove` fast
  path, else `beginExtract` → extract → repack. Extractors: bsdtar → 7z (`runExtractor`, :1676).
- Single lane: `m_queue` / `startNextQueued()` (:1637) dispatching on
  `assembledIngest` / `localArchive` / HTTP.
- Child-enqueue template: `ingestLocalArchive()` (:643) — dedup against active/queue/resolving,
  `adoptExistingCanonicalIfValid()` idempotence, queue-or-start.
- Adoption idempotence: `adoptExistingCanonicalIfValid()` (:291) — probe canonical, build
  Entry, saveIndex.
- Staging path: `baseDir() + "/dl_" + hash10(f.id) + ".archive"` (:1149) — deterministic per
  issue id (Chew: `dl_c5c1573258.archive`).
- Index: `loadIndex()` (:320, runs ONCE per process, from the constructor;
  `migrateLegacyComicsInPlace()` runs at its tail) / `saveIndex()` (:546, QSaveFile atomic).
  Entry JSON fields: seriesId, seriesTitle, label, dir, archive, bytes, addedAt, files, groups.
- Queue-fold pre-plumbing: `activeIssueJobs()` (:2275) emits `groupKey` from `f.partGroupKey`
  (never set today) + `groupUnit` hardcoded `"parts"`; `InFlight::partGroupKey` comment (.h:234)
  documents the intended multi-part use. DownloadsPage `groupJobs()` renders
  `seriesTitle — N <groupUnit>` when groupKey is shared (`DownloadsPage.qml:317-368`).
- Downloads-only series injection precedent: `openUniverseComic()` (`Main.qml:640`) — baked
  release list, no tag, no catalogue. Downloads routing: `routeDownloadItem` (`Main.qml:1263`).
- Reader crossing: `chapters` array consumed via `ComicReaderState.nextEntry/previousEntry`
  (`ComicReaderShell.qml:313-324, 513, 521`); ComicSeriesPage comment: "crossing advances
  toward index 0" over a newest-first array — for volumes, index 0 = v8, last = v1. Executor
  must confirm against `ComicReaderState.js` before wiring.
- Harness pattern to mirror: `tests/comic_downloader_ingest_harness.cpp` — real-JPEG
  `makeCbz()`, tar-renamed-`.cbr` `makeCbr()`, private-path mirrors (safeSeg/hash10/issueDir/
  issueArchivePath), dedicated org/app identity for AppData isolation, event-loop-driven
  scenarios, house CHECK/require idiom, `<NAME>_OK` sentinel.
- Standard deterministic gate: `ctest --test-dir native/build-msvc -L unit
  --output-on-failure`. Build one target: `native/build-target.bat <target>`; grep the log for
  `error C|error LNK|ninja: build stopped` before believing it.

## New contracts introduced by this plan (the executor implements, never re-designs)

- **Child issue id:** `childId = parentId + ":vol:" + hash10(nestedRelPath)` — deterministic,
  unique per nested file, stable across re-runs (`nestedRelPath` = the archive's path relative
  to `extractTmp`, exact bytes as extracted).
- **Entry additions (optional fields, absent = ordinary issue):** `packRole` (`""`/`"main"`/
  `"extra"`), `packOrder` (int, −1 default). Serialized in index.json alongside existing
  fields; legacy rows load unchanged.
- **Pack manifest:** sibling file `<baseDir>/packs.json` (QSaveFile atomic; NEVER a magic key
  inside index.json — the index root maps issueId→entry). Shape:
  `{ <parentId>: { archivePath, extractTmp, seriesId, seriesTitle, active: bool,
  children: [{id, rel, label, role, order}] } }`. Written BEFORE the first child ingests;
  cleared on full success (then pack + extractTmp deleted) or on user cancel (pack file kept).
- **Label parser:** pure function, nested filename → `{label, role, order}`:
  `v(\d+)` (zero-pad normalized) → "Vol. N" / main / order N; a "Bonus" token → extra,
  "Vol. N — Bonus"; "Script Book" and unmatched named specials → extra, cleaned-name label;
  unparseable → main, ordered after parsed mains by natural sort. Must round-trip non-ASCII
  (`´`) safely.
- **Per-InFlight `groupUnit`** (default `"parts"`; demux children set `"volumes"`), emitted by
  `activeIssueJobs()` in place of the hardcoded literal.
- **Ordered-volumes read API:** a Q_INVOKABLE on ComicDownloader (e.g.
  `packVolumes(seriesId)`) returning `{mains: [...], extras: [...]}` rows (id, label, order,
  pages, art) built from `m_index` `packRole`/`packOrder` — the ONLY input the QML shelf and
  reader chain need.

---

### Slice 1: Volume label parser + pack index fields (Entry round-trip)

**Purpose:** Give every future demuxed volume a parsed display label, a main/extra role, and a
deterministic order the index can persist — no user-visible change yet.
**Dependencies:** none.
**Implementation guidance:** Parser as a free function in `ComicDownloader.cpp`'s anonymous
namespace (or a tiny `ComicPackLabels.h` if the harness needs direct linkage — executor's
call; the harness may equally drive it through demux outcomes in Slice 2, but direct-linkage
table tests are cheaper — prefer a small header). Entry gains `packRole`/`packOrder`;
`loadIndex()`/`saveIndex()` round-trip them; `downloadedIssues()` rows expose both fields.
New harness target `comic_downloader_pack_demux_harness` (source
`tests/comic_downloader_pack_demux_harness.cpp`, mirroring the ingest harness's isolation:
dedicated org/app name, QTemporaryDir scratch, path mirrors). Additive `add_executable` in
`native/CMakeLists.txt` + CTest registration in `tests/CMakeLists.txt`, label `unit` —
DECLARE both edits on `agents/chat.md` first.
**Behavior to preserve:** existing index rows (no pack fields) load, save, and behave
identically; `downloadedIssues()` keeps every existing field.
**Baseline:** run the existing registered gate green before touching code
(`ctest -L unit`); confirm `downloadedIssues()` rows carry no role/order today (read the
source, no runtime step needed).
**Focused tests:**
  - Qt Test: none — house CHECK-collecting idiom harness instead (arc convention; every
    failure reported, `<NAME>_OK` sentinel), registered under CTest `unit`.
  - Qt Quick Test: not applicable — no QML in this slice.
  - Existing harnesses: full `unit` label set stays green (includes `cbz_archive_harness`).
  - Negative control: one deliberately wrong parser expectation (e.g. assert `v05` → "Vol. 50")
    must fail red, then restore; one flipped round-trip field likewise.
  Parser table cases: `v1`/`v05` normalize to Vol. 1/Vol. 5; `v1 … - Bonus` → extra tied to 1;
  `Script Book` → extra; non-ASCII `´` round-trip; unparseable filename → main + natural-sort
  order; the 12 real Chew filenames (from the spec) as a literal table asserting the exact
  expected label/role/order dozen.
**Test seam status:** available (new registered harness; pattern proven by the ingest harness).
**Lanista actions:** none — internal slice.
**Completion signal:** not applicable (no runtime action).
**State / events / probes:** not applicable.
**Visual evidence:** not applicable.
**Regression paths:** `ctest -L unit` full set; legacy-index load path (a fixture index.json
without pack fields loads unchanged — harness case).
**Evidence artifacts:** harness stdout (sentinel) captured in the executor's session notes;
ctest output.
**Bridge status:** not applicable — purely internal.
**Completion criterion:** new harness green 3–4 consecutive runs incl. the restored negative
controls; full `unit` gate green; committed by pathspec + pushed.

---

### Slice 2: Demux detection, child enqueue, manifest, reclamation (the engine core)

**Purpose:** A pack download stops failing with "archive contained no pages" and instead
lands N readable volumes under one series — the heart of the feature.
**Dependencies:** Slice 1.
**Implementation guidance:** In `finalizeExtract()`'s `rel.isEmpty()` branch: recursively scan
`f.extractTmp` for nested comic archives (suffix pre-filter `.cbr/.cbz/.cb7/.cbt`, then
content check — `CbzArchive::probe()` for zip-shaped, else a cheap magic-bytes sniff; a
file that is neither is ignored). Zero found → exact current fail path, byte-identical
behavior. Found → build the child list (label parser, child-id contract, inherited
seriesId/seriesTitle), write `packs.json` (active), enqueue one child InFlight per volume
(`localArchive=true`, `archivePath` = nested file inside extractTmp, `partGroupKey` =
parentId, `groupUnit` = "volumes", dedup + adoption checks exactly as `ingestLocalArchive()`
does), then retire the parent WITHOUT an index row and WITHOUT `failed()`: clear the
preserved-source fields, `emit removed(parentId)`, delete m_active, `startNextQueued()`.
Child completion hook (both publish tails — `completeSafeMove()` and the repack handler —
plus the adoption path): after `saveIndex()`, if this id belongs to the active manifest and
ALL expected child ids are now indexed → delete pack archive + extractTmp, clear manifest.
Any child failure: existing per-child failure semantics (its nested source inside extractTmp
is preserved by `failIngest`'s local-archive branch), manifest stays, pack stays.
`activeIssueJobs()` emits per-InFlight `groupUnit`.
**Behavior to preserve:** single-comic ingest (CBZ fast path + CBR repack) byte-identical;
genuinely-empty archive still fails "archive contained no pages" preserving source; a mixed
tree (any accepted image anywhere) never reaches the demux scan (the branch only runs at
zero images); cancel/serial safety of the background workers untouched.
**Baseline:** RED first — add the happy-path demux scenario to the harness BEFORE the engine
change and record it failing with today's "archive contained no pages" `failed()` signal
(that is the live bug reproduced deterministically). Fixture: `makeNestedPack()` — a ZIP
(tar `-a`) containing 2 small real-JPEG CBZs (via `makeCbz`) + 1 tar-renamed `.cbr`
(via `makeCbr`), Chew-shaped names (`Foo v1 - ... .cbz`, `Foo v2 ... .cbr`,
`Foo v1 - Bonus ....cbz`).
**Focused tests (all in `comic_downloader_pack_demux_harness`):**
  - Qt Test: none — house idiom harness (as Slice 1).
  - Qt Quick Test: not applicable.
  - Existing harnesses: `comic_downloader_ingest_harness` (single-comic contract) and the
    full `unit` set must stay green — the strongest available no-regression proof for the
    shared lane.
  - Negative control: corrupt one nested volume in a dedicated fixture (truncate its bytes)
    — the scenario must report that child failed and assert pack+manifest retained; plus one
    deliberately flipped assertion (e.g. expect manifest cleared in the corrupt case) red,
    then restored.
  Scenarios: (a) happy demux — N entries under one seriesId with correct labels/roles/orders,
  pages readable via `localPages()` per child, pack archive + extractTmp gone, manifest
  cleared; (b) one-corrupt-volume — siblings land, failed child reported, pack + manifest
  retained; (c) re-run over the same pack (drive the ingest again) — zero duplicate entries
  (adoption path), same final state; (d) empty archive (no images, no nested archives) —
  today's failure verbatim; (e) mixed tree — single-issue ingest exactly as today (compare
  Entry shape against a pre-slice run).
**Test seam status:** available.
**Lanista actions:** none at this slice — the user-visible composition (folded queue line,
series shelf) is verified in Slice 6 `human-witnessed:`; engine truth is fully deterministic
here. (Plan call #1/#3.)
**Completion signal:** harness scenarios use the `finished`/`failed`/`removed` signals via
QEventLoop with the harness's existing timeout guard — no sleeps.
**State / events / probes:** harness asserts on-disk truth directly: index.json content,
canonical CBZ presence, pack/extractTmp presence, packs.json content.
**Visual evidence:** not applicable at this slice.
**Regression paths:** full `unit` gate; ingest harness; scenario (e) mixed-tree; scenario (d)
empty-archive.
**Evidence artifacts:** harness stdout across 3–4 runs; the RED baseline run's output kept in
the session notes (proof the scenario could fail).
**Bridge status:** not applicable (engine slice; runtime composition gated at Slice 6).
**Completion criterion:** scenarios (a)–(e) green 3–4 consecutive runs; RED baseline recorded;
negative controls performed and restored; `unit` gate green; committed + pushed.

---

### Slice 3: Boot resume (manifest) + retry re-uses the preserved pack

**Purpose:** A crash mid-unpack self-heals on next launch, and the failed 1.46 GB Chew
download becomes fixable WITHOUT re-downloading a byte.
**Dependencies:** Slice 2.
**Implementation guidance:** (1) Resume: at the tail of `loadIndex()` (after
`migrateLegacyComicsInPlace()`), read `packs.json`; for each ACTIVE manifest, filter children
already indexed (or adoptable) and re-enqueue the missing ones — **deferred to the event loop**
(queued `QMetaObject::invokeMethod` / `QTimer::singleShot(0)`) so no extraction subprocess
starts inside the constructor; if the manifest's `extractTmp` no longer holds a missing
child's file but the pack archive exists, re-extract the pack first (reuse
`beginExtract`-shaped machinery on a parent-shaped InFlight; the demux seam then re-runs and
adoption skips landed children). A manifest whose pack AND missing children's sources are all
gone is cleared with a warning (nothing recoverable). (2) Retry/no-re-download: in
`downloadIssue()`, after the adoption check and before any network: if
`baseDir()+"/dl_"+hash10(id)+".archive"` is a file → route it straight into the ingest lane
(parent-shaped InFlight, `archivePath` set, no resolve, no NAM touch), where probe/extract →
demux-or-single-issue runs as normal. If that staged reuse terminally fails at PACK level
(unextractable), delete the stale staging file and emit `failed()` with a reason naming the
discard — the NEXT attempt re-downloads cleanly. (3) Cancel semantics: `cancelDownload()` of
a pack child also drops queued siblings of the same manifest and marks the manifest cleared
(pack archive file kept on disk); landed volumes stay. Subsequent sibling cancels no-op.
**Behavior to preserve:** `downloadIssue()` idempotence contract (downloaded → finished;
active/queued → no-op; adoption) unchanged for ordinary issues; constructor-time `loadIndex()`
stays synchronous for everything that exists today (migration timing unchanged); cancel of an
ordinary (non-pack) download unchanged.
**Baseline:** RED first per scenario: (resume) hand-author an active packs.json + preserved
fixture pack + subset of children indexed, construct ComicDownloader — today nothing resumes
(assert the missing children stay missing) → flips to auto-resume; (reuse) place a fixture
pack at the staged path, call `downloadIssue()` with an unreachable dummy postUrl — today it
attempts the network resolve and fails; after the slice it completes offline.
**Focused tests (same harness):**
  - Qt Test: none — house idiom harness.
  - Qt Quick Test: not applicable.
  - Existing harnesses: `unit` gate + ingest harness green.
  - Negative control: corrupt the staged archive in the reuse scenario — assert the discard
    path (staging file deleted, `failed()` emitted, index untouched); one flipped resume
    expectation red, then restored.
  Scenarios: (f) crash-resume — missing children enqueue and complete, manifest cleared, pack
  reclaimed; (g) resume-with-gone-extractTmp — pack re-extracts, adoption skips landed
  children, no duplicates; (h) staged-reuse offline completion (single-issue fixture AND
  pack fixture variants); (i) corrupt staged archive discarded; (j) cancel mid-pack — landed
  volumes stay, queued siblings dropped, manifest cleared, pack file present, and a fresh
  construct does NOT auto-resume (cancel is sticky).
**Test seam status:** available.
**Lanista actions:** none — engine slice; the live no-re-download proof is Slice 6's step 1
(human-witnessed + my read-only disk check that `dl_c5c1573258.archive`'s mtime/size are
untouched after retry).
**Completion signal:** harness event-loop on signals, timeout-guarded — no sleeps.
**State / events / probes:** on-disk index/manifest/file assertions as Slice 2.
**Visual evidence:** not applicable.
**Regression paths:** ordinary-issue downloadIssue dedup/adoption cases (existing harness);
cancel of a non-pack download; `unit` gate.
**Evidence artifacts:** harness stdout across 3–4 runs; RED baselines recorded.
**Bridge status:** not applicable.
**Completion criterion:** scenarios (f)–(j) green 3–4 consecutive runs; negative controls
performed; `unit` gate green; committed + pushed.

---

### Slice 4: Ordered-volumes read API (`packVolumes`) — the shelf/reader contract

**Purpose:** Give the QML shelf and reader ONE C++ answer for "which volumes, in what order,
which are extras" — so Slice 5 paints instead of deciding.
**Dependencies:** Slice 1 (fields); Slice 2 only for end-to-end fixture convenience.
**Implementation guidance:** Q_INVOKABLE `packVolumes(const QString& seriesId)` on
ComicDownloader returning `{mains: [rows...], extras: [rows...]}` — rows shaped like
`downloadedIssues()` rows (id, label, pages, bytes, addedAt, art) filtered to that seriesId,
mains = `packRole=="main"` sorted by `packOrder` ASCENDING (v1 first — the QML reverses for
the newest-first chapters array; keep the API in natural reading order and document it),
extras = `packRole=="extra"` sorted by `packOrder`. Rows with no packRole (ordinary issues
sharing the seriesId) are NOT the API's business — return them under neither key.
**Behavior to preserve:** `downloadedIssues()` unchanged; no existing QML consumer affected
(new API, no callers yet).
**Baseline:** API absent (trivial).
**Focused tests (same harness):**
  - Qt Test: none — house idiom harness.
  - Qt Quick Test: not applicable (pure C++ read API).
  - Existing harnesses: `unit` gate green.
  - Negative control: one flipped order expectation (assert v2 before v1) red, then restored.
  Scenario (k): after a demux fixture run, `packVolumes()` returns mains [v1, v2] and extras
  [v1-Bonus] in exact order with exact labels; a series with no pack rows returns two empty
  lists; an ordinary downloaded issue never appears in either list.
**Test seam status:** available.
**Lanista actions:** none — internal read API.
**Completion signal / State / Visual:** not applicable.
**Regression paths:** `unit` gate.
**Evidence artifacts:** harness stdout.
**Bridge status:** not applicable.
**Completion criterion:** scenario (k) green 3–4 runs; gate green; committed + pushed.

---

### Slice 5: Shelf + reader wiring (the user-visible composition)

**Purpose:** Opening Chew shows the volume shelf — mains v1→v8 then an Extras group — and
reading flows next across mains only; extras open solo.
**Dependencies:** Slices 1–4.
**Implementation guidance:** (1) Routing: `routeDownloadItem` (`Main.qml:1263`) for a
tankoban item whose seriesId has pack rows → open the western series surface with a
downloads-backed injection mirroring `openUniverseComic()`'s baked shape (`Main.qml:640`):
releases built from `packVolumes()` (mains + a visually separated Extras section), tag/catalogue
paths untouched. **Identity ordering law (the df003eb lesson, non-negotiable): set
seriesId-determining properties BEFORE `openChapterId` in every touched path.** (2) Series
surface: the western series page (`ComicSeries.qml` family) renders the mains list and an
"Extras" labeled group from the injected rows; downloaded rows already read pages via
`dlStore.localPages()`. (3) Reader chain: the `chapters` array passed to the reader = mains
only, ordered per the crossing convention (newest-first array, crossing advances toward
index 0 — confirm against `ComicReaderState.js` `nextEntry` before wiring; the API hands
natural order, the QML adapts). Opening an extra passes a single-entry array. (4) Downloads
queue fold: verify the existing `groupJobs()` renders "Chew — 12 volumes" from the Slice-2
`partGroupKey`/`groupUnit` fields — expected zero QML change; if a change IS needed it is
one line in the title branch (`DownloadsPage.qml:363-367`).
**Behavior to preserve:** catalogue (`gcd:`) and tag (`gc:`) series pages pixel- and
behavior-identical for non-pack series; the reader-resume identity fix (df003eb) intact in
every touched open path; Downloads season/manga grouping untouched.
**Baseline:** before the slice, record what opening the Chew row does today (routes to a
title-only western page with no volume rows) — one sentence + the code path in session notes;
the Slice-2 fixture can't render this (QML), so the baseline is source-traced, and the live
before-state is already on record from the 2026-08-06 diagnosis.
**Focused tests:**
  - Qt Test: not applicable (no new C++ logic — Slice 4 carries the ordering contract).
  - Qt Quick Test: not applicable — no new testable QML component logic beyond paint-and-pass
    (the chapters-array adaptation is a trivial reverse of an already-harness-proven order;
    a bespoke offscreen harness for one `.reverse()` would be test theater). If the executor
    finds themselves writing real ordering/branching logic in QML, STOP — it belongs in
    Slice 4's C++ API, tested there.
  - Existing harnesses: `unit` gate green (guards the C++ side the QML consumes).
  - Negative control: covered at the C++ layer (Slice 4); none manufactured for paint-only QML.
**Test seam status:** available (the logic layer is Slice 4's; this slice is deliberately
paint-only).
**Lanista actions:** none by the bridge. Runtime verification of this slice is Slice 6's
human-witnessed journey — this slice must NOT be reported beyond `Implemented, verification
pending` until Slice 6 closes.
**Completion signal:** deferred to Slice 6.
**State / events / probes:** deferred to Slice 6.
**Visual evidence:** deferred to Slice 6 (Hemanth's eyes; Qt/D3D is uncapturable headless —
pixels are his).
**Regression paths (exercised in Slice 6 live):** open a CATALOGUE series (e.g. the Descender
Deluxe row) → volume list + reader resume unchanged; open a normal single downloaded comic
from Downloads; navigate away/back to the Chew shelf; restart the app and reopen Chew.
**Evidence artifacts:** Slice 6's recorded verdict.
**Bridge status:** available — via the ratified `human-witnessed:` lane, executed as Slice 6.
**Completion criterion:** code landed + `unit` gate green + QML-only changes runnable via the
existing exe where applicable; status capped at `Implemented, verification pending` until
Slice 6.

---

### Slice 6: The live Chew journey — human-witnessed closing gate

**Purpose:** The whole arc's promise, proven on the real thing: retry → 12 readable volumes
under one Chew shelf, mains-only crossing, 1.46 GB reclaimed.
**Dependencies:** Slices 1–5 landed, committed, pushed; build from the COMMITTED tree
(verify-the-committed-artifact rule).
**Implementation guidance:** none (no code). Coordinate with Hemanth for the session; his
running `colosseum.exe` must be closed by HIM (never kill his session; the full-app link
fails LNK1104 while it runs). Rebuild the app from the committed tree, hand him the launcher.
**Behavior to preserve / Baseline:** live before-state already on record (2026-08-06
diagnosis): `dl_c5c1573258.archive` = 1,459,451,314 bytes + `dl_c5c1573258.archive.x/`
present, no Chew entries in `comics/index.json`. I re-confirm both READ-ONLY immediately
before the run and record sizes/mtimes.
**Focused tests:** full `unit` gate green on the committed tree before the session (includes
every demux scenario); not applicable otherwise.
**Test seam status:** available.
**Lanista actions:** `human-witnessed:` — exact steps for Hemanth, in order:
  1. Launch the fresh build. Open Downloads. If the failed Chew row is present, press retry;
     if not, open the Chew (v1–v8 + Extras) GetComics post entry he used before and press
     download again. **Expected: no re-download** — the progress line goes straight to
     unpacking (no hours-long network phase; I verify the staged file's mtime is untouched).
  2. Watch the queue: **one folded line "Chew — 12 volumes"** with aggregate progress;
     expanding it shows per-volume rows. Verdict: does it read as one download becoming
     twelve volumes?
  3. When it completes, open the Chew series from Downloads. **Expected: mains Vol. 1–Vol. 8
     in order, then an Extras group holding Vol. 1–3 Bonus + Script Book.**
  4. Open Vol. 1, read a few pages, jump near the end, press next → **Vol. 2 opens**; repeat
     spot-checks to v8; after v8, crossing ends (no extra appears). Open one Bonus and the
     Script Book from Extras — each opens alone, back returns to the shelf.
  5. Regression sweep (the paths nobody replays): open Descender Deluxe (catalogue series) →
     volume list + resume-to-page intact; open one ordinary downloaded single comic; navigate
     away and back to Chew; quit and relaunch → Chew shelf and reading positions intact.
**Completion signal:** Hemanth's explicit verdict per step, recorded verbatim in the closing
report (human-witnessed is a planned verification with recorded confirmation, not a shrug).
**State / events / probes (mine, read-only, never driving his session):** after his run:
`comics/index.json` contains 12 entries sharing the Chew seriesId with correct
packRole/packOrder; 12 canonical `.cbz` files exist at their `issueArchivePath` locations;
`dl_c5c1573258.archive` and `dl_c5c1573258.archive.x/` are GONE; `packs.json` has no active
Chew manifest. Recorded as file listings in the evidence.
**Visual evidence:** his eyes are the exhibit (Qt/D3D uncapturable headless). If he wants,
he screenshots the shelf + folded queue line for the record — his call, not a gate.
**Regression paths:** step 5 above.
**Evidence artifacts:** closing report in the session notes + `agents/chat.md` LANDED line
citing: harness runs, ctest output, the read-only disk listings, and Hemanth's step verdicts.
**Bridge status:** available — ratified human-witnessed lane (bridge is barred from the daily
app by ledger law; nothing here needs it).
**Completion criterion:** all five steps confirmed by Hemanth + my four read-only disk/index
checks pass → the arc (Slices 2–5) becomes **Runtime-validated**. Any step failing → route
through `brotherhood-systematic-debugging` before any fix; the pack is still safe on disk by
the Slice-2/3 contracts.

---

## Execution notes

- **Order:** 1 → 2 → 3 → 4 → 5 → 6. Slices 1–4 are each independently landable; 5 lands as
  one commit; 6 is a session, not a commit (plus the ledger/status updates it produces).
- **Advisor:** Fable consult before each commit (main seat is Opus this arc) — re-verify every
  advisor claim against real source; don't trust its line numbers.
- **Builds:** one build per out/ dir; a running exe locks its own .exe (kill only MY harness
  processes by PID, never Hemanth's app). Backgrounded build exit codes lie — grep the log.
- **Committed-artifact rule:** Slice 6 runs a build of the committed tree, not the working
  copy.
- **Ledger upkeep:** the new harness target + registration get a row in
  `docs/colosseum-test-verification.md` in the same commit that adds them (ledger law: code
  and ledger move together).
- **Deferred (spec §2, do not smuggle in):** cancel-retry race guard, torrent-orphan sweep,
  long-strip inactive-emit gate.
