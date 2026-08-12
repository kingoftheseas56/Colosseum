# Colosseum Vault — Browse Face Implementation Plan

**Status:** awaiting Hemanth's approval. Execute under `brotherhood-executing-plans`.
**Design (locked):** `docs/superpowers/specs/2026-08-12-colosseum-vault-browse-face-design.md`
**Mock (approved, disposable):** `Brotherhood/agents/colosseum-vault-browse-face-mock.html`
**Parent design:** `docs/superpowers/specs/2026-08-12-colosseum-vault-complete-locked-design.md` (`a0c735e`)
**Recon:** `~/Downloads/Vault-Locked-Design-Discovery-Gates-Recon-Handoff.md` (Preflight, 2026-08-12)
**Ledgers consulted (fresh, 2026-08-12):** `docs/colosseum-test-verification.md` ·
`docs/colosseum-lanista-verification.md`

---

## 0. Ground-truth pins (verified this session, 2026-08-12)

- **Drift re-pin (recon Gate 10 follow-up):** recon inspected `a0c735e` (code tree = `3c55300`).
  Since then exactly the anticipated tactical-fix set landed (`e08424b`: VaultLibrary +25/+6,
  VaultWatcher +121/+16, VaultDownloadsRoot +20, main.cpp +2). `git diff 3c55300..HEAD --stat --
  native qml` shows **no other Vault-code drift**. Recon pins into VaultLibrary/VaultWatcher/
  VaultDownloadsRoot must be re-checked against HEAD during execution; all other pins stand.
- **Recon unknown 1 — CLOSED.** `Main.qml:2852-2854`: `openMediaRequested` →
  `win.openLocalMedia([path])`; `viewWorldRequested` → `win.openVaultIdentity`. One connector, no
  second writer. Stop-condition sweep stays clean.
- **Recon unknown 2 — CLOSED with a caveat.** Live vault appdata confirmed by direct read:
  `%APPDATA%/Brotherhood/Colosseum/vault/` holds `config.json(+.bak)`, `identity.json(+.bak)`,
  `open-recent.json(+.bak)`, `index-v1.sqlite` — `VaultStoreIo` rotation is live for JSON; SQLite
  has no rotation. **The `.prenorm.bak` precedent is NOT reachable on this machine today** (no
  `data/` dir exists under the Colosseum appdata root). That precedent matters to the parent
  design's ownership-schema migration arc, which this plan does not touch; recorded honestly, not
  planned around.
- **API gap (drives Slice 1):** `VaultLibrary` exposes `series(kind)` / `items(kind, seriesKey)` /
  `rootCount()` / `hiddenSeries()` — there is **no root-list projection, no folder-path browse
  projection, no arrivals projection, no per-group state enum**. The rail, grid, carousel, and
  tile states all need a projection spine that does not exist.
- **Grammar gap (drives Slice 1, bites Slice 8):** `VaultKit` TV extraction is the narrow
  `SxxExx` grammar (recon Gate 4, `VaultKit.cpp:443-457`). Hemanth's real Gintama files
  (`[Judas] Gintama - 003 [BD 1080p]….mkv`) carry **absolute numbering, no SxxExx** — the
  design's episode wall cannot be built on today's grammar.
- **Identity-carry hazard (recon's top hazard, guarded not fixed here):**
  `VaultIndex::publish()` carries canonical identity per stable file tuple
  (`VaultIndex.cpp:392-446`). This plan **reads** identity; no slice may widen the carry or make
  regroup-revalidation harder. The fix itself belongs to the parent design's ownership arc.
- **Real library content for fixtures** (from `c:/users/suprabha/desktop/hemanth's folder`,
  inspected): the Spider-Man one-film folder (film + `.srt` + `Subs/` + 2 junk files); Loki S01 +
  S02 as two sibling folders; `The Wire … Season 1-5` folder that actually holds only `Season 4`;
  Gintama absolute-numbered episodes; four local-only Cricket clips. Fixture trees mirror these
  shapes (structural stubs, tiny real archives where a slice needs bytes — the
  `tests/fixtures/vault/` pattern).

## Scope and non-goals

**Builds:** the Browse face per the locked design — carousel head, collapsible root rail,
breadcrumb, folder-true media-faced grid (2:3 posters / 16:9 wides), tile states (resolving /
identified / uncertain / away / local-only / no-art), identify-in-place, detail sheet, series
drill, empty states, keyboard reach — with existing Vault capabilities preserved (§0 of the
parent's acceptance: door, drop-to-add, scan pill, confirm card, hidden shelf, ceremonies,
identify dialogs, open media/recent, reveal, View-in-world).

**Does not build (parent-design arcs, later):** Manage's six areas; the ownership graph /
normalized schema; world `Owned locally` bridge; consumer-search ownership; DLNA; learned-rules
engine; **remote canonical artwork** (network). V1 artwork = existing comic covers
(`image://comiccover/`) + local artwork companions + the design's typographic fallback (§4.7).
This is the plan's biggest visible risk call, named plainly: **until the artwork arc lands, most
video tiles read as typographic cards, exactly like the approved mock's stand-ins.**

## Standing execution discipline

Every slice: build via `native/build-msvc.bat`, grep the log for `error C|error LNK|ninja: build
stopped` (exit codes lie). Deterministic gate = `ctest --test-dir native/build-msvc -L unit
--output-on-failure` (32/32 green baseline, 2026-08-11) + the `colosseum.qml` target for QML
slices (known pre-existing flake: `tst_search_history_flow` ~1-in-3 — not ours, never silently
rerun-until-green). Lanista work runs ONLY in isolated `session run` sessions (unique pipe +
`COLOSSEUM_APPDATA_TAG`); the daily app and live vault data are never fixtures. Vault stores live
in **Roaming** — `--seed` reaches only Local (ledger-documented), so vault fixtures are pre-placed
at `<Roaming>/Brotherhood/Colosseum-dltest-<tag>/vault/` before launch, absolute paths only. New
automation objectNames use the `vaultBrowse` prefix (world-namespaced convention, binding). All
new QML follows "QML paints, C++ decides." Commit+push per slice, explicit pathspec, `git diff`
after every commit (foreign WIP lives in main.cpp / Main.qml / CMakeLists).

---

### Slice 1: The browse projection spine (C++)

**Purpose:** Give the new face its truth: one C++ projection that answers "what is at this level,
what is each thing, and what single fact does its card carry" — so QML only paints.
**Dependencies:** none.
**Implementation guidance:** Pure interpretation lives in `VaultKit` (new: browse-collapse
planner over index rows — folder-is-one-film, sibling-season fold to show, companion/extras/junk
folding, season-presence facts like "holds S4 of 5 claimed", **absolute-numbering episode grammar**
alongside SxxExx with the anchored-climb guard preserved); queries in `VaultIndex` (subtree
listing already exists: `filesInSubtree`, natural-order key); thin `Q_INVOKABLE` wrappers on
`VaultLibrary`: `browseAt(rootOrPath)` → typed rows `{key, nodeType: folder|show|season|film|
episode|clip, displayTitle, physicalFact, state: resolving|identified|uncertain|localOnly,
away, counts, coverRef, path}`, `rootsDetail()` → `{path, name, available, itemCount,
fileCount}`, `recentArrivals(limit)` → newest-mtime identified groups (mtime is the arrival
truth v1 shows; a durable addedAt column is the ownership arc's business). Local-only = the
kind classifier's non-catalogue media (the Cricket shape). Extras folder recognition
(`Extras/`, `Featurettes/` conventions) folds as extras, never grid nodes.
**Behavior to preserve:** existing `series()`/`items()` consumers (current shelves keep working
until Slice 5 swaps the face); identity read-only — no new identity writes; the publish
identity-carry path untouched.
**Baseline:** `ctest -L unit` 32/32 green; `tst_vault_kit` 27/27.
**Focused tests:**
  - Qt Test: extend `colosseum.qttest.vault_kit` (collapse planner table over the five real
    library shapes above; absolute-numbering grammar cases incl. `Gintama - 003` → E3 and the
    bare-`Season N` guard still holding) and `colosseum.qttest.vault_index` (subtree listing
    feeding the planner, natural order). New target `colosseum.qttest.vault_browse` if the
    planner needs library-level composition (register in `tests/CMakeLists.txt`, labels
    `unit;qttest`, pattern per ledger).
  - Qt Quick Test: not applicable — no QML in this slice.
  - Existing harnesses: `-L unit` full gate stays green.
  - Negative control: flip the film-folder collapse expectation (one-film folder must yield
    `nodeType: film`, flipped to `folder`) → exactly one named red; restore.
**Test seam status:** available (the vault Qt Test family + registration pattern exist).
**Lanista actions:** not applicable.
**Completion signal / probes / visual evidence / regression paths:** not applicable (internal).
**Evidence artifacts:** ctest output in the slice's commit message; new cases named.
**Bridge status:** not applicable.
**Completion criterion:** new projection methods exist with the row contract above; all named
Qt Test cases green including negative control performed and restored; `-L unit` green; no
change to any shipped QML.

### Slice 2: Durable uncertainty + state facts (C++)

**Purpose:** Make "Vault is not sure" a first-class, durable fact so a tile can wear it and
identify-in-place can clear it — instead of uncertainty hiding inside "not identified yet."
**Dependencies:** Slice 1.
**Implementation guidance:** `VaultIdentifier` already computes candidates and adopts only on
exactly-one (recon Gate 3 pins). Record the ambiguity outcome durably: an `identityState` column
on `VaultIndex` rows (monotonic `ALTER TABLE ADD COLUMN`, the proven migration shape) written via
the existing owner-thread paths — values `none | ambiguous(candidateCount) | adopted | suppressed`.
Projection (Slice 1's `state`) maps: no candidates yet → `resolving`; ambiguous → `uncertain`;
adopted → `identified`; local-only kind → `localOnly`. Respect recon Gate 9 law: no new
off-thread writes.
**Behavior to preserve:** auto-adoption stays exactly-one (already aligned with the parent's
"Very likely is suggestion-only"); Un-identify still suppresses; ceremonies untouched.
**Baseline:** current behavior — ambiguous groups indistinguishable from unscanned in any
projection (assert this in a pre-change test run).
**Focused tests:**
  - Qt Test: new `colosseum.qttest.vault_identifier_state` compiling VaultIdentifier + fixture
    catalogs built per-run in tempdirs (the estate's proven "catalogs over SQLite-in-tempdir"
    pattern): one-candidate → adopted; two-candidate → ambiguous(2) recorded, NOT adopted;
    zero-candidate → none; suppression round-trip. `vault_index` case: identityState column
    round-trip + survives publish for stable tuples.
  - Qt Quick Test: not applicable.
  - Existing harnesses: `-L unit`; `colosseum.qttest.vault_stores` untouched-green.
  - Negative control: flip two-candidate expectation to `adopted` → one named red; restore.
**Test seam status:** available (constructor-injected catalogs; tempdir DB pattern in estate).
**Lanista actions / completion signal / probes / visual / regression:** not applicable (internal).
**Evidence artifacts:** ctest output; column migration noted in commit message.
**Bridge status:** not applicable.
**Completion criterion:** ambiguity durable and projected; all named tests green with negative
control; `-L unit` green; no QML change.

### Slice 3: Local artwork adoption (C++)

**Purpose:** Tiles wear real art wherever the disk already provides it — comic covers (shipped)
plus folder artwork companions (`poster.jpg`/`folder.jpg`/`cover.jpg`) for video groups — and the
typographic fallback contract for everything else. No network.
**Dependencies:** Slice 1.
**Implementation guidance:** extend `VaultEnricher` (its `coverRef` column + buffered
owner-thread commit discipline already exist): during enrichment, detect artwork companions in
the group's folder and record as the group's `coverRef` (namespaced ref, e.g. `file://` vs the
comic `image://comiccover/` refs). Junk-image guard: the Spider-Man folder's `www.YTS.MX.jpg`
must NOT be adopted — adopt only conventional names, never any-jpg-in-folder. Projection carries
`coverRef` through `browseAt`.
**Behavior to preserve:** comic cover path untouched; enricher cancellation + thread law.
**Baseline:** current: video groups have empty coverRef (assert pre-change).
**Focused tests:**
  - Qt Test: extend `colosseum.qttest.vault_enricher` — conventional-name adoption; junk-name
    refusal (the real `www.YTS.MX.jpg` case); no-artwork → empty ref (fallback is QML's job);
    corrupt image file → honest empty, never a wedge.
  - Qt Quick Test: not applicable.
  - Existing harnesses: `-L unit`.
  - Negative control: flip junk-refusal to expect adoption → one named red; restore.
**Test seam status:** available.
**Lanista / signals / probes / visual / regression:** not applicable (internal).
**Evidence artifacts:** ctest output.
**Bridge status:** not applicable.
**Completion criterion:** artwork companions adopted by convention only; tests green with
negative control; `-L unit` green.

### Slice 4: The card components (QML, unwired)

**Purpose:** Build the two cards every screen uses — poster (2:3) and wide (16:9) — with the full
state wardrobe, so the face assembles from proven parts.
**Dependencies:** Slices 1–3 (row contract), the approved mock (visual truth).
**Implementation guidance:** new `qml/VaultPosterCard.qml` + `qml/VaultWideCard.qml` consuming
one Slice-1 row each. Card language from the locked design §6.3 exactly: art edge-to-edge,
nothing printed over it; centered one-line title below; dim physical-fact line beneath; circular
corner indicators; near-square corners; hover dims art + reveals play affordance; gold ONLY for
the uncertainty mark; away = reduced ink + desaturation, no hover; resolving = filename on plain
ground with the crossfade contract (a `faceState` property transition, animation ≤ the house
motion register); no-art = typographic treatment (title on quiet ground — never an empty frame,
never a broken-image glyph). Tokens from `Theme.qml` singleton — zero literal hex. objectNames:
`vaultBrowseCard_<key>` + `vaultBrowseCard_<key>_art`. Expose `state`, `displayTitle`,
`physicalFact` as readable properties (the Lanista vocabulary for Slices 5–9).
**Behavior to preserve:** none shipped yet (unwired) — but `VaultTile.qml` stays untouched until
Slice 5 retires it from the populated face.
**Baseline:** n/a (new components).
**Focused tests:**
  - Qt Test: not applicable.
  - Qt Quick Test: new `tests/qml/tst_vault_cards.qml` in the `colosseum.qml` runner (seeded
    with plain JS row objects — no library needed): each state renders its contract (resolving
    shows filename not title; uncertain shows the gold mark; away disables hover/open signal;
    no-art shows title text, no Image error); title elision at one line (the real Shubman Gill
    filename as the long-title case); crossfade fires exactly once per faceState change
    (SignalSpy); poster vs wide aspect by nodeType.
  - Existing harnesses: `colosseum.qml` target green (44+new cases).
  - Negative control: flip the uncertain-mark expectation → one named red; restore.
**Test seam status:** available (`colosseum.qml` runner + registration pattern exist).
**Lanista actions:** not applicable (unwired — component truth is Quick Test's layer).
**Completion signal / probes / visual / regression:** not applicable.
**Evidence artifacts:** qml gate output.
**Bridge status:** not applicable.
**Completion criterion:** both components + all named Quick Test cases green with negative
control; no shipped surface changed.

### Slice 5: The assembled Browse face

**Purpose:** The overhaul lands: opening the Vault now shows carousel → collapsible rail →
breadcrumb → media-faced grid, folder-true at every level — with every existing Vault capability
still reachable.
**Dependencies:** Slices 1–4.
**Implementation guidance:** rework `VaultPage.qml`'s populated face: `FeaturedCarousel` reuse
(slides from `recentArrivals`; slide fact-line = physical fact ONLY — a blurb is a tagline;
neutral gradient per the locked design §4.10), rail (collapsed default: glyph + availability dot
per root from `rootsDetail()`; expanded adds names/counts; toggle persists per session),
breadcrumb (middle-collapse per §4.5), grid = `GridView` of Slice-4 cards over `browseAt()`
(virtualized — Gintama scale is real), drill = path push + crumb extend, `Backspace` ascends.
Capability rewiring, explicitly: drop surface + Add storage (rail's add affordance → existing
`addFolder`), scan pill, confirm card (`vaultCard`), hidden shelf (reachable from rail context
or crumb menu — capability preserved, placement is executor's call within the design), identify
dialogs + ceremonies (unchanged components), `revealInExplorer` on card context menu,
double-click/Enter on film/episode/clip → detail sheet comes in Slice 7 — until then Play routes
as today (`openMediaRequested`). The unpopulated (no-roots) face keeps its current invitation
until Slice 9 restyles empty states. objectNames: `vaultBrowseCarousel`, `vaultBrowseRail`,
`vaultBrowseRailRoot_<n>`, `vaultBrowseRailToggle`, `vaultBrowseCrumb`, `vaultBrowseGrid`.
Retired-from-face (not deleted): `vaultMarquee`, `vaultShelf_*` — grep the 88 static-grep
runners for these names first; any runner asserting them is re-pointed in this slice, named in
the commit.
**Behavior to preserve:** every capability in the scope list; `vault_launch_smoke.json` (7/7)
and `vault_open_recent.json` (13/13) scenarios; taskbar door behavior (`taskbarVaultDoor`
pulse/no-badges law untouched); Main.qml routing (`openLocalMedia`/`openVaultIdentity`)
unchanged.
**Baseline:** isolated session against HEAD pre-slice: whole-run manifest + window grab of the
current shelves face; `dump-ui` capture of current vault objectNames (the retirement record).
**Focused tests:**
  - Qt Test: Slice-1 suite stays green (projection is the page's only data source).
  - Qt Quick Test: new `tests/qml/tst_vault_browse_page.qml` — seeded projection stub: grid
    populates one card per row; drill emits path change + crumb row; rail collapsed default,
    toggle expands; carousel present with ≥1 slide when arrivals non-empty.
  - Existing harnesses: `-L unit` + `colosseum.qml` + re-pointed vault `.ps1` gates.
  - Negative control: seeded-empty projection → grid count 0 (proves the populate assertion can
    fail); flip once, restore.
**Test seam status:** available.
**Lanista actions:** new scenario `tests/lanista_scenarios/vault_browse_smoke.json`, isolated
session, vault fixture pre-placed in Roaming (confirmed root at a fixture tree mirroring the
five real shapes): wait `bootSplash.visible == false`; `ui-click` `taskbarVaultDoor`; wait
`vaultPage` visible; `qml-get` `vaultBrowseGrid.count` == expected node count (collapse proven
at runtime: the one-film folder contributes a film card, Loki's two folders contribute ONE
show card); `ui-click` `vaultBrowseCard_<folderKey>` (a plain folder) → wait `vaultBrowseCrumb`
`currentPath` == that folder; `ui-click` `vaultBrowseRailToggle` → wait `vaultBrowseRail`
`expanded == true`; item-grab `vaultBrowseGrid`.
**Completion signal:** each step's `ui-wait-for` strict equality above; scenario exit 0.
**State / events / probes:** `qml-get` on grid count, crumb path, rail expanded,
`vaultBrowseCard_*` `state`/`displayTitle` for the five fixture shapes (Loki card
`displayTitle == "Loki"` while two folders exist on disk — the collapse observed live).
**Visual evidence:** item-grabs of grid + rail both states (item grabs, not whole-window — the
window-grab readback nondeterminism is a known ledger issue); PNGs into the session run dir.
**Regression paths:** `vault_launch_smoke` + `vault_open_recent` replayed green in fresh
isolated sessions; scroll grid away/back (delegate recycling); leave Vault via taskbar → return
(session persistence: same folder + scroll per design §4.8); restart the session app → same
folder + sort restored.
**Evidence artifacts:** `artifacts/lanista-sessions/<id>/` manifests + grabs; baseline
comparison note in the slice report.
**Bridge status:** available (session run, ui-click, ui-wait-for, qml-get, item grabs — all
AVAILABLE; Roaming pre-place per ledger workaround).
**Completion criterion:** Runtime-validated — scenario green in an isolated session + both
regression scenarios green + deterministic gates green + capability checklist ticked in the
report (each preserved capability exercised once, `human-witnessed:` acceptable only for the
native-dialog items the bridge structurally cannot see: OS file picker, drag-drop from
Explorer).

### Slice 6: Living tile states (resolve-in-place, away, uncertain)

**Purpose:** The signature: tiles that admit what they don't know yet, heal in front of you,
mark what's unreachable, and let you fix uncertainty where you stand.
**Dependencies:** Slice 5.
**Implementation guidance:** wire the Slice-4 states to live signals: `changed()`/revision →
re-project visible rows (cards crossfade when their row's state/title changed — key-stable
delegates, no grid rebuild); `onRootAvailabilityChanged` → away flags flow through projection;
uncertain card's mark/click opens the existing `VaultIdentifyDialog` seeded with the group
(identify-in-place; on `identifyGroupWith` success the card settles via the same re-project
path). Sibling-rule resolution (a learned decision resolving siblings) is the parent's rules
arc — here one manual identify settles ONE group; no rule engine.
**Behavior to preserve:** dialog flows exactly as shipped (17B); suppression semantics;
scan pill behavior during the churn.
**Baseline:** Slice-5 session grabs (static states only).
**Focused tests:**
  - Qt Test: projection state-transition table (Slice 1/2 suites extended: row state changes
    when identity adopted / root away).
  - Qt Quick Test: `tst_vault_browse_page.qml` extended — stub projection flips a row
    resolving→identified → exactly that card refaces (SignalSpy on its crossfade), grid does not
    rebuild (delegate identity stable); stub away flip → card enters away, open signal inert.
  - Existing harnesses: `-L unit` + `colosseum.qml`.
  - Negative control: away-flip case expects hover still active → red; restore.
**Test seam status:** available.
**Lanista actions:** extend an isolated scenario family:
  (a) **resolve-in-place** — session with fixture catalogs pre-placed in the tagged root (the
  per-run SQLite fixture-catalog pattern from the test estate) so identification has local truth,
  fixture root pre-placed unidentified: boot → wait `vaultBrowseCard_<key>.state == "resolving"`
  fails fast if already identified (ordering guard) → wait `state == "identified"` (timeout
  raised via payload `timeout_ms`) → `qml-get` `displayTitle` == canonical title.
  (b) **away** — config pre-placed with a confirmed root whose path does not exist: boot → wait
  that root's `vaultBrowseRailRoot_<n>.available == false` → `qml-get` its cards' `state` ==
  "away".
  (c) **uncertain + identify-in-place** — fixture catalog with TWO candidates for one group:
  wait `state == "uncertain"` → `ui-click` the card's mark → wait identify dialog visible →
  `ui-click` first result's Use-this → wait `state == "identified"`.
**Completion signal:** the exact `ui-wait-for` equalities above.
**State / events / probes:** card `state`/`displayTitle` transitions; `log-mark` before/after
each phase for correlation.
**Visual evidence:** item-grabs before/after the resolve transition (two frames of the healing).
**Regression paths:** `vault_browse_smoke` green; scroll-away/back during churn (recycled
delegates re-read state, no stale faces); Un-identify → card returns to resolving/uncertain
honestly.
**Evidence artifacts:** session manifests + paired before/after grabs.
**Bridge status:** available (all actions in AVAILABLE; away is fixture-driven, no hardware).
**Completion criterion:** Runtime-validated — all three scenarios green isolated + Quick Test
transitions green + regressions green. The *feel* of the crossfade (§ signature) is explicitly
deferred to Slice 10's eyes-on — pixels are his.

### Slice 7: The detail sheet

**Purpose:** Opening a film answers "what do I physically hold": every copy with its drive,
companions, extras, why Vault believes the identity — and Play. Never cast, synopsis, related.
**Dependencies:** Slice 5.
**Implementation guidance:** new `qml/VaultDetailSheet.qml` (same-window surface — the bridge
cannot see own-window popups; ledger law) fed by one projection call (`browseDetail(key)` added
to Slice-1's spine: copies = same canonical identity across roots where identity exists, else
the single physical group; companions; extras; evidence summary strings from identifier facts;
best-quality line). Play → existing `openMediaRequested` path; Reveal, Identify/Un-identify,
Hide on the sheet. objectNames `vaultBrowseSheet`, `vaultBrowseSheetCopy_<n>`,
`vaultBrowseSheetPlay`. Evidence copy in user-end language (the mock's register), no provider
jargon.
**Behavior to preserve:** launch routing (`localLaunchState` semantics untouched); ceremonies.
**Baseline:** Slice-5 behavior (film card Play routes directly).
**Focused tests:**
  - Qt Test: `browseDetail` contract — Spider-Man fixture: 1 copy, 2 companions (srt + Subs),
    2 ignored junk; extras-folder fixture: extras listed, not gridded; two-root same-identity
    fixture: 2 copies one sheet.
  - Qt Quick Test: sheet renders seeded detail (copies rows, companions chips, evidence text);
    Play emits with the right path; Esc/back dismisses.
  - Existing harnesses: `-L unit` + `colosseum.qml`.
  - Negative control: junk-count expectation flipped → red; restore.
**Test seam status:** available.
**Lanista actions:** scenario extension: `ui-click` film card → wait `vaultBrowseSheet.visible
== true` → `qml-get` copy count/fact strings → `ui-click` `vaultBrowseSheetPlay` → wait
`localLaunchState.openCount == 1` and `lastRouteKind` == expected (the shipped machine-checkable
launch seam).
**Completion signal:** the equalities above.
**State / events / probes:** sheet facts vs fixture truth; `localLaunchState` after Play.
**Visual evidence:** item-grab of the sheet on the Spider-Man fixture.
**Regression paths:** `vault_launch_smoke` + `vault_open_recent` green (launch path shared);
sheet → back → grid state intact.
**Evidence artifacts:** session manifest + sheet grab.
**Bridge status:** available.
**Completion criterion:** Runtime-validated — scenario green isolated; Play proven through
`localLaunchState`; deterministic gates green.

### Slice 8: Series drill — seasons and the episode wall

**Purpose:** A show opens to its seasons; a season opens to 16:9 episode cards — at Gintama
scale, with the folder's claims checked against what's physically held.
**Dependencies:** Slices 5, 6 (Slice 1 grammar already landed).
**Implementation guidance:** drill nodeTypes: show → seasons band (posters, `<n> episodes`
facts + season-presence honesty: The Wire fixture shows "season 4 only"); season → wide-card
grid (episode fact line `S1:E3 · 1080p`, absolute-numbered mapped by Slice-1 grammar); loose
clips (Cricket) render wide at folder level. Virtualization proof at scale: fixture with 300+
episode stubs, `cacheBuffer` tuned, scroll must recycle.
**Behavior to preserve:** episode Play routes like any file; grid persistence (§4.8) across
drill depth.
**Baseline:** Slice-5 drill (shows open as generic folders until this slice).
**Focused tests:**
  - Qt Test: grammar + presence facts already Slice 1; extend with the Wire-claims case
    (folder-name claim 1-5 vs held {4} → fact string "season 4 only").
  - Qt Quick Test: wide-card grid renders seeded 300-episode model without instantiating all
    delegates (count of created delegates < total; the virtualization assertion); season band
    order natural.
  - Existing harnesses: `-L unit` + `colosseum.qml`.
  - Negative control: flip the Wire fact expectation to "5 seasons" → red; restore.
**Test seam status:** available.
**Lanista actions:** scenario: drill Gintama fixture → wait crumb == show → `qml-get` seasons
band count == 10 → drill Season 1 → wait grid nodeType == episodes → `qml-get` first card
`physicalFact` contains-free exact string (equality only — compose the exact expected string) →
`ui-scroll` the grid → `qml-get` a later card exists by name.
**Completion signal:** equalities above; scenario exit 0.
**State / events / probes:** crumb path at each depth; card counts; episode fact strings.
**Visual evidence:** item-grabs: seasons band; episode wall.
**Regression paths:** `vault_browse_smoke`; back-out from depth restores each level's scroll.
**Evidence artifacts:** session manifest + grabs.
**Bridge status:** available.
**Completion criterion:** Runtime-validated — scenario green isolated at fixture scale;
virtualization Quick Test green; gates green.

### Slice 9: Empty states + keyboard reach

**Purpose:** The four distinct empty answers (no storage / genuinely empty / all away /
filtered out) and full keyboard traversal with a visible focus ring.
**Dependencies:** Slice 5 (6 for the all-away state).
**Implementation guidance:** empty-state component keyed by CAUSE (projection already knows
which); copy per the approved mock, no taglines; "filtered out" ships only if a filter control
shipped earlier — else that state waits for the deferred filter arc (do not invent a filter
for its empty state; name the omission in the report). Keyboard: grid arrow-key model,
Enter opens, Backspace ascends, Tab reaches rail, visible focus ring from house tokens.
**Behavior to preserve:** no shortcut collisions with Colosseum globals (parent §4.22 governs;
inventory Main.qml shortcuts first).
**Baseline:** Slice-5 unpopulated face + mouse-only grid.
**Focused tests:**
  - Qt Test: projection empty-cause enum (no-roots vs empty-folder vs all-away).
  - Qt Quick Test: focus-ring visible on keyboard focus (not on mouse hover); arrow traversal
    order matches visual order; Enter emits open; Backspace emits ascend; each empty cause
    renders its own copy (four distinct strings asserted) — real-window keyboard truth is this
    layer's job (ledger: `ui-keypress` is first-key-only — keyboard proof deliberately lives
    here, NOT in Lanista).
  - Existing harnesses: `-L unit` + `colosseum.qml`.
  - Negative control: two empty causes asserted to share copy → red; restore.
**Test seam status:** available.
**Lanista actions:** empty-cause scenarios: (a) session with NO vault config pre-placed → wait
no-storage state objectName visible; (b) config with confirmed root → empty dir → empty-folder
state; (c) Slice-6 away fixture with ALL roots dead → all-away state (`qml-get` its exact copy
string).
**Completion signal:** visibility equalities above.
**State / events / probes:** empty-state objectName + copy string per cause.
**Visual evidence:** item-grab per empty state.
**Regression paths:** `vault_browse_smoke`; populated↔empty transition when a root is added
via `addFolder` (drivable: `invoke`-free — use the rail add affordance only if it avoids the
native picker; else `human-witnessed:` add-storage step recorded).
**Evidence artifacts:** session manifests + three grabs.
**Bridge status:** available (keyboard excluded from bridge by design, covered at Quick Test).
**Completion criterion:** Runtime-validated for empty states via scenarios; keyboard
Test-reported at Quick Test layer + folded into Slice 10's eyes-on (his hands on real keys).

### Slice 10: Closing gate — encyclopedia, ledgers, eyes-on

**Purpose:** The work becomes maintainable truth and Hemanth judges the face against the mock.
**Dependencies:** Slices 1–9.
**Implementation guidance:** write `docs/encyclopedia/vault.md` (+ index entry) — none exists
for this subsystem today; update BOTH verification ledgers in the same commits as their
capability changes (new tests, new scenarios, new named surfaces — the ledgers' own maintenance
law); README credit line for Jellyfin (standing rule: influence credited, never de-styled);
assemble the eyes-on gallery: baseline (pre-Slice-5) vs final grabs per state/screen, the mock
alongside.
**Behavior to preserve:** everything shipped.
**Baseline:** the Slice-5 baseline artifacts.
**Focused tests:**
  - Qt Test / Qt Quick Test: full deterministic sweep — `-L unit` + `colosseum.qml` + the
    re-pointed vault `.ps1` gates, all green in one run, logged.
  - Existing harnesses: as above.
  - Negative control: not applicable (no new test surface; the sweep is the gate).
**Test seam status:** available.
**Lanista actions:** full scenario suite replay (`vault_browse_smoke`, states family, sheet,
series, empty family, `vault_launch_smoke`, `vault_open_recent`) — fresh isolated sessions, one
evidence run.
**Completion signal:** every scenario exit 0.
**State / events / probes:** per scenario as defined in their slices.
**Visual evidence:** the gallery — item-grabs per screen/state + the mock's plates side by side.
**Regression paths:** the whole suite IS the regression pass.
**Evidence artifacts:** gallery under `Colosseum/agents/eyes-on/<date>-vault-browse/`; ledger
diffs; encyclopedia page.
**Bridge status:** available.
**Completion criterion:** **Hemanth's eyes** — the aesthetic verdict on the assembled face
against the approved mock (spacing, warmth, the crossfade feel, the carousel presence), plus
his hands on the keyboard pass, recorded in the gallery manifest. Only his approval closes the
plan; every prior slice status feeds this gate, never replaces it.

---

## Plan self-review (performed)

- Every user-visible slice (5–10) carries the full field contract; every Lanista action named is
  in the ledger's AVAILABLE section (`session run`, `ui-click`, `ui-wait-for` strict equality,
  `qml-get`, `ui-scroll`, item grabs, `log-mark`); the Roaming seed limitation is honored by
  pre-placement; no sleeps anywhere — every wait is a property equality.
- No Planned/Unavailable capability is used: no event waits, no absence assertions (expected
  strings are asserted as exact equalities instead), no secondary-window surfaces (sheet is
  same-window by design), no `window-set-state`, no whole-window visual gates (item grabs; the
  known render-readback nondeterminism is avoided, not fought).
- Internal slices (1–4) are genuinely invisible: projection, columns, enricher refs, unwired
  components. Slice 4's components ship in the tree but on no surface.
- Baselines exist for 5–10; negative controls named for every new regression assertion.
- Order respects dependencies; no bridge prerequisites needed — the plan was shaped to the
  bridge that exists (keyboard at Quick Test; native dialogs human-witnessed; away
  fixture-driven).
- Honest risk calls: (1) v1 artwork is local-only — most video tiles are typographic until the
  artwork arc; (2) "filtered out" empty state may be legitimately absent (no filter control in
  scope); (3) `tst_search_history_flow` flake is pre-existing and stays owned by its lane;
  (4) recon pins into the three e08424b-touched files need one re-check at execution start.
