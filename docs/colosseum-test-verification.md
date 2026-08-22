# Colosseum Test Verification — the native/QML test ledger

> **What this is.** The honest inventory of Colosseum's native and QML test estate — the
> counterpart to `colosseum-lanista-verification.md` (which owns bridge/runtime capability).
> Planning consults BOTH before naming any test: this file controls what deterministic
> proof exists **today**; naming a test that isn't in here is inventing a capability.
>
> Ground truth as of Colosseum `236021a`, from a full read-only sweep of
> `native/CMakeLists.txt` and `tests/` (2026-08-06). If code and ledger disagree, the code
> wins — fix this file in the same commit. Maintained by whoever changes a test, a runner,
> or a registration.

## Headline shape (read this first)

| Fact | Count |
|---|---|
| Compiled harness/test targets in `native/CMakeLists.txt` | **71** (+ the app + the `lanista` CLI) |
| C++ harness sources in `tests/*.cpp` | 70 (zero orphan sources — every one is a target) |
| Hand-rolled QML harnesses in `tests/*.qml` | 88 (+ 1 fixture scene) |
| Real Qt Quick Test files (`tests/qml/tst_*.qml`) | **2** |
| PowerShell runners in `tests/*.ps1` | **150** (+ 18 in the gated-off player2 lab) |
| CTest / `add_test` / `Qt6::Test` / `Qt6::QuickTest` in the active build | **ZERO** |
| C++ harnesses invoked by NO runner or script | **39 of 69** |
| Runners that are pure static source-greps (no binary run) | **88 of 150** |
| Runners pointing at files that do not exist (broken) | 2 |

**The estate's real problem in one sentence:** the tests exist and mostly isolate
correctly, but there is no registration, no selection, no machine-readable output, and no
master gate — every runner is a standalone script a human must know to run, and more than
half the compiled harnesses are run by nobody.

## Build entry

- Active build root: `native/CMakeLists.txt`. All 70 harnesses are plain
  `add_executable` + `target_link_libraries`, built on every build, run by hand or by a
  `.ps1`.
- **No `include(CTest)`, no `enable_testing()`, no `add_test()`, no `Qt6::Test`, no
  `Qt6::QuickTest` anywhere in the active build.** The in-tree reason
  (`native/CMakeLists.txt:1274`): QVERIFY-style macros don't fit the house
  failure-collecting `main()` idiom.
- **Exception — the Player 2 lab** (`native/player2/CMakeLists.txt`): the repo's only real
  CTest suite (18 `add_test` entries, `Qt6::Test` linked), but gated behind
  `COLOSSEUM_BUILD_PLAYER2=OFF` and scoped by an `enable_testing()` call in a
  SUBDIRECTORY — top-level `ctest` will not see it even when built.
- Only two POST_BUILD deploy steps exist, both on the `colosseum` app target (Qt SQL
  driver + FFmpeg DLLs). **Trap:** the five SQL harnesses find `qsqlite.dll` only because
  they land in the same `build-msvc` dir the app deployed into — run them from elsewhere
  and they fail.

## Standard commands (the fuse box — slice 2, 2026-08-06)

- **Fast native gate:** `ctest --test-dir native/build-msvc -L unit --output-on-failure`
  — runs the registered pilot set (labels below). This is the default deterministic gate.
- **Everything discovered:** `ctest --test-dir native/build-msvc -N` (29 today: 12
  `colosseum.*` registrations + 17 Player 2 lab tests the seam surfaced — see gaps).
- Registration lives in `tests/CMakeLists.txt` (registers only; defines no targets),
  entered from the tail of `native/CMakeLists.txt` under `include(CTest)` +
  `if(BUILD_TESTING)`. OFF skips registration and changes no build output.
- **Toggle nuance (verified 2026-08-06):** configuring an EXISTING build dir with
  `-DBUILD_TESTING=OFF` leaves the previous run's `CTestTestfile.cmake` manifests on
  disk (CMake doesn't write them under OFF, so it doesn't remove them either) — `ctest`
  then reads stale manifests. A fresh generate under OFF writes none. Don't trust
  `ctest -N` on a dirty toggled dir.
- **Red detection is self-proven:** `colosseum.selftest.red_canary` (WILL_FAIL) exercises
  ctest's failure path on every run.

## Legacy commands (the pre-seam estate — still authoritative for what it gates)

- No master gate exists. Each `.ps1` under `tests/` is standalone; plans and handoffs name
  the gate to run. Output is stdout sentinels (`<NAME>_OK` / `FAIL: ...`) + exit codes.
- QML harnesses run via the HARDCODED path `C:/Qt/6.11.1/msvc2022_64/bin/qml.exe`
  (49 runners; breaks in 49 places on any Qt bump), 42 with `-platform offscreen`.
- The two real Quick Test files run via the Qt-install `qmltestrunner.exe`
  (`test_comicreader_chrome.ps1`, `test_search_history_p0.ps1`).
- Lanista's gate: `tests/test_lanista.ps1` (greps + harness selfcheck + two scenarios on
  the `ColosseumLanistaTest` pipe, readiness-polled, never the daily pipe). As of Slice W0
  (2026-08-12) this gate also calls `tests/warning_gate.ps1` on its own captured logs after the
  scenarios run - the first (and, for this slice, only) opt-in caller of the new warning gate.
  **L1-Bridge (2026-08-13)** added 8 named cases to `tests/lanista_harness.cpp` proving the all-item
  structural `dump-ui`/`ui-query` (see the Lanista ledger's "Structural dump" section):
  `structural_fields_are_versioned`, `structural_dump_includes_unnamed_items`, `parent_chain_is_exact`,
  `clipping_chain_is_exact`, `stale_structural_handle_is_rejected`, `requested_bounds_are_clamped`,
  `reply_budget_sets_truncated`, `continuation_resumes_without_duplicates`. Each prints `CASE_OK: <name>`
  before the harness `LANISTA_OK` sentinel; negative control performed (omitting the unnamed fixture row
  turned exactly `structural_dump_includes_unnamed_items` red, restored). No CMakeLists change - the
  `lanista_harness` target already existed.
  No native/QML change; full detail (command, allowlist location, measured baseline, negative
  controls) lives in `docs/colosseum-lanista-verification.md` under "Warning gate (Slice W0,
  2026-08-12)" since it is a runtime/bridge-domain capability, not a compiled test target.
  **F1-Bridge (2026-08-13)** added 5 more named cases to the SAME `tests/lanista_harness.cpp`:
  `vault_forensics_is_read_gated`, `vault_forensics_passes_response_unchanged`,
  `vault_forensics_rejects_bad_scope`, `vault_forensics_clamps_limit`,
  `vault_forensics_deadline_is_bounded` — proving the new `vault-forensics` bridge command against a
  REAL `VaultLibrary` fixture (105 rows, `native/CMakeLists.txt`'s `lanista_harness` target now also
  links the full `VaultLibrary` dependency chain and `Qt6::Sql`/`Qt6::Concurrent`, mirroring
  `tst_vault_forensics`'s own link list). Negative control performed (temporarily asserting exactly
  101 rows through the bridge turned exactly `vault_forensics_clamps_limit` red — "got 100" — restored).
  All 13 named `lanista_harness` cases green together, `LANISTA_OK`. Full detail: the Lanista ledger's
  "Vault forensics — F1-Bridge" section. Companion Python contract tests (no native build/live app
  needed): `tests/test_lanista_mcp_forensics.py` — `tool_schema_is_exact`, `summary_round_trip`,
  `node_round_trip`, `deadline_is_bounded`, `legacy_tools_unchanged` (12 cases total incl. supporting
  checks), run via `python tests/test_lanista_mcp_forensics.py -v`.

## Tankoban Reading Room gate (2026-08-11)

- `tests/test_manga_reading_room.ps1` runs `tests/manga_reading_room_harness.qml` with fake
  `TankobanVolumes`, `Progress`, and `Downloads` seams. It asserts the fixed-height room,
  virtualized long-series grid, bounded cover requests, continue auto-land, all canonical
  tile states, Chapters behavior, Select-mode exact batches, and chapter-only routing.
- Registered as `colosseum.manga_reading_room` in `tests/CMakeLists.txt`, label `unit`.
- Existing Tankoban cover/batch scripts were re-pointed from retired numbered paging to
  continuous viewport fetching and Select mode; their source/progress/ownership assertions
  remain in place.

## Tankoban catalogue-independence Slice 2 gate (amended, 2026-08-20)

- `MalCatalog::mangaById(int malId)` — NEW single-row identity accessor (native/engine/
  MalCatalog.{h,cpp}), one bound `SELECT ... FROM manga WHERE mal_id = ?` mapped to the
  same Jikan-shaped row `genreEntries()` emits for manga (title/title_english/score/
  scored_by/members/status/volumes/chapters/year+published/images.jpg.large_image_url/
  synopsis/authors/genres). Empty map when not ready or malId not found. Covered by NEW
  cases in the existing `mal_catalog_rows_harness` target (`tests/mal_catalog_rows_harness.cpp`
  — a manga-table fixture was added alongside the existing anime/tag fixture): found row
  carries every field (title/score/members/status/volumes/year+published/images/synopsis/
  authors/genres); a zero-volume/null-score row still returns honestly (volumes==0, no
  `score` key when the source is NULL); unknown/0/negative malId → empty map; a not-ready
  db (missing file) → empty map. Run from `native/build-msvc` (qsqlite deploy trap).
  9/9 → still 9/9 pass count unchanged in stdout form (two `PASS` lines now: the existing
  `animeCatalog` contract + the new `mangaById` contract); negative control performed live
  2026-08-20: flipped the expected title to a wrong string → exactly that `require()` reds
  (`FAIL: mangaById row carries title`), rebuilt and restored → green again. Not CTest-
  registered (this target has no `.ps1`/`add_test` wrapper yet, same as most of the 39
  unregistered C++ harnesses named in "Known gaps" below); run directly:
  `native/build-msvc/mal_catalog_rows_harness.exe`.
- `qml/MangaSeries.qml` `resolve()` — rewritten to be fully SYNCHRONOUS and provider-free
  (catalogue-independence Slice 2, amended 2026-08-20): identity resolves via malId
  (Discover) or a single exact `MalCatalog.matchByTitle()` candidate; masthead facts
  (title/author/status/year/score/synopsis/poster) come from `mangaById()` alone. No
  `Manga.search`/`Manga.art`/`Manga.chapters`/`Manga.detail` call remains on this path; the
  12s `revealGuard` timer is gone. `seriesId` is `"mal:"+malId` on every resolved page, `""`
  when identity never resolves (ambiguous/unknown title) — Library falls back to the legacy
  title-keyed shape, no persisted-row re-key. New injectable seams: `malCatalogRef`/
  `tankobanCatalogRef`/`tankobanVolumesRef` properties (context-property defaults, the
  `TankobanDiscoverPage` pattern) so the page constructs bare in a harness. New automation
  Item `tankobanSeriesMasthead` (world-namespaced, invisible): `ready`/`resolvedMalId`/
  `displayTitle`/`hasShelf`/`primaryAction` scalars — truth table `open` (TankobanVolumes
  reports volume "1" `state=="ready"`) > `get` (TankobanCatalog's baked `seriesInfo(malId)
  .volumeCount>0`) > `search` (neither). The legacy MAL-keyed `Manga.volumes("", title,
  malId)` shelf-seed call is KEPT this slice (guarded `typeof Manga !== "undefined"`) so
  the shelf does not go empty in the interim — Slice 3 replaces it with a TankobanCatalog-
  fed seed. `page.score` changed `int`→`real` (a real bug found live wiring the harness: an
  `int` property silently truncated MAL's one-decimal scores, e.g. 9.1→9).
  NEW `tests/manga_series_catalogue_harness.qml` + `tests/test_manga_series_catalogue.ps1`,
  registered `colosseum.manga_series_catalogue` (label `unit`) in `tests/CMakeLists.txt`,
  mirroring `colosseum.manga_reading_room`'s qml.exe/offscreen shape. Fake MalCatalog/
  TankobanCatalog/TankobanVolumes seams (no network, no live app state); instantiates the
  real `MangaSeries.qml` bare via `Qt.createComponent`. Cases: malId-open renders fake-row
  facts (`ready==true`, `seriesId=="mal:1"`, every masthead field, `hasShelf==true`,
  `primaryAction=="get"`); a single exact-title `matchByTitle` candidate resolves the same
  way; an ambiguous title (2 candidates) and an unmatched title (0 candidates) both give the
  honest shelf-less page (`resolvedMalId==0`, `seriesId==""`, `primaryAction=="search"`);
  the full button truth table (`open`/`get`/`search`); the WC error-copy path is gone
  (`errorMsg`/`errorText` never set — no `Manga.search`/`onEngineError` handler remains).
  Green 2026-08-20; negative control performed live via the harness's own
  `_negControlFlipGet` toggle (flips case 4b's `get` expectation to `open`) → exactly
  `case4b` reds (`Actual get / Expected open` shape), all other cases still ran; toggled
  back → green again. Full `ctest --test-dir native/build-msvc -L unit --output-on-failure`
  gate: 69/70 green — the sole failure, `colosseum.manga_reading_room`, is the SAME
  pre-existing/foreign failure ("the focused book height must stay inside the mock safe
  continuum bounds") named in the Slice-1-era baseline, untouched by this slice.
  **Lanista layer: attempted, not completed this slice.** An isolated tagged session
  (`--drive`) was driven by hand through account onboarding (a first-boot gate the plan's
  own scenario sketch did not name — `accountWelcomeContinueLocal` must be clicked before
  ANY world content is reachable; a `modePill_Tankoban` click before that lands on a hidden
  pre-warmed world tree, the ledger's documented DFS-first name-collision trap, and produces
  a false-green "clicked" reply against nothing visible) into Tankoban Discover, and
  confirmed live: (a) `mangaDiscoverCard_1` (Monster, real db malId 1) materializes with
  the correct `mangaDiscoverCard_<malId>` name once scrolled near it — the identity/naming
  plumbing is correct end-to-end against real data; (b) the Discover wall's GridView (`id:
  wall` in `qml/DiscoverBrowser.qml`) has NO objectName, so `ui-scroll` has nothing
  reliable to target — wheel events sent at a delegate card's center repeatedly produced
  ZERO measurable movement across many attempts; (c) `ui-keypress` arrow-key navigation on
  the grid worked intermittently but non-monotonically (single presses sometimes moved
  many rows, sometimes zero, not reproducible run to run) and once left the window itself
  minimized mid-sequence. This is a bridge/UI-surface gap in the Discover wall's automation
  reach, not a defect in the masthead/identity code above (proven correct by the harness
  using the identical data shapes MalCatalog/TankobanCatalog would return in production).
  No `tests/lanista_scenarios/tankoban_catalogue_smoke.json` was committed this slice —
  authoring one with steps that did not reproduce reliably in-session would misrepresent it
  as a proven gate. Naming this here rather than papering over it; a follow-up slice should
  give the Discover wall a named Flickable (or an index-jump automation seam) before this
  scenario can be authored honestly.

## Tankoban catalogue-independence Slice 3 gate (2026-08-20) — the count-only shelf

- `qml/MangaSeries.qml` `_prepareTankoban()` — rewritten to seed `TankobanVolumes` straight
  from `TankobanCatalog.volumes(resolvedMalId)` (Slice 1's baked catalogue: numeric-ordered
  `{number,cover,name}` rows, numbers "1".."N" synthesized from the baked count, baked
  cover/name overlaid where the harvest has landed), mapped `name`→`title` for
  `MangaTankobanLogic::prepareSeries`'s row shape, chapters ALWAYS `[]`. The legacy MAL-keyed
  `Manga.volumes("", title, malId)` call Slice 2 kept (Comick/volume-db ladder) is REMOVED,
  and with it the now-dead `Connections{target:Manga; onVolumesResult}` handler — no
  provider data reaches the shelf on this path at all (purity law, spec §2.1). Gated on
  `page.hasShelf` (the Slice-2 catalogue-count truth), one-shot per `seriesId` via the
  existing `_tankobanPrepared` latch.
- `native/engine/MangaTankobanLogic.cpp` — **zero C++ change needed.** Verified by direct
  reading (not touched): `prepareSeries()`'s three-phase assembly already builds one
  canonical `VolumeRecord` per volume row regardless of chapters (`chapterIds` stays empty,
  `chapterMapComplete=false` with no range/no explicit tag), and the pre-existing "WHOLE
  SHELF with no chapters" case in `tests/manga_tankoban_logic_harness.cpp` (lines ~148-171,
  a 38-volume chapterless shelf) already pins exactly this contract. No new Qt Test case
  added — the plan's own instruction was to touch this harness "only if assembly rejects
  [chapterless records]"; it doesn't.
- `qml/MangaTankobanLibrary.qml` — the WC thumb-scrape machinery is fully REMOVED:
  `requestCovers`/`visibleRowsForCovers`/`visibleGridRows`/`_firstChapterIdIn`/
  `_thumbWanted`/`coverByVolume`/the `onThumbReady` Connections/the cover-prefetch Timer and
  every `chapters`-driven binding that fed it. The `CuratedVolumeCovers.js` XHR detour (dead
  code, Qt6 blocks file XHR) is deleted along with `qml/data/manga_volume_covers.json` —
  both were UNTRACKED in git (never committed), so removal is a plain file delete, no `git
  rm` needed. `coverFor(row)` ladder is now: catalogue cover (`row.cover`, baked) →
  `localPages()` first page when `effectiveState(row)=="ready"` (app-owned bytes) → `""`
  (the delegate's own `coverImage.status !== Ready` branch paints the NO COVER glass — no
  code change needed there, it was already conditional on load failure). Card range
  captions are removed (`chapterSpanFor`/`shelfRangeFor` deleted); the caption line now
  shows only the catalogue's own `title`/`name` field when non-empty (already what the
  markup did for a real title — the range line was the only thing cut). The prefetch-cursor
  API (`focusIndex`/`focusToken`/`focusAtNumber`/`focusAtIndex`/`jumpToNumber`) is KEPT,
  fully inert now (no `requestCovers` call anywhere in its path) — callers (Select-mode
  batch contract, programmatic/keyboard callers) are unaffected. NEW bridge scalars:
  invisible `Item objectName:"tankobanShelfState"` with `rowCount` (mirrors
  `volumeRows.length`) and `coveredCount` (rows whose `coverFor()` resolves non-empty).
  Volume cards are renamed `objectName: "tankobanVolumeCard_" + <number>` (was the bare
  shared stem `"volumeTile"` — naming-law violation, fixed as part of this rename; Slice 4's
  own automation depends on the new stem per the plan).
  `coverFetchingEnabled` is left declared (vestigial, unread anywhere) rather than removed —
  a stale external binding on it must not hard-error.
- `qml/MangaReadingRoom.qml` — the embedded shelf (`MangaReadingRoom` → its own `tankLib`
  instance) needed NO separate catalogue wiring: it reads the SAME `TankobanVolumes`
  service the page-level shelf does (`service: page.tankobanVolumesRef`), so once
  `_prepareTankoban()` seeds the service once per series, both shelf instances see the
  identical canonical rows. One new automation name: `tankobanReadingRoomBack` on the
  room's `BackAction` (previously unnamed) — needed for the Lanista scenario to leave a
  series page and prove reopen/second-series regressions; `BackAction.qml` itself carries no
  default objectName.
- `qml/TrendingTop10.qml` + `qml/TankobanMangaTab.qml` — NEW opt-in `namePrefix` property on
  the shared `TrendingTop10` rail (default `""`, no name — Theatre/Biblio/Demo reuse it
  unaffected); `TankobanMangaTab.qml`'s "Top in Tankoban — Manga" row opts in with
  `namePrefix: "tankobanTopMangaTile_"`, naming each tile `tankobanTopMangaTile_<index>`.
  Added specifically to give the Lanista layer a scroll-free click path into a real series
  page, since the Discover wall's GridView remains the Slice-2-documented un-scrollable gap
  — see the Lanista ledger entry below for what this route actually proved and where it
  stalled.
- Focused tests:
  - Qt Test: not applicable (see MangaTankobanLogic.cpp note above — no C++ contract
    changed).
  - Qt Quick Test: not applicable — no `tests/qml/tst_*.qml` exists for this surface.
  - Existing harnesses, both updated:
    - `tests/manga_reading_room_harness.qml` (`colosseum.manga_reading_room`, registered):
      rewritten against the REAL current `MangaTankobanLibrary` contract — ground-truthed
      live that the harness's own `activeTab`/`selectTab`/`bookHeight` assertions (its
      documented pre-existing red, "the focused book height must stay inside the mock safe
      continuum bounds") tested properties/functions that **do not exist** on the shipped
      component (confirmed by grep: zero matches for `activeTab`/`selectTab`/`bookHeight` in
      `MangaTankobanLibrary.qml`) — stale from an earlier shelf design, predating the
      2026-08-14 GridView bookshelf rebuild, never reached by any later assertion because
      the harness threw at the first bad one and every check after it in the file had
      therefore never actually run. Rewrote against the real API (GridView-based,
      `volumeRows`/`effectiveState`/`chipTextFor`/`liveCaptionFor`/`selecting`/
      `selectedNumbers`/`downloadSelected`/`batchRequested`/`focusIndex`/`focusToken`/
      `focusAtIndex`/`flowCurrentIndex`), fixtures now catalogue-shaped (`number`/`cover`/
      `title`, no `chapterStart`/`chapterEnd` — a baked catalogue row carries no chapter
      range). Cases: state-word/tile-state contract (unchanged shape, kept); Select-mode
      batch contract (unchanged, kept, `selectTab` call dropped — no tab concept anymore);
      chapter-only series still shows its full chapter run in the footer tail
      (`chapterRows.length===42`, still real — `MangaReadingRoom.chapterDisplayRows`
      returns the flat `chapters` prop directly when `showVolumes` is false); fractional/
      named volume token contract (unchanged, kept). NEW: `downloads.asked.length === 0`
      ("the shelf must never call fetchThumb"), the cover ladder
      (`lib.coverFor(row)` for a catalogue-covered row / a ready-with-local-page row / a
      bare row → exact URL / exact URL / `""`), `tankobanShelfState.rowCount===115` +
      `coveredCount===2` (exactly the two seeded rows: one catalogue-covered, one
      ready-with-local-page), and two positive-absence proofs
      (`typeof lib.shelfRangeFor === "undefined"` etc., `typeof lib.requestCovers ===
      "undefined"` etc. — the removed functions are gone, not just unused). The
      pre-existing red **FLIPPED GREEN** under this rewrite (confirmed in the full `-L unit`
      run below) — recorded as required by the plan.
    - `tests/manga_tankoban_service_harness.cpp` (integration, fake nyaa, unregistered —
      run directly): ADDED one new isolated sub-scenario proving chapterless catalogue
      seeding end-to-end — `prepareSeries()` called with catalogue-shaped rows (`number`/
      `cover`/`title` only, chapters always `[]`, exactly TankobanCatalog's synthesized/
      overlaid shape) — `volumesForSeries` returns every canonical row, then the ordinary
      search→downloadNyaa→ingest→ready path (fake Nyaa candidate, real
      `MangaVolumeArchiveIngestor` over the real `tests/fixtures/tankoban/tiny-volume.cbz`
      fixture) converges to `ready` with 3 extracted pages — proving the WHOLE façade,
      not just the pure-logic assembly the existing chapterless case already covered.
      Every pre-existing case in this harness (search/download/ingest/ready, restart-replay,
      batch, short-pack honesty) is UNCHANGED and still green.
  - Negative controls, both performed LIVE against real source (QML has no compile step,
    so these are true source-level reds, not just in-harness inverted assertions):
    (a) temporarily reintroduced a `Downloads.fetchThumb(...)` call in
    `MangaTankobanLibrary.qml`'s `Component.onCompleted` → exactly
    `MANGA_READING_ROOM_FAIL: the shelf must never call fetchThumb` reds, restored, reran
    green. (b) temporarily forced `coverFor()` to always return `""` → exactly
    `MANGA_READING_ROOM_FAIL: a card with a baked catalogue cover must show it` reds,
    restored, reran green. (c) C++: temporarily flipped the new chapterless-seeding case's
    expected row count (2→3) → rebuilt → exactly `FAIL: chapterless catalogue seeding still
    returns every canonical volume` reds, restored, rebuilt, reran green
    (`MANGA_TANKOBAN_SERVICE_OK`). All six red+restore logs preserved under
    `artifacts/tankoban-independence/slice3/`.
  - Full `ctest --test-dir native/build-msvc -L unit --output-on-failure` gate: **70/70
    green** — up from the 69/70 baseline this slice inherited (`colosseum.manga_series_catalogue`
    also reconfirmed green, unaffected by the `_prepareTankoban()` rewrite: its fake
    `FakeTankobanCatalog`/`FakeVolumesService` seams have no `volumes()`/`prepareSeries()`
    methods, but the call path is gated behind the SAME `typeof TankobanVolumes ===
    "undefined"` guard the pre-existing code already used — true in that bare harness, so
    `_prepareTankoban()` no-ops there exactly as it always silently did). Full log:
    `artifacts/tankoban-independence/slice3/ctest_unit_run1.log`.

## Tankoban catalogue-independence Slice 4 gate (2026-08-20) — acquisition purity + series search

- `qml/MangaTankobanSourcesPage.qml` — the pinned-last WeebCentral "Build from chapters"
  row is REMOVED from this page's visual tree (the `Item{visible: row.isWeeb}` delegate
  block, `weebBadge`/`weebBtn`/`tankobanSourceBuildFromChapters`), along with `pickWeeb()`
  (the service kick to `compileWeebCentral`), the `pickedWeeb` state field, and `isWeeb`.
  `applySources()` now filters `results` to `kind !== "weebcentral"` BEFORE assigning
  `rows` — the one enforcement point for "nyaa-only", independent of what the native
  façade still emits (it does still emit a weebcentral card; see the native note below —
  "unplug, not delete"). NEW named scalar `hasCompileFallback: false` (a literal constant,
  the positive-assertable proof the ledger's missing-absence-assertion gap calls for).
  NEW `show()` branch: `context.seriesMode === true` calls
  `TankobanVolumes.searchSeriesSources(context.volumeId, context.seriesTitle)` instead of
  `searchSources(volumeId)` — `context.volumeId` doubles as the opaque series-mode result
  key so the existing stale-handle guards in `applySources`/`applyFailure` need no new
  branch. NEW `objectName: "tankobanSourcesBack"` on the picker's `BackAction` (previously
  unnamed) — the Lanista dismiss target. Empty-state copy ("No releases matched this
  volume yet." / "Searching Nyaa releases…" / the failure line) is UNCHANGED — it already
  states the empty case plainly, per the plan's "do not invent new design."
- `native/torrent/MangaNyaaSource.{h,cpp}` — `filterAndRank()` gains a `bool seriesMode =
  false` trailing parameter (default preserves every existing call byte-identical): when
  true, the `coverageIncludesTarget()` check is skipped entirely — every other rejection
  (chapter-pack, raw/untranslated, weak series-match, blocked uploader, hash-less, dedup)
  and the tier/standalone/digital sort stay unchanged. NEW `searchSeries(series)` public
  method + a shared private `startSearch(vid, series, targetVolume, seriesMode)` both
  `search()` and `searchSeries()` now call — `search()` computes
  `vid=volumeId(seriesId,targetVolume)` as before; `searchSeries()` uses `series.seriesId`
  verbatim as `vid` (the caller — the façade — already hands in its own opaque key, so no
  double-prefixing). `queryVariants`/`parseRss` are UNCHANGED, reused as-is (an empty
  target volume naturally falls to the bare-title query family inside `queryVariants`,
  producing "SeriesTitle" and "SeriesTitle Vol" query variants — harmless, filtered same
  as any other query by the trust/rejection ladder).
- `native/engine/MangaTankobanService.{h,cpp}` — `IMangaNyaaSearch` gains a pure virtual
  `searchSeries(series)`; `MangaNyaaSearchAdapter` forwards it to
  `MangaNyaaSource::searchSeries`. NEW `Q_INVOKABLE void searchSeriesSources(QString key,
  QString seriesTitle)`: builds a minimal `SeriesSnapshot{seriesId: key, title:
  seriesTitle}` (no aliases — the shelf-less page carries none) and calls
  `m_search->searchSeries(snap)`. Deliberately does NOT touch `onSourcesFound`/
  `weebCardFor` — the native façade STILL appends a weebcentral-kind card to every
  `sourcesReady` payload (per-volume AND series-mode alike); this is "unplug, not
  delete" for the whole WeebCentral-compile organ (`MangaVolumePacker`,
  `compileWeebCentral` stay in-tree, reachable only from a future explicit route, never
  from today's QML) — purity enforcement lives solely in the QML layer's
  `applySources()` filter (see above). A series-mode search key is never promoted into
  `m_series`/`m_volumes` — proven by a dedicated harness case (below).
- Focused tests:
  - Qt Test (pure-logic, unregistered — run directly): extended
    `tests/manga_tankoban_logic_harness.cpp` with a new series-mode block reusing the
    SAME 7-item `nyaa_volume_results.xml` fixture the volume-mode contract pins. Proves
    series mode keeps the two volume-mode survivors (exact volume-2, the 1-12 pack) AND
    now ALSO keeps item 4 (Volume 03 — rejected in volume mode as "wrong target",
    surviving here because series mode has no target to miss) — three candidates, not
    two. Proves every OTHER rejection is unchanged: chapter-pack item 3 still absent,
    blocked-uploader item 5 still absent, raw item 6 still absent, duplicate-infohash
    item 7 still deduped to one survivor. Proves trust-tier ranking is unchanged (the
    tier-1 uploader's exact-volume-2 release still ranks first). `manga_tankoban_logic_harness.exe`
    → `MANGA_TANKOBAN_LOGIC_OK`.
  - Existing harnesses, both re-run + one extended:
    - `manga_tankoban_logic_harness.exe` — see above; green.
    - `tests/manga_tankoban_service_harness.cpp` (integration, fake nyaa, unregistered):
      the `FakeNyaaSearch` test double gained a `searchSeries()` override (echoes
      `series.seriesId` back verbatim as the result key, same as the real
      `MangaNyaaSource`). TWO new sub-scenarios added to the existing chapterless-seeding
      block: (a) "no compile path is ever offered for a chapterless volume" —
      `dService.searchSources(d1)`'s `sourcesReady` payload asserted directly: the
      trailing weebcentral card is present (native still emits it, unchanged) but
      `enabled==false` and `chapterCount==0` — the honest disabled state Slice 3 already
      proved for a chapterless volume, now asserted explicitly per the plan's ask; (b)
      "searchSeriesSources fires sourcesReady under the caller's own key, with no prior
      prepareSeries" — `dService.searchSeriesSources("series:s5", "Never-Prepared
      Series")` delivers a `sourcesReady("series:s5", …)` payload with zero setup, and
      `volumesForSeries("series:s5")` stays empty (the series-mode key is never promoted
      into the canonical volume model). `manga_tankoban_service_harness.exe` →
      `MANGA_TANKOBAN_SERVICE_OK`.
  - A THIRD target needed a one-line fix, not a design change: `tst_local_downloads_failure`
    (`colosseum.qttest.local_downloads_failure`, registered) lists only
    `MangaTankobanService.h` as a MOC source (not the `.cpp`) and instead links
    `tests/auto/downloads/local_downloads_test_stubs.cpp`'s hand-written stub body for
    every `MangaTankobanService` method it needs — a NEW `Q_INVOKABLE` method needs a
    matching stub line or the target's moc-generated `qt_static_metacall` carries an
    unresolved external at link time (confirmed empirically: reverting the native changes
    via `git stash` made this target link clean again; restoring them reproduced the
    exact `LNK2019` on `searchSeriesSources`). Added
    `void MangaTankobanService::searchSeriesSources(QString, QString) {}` alongside the
    other 13 stub lines — same pattern, no new machinery. Not a foreign/pre-existing gap:
    a straightforward, expected consequence of growing this class's invokable surface,
    now closed for future additions too (the pattern is self-evident from the file).
  - Negative control performed live: `manga_tankoban_logic_harness.cpp`'s new series-mode
    trust-tier assertion (`ranked[0].tier == 1`) flipped to `== 99` → rebuilt → exactly
    `FAIL: series mode still ranks the tier-1 uploader's release first` red, nothing else
    touched → restored → rebuilt → `MANGA_TANKOBAN_LOGIC_OK` again.
  - Full `ctest --test-dir native/build-msvc -L unit --output-on-failure` gate: **70/70
    green**, zero regressions, zero foreign reds (unchanged from the Slice 3 baseline this
    slice inherited).
- QML-side completion: `qml/MangaSeries.qml` gained `_openSeriesSearch()` (opens the
  picker with `seriesMode: true`, `volumeId: "series:" + (seriesId.length ? seriesId :
  seriesTitle)`) and `readPrimary()` now routes the `!hasShelf` case there instead of
  falling through to the (purity-emptied, dead since Slice 2) chapter fallback. Slice 2's
  own promised three-way button text ("Open volume 1"/"Get volume 1"/"Search nyaa") was
  left as a TODO — `MangaReadingRoom.qml`'s `continueText` only ever said "Open volume 1"
  regardless of whether volume 1 was actually ready (ground-truthed live: the button read
  "Open volume 1" on a NOT-yet-downloaded shelf). Closed this slice: `MangaReadingRoom`
  gained a `primaryAction` property (bound from `page.primaryAction`), and `continueText`
  now branches on it — confirmed live in the Lanista session below (One Piece's button
  reads "Get volume 1", not "Open volume 1", once threaded through). `tankobanVolumeCard_1`
  click routing ("get" opens the picker for volume 1 via the pre-existing `chooseSource`)
  needed no change — Slice 3 already wired it correctly; ground-truthed, not assumed.

## Tankoban catalogue-independence Slice 6 gate (AMENDED, 2026-08-20) — Discover-as-browse depth

- **Amendment context.** The original Slice 6 named a dedicated `MangaCatalogPage.qml`/
  `MalCatalog::topManga` "wall" that a prior executor proved never existed (Plan
  contradicted). Hemanth's ruling: Discover IS the browse surface for the 10k catalogue
  (Option A); a dedicated wall page is deferred. This amended slice is verification-first:
  ground-truth `MalCatalog::discoverPage`'s deep-offset paging, prove a deep-rank Discover
  card click still lands the Slice-2 masthead, prove a series absent from
  `tankoban_catalog.db`'s 10k band renders the honest shelf-less page. No new UI/C++ unless
  a paging defect was proven — none was; every change this slice is test-fixture-only.
- `tests/mal_catalog_discover_harness.cpp` — extended with a "deep offset paging" block
  proving `MalCatalog::discoverPage("popular", ...)` pages correctly well past offset 2000,
  not just the pre-existing offset-0/3 cases. A NEW 2600-row "deep-fill" fixture block
  (`mal_id` 5001-7600, `members` 20000-i for i=1..2600, `start_date` "2000-01-01" —
  deliberately OLDER than every other fixture row so it never intrudes into the existing
  "new-releases" assertions) sits strictly between the named rows (4000-600000 members) and
  the pre-existing 120-row filler block (101-220 members) in popular order, giving the new
  cases a closed-form expected mal_id/members at any offset instead of a guessed value.
  Total fixture size 2728 rows (8 named + 2600 deep-fill + 120 old-filler). New cases: (1)
  a page at offset 2400 (well past 2000) returns exactly the predicted 5 rows
  (mal_id 7396-7400), strictly members-descending — proves both row IDENTITY and the ORDER
  BY clause itself hold that deep, not just a row count; (2) the `[1,100]` limit clamp is
  still honored at a deep offset (`limit=99999` at `offset=2400` still returns exactly 100,
  `exhausted=false`); (3) `exhausted` flips true ONLY at the fixture's true end (offset
  2723, 5 rows short of 2728, returns exactly 5 + `exhausted=true` + `nextOffset==2728`),
  stays false one page earlier with 10+ rows still behind it, and an offset past the true
  end returns empty + `exhausted=true` with no crash. Negative control performed live: the
  deep-page case's expected `mal_id` deliberately offset by +1 → rebuilt → exactly
  `FAIL: deep page row carries the exact expected mal_id (ORDER BY holds past offset 2000)`
  reds → restored → rebuilt → green (`MAL_CATALOG_DISCOVER_OK`). Run directly (not
  CTest-registered, same as before): `native/build-msvc/mal_catalog_discover_harness.exe`;
  already wired into `tests/test_tankoban_discover.ps1` (child 7) unchanged. Full
  `ctest --test-dir native/build-msvc -L unit --output-on-failure` gate: **70/70 green**
  (unchanged from the Slice 3/4 baseline — this slice touches no CTest-registered target).
- **Runtime layer: partially Runtime-validated, honestly split.** See the Lanista ledger's
  own Slice 6 entry for the full account. Summary: a genuinely new, working scroll
  technique was found live (page-level scroll on a stable always-visible item to clear a
  featured-banner dead zone, then handing off to a materialized delegate's own objectName)
  that reaches rank 1-18 (3 materialized rows) reliably and reproducibly, both by hand and
  scripted — a real improvement over the Slice-2-documented "ZERO measurable movement." The
  deep-rank card CLICK (materialized delegate → masthead) was proven live by hand (Naruto,
  malId 11, session 20260820-202607-0ade0c74) but did NOT reproduce in 2/2 scripted
  attempts even with settle reads and a belt-and-braces re-click — a script-only automation
  gap, not a masthead/identity defect (the identical click mechanism Slice 4 already proved
  Runtime-validated via the static "Top in Tankoban" rail). The committed
  `tests/lanista_scenarios/tankoban_discover_depth.json` therefore stops at the
  materialization proof (18/18 green, 3 scripted runs) rather than ship an unreliable click
  step as if it were a proven gate. The plan's own pinned deep-rank series (Hal, malId
  49611, live rank ~3000) was not reached this slice; a bridge-addressable load-more/
  index-jump seam on the wall (or the click-resolution gap's root cause) is owed to a
  future slice or Slice 7's eyes-on list.

## Tankoban catalogue-independence Slice 5 gate (2026-08-20) — the surgical unplug

- **Scope.** QML: deleted the entire `legacyCorridor` `Component` in `qml/MangaSeries.qml`
  (~490 lines) — GROUND-TRUTHED dead code, never instantiated by any `Loader`/`createObject`
  (the only two hits for its id are its own declaration and an untracked thumbnail-mock
  copy). It held the "Latest chapters" tail, per-chapter Get rows, chapter open/download
  routing, and a duplicate/dead `MangaTankobanLibrary` instance. Also removed:
  `chaptersModel` (was always `[]` — Slice 2 already dropped the `Manga.chapters()` call
  that used to fill it) and everything derived from it (`volGroups`, `visibleChapters`,
  `_openChapter`/`_downloadChapter`, the factRows "Chapters" row, the dead-anyway
  `readPrimary()` chapter fallback, the now-unused `_tankobanPreparedWithChapters` latch,
  the `MangaVolumes.js`/`QtQuick.Controls` imports that only the deleted code used).
  `qml/MangaReadingRoom.qml`: removed `chapters`/`chapterDisplayRows`/the two chapter
  signals, the metaRow "N chapters" stat text, the chapter-count wiring into its live
  `MangaTankobanLibrary` instance, and the dead "Read first chapter" continueText fallback
  (unreachable — collapses to "Open volume 1"). `qml/MangaTankobanLibrary.qml`: removed
  `chapters`/`chapterRows` properties, the two chapter signals, and the GridView `footer`
  Component that rendered the LIVE "Latest chapters" tail (this one WAS reachable at
  runtime, but always rendered zero rows in production since Slice 2 removed the only
  thing that ever fed `chapters` non-empty). Grep-verified zero `Manga.search/chapters/
  detail/art/volumes` or `Downloads.fetchThumb/downloadChapter/deleteChapter/
  cancelDownload` call sites remain anywhere in `qml/*.qml` (excluding untracked
  `*ThumbnailMock.qml` scratch files) after these edits — `native/MangaEngine.h`'s
  Q_INVOKABLEs and `MangaDownloader`'s chapter API compile untouched (organs kept, per
  "unplug not delete") but are provably unreached from QML. One deliberately UNTOUCHED
  live read: `qml/TankobanWorld.qml`'s `nextUpRows()` still calls
  `Downloads.downloadedChapters()` for its "Next Up" manga branch — not in the plan's named
  file list, and it degrades honestly to empty once the migration purges `kind:"manga"`
  progress and no route can create new ones (no fabricated data, just an increasingly-dead
  branch); named here rather than silently left.
- **GROUND-TRUTH DEVIATION — the two named JS/QML files are unlanded WIP, not committed
  source.** The plan named `qml/TankobanLibraryApi.js` + `qml/TankobanLibraryTab.qml` for
  the chapter-progress-join removal. `git log` on both is empty — they have never been
  committed, sit on disk since 2026-08-06, and chat.md's 2026-08-08 vault-planning entry
  already named this exact trio (`TankobanLibraryTab.qml` + `TankobanWorld.qml` +
  `WorldTabBar.qml`) as "the Tankoban Library lane's uncommitted WIP" with unresolved
  ownership. `TankobanLibraryApi.js::buildRows()`'s manga branch was edited (the chapter-
  lane join removed, volume-lane-only now, `mangaProgress` param kept for call-site
  compat) so the join is clean whenever that WIP lands, but the edit is NOT staged/
  committed under this slice — landing someone else's whole unlanded Library tab under a
  chapter-removal commit message is not this slice's call to make. Same reasoning applied
  to `tests/test_tankoban_library.ps1` (also untracked, also part of that bundle,
  unregistered in CTest) — inspected, not deep-edited or committed.
- **GROUND-TRUTH DEVIATION — main.cpp's ProgressStore construction moved since the plan
  was written.** The plan assumed a boot-time `ProgressStore` singleton reachable right
  after `AppLog::install()`. Ground-truthing `native/main.cpp` (itself currently DIRTY —
  mid-adoption of a "Bundle 8C" account/profile runtime, `native/account/
  ProfileStoreRuntime.cpp` whose OWN file header reads "PRE-FLIGHT DRAFT STATUS:
  uncompiled/untested/unexecuted/unadopted/unverified") found no local `ProgressStore*` at
  all that early — `AccountRuntime`/`ProfileStoreRuntime` is now the sole constructor, and
  it only exists after `accountRuntime->prepareForQml(&engine)` (~line 1537, well after
  `AppLog::install()`). The migration call was moved to right after that line, using
  `accountRuntime->profileStores()->progressStore()`. Recorded honestly, not chased
  further this slice: this purges whichever store is bound at that instant (the "sealed"
  pre-onboarding-choice store, per that runtime's own design) — whether a later "continue
  local" rebind swaps in a different store instance, and what that does to an
  already-written migration marker, was not traced (outside this slice's fence; the
  account/profile system is itself unverified/in-flight, not this slice's to fix).
- **C++ organs.** NEW `native/engine/TankobanChapterMigration.{h,cpp}` — `static Result
  run(appDataRoot, ProgressStore*)`: deletes `<appDataRoot>/manga/` (`QDir::
  removeRecursively()`, only after recording `chapterDirsDeleted`/`indexDeleted` for the
  log line), purges `kind:"manga"` records via a NEW `ProgressStore::purgeKind(kind)`
  (additive Q_INVOKABLE, mirrors `forget()`'s scheduleSave/syncDirty/bump shape but no
  group semantics — a whole-kind wipe), and writes a plain marker file
  (`<appDataRoot>/tankoban-chapter-migration.v1.done`) ONLY after a successful disk purge
  (a failed `removeRecursively()` — e.g. a locked file — withholds the marker so the next
  boot retries instead of silently abandoning the tree). Deliberately does NOT use a
  hardcoded `QSettings("Brotherhood","Colosseum")` for its own marker (the ProgressStore.h
  store-isolation trap) — the marker is a plain file under `QStandardPaths::
  AppDataLocation`, which already follows the active `applicationName` (tag or real) the
  same way every other AppData-backed store here does; no tag-aware QSettings helper
  needed. `manga-volumes/` and `kind:"tankoban"`/`"comic"` records are never touched by
  any code path in this class.
- **Qt Test: NEW `colosseum.qttest.tankoban_chapter_migration`** (labels `unit;qttest`,
  registered `tests/CMakeLists.txt`, same shape as `tst_tankoban_catalog`). 6 cases, all
  QTemporaryDir-fixtured (both the disk tree and a real `ProgressStore` over a temp ini —
  never a real AppData root, never the registry): disk purge deletes the chapter tree and
  leaves a seeded `manga-volumes/` archive untouched; progress purge removes the one
  seeded `kind:"manga"` record and leaves seeded `kind:"tankoban"`/`"comic"` records
  intact; the marker lands only after success; a second run is a TRUE no-op (reseeds a
  fresh chapter dir AND a fresh manga-kind record AFTER the first run's marker exists,
  asserts the second run touches NEITHER — stronger than "returns early"); a missing
  `manga/` dir (fresh install) still purges progress and writes the marker cleanly; a null
  `ProgressStore*` still purges disk with zero crash and zero progress side effect.
  **Negative control performed live**: sabotaged `purgeMangaProgress()` to also call
  `purgeKind("tankoban")` → exactly the three tankoban-survival-dependent cases (
  `progress_purge_removes_manga_keeps_tankoban_and_comic`,
  `idempotent_second_run_is_noop`,
  `missing_manga_dir_still_purges_progress_and_writes_marker`) went red with the expected
  `progressRecordsPurged` mismatch, the other three (disk-only/marker/null-store cases)
  stayed green → restored → rebuilt → green again (`ctest -R
  colosseum.qttest.tankoban_chapter_migration`, ran clean both before and after).
- **Existing harnesses.** `tests/manga_reading_room_harness.qml` (registered
  `colosseum.manga_reading_room`) — was RED after the QML edits (`MangaReadingRoom does
  not have a property called chapters`, the harness's old "chapter-only room" case setting
  `chapters: chapters(42)`). Replaced that case with a shelf-less-series case asserting
  `library.showVolumes === false`, `library.volumeRows.length === 0`, and — the stronger
  proof, matching this file's existing "assert fully removed, not just unused" idiom
  (lines 188-192 already do this for the WC thumb-scrape machinery) — `typeof` on every
  removed chapter property/signal on BOTH the room and its library returns `"undefined"`.
  Removed the now-dead `chapters(count)` fixture helper. Rebuilt green
  (`ctest -R colosseum.manga_reading_room`). The TB-002/TB-003 grep-assertion runner
  (`tests/test_tankoban_library.ps1`, located via `grep -l TankobanLibraryApi tests/*.ps1`
  per the plan's instruction) is untracked/unregistered — see the deviation note above;
  not deep-edited or committed this slice.
- **Full `-L unit` gate: 71/71 green** (baseline was 70/70 before this slice's one new Qt
  Test target). Two PRE-EXISTING/FOREIGN failures were observed and are NOT this slice's:
  `colosseum.qttest.profile_activity_isolation` (2 sub-cases,
  `accountSwitchRebindsAndDestroysPreviousActivityStore` +
  `noStaleCrossProfileActivityLeakage`) — belongs entirely to the in-flight, self-described
  "unverified" account/profile system this slice only had to read around, zero overlap
  with chapters/progress/migration code. Named explicitly, not swept under "known noise."
- **App target compiles clean.** `cmake --build build-msvc --target colosseum` — all 35/35
  changed objects compiled (including `main.cpp` and the new migration files); the FINAL
  LINK step failed with `LNK1104: cannot open file 'colosseum.exe'` because Hemanth's
  daily `colosseum.exe` (PID 9296) was running the whole session — per the plan's own
  standing constraint, never killed. This is a lock, not a code defect: every translation
  unit this slice touched or added built without error.
- **Runtime layer: Bridge blocked (exe lock), not run this slice.** The planned Lanista
  regression replay of both committed scenarios (`tankoban_catalogue_smoke.json`,
  `tankoban_discover_depth.json`), the NEW seeded-fixture `.ps1` disk gate
  (`tests/test_tankoban_chapter_migration.ps1`), and the human-witnessed eyes-on all
  require a colosseum.exe that actually contains this slice's C++ (the migration class,
  `ProgressStore::purgeKind`, the main.cpp hook) — which cannot link while the daily
  instance holds the file. None of these were written or run as unverified/speculative
  gates; they are honestly deferred to the next pass, once Hemanth closes the daily app.
  Safety note: the QML edits (chapter UI removed) may already be visible in Hemanth's
  CURRENTLY RUNNING instance if it loads QML live from the source tree and he opens a
  manga series page — this is cosmetic and reversible (no C++, no data, no migration logic
  has reached his running process, since that requires the blocked rebuild+relaunch); his
  real chapter downloads and progress are untouched and remain so until he deliberately
  rebuilds and relaunches, per the plan's own completion criterion ("only after this
  criterion may the migrated build run as the daily app").

## House assertion idioms (no framework)

- **require idiom:** `require()` prints `FAIL: <msg>`, `exit(1)`; one `*_OK` on success.
  Chosen over `Q_ASSERT` because Q_ASSERT compiles out under NDEBUG.
- **CHECK-collecting idiom** (comicreader family): collect every failure, print each, emit
  `<NAME>_OK` iff zero — the pattern Qt Test migration must preserve (one failure must not
  hide the rest; today three harnesses still `qFatal` and DO hide the rest:
  `download_file_ops`, `window_shell_gui`, `window_state_policy`).
- **QML idiom:** hand-rolled checks + `Timer` + `Qt.exit(0|1)` + stdout sentinel. None of
  the 88 imports QtTest (one exception below).

## Registered CTest entries (slice 2 — existing harnesses, unconverted)

| CTest name | Labels | Notes |
|---|---|---|
| `colosseum.window_state_policy_harness` | unit, windows | qFatal idiom — first failure hides the rest (slice-3 conversion pilot) |
| `colosseum.search_history_store_harness` | unit | |
| `colosseum.progress_store_harness` | unit | |
| `colosseum.collection_store_harness` | unit | |
| `colosseum.cbz_archive_harness` | unit | |
| `colosseum.poster_scoreboard_harness` | unit | |
| `colosseum.comicreader_cache_harness` | unit | |
| `colosseum.biblio_catalog_logic_harness` | unit | fixture dir baked at compile time |
| `colosseum.update_version_harness` | unit | strict three-component version parsing, canonical release-tag/display formatting, and comparison ordering; no network or filesystem writes |
| `colosseum.update_manifest_trust_harness` | unit | RFC 8032 Ed25519 verification (valid, mutated, short-key, and short-signature cases) plus strict signed-manifest schema rejection; production public key only, no private key material in the repository |
| `colosseum.update_release_client_harness` | unit | loopback GitHub Releases API fixture: stable-release filtering, ETag/304, exact asset/digest matching, signature-before-parse, bounded metadata, redirect/error/timeout handling, and cancellation/destruction safety |
| `colosseum.update_download_harness` | unit | 2 MiB loopback installer stream: bounded cache paths/metadata, cancel-and-resume Range/If-Range, ignored-range and changed-ETag restarts, truncation/length/hash rejection, space/path preflight, atomic promotion, and root-scoped superseded cleanup |
| `colosseum.update_service_harness` | unit | injected-clock/release/downloader/launcher lifecycle: six-hour policy and manual bypass, chronicle-preserving failures, unseen/seen state, pause/resume/verify/ready/install transitions, failed-target suppression, minimum-updater manual path, signed offline restart, verified-artwork fallback, and the two Lanista cache seeds with mutated-signature rejection |
| `colosseum.update_install_bridge_harness` | unit, windows | installed-layout eligibility (source/dev/registry mismatch suppression), verified-cache installer launch arguments, detached restart contract, success/rollback parsing, and exact sibling backup cleanup with unsafe-path refusal |
| `colosseum.comic_downloader_pack_demux_harness` | unit | pack-demux Slice 1: volume label parser table (the 12 real Chew filenames) + index Entry round-trip of `packRole`/`packOrder`; legacy rows load unchanged. **Slice 2 added:** the demux happy-path scenario — a 3-volume pack (2 CBZ + 1 CBR nested in a top-folder ZIP) ingests into 3 readable child volumes sharing seriesId, the parent retires via `removed()` (no `failed()`), the pack archive + extractTmp are reclaimed after all children index, the manifest clears. **Slice 3 added:** crash-resume (hand-authored active manifest + preserved pack + subset of children indexed → construct re-enqueues the missing children, completes, reclaims), staged-reuse offline completion (a pack at the canonical `dl_<hash>.archive` path completes via `downloadIssue()` with an unreachable postUrl — no network touch), cancel-mid-pack (cancel before the deferred resume fires → no children enqueue, pack file kept on disk, sticky inactive manifest). **Slice 4 added:** the ordered-volumes read API `packVolumes(seriesId)` — hand-authored index of 2 mains (v1, v2), 1 extra (v1-Bonus, em-dash label), and 1 ordinary issue sharing the seriesId → `packVolumes()` returns `{mains:[v1,v2], extras:[v1-Bonus]}` sorted by packOrder ASCENDING (v1 first, natural reading order), the ordinary issue excluded from both lists, rows shape-identical to `downloadedIssues()` (shared delegate contract for Slice 5), and a no-pack-rows seriesId returns two empty lists. Negative control: a flipped descending comparator makes the two ordering assertions fail RED, then restored to GREEN. 108 checks total. **Slice 5 (paint-only QML, 2026-08-07):** the shelf + reader wiring consumes this C++ API — `routeDownloadItem` routes pack-role rows to a new `openPackSeries()` (Main.qml) that injects `packVolumes()` rows into `ComicSeries.qml` as a baked release list with an explicit `packSeriesId` identity (df003eb identity-ordering law preserved). The page renders VOLUMES + EXTRAS sections (by packRole) and feeds the reader a mains-only DESCENDING chapters array (crossing convention: v8→v1); extras open solo (single-entry chapters). No new harness checks (paint-only — the ordering contract is Slice 4's 108-check gate); full app builds clean, `unit` gate stays 10/10. **Arc closed Runtime-validated 2026-08-07:** Slice 6's human-witnessed live Chew journey completed — the failed pair self-healed via boot resume on the fixed build, all 12 volumes landed under Hemanth's eyes ("done, it worked"), pack + extract tree reclaimed, manifest cleared (read-only disk checks recorded in chat.md). **Slice 7 added (2026-08-07):** the accent scenario — the live Chew "file stat failed" failure mode: CbzArchive fed miniz ANSI (`QFile::encodeName`) while the vendored miniz on MSVC decodes paths as UTF-8 (`mz_utf8z_to_widechar` → wide CRT APIs), so any non-ASCII path component mangled; fixed by `nativePath()` → `.toUtf8()`. Accent-named CBZ (probe fast path) + CBR (extract-repack path) via direct `ingestLocalArchive`, both land readable; the fixture carries U+00B4 in the archive FILE name only (Qt-written) because Windows bsdtar's ZIP writer transliterates ´→' in entry names (proven — a zip-entry accent fixture is silently vacuous), self-guarded by `accent-fixture-name-faithful`. RED recorded pre-fix (both ingests fail, 3 named reds); negative control (flipped landed-count → exactly one red, restored). 117 checks total. Mirrors `comic_downloader_ingest_harness` isolation (dedicated org/app, QTemporaryDir, path mirrors) |
| `colosseum.http_header_channel_harness` | unit | Theatre House HTTP Source slice 1 WIRE proof: drives a REAL MpvItem at a loopback QTcpServer and records the request headers mpv/ffmpeg actually send. Asserts the addon Referer reaches the wire (+ the forced VLC user-agent, proving production config), a comma value stays ONE header (node-array, not comma-joined), `ytdl` is off (else ytdl_hook clobbers our headers), and `loadFile` CLEARS `http-header-fields` so the next plain load carries no leftover header (leak guard + plain-path-no-header negative control in one). Loopback only, no live network; event-driven waits, no sleeps. Compiles `mpvitem.cpp` + links the app's MpvQt/libmpv. Green 2026-08-07; **negative control performed live** — removing the clear in `loadFile` turned it red (`leak: /plain.bin carried a leftover X-Thing`), restored to green |
| `colosseum.vault_admission_probe_harness` | unit | The Vault's video admission gate (execution plan Slice 6): drives `MediaAdmissionProbe`'s OWN headless libmpv handle (vo=null, muted, software decode) against ffmpeg-generated fixtures and admits ONLY on a decoded frame (`dwidth>0`), never FILE_LOADED. Asserts a valid 64×64 MP4 admits; plain non-video bytes and a truncated MP4 reject (demux/decode error); an audio-only file rejects (FILE_LOADs but has no video track — the exact FILE_LOADED vacuity). Bounded: a no-video-track file rejects AT FILE_LOADED via a track-EXISTS check (not `vid=no`), never waits the timeout. Links libmpv directly (not MpvQt); `libmpv-2.dll` sits beside the exe; `QCoreApplication` only (headless). Green 2026-08-08; **negative control performed live** — `COLOSSEUM_ADMISSION_MODE=fileloaded` weakens the gate to FILE_LOADED → audio-only wrongly admits + tiny.mp4 admits with dwidth=0 (2 checks red); restored by unsetting the env |
| `colosseum.vault_launch_router_harness` | unit | The Vault's launch router (execution plan Slice 7): `LocalLaunch` classifies by extension then backend-validates — comics via `CbzArchive` (CBR accepted by extension; no in-place CBR reader in Colosseum yet), video via the decoded-frame admission probe, books by extension (Reader 2 authoritative at open) — routing BEFORE any session and rejecting with a category (corrupt / no-decoder / unsupported / not-found). Plus `VaultPageStore`, the `ComicReaderShell` injected-store adapter returning `[{index,archive,entry,group}]` descriptors in natural order (mirrors `MangaVolumeIndex`; zero reader edits). Drives real fixtures — valid CBZ/MP4/epub accept; corrupt/non-video/png/missing reject; the page store lists `tiny-volume.cbz` in order — against real libmpv. House sentinel/exit-code; headless. Green 2026-08-08; negative control performed live (`validateComic` weakened to accept-all → the corrupt-CBZ reject + no-vault-id checks red; restored). App wiring (main.cpp registration, Main.qml `vault:` branch, PlayerPage local-subtitle gate, real-`ComicReaderShell` Qt Quick Test) deferred to Slices 8/10/14 |
| `colosseum.selftest.red_canary` | selftest | WILL_FAIL negative control |

## Auto-update verification (Task 11, 2026-08-08)

The committed updater gates were rerun with the pinned Qt/CMake tools. `native\build-msvc.bat`
completed with `BUILD_OK`; the updater-specific CTest harnesses (version, trust, release client,
download, service, and install bridge) passed, as did the `colosseum.qml` Quick Test target.

| Gate | Command | Result |
|---|---|---|
| Full build | `cmd /c native\build-msvc.bat` | `BUILD_OK` |
| Updater CTest subset | `C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -R "colosseum.update_" --output-on-failure` | green (6 updater harnesses) |
| QML gate | `C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -R colosseum.qml --output-on-failure` | 1/1 passed |
| Taskbar contract | `tests\test_update_taskbar_p0.ps1` | `PASS` |
| Version/data boundary | `tests\test_update_data_boundary.ps1` | `UPDATE_DATA_BOUNDARY_OK` |
| Installer matrix | `tests\installer\update_matrix.ps1` | `UPDATE_MATRIX_OK` |
| Release tooling | `python tests\update_release_tooling_test.py` | 3 tests passed |
| Lanista runtime | `tests\test_update_lanista.ps1` | Available 12/12 and UpToDate 13/13; production cache restored `COLOSSEUM_UPDATE_TESTING=OFF` |

The aggregate `-L unit` run was 24/25 green in this worktree; the sole failure is the unrelated
dirty-worktree `colosseum.vault_launch_router_harness` fixture lane. It is outside the updater
plan and was not changed. The updater subset above is the authoritative Task 11 unit result.

Lanista evidence is preserved under `artifacts/lanista-sessions/` and the exact final paths are
listed in `artifacts/update-lanista-session-paths.txt`.

The six-test updater-inclusive subset ran green under `ctest` on 2026-08-08. The broader `-L unit`
run is reported above with the unrelated dirty-worktree fixture failure called out explicitly.
Registration ≠ conversion: these still speak the
house sentinel/exit-code contract; CTest consumes the exit code.

**Surfaced, not registered by us:** the top-level `include(CTest)` made the Player 2
lab's 17 `add_test` entries visible from the top build dir for the first time (15 pass;
2 fail environmentally in this build dir — `player2_state_machine_test` exits 0xc0000135
= a DLL missing beside the test exe, `player2_seek_generation_test` fails — Player lane's
to triage; excluded from the fast gate by having no `unit` label).

## Registered Qt Test targets

| CTest name | Source | Proves | Status |
|---|---|---|---|
| `colosseum.qttest.window_state_policy` | `tests/auto/window/tst_window_state_policy.cpp` | WindowStatePolicy geometry contracts (4 as named data rows) + WindowModeStore settings round-trip, isolated INI in QTemporaryDir, GUILESS | 11/11 green 2026-08-06; negative control performed (one deliberate break → exactly one named red, all other cases still ran); labels `unit;windows;qttest` |
| `colosseum.qttest.http_header_fields` | `tests/auto/player/tst_http_header_fields.cpp` | `httpHeaderFieldsList()` — the map→mpv `http-header-fields` formatter: comma/colon safety + the third-party-JSON injection guards (CRLF / colon-in-key / whitespace-in-key / empty-key dropped). Pure, APPLESS, no mpv. Theatre House HTTP Source slice 1 | green 2026-08-07; data rows are self-falsifying (flip any expected → one named red); labels `unit;qttest` |
| `colosseum.qttest.vault_kit` | `tests/auto/vault/tst_vault_kit.cpp` (compiles `native/engine/VaultKit.cpp`) | VaultKit pure logic — the Vault's shared brain (execution plan Slice 1): census classifier (per-subtree kind inference, mixed-leaf flag + honest leftover line, loose-file capture, user `scanIgnore` needle exclusion), the ported `cleanMediaFolderTitle`, the SxxExx grammar + anchored season-climb guard (bare `Season N` never masquerades as a show), and the walker's depth cap + pre-cancel. GUILESS, pure Qt6::Core (no app deps); fixture tree baked via `VAULT_FIXTURES_DIR` at `tests/fixtures/vault/` (structural stubs incl. a U+00B4 accent filename; real-byte corrupt-CBZ/decodable-MP4 deferred to Slices 5–6). Ported from Tankoban 2 ScannerUtils/BulkPackVerifier/VideosPage | 27/27 green 2026-08-08; negative control performed (two flipped expectations → exactly two named reds, `kind_for_file(epub_is_book)` + `title_cleaner(plain_unchanged)`, all other cases still ran; restored). **Slice 8 added (2026-08-13, browse-face execution plan):** the episode wall's fact-line composition — `S<season>:E<episode> · <quality>` when a season is known (SxxExx), else the honest `Episode <n> · <quality>` for absolute numbering (no season known, e.g. Gintama's `- 003` files); quality parsed via the new shared `VaultKit::qualityLineFromFileName` (deduped out of `VaultBrowseDetail`'s own private copy — both call sites now share one implementation, `vault_browse_detail` stayed 7/7 green unchanged). Updated the Gintama absolute-numbering case (`Episode 3` → `Episode 3 · 1080p`) and added a case proving the S:E shape against The Wire's real Season 4 files (`S4:E1 · 1080p BluRay`) | 44/44 green 2026-08-13; negative control performed live (Wire honesty-fact expectation flipped `Season 4 only` → `5 seasons` → exactly one named red, `browse_collapse_nested_season_folder_reports_honest_presence`, all 43 other cases still ran; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_stores` | `tests/auto/vault/tst_vault_stores.cpp` (compiles `native/engine/VaultConfig.cpp` + `VaultIdentity.cpp`; shared `VaultStoreIo.h`) | The Vault's two stores (execution plan Slice 2): **VaultConfig** user-intent round-trip (roots + confirmed flag, per-subtree kind overrides, scanIgnore, hidden), atomic-write recovery from the last-known-good `.bak` (corrupt primary → restore; both corrupt → clean fresh), and path normalization (slash + Windows case); **VaultIdentity** content-addressed id (`vault:`+SHA-1 of `normPath::size::mtimeMs`) stability + normalization, the unique-(size,mtimeMs)-signature rename/move re-attachment (progress follows via alias, persisted), and the two-candidate ambiguity guard (parked, never silently merged — the copy ceremony is Slice 21). Pure Qt6::Core, GUILESS; QTemporaryDir isolation, no net | 11/11 green 2026-08-08; negative control performed (ambiguity expectation flipped to assert migration → exactly one named red, `reconcile_two_candidate_ambiguity_does_not_migrate`; restored). **Slice 9 added:** VaultRecent — the Open Media recent-files store: move-to-front record with dedup by normalized path + kMax cap + reload round-trip + live availability (`QFileInfo::exists`), and clear-wipes-shortcuts-not-progress (a sibling progress file survives `clear()`). Negative control performed live (`recent_clear_wipes_shortcuts_not_progress` flipped to expect the progress file GONE → one named red; restored). labels `unit;qttest` |
| `colosseum.qttest.vault_index` | `tests/auto/vault/tst_vault_index.cpp` (compiles `native/engine/VaultIndex.cpp`) | The Vault's rebuildable scan product (execution plan Slice 3): a SQLite index with transactional full-replace `publish()` (a cancelled or errored publish rolls back — previous contents intact, decision 4), incremental `upsert()` for live-shelf arrivals (Slice 15), and numeric-aware folder-order listing via a zero-padded sort-key column (SQLite default collation is lexicographic, not natural). Query surface: `itemCount` / `itemCountForKind` / `kinds` / `groupsForKind` / `filesInSubtree`. Qt6::Sql; the qsqlite driver resolves from `build-msvc/` beside the app-deployed plugin (ledger deploy note); QTemporaryDir DB per run, no committed `.sqlite`. GUILESS | 7/7 green 2026-08-08; negative control performed (dropped the sort-key numeric padding in production → the two ordering cases red, `folder_listing_is_natural_order` + `natural_sort_key_is_numeric_and_case_insensitive`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_scanner` | `tests/auto/vault/tst_vault_scanner.cpp` (compiles VaultScanner + VaultIndex + VaultIdentity + VaultKit) | The Vault's cancellable off-thread census (execution plan Slice 4): pure `buildScan()` over the fixture tree (kind-pure slice model + dominant-kind index rows, season-nested subfolders, leftovers excluded), the generation-guarded `applyResult()` (a stale result is dropped; a cancelled census never publishes — a real bug the test caught: a cancelled walk returns empty, and without the explicit cancelled flag applyResult would publish empty and WIPE the index), and — async — `scanRoot()` + the buffered-rescan-runs-after guarantee. QtConcurrent pool + QFutureWatcher marshalling; thread-affine index/identity work stays on the GUI thread. Qt6::Concurrent/Sql; GUILESS; QTemporaryDir index+identity per run. App registration + boot check deferred to Slice 10 (no consumer yet). | 9/9 green 2026-08-08; negative control performed (generation guard disabled → the stale-drop case red, `generation_guard_drops_stale_result`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_enricher` | `tests/auto/vault/tst_vault_enricher.cpp` (compiles VaultEnricher + VaultIndex + CbzArchive + miniz) | The Vault's fact-filler (execution plan Slice 5), deliberately THIN — reuses Colosseum's existing facilities instead of re-porting TB2 ArchiveReader: comic page count + cover-entry pick via `CbzArchive` (the cover is served on demand by the existing `image://comiccover/` provider — no new decoder, no thumbnail cache), a corrupt-archive honest error state (never a wedge/hang), the triple-keyed video duration cache (hit / miss-on-triple-change / persist), and `enrich()` writing facts back to the index. Added a `coverRef` column to VaultIndex. Deferred (gradient-fallback until their slices): epub cover ladder + author, video thumbnails, page dimensions (the Vault UI shows none of the last); the live ffprobe path is exercised at Slice 6. Pure Qt6::Core/Sql, GUILESS; real CBZ (`tiny-volume.cbz`) + corrupt-CBZ fixtures + QTemporaryDir | 5/5 green 2026-08-08; negative control performed (corrupt-CBZ flipped to expect success → one named red, `corrupt_cbz_is_error_not_wedge`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_browse_detail` | `tests/auto/vault/tst_vault_browse_detail.cpp` (compiles `VaultBrowseDetail.cpp` + `VaultKit.cpp` + `VaultIndex.cpp`) | The Browse face's detail sheet projection, `VaultBrowseDetail::detailFor()` (browse-face execution plan Slice 7) — pulled out of VaultLibrary the same way VaultBrowseAway was (Slice 6), so it drives a real VaultIndex without the full façade's watcher/scanner/identifier tree. Copies you hold (same canonical identity across roots via new `VaultIndex::rowsForIdentity`, else the single physical group); companions/extras via `VaultKit::describeFilmFolder`'s filesystem walk (subtitle exts, `.nfo`, conventional cover names, a `Subs/` folder → one companion label; `Extras`/`Featurettes` → listed, playable, never gridded; anything else → counted as ignored junk, never surfaced). Real `browse-film/` fixture (the Spider-Man shape) + hand-authored FileRows for the identity-join cases. GUILESS, pure Qt6::Core/Sql | 7/7 green 2026-08-13; negative control performed live (the Spider-Man fixture's `ignoredCount` expectation flipped 2→3 → exactly one named red, `spiderManFixtureOneCopyTwoCompanionsTwoJunk`, `Actual 2 / Expected 3`, all 5 other cases still ran; restored). A real bug found via the Lanista replay (not by this suite's original fixtures, which only ever seeded 1 row per group): VaultScanner's grouping puts a film's own Extras/Featurettes files in the SAME group as the film, so an unfiltered `rowsForIdentity()`/`rowsForGroup()` read counted them as extra "copies" — fixed by folding rows under an Extras/Featurettes `subfolder` out of the copies join (`VaultKit::isExtrasDirName`, now exposed); the regression case `groupedExtrasRowsNeverCountAsCopies` reproduces the real 3-rows-one-group shape directly. labels `unit;qttest` |
| `colosseum.qttest.vault_browse_away` | `tests/auto/vault/tst_vault_browse_away.cpp` (compiles `VaultBrowseAway.cpp` + `VaultIndex.cpp` + `VaultConfig.cpp`) | The Browse projection's away-propagation contract (browse-face execution plan Slice 6): which confirmed root owns a browse path, whether that root is currently away (a ROOT-WIDE index fact), and `offlineBrowseAt()` — the durable-index fallback for a level whose owning root can no longer be walked at all (design §4.7 “nothing disappears”): tiles the index remembers stay present, marked away, when the live filesystem walk cannot reach them, and honestly returns empty when the root was never scanned at all. Pulled out of VaultLibrary so it drives a real VaultIndex/VaultConfig without the full façade's watcher/scanner/identifier tree. GUILESS, pure Qt6::Core/Sql; QTemporaryDir index + real small fixture trees per run | Ledger-drift fix (Slice 9, 2026-08-13): this target has been registered and green since Slice 6 but was missing from this table until now (“if code and ledger disagree, the code wins”) — 4/4 green; labels `unit;qttest` |
| `colosseum.qttest.vault_browse_empty` | `tests/auto/vault/tst_vault_browse_empty.cpp` (compiles `VaultBrowseEmpty.cpp`) | The grid's empty-cause classification (browse-face execution plan Slice 9): `VaultBrowseEmpty::classify` — no-roots vs empty-folder vs all-away vs “none” (rows present), design §4.5's four distinct causes minus the deferred “filtered”, which the enum never produces since no filter control has shipped; and `isLevelAway`, the away-detection combinator VaultLibrary::browseEmptyCause() needs on top of `VaultBrowseAway::ownerRootAway` (a root that was NEVER scanned while present has no index row to carry the away flag at all, so the row-based check alone cannot see it — found live driving the all-away-empty Lanista fixture). Pure logic, no VaultIndex/VaultConfig needed. GUILESS, pure Qt6::Core | 13/13 green 2026-08-13 (6 `classify` data rows + `causeName` + 5 `isLevelAway` data rows + 1); two negative controls performed live (`classify`'s all-away row flipped → one named red; `isLevelAway`'s never-scanned-and-gone row flipped → one named red), both restored; labels `unit;qttest` |

| `colosseum.qttest.local_downloads_failure` | `tests/auto/downloads/tst_local_downloads_failure.cpp` (compiles `native/engine/LocalDownloads.cpp` with test-only backend stubs) | Failed Tankoban volume rows recover the human title from the already-failed active job, and an empty-title `tankoban:<id>:volume:N` fallback renders `Vol. N` without leaking routing ids. The fake owner exposes `activeVolumeJobs()` and drives the real `failed(id, reason)` retention signal. | **2/2 green 2026-08-11; full `-L unit` gate 32/32 green**; negative controls proved the old failed-state filter breaks human-title recovery and raw-id fallback breaks `Vol. N`; labels `unit;qttest` |
| `colosseum.qttest.journey_play_video` | `tests/auto/player/tst_journey_play_video.cpp` (compiles `native/player/mpvitem.cpp`) | The production player's decoded-frame readiness seam (Agent Visibility Phase 2, J1-Video-Seam): new read-only `MpvItem::decodedWidth`/`decodedHeight` mirror mpv's own `dwidth`/`dheight` — the same two `MediaAdmissionProbe.cpp:59-60` observes — over the same real fixtures the Vault admission gate uses. Proves a routed+shown player carries no decoded frame yet (`route_is_not_ready`); an audio-only source reaches mpv's `fileLoaded` but its dims never leave zero, closing the FILE_LOADED vacuity (`audio_only_never_ready`); the real fixture reports its EXACT dims not merely `>0` (`decoded_fixture_reports_exact_dimensions` — caught a live `dwidth`/`dheight` race latching `64x0`, so it waits for both); and `currentUrl` matches the loaded path (`source_identity_matches`). Real `MpvItem` in a shown `QQuickWindow`, event-driven waits, no sleeps. | 6/6 green (4 cases + init/cleanup) 2026-08-13, verified on the merged tree at Agent 0's gate; negative control performed (drop the decoded-dims clause → exactly `audio_only_never_ready` red, restored); labels `unit;qttest`. **Qt Quick Test (`tests/qml/tst_journey_play_video.qml`) SKIPs honestly** — `PlayerPage.qml` imports the native `Colosseum.Player` module registered only in the real app bootstrap (same standing limit `parity_load_harness.qml` already excludes it for); closing it needs a `tests/qml/quicktest_main.cpp` registration, outside this slice's fence. |
| `colosseum.qttest.layout_verdict` | `tests/auto/lanista/tst_layout_verdict.cpp` (compiles ONLY the test TU — header-only unit under test, `native/tools/LanistaLayoutVerdict.h`, no app/engine sources) | Agent Visibility Phase 2, Slice L2: the pure, header-only `LanistaLayoutVerdict` evaluator behind the runner-local `layout_verdict` scenario step in `native/tools/lanista.cpp` — `actionableNonzero` (nonzero size + visible + enabled), `contained` (target-within-viewport with logical-px tolerance, inclusive boundary), `noPeerOverlap` (explicit named peers only, never a global sweep), and the single-generation guarantee (a `LayoutSnapshot` merge from a second `dump-ui` page reporting a DIFFERENT `generation` is rejected outright, its rows never entering the snapshot). Fed synthetic dump-ui-shaped JSON only — no bridge, no app, no QGuiApplication (`QTEST_APPLESS_MAIN`), same deploy pattern as `tst_http_header_fields.cpp` (land beside the app's Qt DLLs, stage `Qt6Test.dll`, or 0xc0000135 before main()). No `native/CMakeLists.txt` edit. | 11/11 green 2026-08-13 (the plan's 10 named cases — `actionable_zero_size_fails`, `hidden_actionable_fails`, `disabled_actionable_fails`, `contained_inside_passes`, `contained_outside_tolerance_fails`, `touching_edges_do_not_overlap`, `named_peer_overlap_fails`, `unnamed_peers_are_not_global_rules`, `one_generation_is_required`, `logical_units_ignore_device_pixel_ratio` — plus `duplicate_names_resolve_dfs_first`, added beyond the plan after a real bug was found and fixed live, see the Lanista ledger's own L2 entry). Two negative controls performed live against the production evaluator: (a) the `actionableNonzero` zero-size guard weakened `<= 0.0` → `< 0.0` → exactly `actionable_zero_size_fails` red, all 9 others green, restored; (b) the `contained` boundary comparison weakened inclusive `<=` → strict `<` on the right/bottom edges → exactly `contained_outside_tolerance_fails` and `logical_units_ignore_device_pixel_ratio` red (both genuinely boundary-exact fixtures), all others green, restored. Full `-L unit` gate 39/39 green after landing; labels `unit;qttest` |
| `colosseum.qttest.window_set_state` | `tests/auto/lanista/tst_window_set_state.cpp` (compiles `native/devtools/LanistaServer.cpp` + the full VaultForensics/VaultLibrary link closure, same reasoning as `tst_vault_forensics`, plus `native/player/mpvitem.cpp` — `VaultThumbnailer.cpp` calls `MpvItem::findFfmpeg()`, a static path lookup no case here ever invokes, but the symbol still needs MpvQt::MpvQt linked) | Agent Visibility Phase 2, Slice J1-Tray-Bridge: the Drive-gated `window-set-state` bridge command — real `QWindow::showNormal()`/`showMinimized()`/`hide()` on the first root window only, plus `get-state`'s new per-window `state` field. Drives the REAL `LanistaServer::dispatch()` path (including its central Drive gate) over a real `QLocalSocket`, against inline `Window{}` QML fixtures (no scene file). `read_gate_refuses_window_set_state` (no `COLOSSEUM_LANISTA_DRIVE` → `DRIVE_DISABLED`), `drive_gate_accepts_normal`/`_minimized`/`_hidden` (real transition, reply AND live `QWindow::visibility()` both checked), `bad_state_is_rejected` (an unrecognized state fails BEFORE touching the window — the window's `visibility()` reads back byte-identical to before the refused call), `only_first_root_is_addressed` (two root `Window`s — only the first moves, the second is proven UNCHANGED, not merely unasserted). | 8/8 green 2026-08-14 (6 named cases + init/cleanup), built and run in an isolated worktree (`.worktrees/tray-bridge`, main checkout's `build-msvc` is the locked daily-driver PID 22956). Negative control performed live: reclassified `window-set-state` as Read-gated in the worktree source (never committed) → exactly `read_gate_refuses_window_set_state` red (7 passed/1 failed), all 5 others stayed green → restored → 8/8 green again. **Isolated-session runtime replay against the assembled app** (unique pipe, tagged AppData, windowed): minimized → normal → hidden → normal, each confirmed via `get-state`'s own `state` field; one ground-truthed nuance (a `state=normal` request observed back as `"fullscreen"` once — this app's own default chrome re-asserting borderless fullscreen presentation, not a bridge defect; see the Lanista ledger's "Window state" section). Daily app PID `22956` verified running throughout, never touched. labels `unit;windows;qttest` |
| `colosseum.qttest.store_isolation` | `tests/auto/store_isolation/tst_store_isolation.cpp` (compiles the test TU plus `native/ProgressStore.h` + `native/CollectionStore.h` directly — header-only stores, no separate .cpp, AUTOMOC handles their in-header Q_OBJECT classes the same way `tst_vault_stores` already does for `VaultConfig.h`/`VaultIdentity.h`) | CRITICAL isolation fix (2026-08-14): `ProgressStore`/`CollectionStore`/`SearchHistoryStore` hardcoded `QSettings("Brotherhood", "Colosseum")`, which resolves straight to the Windows registry regardless of `COLOSSEUM_APPDATA_TAG` — a tagged/isolated Lanista test session read AND wrote the REAL user's Continue map, Collection shelf, and search history (proven live: a test journey wrote a manga entry into the real registry). Proves, without ever touching the real registry: (a) untagged, the tag-gate free functions (`progressStoreTaggedIniPath`/`collectionStoreTaggedIniPath`) return empty — the exact condition selecting the UNCHANGED registry constructor; (b) tagged, the same functions resolve a private path under `AppDataLocation`, and a real `ProgressStore`/`CollectionStore` constructed under that tag persists there end-to-end (read back and parsed). `QStandardPaths::setTestModeEnabled(true)` sandboxes every tagged file into Qt's own disposable `qttest` temp root. | 6/6 green 2026-08-14 (`gate_returns_empty_when_untagged`, `gate_returns_tagged_path_when_set`, `tagged_progress_store_writes_isolated_file`, `tagged_collection_store_writes_isolated_file` + init/cleanup), built and run in an isolated worktree (`.worktrees/fix-isolation`, main checkout's `build-msvc` is the locked daily-driver PID). Negative control performed live against the production gate: `progressStoreTaggedIniPath` forced to always return empty (simulating the pre-fix bug) → exactly `gate_returns_tagged_path_when_set` and `tagged_progress_store_writes_isolated_file` red (2 failed/4 passed), `tagged_collection_store_writes_isolated_file` correctly stayed green (CollectionStore's own gate was untouched by the sabotage); reverted → 6/6 green again. Regression check: the pre-existing `colosseum.search_history_store_harness` / `colosseum.progress_store_harness` / `colosseum.collection_store_harness` (iniPath-constructor legacy harnesses) all still pass unchanged — the fix only touches the DEFAULT (registry-vs-tag) constructor path. **Runtime proof beyond ctest:** a real tagged session (`COLOSSEUM_APPDATA_TAG`, unique pipe, worktree-built `colosseum.exe`, PID separate from and never touching the daily driver) booted straight into the Tankoban world showed NEITHER "Next Up" nor "Continue Reading" rows (both `ContinueRow`s collapse to zero height when `Progress.recent()` is empty) — the tagged session's own `progress-store.ini` never even got created (nothing recorded yet) and its `collection-store.ini` held only an internal `_meta/backfill_v3` marker, never real user data; the real registry was never opened by any of this. labels `unit;qttest` |
| `colosseum.qttest.tankoban_catalog` | `tests/auto/tankoban/tst_tankoban_catalog.cpp` (compiles `native/engine/TankobanCatalog.cpp`) | Catalogue-independence Slice 1 (2026-08-20): `TankobanCatalog`, the baked, provider-free volume-count/cover store read via `data/tankoban_catalog.db` (built by `scripts/manga_brain/build_tankoban_catalog.py`, MalCatalog's doctrine — dormant when absent). Proves missing-db honest emptiness (`ready()==false`, empty accessors); series `volumeCount`/`countBasis` round-trip; `volumes()` synthesizing "1".."N" from a known count with no baked rows; numeric-aware overlay ordering ("2" before "10" — a harvest-order fixture, TEXT-lexicographic would fail this); baked cover/name overlay attaching onto exactly one synthesized row, others staying bare; unknown malId returning empty on an otherwise-`ready()` db; and `count_basis` passthrough ("mal" vs "bookwalker"). Qt6::Sql; same deploy-trap resolution as `tst_vault_index` (exe lands in `build-msvc/` beside the app-deployed qsqlite plugin). GUILESS; QTemporaryDir DB per run, no committed `.sqlite`. | 9/9 green 2026-08-20 (7 named cases + init/cleanup). Negative control performed live: `count_round_trip`'s expected `volumeCount` flipped 41→42 → exactly `count_round_trip` red (`Actual 41 / Expected 42`), all 6 other cases still ran; restored → 9/9 green again. Fixed a genuine test-fixture bug found in the process: the file's own `insertVolume()` helper bound a default-constructed `QString()` (Qt's SQLite driver treats `isNull()==true` as SQL `NULL`) into the `cover_url`/`name` columns — `NOT NULL DEFAULT ''` rejects an explicit `NULL` (the `DEFAULT` only applies when a column is omitted from the `INSERT`, not when `NULL` is bound), so `numeric_order_two_before_ten`'s bare-cover inserts failed outright; coerced null→empty before binding, production `TankobanCatalog.{h,cpp}` untouched. Builder run: `python scripts/manga_brain/build_tankoban_catalog.py` against the arc's `spine_top10000.jsonl` baked 10,000 series rows / 0 volume rows (SQL-verified) to `data/tankoban_catalog.db` — counts-only; the BookWalker harvest at `bookwalker_volumes.jsonl` (2,474 series / 28,985 rows) was NOT passed via `--covers` because its shape mismatches the builder's expected input: the harvest uses `cover_url` (snake_case) where `load_covers()` reads `coverUrl` (camelCase, always empty match), and carries no `volumeCount` key anywhere (each row's `countBasis: "bookwalker"` is a per-row echo, not a series-level count fact the builder's override logic can consume) — running it as-is would have silently inserted 28,985 volume rows with real `name` ("Volume N") but permanently empty `cover_url`, never a loud failure. Reported verbatim per plan instruction, schema not redesigned. `-L unit` full gate 68/69 green (baseline was 66/68) — the sole failure, `colosseum.manga_reading_room`, is the same pre-existing/foreign failure named in the pre-slice baseline, untouched by this slice. Labels `unit;qttest` |

Parity: the legacy `window_state_policy_harness` covers the identical contracts and
stays built + registered until a parity review retires it (migration policy). The
conversion's evidence gain, demonstrated live: the legacy `qFatal` idiom reports ONLY the
first failure; the Qt Test reports every case independently.

**Qt Test build facts (slice 3):** `Qt6::Test` is discovered in `tests/CMakeLists.txt`
under `BUILD_TESTING` only — never linked into the app. Two deploy traps solved there,
both verified live as 0xc0000135-before-main: Qt Test exes must land in `build-msvc/`
beside the app-deployed Qt DLLs (`RUNTIME_OUTPUT_DIRECTORY`), and `Qt6Test.dll` itself
is staged by a POST_BUILD copy (no app deploy step ever shipped it).

## Registered Qt Quick Test targets (slice 4–5, 2026-08-06)

**One runner, one CTest entry, six test files, 44 cases (2026-08-09):**
`colosseum.qml` runs the repo-built `colosseum_qml_tests` (QUICK_TEST_MAIN_WITH_SETUP —
the setup object supplies a TEST application identity + INI settings in a per-run temp
dir, because production `Settings` blocks fail to initialize without one; verified live)
with `-input` pointed at the SOURCE `tests/qml/`, so file-relative production imports
resolve against the real tree. Labels `qml;windows` — these open REAL windows; never an
offscreen gate. Qt6::QuickTest discovered only under BUILD_TESTING; `Qt6QuickTest.dll`
staged by POST_BUILD beside the exe (same 0xc0000135 disease as Qt6Test.dll).

| File | Proves | Notes |
|---|---|---|
| `tst_comicreader_title_controls.qml` | REAL mouse hit-testing against production `ComicReaderHud` | pre-existing; legacy `qmltestrunner` gate still works |
| `tst_search_history_flow.qml` | search-history flow against production QML | pre-existing. **KNOWN FLAKE:** `test_biblioRecentChipBodyAndRemoveHaveIndependentClickTargets` fails ~1 run in 3 (real-window focus/timing); pre-dates the runner — reconciliation owed by its owner, not silently rerun-until-green |
| `tst_comicreader_resume_race.qml` | the four resume-race regressions (T1 mount-time page-1 cannot overwrite a restore · T2 manualActivity disarms · T3 give-up clears both arms · T4 goMinimize flushes synchronously before emitting once) as independent cases: tryVerify on the debounced write, SignalSpy, createTemporaryObject; T3 injects a `seriesRecords` record (`layout: long_strip`) because the fraction arms only at long_strip OPEN — the legacy harness got that from ambient runner prefs. Negative control performed (one flipped expectation → exactly one named red). Converted from `comicreader_resume_race_harness.qml`, which stays with its gate until parity review | slice-5 pilot |
| `tst_open_media_control.qml` | the taskbar Open Media… control's signal seam (Vault execution Slice 8): drives the PRODUCTION `Taskbar` (Sessions guarded to undefined so the switcher shows no tiles) and proves the control renders ONLY while the dock is open (`visible: bar.open`) and a real `mouseClick` emits `openMediaClicked` exactly once. The native OS file dialog is NOT opened in-test — the signal is the unit; the dialog + routing + reader/player render are the Lanista replay (`vault_launch_smoke`) + the human-witnessed items. Negative control performed live (flip the click expectation 1→2 → exactly one named red, `test_click_emits_open_request` `Actual 1 / Expected 2`, all other cases still ran; restored) | 2 cases green 2026-08-09; labels `qml;windows` |
| `tst_open_recent_panel.qml` | the Open Recent panel's model contract (Vault execution Slice 9): drives the PRODUCTION `OpenRecentPanel` (extracted from Main.qml so it is seedable) with a seeded model and proves a seeded model renders one row per entry (`openRecentRow_<n>`), clicking an AVAILABLE row emits `reopenRequested` carrying that entry, a DEAD (unavailable) row offers nothing, and `openRecentClear` emits `clearRequested` — after which an emptied model shows `rowCount == 0`. The reopen semantics (resume-at-page, restart-when-finished) are the Lanista replay + human-witnessed | 4 cases green 2026-08-09; labels `qml;windows` |
| `tst_vault_browse_page.qml` | the Browse face's page-level component/navigation contract (browse-face execution plan Slices 5–7), driving the PRODUCTION `FeaturedCarousel`/`VaultBrowseRail`/`VaultBrowseCrumb`/`VaultPosterCard`/`VaultWideCard`/`VaultDetailSheet` through a local replica of `VaultPage`'s own navigation state machine, fed by a seeded `browseAt()`/`browseDetail()` stub — the assembled page with the real `VaultLibrary` is the Lanista replay, not this layer. Slice 7 added 3 cases: a Film card click opens the detail sheet with the seeded copy rows/companion chips/evidence text rendered; Play emits the CONCRETE seeded file path (never merely non-empty) and closes the sheet; Escape dismisses without opening media. Two real bugs found and fixed getting these green: instantiating `VaultDetailSheet` with both an external `anchors.fill` and its own internal `anchors.fill: parent` collapsed it to 0×0 (fixed by not double-assigning); the harness's sheet needed an explicit `z` to win hit-testing over an equal-z sibling (matching production's existing `z: 45`). **Slice 8 added 2 cases:** a seeded 10-season band renders in the grid's OWN backing model in natural numeric order (Season 2 before Season 10 — the lexical-sort trap); a seeded 300-episode wall through production's own `cacheBuffer: 900` renders with `grid.contentItem.children.length < 300` (and `< 60`) — the virtualization assertion itself. **Slice 9 added 4 cases:** all four empty causes render their own exact copy (§4.5, no two share wording) with the “noRoots”-only Add-storage affordance; a visible focus ring shows ONLY on `grid.activeFocus` (asserted absent both idle and after a real `mouseClick`); arrow traversal (Right/Down/Left/Up) matches the visual/model order against a computed column count; `Enter` opens the keyboard-focused card and `Backspace` ascends, end to end | 17 cases green 2026-08-13 (8 from Slices 5–6 + 3 from Slice 7 + 2 from Slice 8 + 4 from Slice 9); labels `qml;windows` |
| `window_behavior_harness.qml` | (still top-level, still orphaned) | adoption candidate |

**Conversion learning worth keeping:** QML `Settings` writes are batched/deferred — a
test that writes a preference through one component instance and expects another
instance to read it immediately is racing the batch timer. Inject the record layer
instead; never "fix" it with a wait.

## Existing bespoke estate — classification

Full per-target build facts (sources, links, compile defs, CMake lines) live in the sweep
this ledger was built from; the classes and gates below are the planning surface.

### C++ harnesses (69) by class

- **Deterministic unit (48):** pure contracts over temp dirs, no net. Families: comics
  torrent/edition stack (~15), comicreader engine (8), biblio catalog (3), manga/tankoban
  logic (5), catalogs over SQLite-in-tempdir (5 — no committed .sqlite fixtures; DBs are
  built per-run), stores (progress/collection/search-history/model-manifest), net policy
  units (poster scoreboard, pin proxy factory), window-state policy, reader2 stores/bridge,
  anime order index, archive (cbz) pair, download file ops, hosted player bridge, knaben
  indexer, comick pair.
- **Integration with fakes at the boundary (7):** `manga_tankoban_service`,
  `comic_torrent_pack_transport`, `manga_volume_torrent`, `comic_torrents_search`
  (fake torrent/nyaa engines), `reader2_autoattach`, `anime_order_service` (local
  QTcpServer), `loopback_pin_proxy` (loopback sockets + hang failsafe).
- **Live network — NEVER in a deterministic gate (3):** `knaben_probe` (real Cloudflare
  verdict), `audiobook_engine_probe` (self-declared triage tool), `torrent_engine_download`
  (live DHT, watchdog, exit 2 = timeout).
- **Infrastructure, not tests (3):** `comic_torrent_seed` + `comic_torrent_pack_seed`
  (loopback seeders that serve for 5 minutes), `torrent_engine_link` (link-only smoke).
- **GUI/offscreen-sensitive (3):** `comicreader_core` + `comicreader_provider` (need
  `QT_QPA_PLATFORM=offscreen`), `window_shell_gui` (needs offscreen AND
  `QT_QPA_PLATFORM_PLUGIN_PATH` to the Qt install — the windeployqt `platforms/` beside
  the exe ships only `qwindows.dll`, and the failure is a SILENT `0xC0000409`).

### QML harnesses (88) by class

- **~70 deterministic component harnesses** (offscreen qml.exe, sentinel + exit code),
  importing production QML/JS by relative path from `tests/`.
- **Probes, not tests (~9):** the Cloudflare/image probes (`batcave_guard`, `comichub_img`,
  `rco_cf`), reality/perf probes (`catalogue_residency`, `theatre_shelf_reality`,
  `comicreader_fullscreen_timing`), genre/world-search probes.
- **Live network (2):** `abb_live_probe`, `hosted_player_webengine_smoke` (WebEngine +
  live VidKing).
- **Two giants:** `comicreader_shell_harness.qml` (187 KB, 18 Timers) and
  `comicreader_surfaces_harness.qml` (160 KB, 8 Timers) — hundreds of hand-rolled checks,
  no isolation between them; the highest-flake, highest-value migration surface after the
  named pilots.

### PowerShell runners (150) by class

- **88 pure static source-grep gates** — assert a string exists in a source file. They
  regress on rename, not behavior; zero coverage signal. The single largest population.
- **49 qml.exe component gates** (hardcoded Qt path), some hybrid grep+behavior by design
  ("the 'no guided' assertion is a grep here, the behavior is the harness").
- **14 gates that run compiled C++ harnesses** (the real native gates):
  `test_biblio_discover_explore`, `test_collection_p0`, `test_comic_torrent_pack_dltest`,
  `test_comic_torrent_sources_v2`, `test_comicreader_chrome`, `test_comics_catalog_db`,
  `test_lanista`, `test_manga_tankoban_native`, `test_native_deploy_runtime` (grep-only),
  `test_search_history_p0`, `test_tankoban_discover`, `test_theatre_search_p0`,
  `test_theatre_shelf_reality`, `capture_catalogue_perf` (perf capture, no verdict).
- **6 launch the real app**; **2 are destructive-by-design real-download gates** made safe
  by `COLOSSEUM_APPDATA_TAG` isolation (`test_comic_torrent_pack_dltest`,
  `test_manga_tankoban_native`).

## Test labels (proposed vocabulary — nothing carries labels yet)

`unit` · `qml` · `integration` · `network` (explicit live-net probes only) · `slow` ·
`legacy` (registered bespoke harness) · `lanista` · `windows` · `visual` · `probe`
(no verdict; never a gate) · `destructive` (real side effects; opt-in env-gated only).

## Fixture and isolation rules (as practiced today)

- **Compile-def fixture dirs:** `TANKOBAN_FIXTURES_DIR`, `BIBLIO_FIXTURES_DIR`,
  `COMICS_PACK_FIXTURES_DIR` bake `tests/fixtures/<domain>/` paths in at build time.
  Fixture inventory: tankoban (6 files incl. `tiny-volume.cbz`), biblio (3 JSON), comics
  pack (3 CBZ), anime order (argv-passed), locg (4 JSON), abb (2 HTML, node-consumed),
  comicreader pages (2 PNG), lanista golden (1 PNG).
- **Isolation:** 23+ harnesses use `QTemporaryDir`; settings-touchers use
  `QStandardPaths::setTestModeEnabled(true)` so the live AppData is never touched. SQL
  harnesses build their DBs in temp dirs per run — no committed .sqlite.
- **Env flags that gate danger:** `COLOSSEUM_*_DLTEST` + `COLOSSEUM_APPDATA_TAG` for the
  real-download gates; `COLOSSEUM_LANISTA_SELFTEST/_PIPE/_DRIVE/_WRITE` for the bridge;
  `QT_FORCE_STDERR_LOGGING=1` needed by ~30 runners (GUI-subsystem binaries are otherwise
  silent).

## Machine-readable output

**None.** No JUnit, no XML, no manifest anywhere in the active estate (Lanista's `suite`
verb emits junit.xml + report.md for scenarios — the only machine-readable reporter in
the repo). Everything else is stdout sentinel + exit code.

## Known gaps (the honest list)

1. **39 of 69 C++ harnesses are invoked by nothing** — they compile on every build (so
   they can't rot at compile level) but nothing runs them: the entire `reader2_*` family,
   the whole `net/` family, both window-mode harnesses, `progress_store`, most of the
   comics edition stack. Unrun tests protect nothing.
2. **19 QML harnesses are referenced by no runner**, including the 48 KB
   `reader2_logic_harness.qml`, the only QtTest-importing top-level file
   (`window_behavior_harness.qml`), and the complete 3-file calendar cluster — a whole
   feature's test surface with no gate.
3. **2 broken runners** point at QML files that don't exist:
   `test_comics_catalog_v1.ps1` → `comics_catalog_logic_harness.qml`;
   `test_reader2_readalong.ps1` → `reader2_readalong_harness.qml`.
4. **88 grep-gates** prove strings, not behavior.
5. **Hardcoded Qt path in ~49 runners** — one Qt bump breaks the whole QML gate estate in
   49 places.
6. **Player2 CTest is invisible** from the top-level build even when enabled
   (subdirectory `enable_testing()`).
7. **Misfiled artifacts:** captured probe logs stored under `tests/fixtures/` with no
   consumer; ~350 KB of stray run logs/CSVs checked into `tests/`.
8. **No selection, no per-case reporting:** the two giant comicreader QML harnesses run
   hundreds of checks as one all-or-nothing process.

## Migration candidates (register first; convert only for named benefit)

**Pilot CTest registration set** (deterministic, isolated, sentinel+exit-code, no net —
lowest-risk first registrations): `window_state_policy_harness`,
`search_history_store_harness`, `progress_store_harness`, `collection_store_harness`,
`cbz_archive_harness`, `poster_scoreboard_harness`, `comicreader_cache_harness`,
`biblio_catalog_logic_harness` (fixture-dir baked, still deterministic).

**Qt Test conversion pilot:** `tests/window_state_policy_harness.cpp` — deterministic,
already QTemporaryDir + isolated QSettings, naturally splits into test functions, and its
`qFatal` idiom currently hides every later case on first failure (a named evidence
benefit).

**Qt Quick Test pilots:** register the two existing `tst_*.qml` under a repo-built runner
(they run today only via external qmltestrunner); adopt the orphaned
`window_behavior_harness.qml`; then migrate
`tests/comicreader_resume_race_harness.qml` (timer-chained today; the resume-to-page-one
race is a real shipped regression worth permanent per-case protection).

**Leave bespoke (do not convert):** the probes, the seeders, the live-network triage
tools, the grep-gates (retire or fold into behavior gates over time, don't convert), the
giant comicreader shells until the pilot pattern is proven.

## Reader-state vocabulary (slice 6, 2026-08-06)

The reader's authoritative session state is readable as stable properties on ONE named
surface: `qml-get comicReaderShell` (production `MangaReader.qml` names its shell) —
`seriesId`, `curChapterId`, `currentPage`, `pageCount`, `mode`, `_stripRestorePending`.
This is the shared state vocabulary for Qt Quick Test assertions AND Lanista replays. A
versioned C++ snapshot object (`colosseum.reader-state.v1`) is deliberately DEFERRED
until a consumer needs more than these properties answer — demand-driven, not built on
spec.

## Three-layer minimize/restore regression (slice 7 — layer status, 2026-08-06)

- **Qt Test:** pass — `colosseum.qttest.window_state_policy` (geometry + window-mode
  persistence round-trip).
- **Qt Quick Test:** pass — `tst_comicreader_resume_race.qml` (restored-state
  consumption, stale-write prevention, synchronous minimize flush).
- **Lanista (real OS minimize → taskbar restore → same page):** **Bridge prerequisite
  landed, Slice J1-Tray-Bridge, 2026-08-14 — the journey itself is the next slice.** The
  Drive-gated `window-set-state` command (`colosseum.qttest.window_set_state`, 6/6 cases
  green; isolated-session runtime replay against the assembled app, PID of the daily app
  verified untouched) now lets an agent restore a minimized/hidden root window via the
  real `QWindow::showNormal()`/`showMinimized()`/`hide()` path — see the Lanista ledger's
  "Window state (tray/minimize)" section for the full command shape and one ground-truthed
  nuance (a `state=normal` request can observe back as `"fullscreen"`, this app's own
  default chrome, not `"normal"` — assert non-minimized/non-hidden, not the literal
  string). **What this does NOT yet prove:** the real Windows taskbar/tray icon and a
  full minimize-to-tray-and-back page-state-preserved journey remain Slice J1-Tray's own
  scope; until it lands, THAT specific claim stays human-witnessed only.

## Runtime boundary (unchanged by this arc)

Qt Test proves C++ contracts; Qt Quick Test proves QML component behavior (its window is
NOT the Windows shell — no taskbar/lifecycle claims); Lanista proves the assembled app in
isolated sessions per its own ledger; pixels are exhibits; aesthetic verdicts are
Hemanth's. A green suite here earns **Test-reported**, never **Runtime-validated**, for a
user-visible slice.

## Vault Browse face — Slice 10 closing sweep (2026-08-13)

The browse-face execution plan's closing slice ordered one full deterministic sweep — `-L unit`
+ `colosseum.qml` + the "vault `.ps1` gates" — logged in one run, no new test surface (the sweep
itself is the gate; no negative control applies).

- **`ctest --test-dir native/build-msvc -L unit --output-on-failure`: 36/37.** The one failure,
  `colosseum.qttest.vault_forensics` (2 of 13 cases: `byte_budget_sets_truncated`,
  `projection_does_not_mutate_files`), is **not this plan's surface** — it is the Phase 2
  visibility lane's F1-Core slice (`b0fde45`, landed mid-run by a concurrent workstream building
  in `native/build-msvc` at the same time; its own commit message states "nothing here has been
  compiled, linked, or run" at commit time). Reproduced deterministically (ran it alone twice,
  same two cases red both times) — not flaky, just out of fence. `docs/encyclopedia/vault.md`'s
  `.paths` already excludes `VaultForensics.*` as belonging to a different plan; this sweep entry
  records the same boundary at the test-ledger layer. Every OTHER vault target is green,
  including the two C++ harnesses folded into `-L unit` (`colosseum.vault_admission_probe_harness`,
  `colosseum.vault_launch_router_harness`).
- **`ctest --test-dir native/build-msvc -R colosseum.qml`: 196/196** (`-VV` case log), matching
  the plan's own expectation exactly. `UpdatePage::test_same_count_swap_crossfades_and_resets` —
  named in the plan as a foreign lane's known intermittent case — passed clean in this run; it did
  not fire.
- **"The vault `.ps1` gates" the plan names do not exist as a separate artifact.** Unlike the
  updater arc (`tests/test_update_lanista.ps1`), no `.ps1` wrapper was ever written for the Vault
  Browse face — searched `tests/*.ps1` for any file referencing a `vault*` name; none exist. The
  Vault's deterministic native coverage lives entirely inside `-L unit` (the `qttest.vault_*`
  targets above plus the two harnesses named above) and `colosseum.qml`; its runtime coverage is
  the `lanista session run` scenario replay directly, CLI-driven, no `.ps1` in between. Recorded
  here as a plan inaccuracy, not a gap — nothing named by the plan is missing, the plan just named
  an artifact shape (`.ps1`) that this arc never needed.

Full logs: `artifacts/slice10-sweep/unit.log`, `artifacts/slice10-sweep/qml_verbose.log` (gitignored
evidence, not committed).

## Watch Party test estate (owed from bb265e6/09f8d3a, recorded 2026-08-20)

The Watch Party adoption (`bb265e6`, arc 03 Preflight slice 08) and its Join-sheet polish
(`09f8d3a`) landed a full test surface that was never entered here — this is the drift fix,
Slice 0 of the relay plan (`docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md`).
Baseline before this entry: `grep -c watchparty docs/colosseum-test-verification.md` → 0.
Nothing below changes any existing entry; this is an append only.

### Registered Qt Test targets — Watch Party

`tests/CMakeLists.txt`'s `find_package(Qt6 REQUIRED COMPONENTS Test Network WebSockets)`
gained `Network` and `WebSockets` for this estate (transport/identity/ui/lifecycle link real
Qt websocket plumbing behind a fake socket — see Proves column). All seven targets carry
labels `unit;qttest`, `TIMEOUT 120`, `ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"`, and land beside
the app's Qt DLLs via the same `RUNTIME_OUTPUT_DIRECTORY` + POST_BUILD `Qt6Test.dll` copy every
other Qt Test target here uses.

| CTest name | Source | Proves | Status |
|---|---|---|---|
| `colosseum.qttest.watchparty_room` | `tests/auto/watchparty/tst_watchparty_room.cpp` (compiles `WatchPartyTypes` + `WatchPartyProtocol` + `WatchPartyTransport` + `FakeWatchPartyTransport` + `WatchPartyRoomController`) | Room domain (approved plan Slice 1): pure Qt6::Core room rules + protocol shape over a deterministic fake transport. No QML, player, account, persistence, or live-network dependency enters this target | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_source` | `tests/auto/watchparty/tst_watchparty_source.cpp` (compiles `WatchPartyTypes` + `WatchPartyProtocol` + `WatchPartySource`) | Authoritative source descriptor (Slice 2): pure Qt6::Core classifier + structured protocol shape, mirroring Player 1's torrent/direct routing without constructing QML/mpv/network/provider state. R1 repair below changed its expected-substring assertion; production behavior untouched | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_sync` | `tests/auto/watchparty/tst_watchparty_sync.cpp` (compiles `WatchPartyTypes` + `WatchPartyPlayerSync`) | Player 1 sync policy (Slice 3): pure Qt6::Core controller against explicit fake player observation/clock values, no mpv/QML/network/account dependency. R2 repair below changed its expected-literal assertion; production behavior untouched | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_transport` | `tests/auto/watchparty/tst_watchparty_transport.cpp` (compiles `WatchPartyTypes` + `WatchPartyProtocol` + `WatchPartyTransport` + `WatchPartySocket`/`QtWatchPartySocket` + `WebSocketWatchPartyTransport` + `FakeWatchPartyTransport` + `WatchPartyRoomServiceClient`; links `Qt6::Network`, `Qt6::WebSockets`) | Production client transport/service seam (Slice 4): WebSocket behavior exercised entirely through an injected fake socket — no live-network endpoint is contacted by this target | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_identity` | `tests/auto/watchparty/tst_watchparty_identity.cpp` (compiles the transport chain above plus `WatchPartyIdentity` + `WatchPartyRoomController`) | Account-owned identity/invite seam (Slice 5): deterministic interface/domain test. The fake account bridge exists only in the test TU — production gets no fabricated account database/client, and this target performs no live account or room-service I/O | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_ui` | `tests/auto/watchparty/tst_watchparty_ui.cpp` (compiles the identity chain above plus `WatchPartyPlayerSync` + `WatchPartyUiController`; links `Qt6::Network`, `Qt6::WebSockets`) | QML-facing lifecycle/readiness seam (Slice 6): deterministic native contract test using fake room/account transport and the real PlayerSync policy — no live account service, room server, QML engine, or mpv instance required | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |
| `colosseum.qttest.watchparty_lifecycle` | `tests/auto/watchparty/tst_watchparty_lifecycle.cpp` (compiles the full chain: types/protocol/transport/socket/websocket-transport/fake-transport/room-service-client/identity/player-sync/ui-controller; links `Qt6::Network`, `Qt6::WebSockets`) | Assembled lifecycle/failure matrix (Slice 7): deterministic fake transport + real repo-local room/service/UI owners, no live service/account/QML/mpv dependency. R3 repair below changed its forbidden-substring guard; production behavior untouched | green at `38750ba`, fresh-runner re-verification 2026-08-20; labels `unit;qttest` |

### Registered Qt Quick Test files — Watch Party (under the `colosseum.qml` aggregate)

All six files below run inside the existing single `colosseum.qml` CTest entry documented in
"Registered Qt Quick Test targets" above (same runner, same production-import resolution, same
`qml;windows` labeling — these open REAL windows). `WatchPartyVisualHarness.qml` is a test-only
harness component (not itself a `tst_*` file) that several of these drive; it ships no production
behavior. All green at `38750ba`, fresh-runner verified 2026-08-20.

| File | Cases | Proves |
|---|---|---|
| `tst_watch_party_join_sheet.qml` | 10 | The Join sheet's guest/host/room states against production `WatchPartyJoinSheet`. The 09f8d3a Join-sheet polish (650px card, gold badge, glass fact pills, status card) changed `test_slice06_guest_join_atlas_state`'s width assertion from 430 to 650 to match the shipped mockup — no other case changed |
| `tst_watch_party_panel.qml` | 18 | The in-player `WatchPartyPanel` chrome against production, including hit-testing through the panel's reparented tree. Two real fixture-geometry bugs were found and fixed in this test file before `bb265e6` landed (not production defects — the harness fixtures were wrong), and its `findChild` search roots were moved from the naive default to the window's content root / sheet's `contentItem` so hit-testing reaches the reparented panel chrome at all |
| `tst_watch_party_visual_harness.qml` | 8 | `WatchPartyVisualHarness.qml` itself — the shared test-only harness component's own rendering/state contract, exercised directly rather than only as a dependency of other files |
| `tst_watch_party_lifecycle.qml` | 5 | The QML-facing lifecycle/readiness surface end to end through production `WatchPartyUiController` bindings, complementing the native `colosseum.qttest.watchparty_lifecycle` target's C++-only coverage |
| `tst_watch_party_taskbar.qml` | 6 | The new taskbar Watch Party action's signal seam against production `Taskbar` |
| `tst_watchparty_source_provenance.qml` | 4 | Source-provenance display in the panel/join UI — which source facts are shown and which are withheld, matching the native `watchparty_source` exact-identity contract |

### Test-side repairs (R1–R3, adoption-time)

First real Qt execution of the adopted package (2026-08-19, pre-`bb265e6`) found 3 test-side
defects against an otherwise-correct production (30/33 native cases passed on that first run);
each was a test expectation drifted from the real contract, not a production bug, confirmed
against `SERVER-PROTOCOL-CONTRACT`/`VERIFICATION.md` before the test was changed
(`Preflight-Architect/arcs/03-watch-party/exec/EXECUTION-READY.md`, "Post-execution package
repairs"):

- **R1** `tst_watchparty_source.cpp:221,233` — expected the substring `"unknown source key"`;
  production emits `"unknown <torrent|debrid> source key '<k>'"` per `exactKeys()`. Fixed the
  test to expect `"source key"`.
- **R2** `tst_watchparty_sync.cpp` `smallDriftDoesNotCauseMicroSeek` — expected the literal
  `"in_sync"`; the canonical name is `"inSync"` (`WatchPartyTypes::syncStatusName` +
  `SERVER-PROTOCOL-CONTRACT`'s own `participantState` example). Fixed the literal.
- **R3** `tst_watchparty_lifecycle.cpp:514` — a blanket `!bytes.contains("source")` forbidden-
  substring guard conflicted with the contracted category value `"sourceUnavailable"`
  (`VERIFICATION.md` §G row 1). Fixed by replacing the blanket ban with targeted forbidden-
  identity sentinels that don't collide with the legitimate category name.

All three repairs changed test expectations only; no production `native/watchparty/*` source
changed to make them pass.

## Tankoban catalogue-independence closing sweep (2026-08-21) — relink + Slice 5 runtime + Slice 7 gates

Hemanth closed the daily `colosseum.exe` (PID 9296 from the Slice 5 gate entry above); this
sweep relinked and ran every gate the exe lock had blocked. Verification-only per the task
brief; no production code touched. Two build-slot collisions with Agent 4's concurrent Theatre
Lanista sessions occurred and were handled per the coordinator's own ruling (waited them out
bounded, never killed a foreign process); recorded in `agents/chat.md`.

- **Relink: green.** `cmd /c native\build-msvc.bat` — full rebuild (945 targets; the CMakeLists
  churn from concurrent work forced a from-scratch reconfigure, not a quick incremental link).
  First attempt hit `LNK1104` again transiently (no process held the file at the time of
  either check; root cause not chased — plausibly AV/OS handle lag on the freshly-written
  exe) — retried clean, `BUILD_OK`, zero `error C`/`ninja: build stopped` lines. Evidence:
  `artifacts/tankoban-independence/closing/build_relink.log` (+ `_attempt1_failed.log`).
- **Slice 5 runtime gate: authored and run for the first time.** The plan's `tests/
  test_tankoban_chapter_migration.ps1` + `tests/lanista_scenarios/tankoban_chapter_migration.json`
  + seed fixture `tests/lanista_fixtures/tankoban-chapter-migration-v1/` (a WC-era `manga/`
  chapter tree for `berserk-1`, a `manga-volumes/berserk-1/1.cbz` archive that must survive,
  and a hand-authored `progress-store.ini` carrying one `manga`/`tankoban`/`comic` record
  each, mirroring `tst_tankoban_chapter_migration.cpp`'s own fixture shape and the existing
  `tests/lanista_fixtures/journeys/ceremony-use-existing-v1/progress-store.ini` for the exact
  QSettings ini-escaping idiom) did not exist before this pass — Slice 5's own report deferred
  writing them "to be written AND run together in the next pass." Authored now, iterated to
  green:
  - **Disk-byte purge: PASS.** `manga/` chapter tree deleted, `manga-volumes/berserk-1/1.cbz`
    survives untouched, the `tankoban-chapter-migration.v1.done` marker lands, and
    `colosseum.log` carries the exact summary line (`existed=yes deleted=yes, 1 series
    dir(s), index.json=removed`). This is Hemanth's actual explicit lock ("on-disk bytes
    included") and it holds.
  - **Progress-record purge: CONFIRMED BROKEN (pre-existing, not fixed).** The seeded
    `manga`-kind record survives the migration — `colosseum.log` reports `0 manga-kind
    progress record(s) purged`, not the seeded 1. Root cause ground-truthed, not guessed:
    `native/main.cpp`'s own comment at the `TankobanChapterMigration::run()` call site
    (~line 1549-1555) already named this exact risk — the migration purges "whichever
    [ProgressStore] is bound at THIS instant (the sealed store pre-onboarding-choice)...
    not necessarily the same instance a later 'continue local' rebind swaps in." This
    sweep's scenario clicks exactly that "continue without an account" affordance
    (required to reach any Tankoban content in a fresh/tagged session), triggering the
    rebind, and the purge count proves the sealed store it purged is provably NOT the one
    that ends up bound to QML's `Progress` and flushed to disk at shutdown (confirmed: the
    tagged `progress-store.ini` was rewritten at session end with all three records,
    including the manga one, still present). This is Bundle 8C account/profile runtime
    territory (`ProfileStoreRuntime.cpp`'s own header: "PRE-FLIGHT DRAFT STATUS:
    unverified") — explicitly out of this sweep's fence per the plan's own Slice 5 text
    and this task's "verification only" mandate; not patched here. Practical read for
    Hemanth: the disk-side chapter files are deleted regardless (unconditional, no
    ProgressStore dependency), so the user-visible risk is limited to a possible stale
    Continue tile pointing at a now-deleted chapter — cosmetic, not data-loss.
  - Scenario iteration (recorded for the next author who touches this file): the FIRST
    scenario draft clicked straight into the "Top in Tankoban" rail immediately after the
    tab switch and hit `NO_SUCH_ITEM` on `tankobanTopMangaTile_0/1` twice, even after adding
    extra `qml-get` settle round-trips — proven NOT a materialization-timing issue (an
    interactive `mcp__lanista__act()` session reached the identical target first try with
    no settle needed) but the SAME script-only `lanista session run` batching gap Slice 6's
    ledger already named for Discover deep-rank clicks. Resolved honestly the same way
    Slice 6 did: the shipped scenario stops at a reliable clean-boot proof (bootSplash
    clears, onboarding dismisses, Tankoban opens, tab bar renders) instead of shipping the
    flaky rail click as a gate; the deeper masthead check was proven by hand via the
    interactive adapter (session `20260820-234936-963268d6`, Vagabond check reused the same
    technique below) and is recorded as hand-driven evidence, not a scripted gate.
  - Evidence: `artifacts/tankoban-independence/slice5/test_tankoban_chapter_migration_run{1..4}.log`
    (iteration history) + `artifacts/tankoban-independence/closing/` (final green run).
- **Slice 7 scenario replays.**
  - `tankoban_discover_depth.json`: clean, 18/18, matching the plan's own expectation (the
    materialization proof only — the deep-rank click leg was already known Bridge-blocked
    per Slice 6, unchanged this sweep). Warning gate: `WARNING_GATE_OK`.
  - `tankoban_catalogue_smoke.json`: replayed 4× fresh isolated sessions. Run 1: 30/31
    (one failure). Run 2: 9/10-equivalent early failure (a rail-click `WAIT_TIMEOUT`, the
    same script-only class named above). Runs 3-4: 30/31, IDENTICAL failure both times —
    the scenario's FINAL assertion (`Berserk is honestly shelf-less`) reads
    `displayTitle=="One Piece"` instead of `"Berserk"` after the late-sequence tile click.
    This is a NEW, distinct symptom from the click-target-resolution class above: the
    preceding `ui-wait-for tankobanSeriesMasthead.ready==true` step PASSES cleanly every
    time (the click itself lands), but the very next `qml-get` reads stale content —
    `ready` flipping true a frame (or more) before `displayTitle`/`resolvedMalId` finish
    updating to the newly-opened series, specifically on a LATE re-navigation (this is the
    series page's 3rd+ open in the scenario, after two prior opens, two picker cycles, and
    a world-tab round trip). 3 of 4 replays hit this; every OTHER step across all 4 runs —
    masthead identity, the 113-volume shelf, both picker open/dismiss cycles, the
    world-tab-away/back regression, the re-render-after-round-trip regression — passed
    clean every single time. Named here as a genuinely reproduced, NOT previously
    documented defect class (masthead `ready`/content update ordering on rapid
    re-navigation) for a future slice to root-cause in `qml/MangaSeries.qml`'s `resolve()`
    — not fixed this pass (out of "verification only" scope; the existing committed
    scenario file was not altered). Warning gate on the one full-length run:
    `WARNING_GATE_OK`. Evidence: `artifacts/tankoban-independence/closing/
    smoke_replay{,_r2,_r3,_r4}.log`.
- **Deterministic sweep.**
  - `ctest --test-dir native/build-msvc -L unit --output-on-failure`: **71/71 green** — the
    two previously-named foreign `colosseum.qttest.profile_activity_isolation` sub-case
    reds (Slice 5 gate entry above) now PASS too; exceeds the plan's own "71/71 + up to 2
    named foreign reds" expectation.
  - `ctest -R colosseum.qml`: 436 passed / 30 failed / 3 skipped, both of two full runs
    (stable, not flaky — identical failure set both times). Every failure is in
    `AccountDataPrivacy`/`AccountDevicesCentre`/`AccountRecoveryCentre`/`AccountYourColosseum`
    (Bundle 8C, self-described unverified), `GuideOverlay`/`GuidePage` (20 of the 30 —
    the Guide search/journey rework whose source files were already dirty/uncommitted
    in the working tree before this sweep touched anything), and `WatchPartyVisualHarness`
    (Agent 4's own in-flight Theatre lane, one case). Zero Tankoban/manga-lane failures.
    The one ledger-named flake, `SearchHistoryFlow::
    test_biblioRecentChipBodyAndRemoveHaveIndependentClickTargets`, failed on the first run
    and PASSED clean on the one allowed rerun (confirmed via the full-target rerun, since
    the standalone `colosseum_qml_tests.exe` function-filter CLI syntax did not cooperate
    within this pass's time budget — recorded as a minor tooling gap, not chased further).
    Evidence: `artifacts/tankoban-independence/closing/ctest_qml{,_rerun}.log`.
  - `tests/test_manga_series_catalogue.ps1`: OK. `tests/test_manga_reading_room.ps1`: OK.
    `tests/test_tankoban_chapter_migration.ps1`: see above (disk PASS, progress-purge
    confirmed-gap). The plan's "updated Library gates" (`TB-002`/`TB-003`) remain the same
    untracked/unregistered `tests/test_tankoban_library.ps1` Slice 5's own report already
    named as unowned WIP — not run (unregistered in CTest, not this sweep's to adopt).
- **Step E (optional-if-time): the mal-basis/uncovered shelf branch, reached and recorded.**
  Vagabond (in the static "Top in Tankoban" rail, index 3) opened clean via the interactive
  adapter: malId 656, `hasShelf true`, `primaryAction "get"`, shelf `rowCount 37`,
  `coveredCount 0` — every card an honest "NO COVER" glass, matching MAL's own count
  (`count_basis=mal`, no BookWalker harvest yet). Screenshot:
  `artifacts/tankoban-independence/closing/step-e-vagabond/vagabond-uncovered-shelf.png`.
  Hal (malId 49611) and Baby Princess (malId 8676) remain unreached — Slice 6's own finding
  stands unchanged this sweep (no search-to-series bridge route exists; scroll depth caps
  around rank 18) — both moved to the human-witnessed list.
- **Human-witnessed checklist** written (not performed, per this task's mandate):
  `artifacts/tankoban-independence/closing/human-witnessed-checklist.md`.
- **Slice 5 Overall status: Runtime-validated for the disk-byte purge, the progress-record
  purge, AND the app's own functional health post-migration.** The progress-record purge
  line above is FLIPPED from "confirmed-broken" to fixed, 2026-08-21 — see "Held runtime
  gates closed (R1 sweep)" below for the runtime confirmation: `tests/
  test_tankoban_chapter_migration.ps1` green (`TANKOBAN_CHAPTER_MIGRATION_OK`), disk +
  durable-store-ini checks all pass in a fresh isolated seeded session. Human-witnessed
  confirmations (this sweep's checklist) still pending Hemanth's own eyes.
- **Slice 7 / arc status: gates run and recorded; NOT declared fully closed.** Two real,
  newly-characterized-or-confirmed gaps stand open (the progress-purge rebind gap; the
  masthead stale-read race on late re-navigation), plus the pre-existing 30 foreign QML
  failures (unrelated lanes, unchanged by this sweep) and the still-unreached Hal/Baby
  Princess pair. None of these block Hemanth's own eyes-on pass — the checklist above is
  ready for him regardless.

## Catalogue-independence closing-sweep FOLLOW-UP: two named defects fixed (2026-08-21)

Systematic-debugging pass (`brotherhood-systematic-debugging`) on the two gaps the closing
sweep above characterized but explicitly left unfixed (out of its own "verification only"
fence). Both root causes were confirmed by direct code inspection of the boot/rebind
sequence and the masthead's signal-dependency graph, not guessed.

### Defect 1 — progress-record purge missed the rebound ProgressStore

**Mechanism, ground-truthed.** `ProfileStoreRuntime`'s constructor
(`native/account/ProfileStoreRuntime.cpp:38-55`) ALWAYS starts behind a Sealed placeholder
store (`createSealedStores()`, line 344): a `ProgressStore` backed by a fresh
`QTemporaryDir` under `<appDataRoot>/profile-session/sealed-XXXXXX/progress.ini` — a
throwaway instance discarded the moment onboarding resolves. `main.cpp:1556-1557` (as it
stood after the closing sweep) called `TankobanChapterMigration::run()` exactly once,
synchronously, right after `accountRuntime->prepareForQml()` — i.e. against THIS sealed
placeholder in the common case (a fresh session with no remembered account). The purge ran,
found the placeholder empty (nothing was ever seeded into a per-boot temp path), wrote its
once-only marker (`tankoban-chapter-migration.v1.done`) regardless, and the REAL store a
later "continue without an account" click rebinds `Progress` to
(`ProfileStoreRuntime::activateLocalOnlyProfile()` / `reloadLegacyProfile()`, both invoked
from `FirstAccountProfileCoordinator::prepareLocalOnly()`,
`native/account/FirstAccountProfileCoordinator.cpp:279-305`) never got purged — the marker
already existed, so the migration's own idempotency guard silently skipped it forever.
Traced further: in a fresh/never-adopted tagged session (this test's own shape),
`prepareLocalOnly()` takes the `reloadLegacyProfile()` branch (`legacyPersonalStateClaimed()`
is false pre-adoption), whose `ProgressStore` uses the tag-diverted DEFAULT constructor
(`ProgressStore.h:139-149`, `progressStoreTaggedIniPath`) — landing at
`<appDataRoot>/progress-store.ini`, the SAME path the seed fixture writes to, which is why
the closing sweep could directly confirm the seeded manga record survived to session end.

**Fix (native/engine/TankobanChapterMigration.{h,cpp}, native/main.cpp).** Added a
`progressStoreIsDurable` parameter to `TankobanChapterMigration::run()` (default `true`,
preserving every existing disk-only/no-store caller's contract unchanged). When `progress`
is non-null and `progressStoreIsDurable` is false, `run()` still performs the unconditional,
idempotent disk-side purge (`<AppDataLocation>/manga/`, no `ProgressStore` dependency) but
WITHHOLDS the once-only marker and skips the progress purge — the once-only marker must not
burn before the real purge succeeds. `main.cpp` now wraps the call in a lambda
(`runTankobanChapterMigration`) run once immediately (covering a remembered session that
already rebound synchronously inside `prepareForQml()`) and reconnected to
`ProfileStoreRuntime::storesChanged` (covering every later rebind: continue-local, sign-in,
sign-out-to-local). The lambda computes `durable = activeProfile().kind() != Sealed` fresh
on every call. A second, load-bearing correction found while tracing the rebind: `storesChanged`
also fires from `ProfileStoreRuntime::suspendPersonalStoresForMigration()`
(`ProfileStoreRuntime.cpp:156-166`, the first half of `prepareLocalOnly()`) with `m_stores`
already reset to null — a transitional signal, not "no store handed in" by caller choice.
Calling `run()` on that null-progress transitional emission would have re-created the exact
same bug one signal later (disk purge repeats harmlessly, but a null-progress call still
withholds nothing and would burn the marker on the "no store" path). The lambda now returns
early whenever `progressStore()` is null, so only a call with a REAL store pointer ever
reaches `run()`.

**Verification — Qt Test layer (`colosseum.qttest.tankoban_chapter_migration`), Root cause
confirmed, red-then-green with negative control performed.** Extended
`tests/auto/tankoban/tst_tankoban_chapter_migration.cpp` with
`sealed_store_purge_deferred_until_durable_rebind()`: seeds a SEPARATE `sealedStore` (the
ephemeral placeholder shape) and `realStore` (the durable shape) each with one
manga/tankoban/comic record; calls `run(root, &sealedStore, /*durable=*/false)` and asserts
the disk purge still ran but the marker is withheld and BOTH stores' manga records survive
untouched; then calls `run(root, &realStore, /*durable=*/true)` and asserts the marker now
lands, the real store's manga record is gone, tankoban/comic survive, and a third call is a
true no-op. **Negative control:** temporarily disabled the new `if (progress &&
!progressStoreIsDurable)` guard (`if (false && progress && ...)`) and rebuilt — the new case
went RED exactly as predicted (`'!QFile::exists(marker)' returned FALSE` — the marker landed
after only the sealed-placeholder pass, reproducing the original defect precisely); restored
the guard and rebuilt clean — full suite green again. Evidence: local build/run cycle,
`tst_tankoban_chapter_migration.exe` — sabotaged run: `Totals: 2 passed, 1 failed` (the new
case FAILing on the marker assertion); restored run: `Totals: 9 passed, 0 failed`
(all prior cases plus the new one). Build via `native/_slice5_build.bat`
(`cmake --build build-msvc --target tst_tankoban_chapter_migration`), invoked through
PowerShell per `feedback_verify_exe_mtime_after_build` (bare `cmd /c` from Bash no-ops on
this machine); exe `LastWriteTime` verified to advance on every rebuild.

**Runner-side records check extended.** `tests/test_tankoban_chapter_migration.ps1` gained a
direct disk-ini check (beyond the existing `colosseum.log` summary-line assertion, which only
proves the migration's LAST pass purged 1 record from WHATEVER store it held, not that the
store is the durable one): reads
`$appDataRoot/progress-store.ini` and `$appDataRoot/profiles/local/progress.ini` (whichever
exists) and asserts the seeded `berserk-1` id string is gone while `mal:2`/`locg:123` (the
tankoban/comic ids) survive — matched by id VALUE, not the `"kind":"manga"` JSON key, so the
check does not need to reproduce the disk writer's own QSettings ini-escaping exactly.
Syntax-checked (`[System.Management.Automation.PSParser]::Tokenize`, `PARSE_OK`) and
confirmed ASCII-only (file's own house rule) via `perl -ne 'print if /[^\x00-\x7F]/'`
(zero matches).

**Bridge-layer / full runtime .ps1 gate: NOT run this pass — Bridge blocked, not a defect in
the fix.** `native/build-msvc/colosseum.exe` (PID 18392, started 00:31:52, the closing
sweep's own relink) was live for this entire pass — per the sweep's own written
human-witnessed checklist, THIS is Hemanth's real first daily boot of the migrated build (the
actual one-time destructive chapter purge on his real AppData root), not a spare build
someone left running. Standing rule (never kill a `colosseum.exe`) plus the app-lock-the-exe
build trap (`feedback_no_concurrent_builds_same_out_dir`) both apply; polled bounded
(~17 min direct wait, ~37 min total elapsed since claim) per this task's own instruction
rather than block indefinitely or touch the live process. `tests/test_tankoban_chapter_migration.ps1`
(the runner-side disk+records gate this fix's completion criterion names) and the
`colosseum.exe`-side half of `tst_tankoban_chapter_migration`'s Qt Test build both need a
full-app relink this pass could not obtain. **Test seam status: available** (harness and
runner both exist, extended, syntax-valid); **Bridge status: bridge blocked** (needs
`colosseum.exe` free to relink + `lanista session run`). Next actor: rebuild
(`native/build-msvc.bat` or the targeted `_slice5_build_app.bat`) once the daily app is
closed, then rerun `tests/test_tankoban_chapter_migration.ps1` for the `TANKOBAN_CHAPTER_MIGRATION_OK`
sentinel and the new durable-ini records check.

### Defect 2 — masthead ready/displayTitle stale-read race on late re-navigation

**Mechanism, ground-truthed by direct QML signal/slot analysis (Qt connects slots to a
signal in connection order).** `qml/MangaSeries.qml`'s `resolve()` (triggered by
`onSeriesTitleChanged`, itself fired by the SAME `page.seriesTitle` assignment that opens a
new series) is the sole writer of every masthead-facing `page` property. Every OTHER masthead
scalar (`tankobanSeriesMasthead.resolvedMalId`/`hasShelf`/`primaryAction`) binds to a `page`
property that changes INSIDE `resolve()` itself (`page.resolvedMalId`, and the `hasShelf`/
`primaryAction` chain derived from it) — each such property change fires its OWN dedicated
notify signal, synchronously nested inside `resolve()`'s call stack, well before `resolve()`
returns. `tankobanSeriesMasthead.ready` (`!page.loading`) is the same shape: `page.loading`
changes inside `resolve()` too, so `ready` flips true synchronously nested, still inside
`resolve()`. `tankobanSeriesMasthead.displayTitle`, before this fix, was a live binding on
`page.seriesTitle` — the ONE property that changes BEFORE `resolve()` starts (it's what
triggers `resolve()` via `onSeriesTitleChanged` in the first place). That means
`displayTitle`'s binding-update and `onSeriesTitleChanged`'s handler (`resolve()`) are BOTH
subscribers of the SAME `seriesTitleChanged` signal; Qt dispatches connected slots in
connection order, and `onSeriesTitleChanged` (declared at `page`'s own construction, earlier
in the file) connects before `tankobanSeriesMasthead`'s child-Item binding (declared later).
So `resolve()` runs to completion — including `ready` flipping true — strictly BEFORE
`displayTitle`'s own binding re-evaluation gets its turn on that same signal. Internally this
ordering is deterministic every time `resolve()` runs on a title change; what made it read as
"3 of 4 replays" flaky (per the closing sweep's own log) is that the race is only OBSERVABLE
by an external, differently-scheduled reader (Lanista's bridge poll) landing inside that
narrow inter-slot window on the GUI thread — a window whose odds of being sampled rise with
how much prior work is queued, matching the sweep's own finding that it only hit on the
scenario's THIRD-plus series-page open, late in a 31-step sequence.

**Fix (qml/MangaSeries.qml).** Changed `tankobanSeriesMasthead.displayTitle` from a live
`page.seriesTitle` binding to a plain property (`property string displayTitle: ""`),
assigned EXPLICITLY inside `resolve()` (`tankobanSeriesMasthead.displayTitle = seriesTitle`,
right before `loading = false`). This folds `displayTitle` into the SAME synchronous call
stack as every other masthead scalar and `ready`, closing the cross-slot gap structurally —
no reliance on Qt's connection-order semantics survives in the fixed code. Minimal: one
property-declaration line, one assignment line, both inside the file's own existing
`resolve()`/masthead-Item shapes; no other masthead scalar needed the same treatment (each
already updates via its own dedicated notify chain nested inside `resolve()`, as traced
above).

**Verification — static layer: Root cause confirmed by code-level signal-dependency tracing;
`qmllint` clean.** `qmllint qml/MangaSeries.qml` shows the same pre-existing "unqualified
access" warnings this file already carried (context-property references — expected pattern,
unrelated to this change) and zero new warnings or errors at or near the edited lines
(`resolve()`, the masthead `Item` block). No new unit/harness seam applies here: an
in-process QML harness call (e.g. `tests/manga_series_catalogue_harness.qml`) cannot exercise
this race at all — the whole `resolve()` call, INCLUDING the previously-buggy sibling-binding
catch-up, completes synchronously within one harness JS statement, before an in-process
assertion could ever observe the intermediate state. The only layer that can actually
observe the race is an external, differently-scheduled reader — i.e. Lanista's own bridge
poll — matching this defect's own completion criterion below.

**Bridge-layer replay: NOT run this pass — Bridge blocked, same live `colosseum.exe` as
Defect 1.** The fix's own required gate — 4 fresh isolated `lanista session run` replays of
`tests/lanista_scenarios/tankoban_catalogue_smoke.json`, the previously-flaky final Berserk
assertion must pass 4/4 — needs a colosseum.exe built from this fix AND an idle machine (no
foreign `colosseum.exe`/`lanista.exe`), per this task's own pre-session-claim discipline. The
same live daily instance blocking Defect 1's runtime gate blocks this one too. **Test seam
status: not applicable** (no deterministic unit/harness seam exists at this layer, by
design — see above). **Bridge status: bridge blocked.** Next actor: once `colosseum.exe`
is free, rebuild, then run 4 fresh tagged sessions of `tankoban_catalogue_smoke.json` and
confirm the Berserk shelf-less final assertion (`displayTitle=="Berserk"`, not a stale prior
title) passes in all 4, posting a claim/release line to `agents/chat.md` around the session
block per standing discipline.

**Overall status: Root cause confirmed (both defects); Test seam status: available (Defect
1) / not applicable (Defect 2); Bridge status: bridge blocked (both) — the live daily
`colosseum.exe`, not a fix defect, is the blocker.** Both fixes are `git diff`-reviewable
now; the runtime confirmations above are this pass's honest handoff, not a claimed pass.

## Held runtime gates closed (R1 sweep, 2026-08-21) + Slice R1 landed ("nyaa ships dark")

Two-mission pass once the daily `colosseum.exe` was closed and the build-msvc + Lanista slot
was free: (1) the two held runtime gates the closing-sweep follow-up (above) could not run
because the daily app was live; (2) Slice R1 (`docs/superpowers/plans/2026-08-20-colosseum-
tankoban-catalogue-independence-plan.md`, added 2026-08-21). A full build was clean
(`native/build-msvc.bat`, exe mtime advanced, grep-verified no `error C`/`ninja: build
stopped`/`LNK`).

### Held gate 1 — chapter-migration disk gate: GREEN, with one genuine gap found and fixed

Running `tests/test_tankoban_chapter_migration.ps1` fresh (the gate the two closeout fixes
in `3722794` were written for but never runtime-validated, both blocked by the live daily
exe) surfaced a REAL, previously unexercised defect distinct from either closeout fix: the
log-line assertions this `.ps1` already carried (from `21c4f2f`, predating the sealed/
durable split) assumed a SINGLE combined `[tankoban-migration]` summary line reporting disk
+ progress facts together. Defect 1's fix (`3722794`) legitimately calls
`TankobanChapterMigration::run()` TWICE per boot — once against the Sealed placeholder
(disk-only pass, marker withheld) and once against the real durable store (marker written)
— and by the second call the disk side is already clean, so the marker-writing line
honestly reports `0 series dir(s)` even though 1 was purged on the FIRST call. The first
run against the rebuilt fix genuinely reproduced this as a real RED
(`migration summary line does not report 1 series dir(s) purged`), not a guess.

**Fix.** `native/engine/TankobanChapterMigration.cpp`: the Sealed-pass `qInfo()` (previously
only logging `existed`/`deleted` booleans) now also logs `chapterDirsDeleted`/`indexDeleted`
— the same detail fields the final line has — so the disk-purge facts land on whichever
call actually found the tree present. `tests/test_tankoban_chapter_migration.ps1`: the log
assertions no longer require one combined line; they check the WHOLE log text for
`existed=yes deleted=yes` + `1 series dir(s)` (wherever they land) and the final
`chapter store purge complete`/`1 manga-kind progress record(s) purged` line separately —
comment explains the sealed/durable split so a future reader doesn't reintroduce the
single-line assumption. Verification: the ORIGINAL pre-fix run is the negative control (a
genuine red on the exact predicted line, not a synthetic sabotage) — rebuild + rerun after
the fix went green immediately. `colosseum.qttest.tankoban_chapter_migration` stayed 9/9
green throughout (this defect was never visible at the unit-fixture layer — the fixture
doesn't drive two real boot-time calls the way main.cpp does, matching why the held gate
exists).

**Runtime result:** `TANKOBAN_CHAPTER_MIGRATION_OK` — `manga/` gone, `manga-volumes/`
intact, marker written, durable-store ini confirms `berserk-1` (manga-kind) purged and
`mal:2`/`locg:123` (tankoban/comic-kind) survive. Slice 5's progress-purge line above is
flipped to Runtime-validated.

### Held gate 2 — masthead race: Defect 2's OWN fix verified correct; a SEPARATE,
### previously-uncharacterized scenario defect found blocking a clean 4/4 replay

The 4x fresh-session replay of `tankoban_catalogue_smoke.json` this gate calls for did NOT
reach 4/4 clean on the first attempts — but ground-truthing WHY (an interactive lanista
session, tag `r1sweep-diag`, session `20260821-013744-532e91b6`) proved Defect 2's own fix
(the `resolve()`-synchronous `displayTitle` assignment, `qml/MangaSeries.qml`) is correct:
driven step-by-step through the exact committed sequence up to the point the CLI replay
always failed, the masthead resolved Berserk's `displayTitle`/`resolvedMalId`/`hasShelf`/
`primaryAction` atomically and correctly the moment `resolve()` actually ran. The race
Defect 2 targeted (Qt's connection-order dispatch on `seriesTitleChanged`) is closed.

**What was actually failing: a click-target gap, not a masthead race.** `qml/MangaSeries.
qml`'s root carries a full-window absorbing `MouseArea` (`anchors.fill: parent`, "absorb
clicks from the world page below" — by design, so the hidden world page underneath a
showing series page never receives stray clicks). The committed scenario's own "back to the
Manga tab to reach the shelf-less fixture" step clicks `tankobanTab_manga` while One Piece's
series page is STILL showing — that click is unconditionally absorbed (Qt hit-tests to the
topmost item at that screen position, regardless of which objectName was requested), so
`tankobanTopMangaTile_1` (Berserk) never gets a real click either: it lands wherever the
showing shelf happens to render at that pixel, sometimes a no-op, sometimes (reproduced
live) a MISCLICK into an unrelated volume card that started a REAL torrent resolving.
`displayTitle` staying "One Piece" is the exact, correct, honest report of "nothing actually
happened" — not the race Defect 2 fixed recurring. `tankobanReadingRoomBack` (the page's
OWN back control, forwards to `page.backRequested()` -> `win.closeSeries()`) is the only
real path back to world-tab navigation from an open series page.

**Fix applied to `tests/lanista_scenarios/tankoban_catalogue_smoke.json`:** an explicit
`tankobanReadingRoomBack` click (with its own async-frame-aware close-settle wait, matching
the file's existing convention) is now inserted before EVERY subsequent world-tab/tile
navigation attempted from an already-open series page (both the One Piece world-round-trip
regression and the Berserk shelf-less leg); the round-trip regression also gained a
`qml-get` on `displayTitle`/`resolvedMalId` so it asserts a REAL re-render instead of a
vacuous "nothing changed, so nothing failed" pass. With this fix, a fresh replay reaches the
Berserk masthead assertions and passes them cleanly.

**A second, separate, NOT-fixed defect surfaced during this same investigation:** the
scenario's `modePill_Biblio` -> `modePill_Tankoban` world-tab bounce (fired back-to-back, no
settle wait) intermittently fails to land the second click — reproduced 3 of 4 fresh
replays this pass, always at that exact step, never past it. Root cause not fully
ground-truthed this pass (candidate: `openWorld()`'s keep-alive per-world Loaders mean a
world's own `WorldPage`/`TopBar`/`Pill` tree — and its `modePill_*`/`tankobanTab_*`/
`tankobanTopMangaTile_*` objectNames — may exist MULTIPLE times simultaneously across
loaded-but-inactive worlds, a duplicate-objectName shadow the ledger's own naming law warns
about; a `modePill_Biblio.active`/`modePill_Tankoban.active` settle-wait attempt this pass
did not resolve it and was reverted rather than shipped unverified). **This is
pre-existing, orthogonal to both closeout defects, and NOT fixed this pass** — named here
as a next-actor handoff, not papered over. **Overall for held gate 2: Defect 2's fix is
Runtime-validated by direct evidence (not a CLI-replay count); the scenario's own
click-target gap is fixed and verified (fresh replay reaches and passes the Berserk
masthead); the separate world-bounce race is open, unfixed, named.**

### Slice R1 — "nyaa ships dark": Runtime-validated

**Ground-truthed first (no guessing):** the extension-gate INFRASTRUCTURE already exists and
already covers manga Nyaa. `native/engine/ExtensionsStore.cpp::appendHouseDefaults()`'s
`entry()` lambda computes `removableWell = !core && resources.contains("stream")` and seeds
`enabled: !removableWell` — Torrentio (`com.stremio.torrentio.addon`) and the manga well
`colosseum.well.nyaa` (added `b528c98`, "the store carries three worlds") are BOTH
non-core, stream-providing wells, so BOTH already seed disabled. Live-confirmed in this
pass's own session: Torrentio, NoTorrent, Nyaa, WeebCentral, GetComics, and Tankorent all
render OFF by default; only core catalogues (Colosseum Grand Database, AniList, Apple Books)
and non-fetching capabilities (Anime Kitsu, OpenSubtitles v3) render ON. The Extensions page
(`qml/ExtensionsPage.qml`) already lists and toggles every well generically (`extensionRow_
<id>`/`extensionToggle_<id>`, `Extensions.setEnabled(id, bool)`) — no changes needed there.
**The actual gap: the manga picker never consulted this state at all** —
`qml/MangaTankobanSourcesPage.qml`'s `show()` called `TankobanVolumes.searchSources()`/
`searchSeriesSources()` unconditionally, the exact class of violation
`feedback_no-default-acquisition-sources.md` names against v1.0.

**Fix, mirroring Theatre's OWN gate exactly** (`qml/SourcesSheet.qml` + `AddonClient.js`
`streamExtensions()`, which filters to `enabled === true` extensions BEFORE ever asking
them — the same shape, not reinvented):
- `qml/MangaTankobanSourcesPage.qml`: new `sourcesEnabled` property (positive scalar, per
  the ledger's no-absence-assertions law) + `_nyaaEnabled()`, read fresh on every `show()`
  call (never cached) against an injectable `extensionsRef`/`extensionsObject` seam (same
  shape as the file's existing `service`/`serviceObject` seam) falling back to the real
  `Extensions` context property. `show()` now checks the gate FIRST: dark -> `loading=false;
  complete=true; return` before any native search kicks — no auto-enable, no nag, and the
  "get"/"search" primary button that opened the sheet still opens it every time (never a
  silent no-op, per the plan's own requirement). The empty-state text
  (`tankobanSourcesEmptyText`) names the fix ("No sources enabled — enable Nyaa in
  Extensions to search.") when dark; a new `tankobanSourcesEnableRoute` control (house
  style — gold-outline pill, no color pop, no tagline) is the ONLY route that ever turns
  nyaa on, emitting `openExtensionsRequested()`.
- `qml/MangaSeries.qml`: forwards `openExtensionsRequested()` from its `sourcesPage` child
  up to the host (same shape as `backRequested`/`minimizeRequested`); exposes
  `readonly property alias sourcesPage: sourcesPage` so a bare-page test harness can reach
  the child and inject a fake `extensionsRef` (no Lanista bridge, no real Extensions
  singleton available there).
- `qml/Main.qml`: `openExtensionsPage(world)` gained an optional `world` param — the manga
  route passes `"tankoban"` so enabling Nyaa is a direct one-click path (Extensions'
  `pane: "sources"` default already lands the House Sources list; passing `world` skips
  past Theatre's own rows) — wired via `item.openExtensionsRequested.connect(function() {
  win.openExtensionsPage("tankoban") })`. `qml/Main.qml` carries unrelated foreign
  in-flight hunks (a `reducedMotion` property, an `immersiveSurfaceOpen` refactor) —
  committed via a surgical blob (`git update-index --cacheinfo`) so only these two hunks
  land, per the shared-file discipline; `git diff qml/Main.qml` after this commit still
  shows the foreign hunks, untouched, in the working tree.

**Focused tests.**
- `tests/auto/extensions/tst_extensions_first_run.cpp`: new
  `manga_nyaa_well_seeded_disabled()` — names `colosseum.well.nyaa` directly (the existing
  `fresh_profile_requires_consent_for_removable_wells()` already proved the GENERIC
  removable-well mechanism nyaa rides, generically; this case is R1's own direct,
  traceable assertion). 8/8 green including the new case. Negative control: inverted the
  assertion -> exactly that case red (`'nyaa.value(...).toBool()' returned FALSE`) with
  the other 7 unaffected -> restored -> 8/8 green.
- `tests/manga_series_catalogue_harness.qml`: new Case 6, using the new
  `sourcesPage`/`extensionsRef` seams — a `FakeExtensions` (id/enabled pairs, the exact
  shape `ExtensionsStore::installed()` returns) toggled dark -> `sourcesEnabled===false`,
  `loading===false`, `complete===true` (honest immediate empty state, never a hang),
  `rows.length===0`; toggled enabled -> `sourcesEnabled===true`. Negative control: flipped
  6a's expectation -> exactly that case red (`MANGA_SERIES_CATALOGUE_FAIL: case6a...`) ->
  restored -> `MANGA_SERIES_CATALOGUE_OK`.
- `qmllint` clean on all three touched QML files (`MangaTankobanSourcesPage.qml`,
  `MangaSeries.qml`, `Main.qml`) — only the pre-existing "unqualified access" class
  (context-property references, the file's own established convention) at or near the
  edited lines, zero new warnings/errors.

**Runtime — live evidence, interactive session (tag `r1-dark-verify`), then a committed
scenario replayed multiple times fresh:** fresh install -> One Piece volume 1 picker shows
`sourcesEnabled: false`, honest empty text + `tankobanSourcesEnableRoute` visible -> click
route -> Extensions opens on the Tankoban world -> Installed pane -> toggle Nyaa on
(`checked: true`) -> close Extensions (Escape) -> series page still showing underneath
(Extensions was a layer over it) -> close the picker properly (async-frame-aware settle
wait — the bug class also just fixed in `tankoban_catalogue_smoke.json`) -> reopen ->
`sourcesEnabled: true`, `tankobanSourcesList.count: 10` real nyaa rows for One Piece vol. 1
(grab shows `1r0n`/`Nyaa` STRONG rows with real release titles, sizes, seeder counts).
**New committed scenario `tests/lanista_scenarios/tankoban_nyaa_dark_gate.json`** encodes
this exact working sequence (including the "Installed pane, no scroll" route that avoids a
separate bridge coordinate-cache nuance ground-truthed this pass: a click fired immediately
after a scroll or a pane/tab switch can resolve a stale screen position — worked around here
with settle waits on real properties, per the plan's "no sleep" law; not fixed at the bridge
layer, named as a tooling nuance for future scenario authors). Multiple fresh isolated
replays: the core gate (fresh-dark -> honest empty state -> route -> enable -> sourcesEnabled
flips true on reopen) passed clean every time; one early replay attempt also hit the
live-network-dependent tail wait (nyaa search taking longer than a fixed timeout —
explicitly flagged in the scenario's own comments as never a deterministic gate), so that
wait was removed from the gate path entirely (the closing exhibit grab is now best-effort,
no loading-state assertion) — reran clean afterward. Warning gate: the `[W] ... revision of
null` class seen on an earlier diagnostic session is CONFIRMED foreign and pre-existing (11
occurrences reproduced in session `20260821-013744-532e91b6`, captured BEFORE any R1 QML
edit existed) — zero occurrences in the final clean R1 scenario runs, and zero warnings
anywhere referencing `MangaTankobanSourcesPage`/`MangaSeries`/`Extensions`/`nyaa`; remaining
warnings (`QNetworkReplyHttpImpl device not open`, `QRhiGles2 context current`,
`QSqlDatabase requires a QCoreApplication`) are known foreign/boot-timing noise, unrelated
to this slice.

**Full `-L unit` re-sweep after all fixes: 71/71 green** (includes `colosseum.
extensions_first_run`, `colosseum.qttest.tankoban_chapter_migration`,
`colosseum.manga_series_catalogue`), 91.98s total.

**Slice R1 Overall status: Runtime-validated** — the fresh-install dark default, the
picker's honest empty state + route, the enable-via-Extensions path, and the live-rows
result on the next open are all proven by direct live evidence and a passing committed
scenario; unit + harness gates green with negative controls performed; ledgers updated in
the same commit as the code.

## Release 1.1.1 — post-publish hygiene + live verification (2026-08-21)

Colosseum 1.1.1 published: https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.1
(three assets: `Colosseum-1.1.1-setup.exe`, `colosseum-update-v1.json`,
`colosseum-update-v1.json.sig`). This section is post-publish hygiene on the main tree, not
a rebuild of the release itself — the published artifacts, tag, and release worktree are
untouched.

**Stale fixture, found and fixed.** `tests/update_release_client_harness.cpp:276` hardcoded
`User-Agent: Colosseum/1.1.0` in its `require()` assertion — the production literal at
`native/update/UpdateReleaseClient.cpp:107` was correctly bumped to `Colosseum/1.1.1` in
`0630317`, but the matching test fixture was missed in that commit. Bumped the fixture
string to match. Rebuilt `update_release_client_harness` (Ninja, MSVC 2022, Qt's bundled
CMake at `C:/Qt/Tools/CMake_64/bin`), reran `ctest --test-dir native/build-msvc -R
colosseum.update_release_client --output-on-failure` — green (0.90s). Full
`ctest --test-dir native/build-msvc -L unit --output-on-failure`: **71/71 green**, 95.45s
total (zero regressions from the fixture bump; label breakdown unchanged from the R1
baseline above).

**Packaging gap, found and fixed.** `scripts/installer/package_release.sh` — the tracked
packaging script — never staged `data/mal_catalog.db` or `data/tankoban_catalog.db` into
the installer (git history confirms `76cf5b6..1485d8c` never touch it; 7z-inspecting the
shipped 1.1.0 installer confirms zero `data\*.db` entries). That was survivable for 1.1.0
because Tankoban's consumers all had live Jikan/AniList fallbacks. Catalogue-independence
(2026-08-20) removed those fallbacks — `qml/MangaSeries.qml` now names MalCatalog as the
SOLE source of masthead facts and TankobanCatalog as the sole source of the volume shelf —
so an installer without both dbs renders an empty series page and an empty Discover on a
fresh install. The actual 1.1.1 installer that shipped was packaged with a one-off wrapper
outside the repo (`package_release_with_data.sh`, Temp-scoped, so the release worktree
never went dirty) that overlaid the two dbs; this pass ports that overlay INTO the tracked
`package_release.sh` itself (new `[4/7]` step, existence-check-gated before staging starts)
so the next release doesn't need a hand-maintained side wrapper. `comics_catalog.db` /
`imdb_catalog.db` are deliberately not overlaid — those lanes keep their live-fallback
shape. `bash -n` syntax-checked; both source dbs confirmed present at
`data/mal_catalog.db` (41,381,888 bytes) and `data/tankoban_catalog.db` (2,928,640 bytes).
Not run end-to-end (that would repackage a real installer outside this task's scope) — the
existence-check gate and step ordering were verified by inspection against the working
one-off wrapper's already-proven shape.

**Live published-release verification (read-only against GitHub, 2026-08-21).**
- `gh release view v1.1.1 --repo kingoftheseas56/Colosseum --json assets,tagName,name,publishedAt,isDraft,isPrerelease`
  — 3 assets, `isDraft:false`, `isPrerelease:false`, tag `v1.1.1`.
- Downloaded `colosseum-update-v1.json` + `.sig` via `gh release download v1.1.1`.
- Reconstructed the production Ed25519 public key as a DER SPKI blob from the 32 raw bytes
  embedded in `native/update/UpdatePublicKey.h` (`kUpdatePublicKey`) using the standard
  Ed25519 SPKI prefix (`302a300506032b6570032100`); its SHA-256
  (`7bbb3bc13cfdcd20f1c02e94da103c0be70b2ae09346897a1dc203dc660ca3fa`) matched the header's
  own documented "DER SPKI SHA-256" comment exactly, confirming the reconstruction before
  using it to verify anything.
- Ran `scripts/update/verify_update_release.py`'s own `verify_signature()` and `verify()`
  functions (imported directly, not reimplemented) against the downloaded manifest + sig +
  reconstructed public key — **signature valid**. The installer digest step used the
  GitHub-computed asset digest (`gh api`'s per-asset `digest` field —
  `sha256:8bff57afcf60d800ab679b55934c2d2cf224f48c52c979256a29e993ed9bbcc7`, size
  210927498) in place of downloading the 200MB installer, per the task's own
  gh-api-or-download allowance; this exactly matches the manifest's declared
  `installer.sha256`/`installer.size`. Result: `UPDATE_RELEASE_OK`,
  `VERSION=1.1.1`, `INSTALLERBYTES=210927498`,
  `INSTALLERSHA256=8bff57afcf60d800ab679b55934c2d2cf224f48c52c979256a29e993ed9bbcc7`.
- Schema/tag/notesUrl acceptance (inside the same `verify()` call): `schemaVersion==1`,
  `tag=="v1.1.1"`, `notesUrl=="https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.1"`
  — all pass.
- `UpdateReleaseClient`'s own acceptance rules (read directly from
  `native/update/UpdateReleaseClient.cpp`, not re-derived): rejects on `draft||prerelease`
  (line ~230-234) — both false on the live release, accepted; expects asset names
  `colosseum-update-v1.json` (`kManifestAsset`) and `colosseum-update-v1.json.sig`
  (`kSignatureAsset`) — both present; the harness's own `"exact manifest/signature/installer
  assets selected"` contract (`assetUrls.size()==3`) matches the live release's exact asset
  count (3).

**Release-tooling artwork staleness — confirmed, NOT fixed (out of scope, not this lane's
file).** `tests/update_release_tooling_test.py` has 6 unittest cases; against the CURRENT
working tree (which carries a foreign uncommitted WIP fix — a dirty
`release/presentation/1.1.0.json` plus an untracked `release/presentation/artwork/` dir
with all 5 PNGs) all 6 pass. Ground-truthed the COMMITTED baseline separately (temporarily
swapped in `git show HEAD:release/presentation/1.1.0.json` and moved the artwork dir aside,
reran, then restored both exactly — `diff` confirmed byte-identical restoration): against
HEAD, **exactly 3 of 6 fail** —
`test_generates_signed_bundle_with_five_verified_artwork`,
`test_rejects_highlight_referencing_missing_artwork`, and
`test_real_presentation_hashes_all_five_artwork_and_rejects_missing_reference` — all three
because the committed `1.1.0.json` still has `"artwork": []` and highlights with no
`artwork_assets`, while the tests expect the 5-image set. This is a 1.1.0-era gap (the
presentation file was never finished/committed for its own release), unrelated to the
1.1.1 fixture/packaging fixes above and outside this pass's ownership — named here per
instruction, left for its owner to finish and commit.

**Commands run, for the record:** `ninja -v update_release_client_harness` (via vcvars64 +
Qt's bundled Ninja/cl.exe), `ctest --test-dir native/build-msvc -R
colosseum.update_release_client --output-on-failure`, `ctest --test-dir native/build-msvc
-L unit --output-on-failure`, `gh release view v1.1.1 --repo kingoftheseas56/Colosseum
--json ...`, `gh release download v1.1.1 --repo kingoftheseas56/Colosseum --pattern
"colosseum-update-v1.json" --pattern "colosseum-update-v1.json.sig"`, `python -m unittest
tests.update_release_tooling_test -v` (both against the dirty working tree and, separately,
the restored committed baseline).

## Tankoban series volume-flow (arc-08 v2.3 adoption, 2026-08-21)

Adopted Preflight arc-08's Hemanth-approved ("perfect", 2026-08-20) horizontal Pages/Flow
volume continuum into `qml/MangaTankobanLibrary.qml` (vertical `GridView` shelf ->
horizontal `ListView` flow) and `qml/MangaReadingRoom.qml` (un-boxed masthead, 2-line
synopsis with a glass MORE/LESS chip). The arc's own candidates were re-derived against a
`reference/baseline/` that predated the LANDED catalogue-independence Slices 3-5 + R1 (the
gate this arc was waiting on, per its own `STATUS.md` "Adoption risks" #1) — the transplant
reconciled against the LIVE tree at adoption time, not the stale candidates verbatim:

- `coverFor()` is the LIVE simple ladder (catalogue `row.cover` -> `localPages()` first page
  when ready -> NO COVER), not the arc candidate's WC-thumb/`CuratedVolumeCovers.js` ladder
  — that machinery was already fully deleted by Slice 3 before this adoption landed.
  `chapters`, `curatedCovers`, `requestCovers`/`visibleRowsForCovers`/`visibleGridRows`/
  `_firstChapterIdIn`/`_thumbWanted`/`coverByVolume` do not exist in the adopted file.
- The delegate's automation name is `tankobanVolumeCard_<token>` (Slice 3/4's naming law,
  what the committed Lanista scenarios click), not the arc candidate's bare `volumeFlowTile`.
- `tankobanShelfState` (rowCount/coveredCount bridge scalars) is preserved unchanged.
- The Select-mode header toggle (`volumeSelectToggle`/`volumeDownloadNextAction`, "Hemanth
  greenlit KEEP-IT" 2026-08-14) is live, separately-approved work the arc candidate was never
  briefed against — transplanted onto the new flow's lane header rather than dropped.
- `MangaReadingRoom.qml` keeps the LIVE three-way `primaryAction`/`continueText` truth table
  (`open`/`get`/`search`, catalogue-independence Slice 4) instead of the arc candidate's stale
  two-way stand-in. The masthead's one contextual action now follows the shelf's own truth:
  a shelved series (`library.showVolumes` true) carries no masthead CTA — the flow's own
  action bar (Get/Read/Retry/percent) is the one contextual action; a shelf-less series
  (`primaryAction === "search"`, no known catalogue count) reserves no action bar at all
  (`actionBarHeight === 0`), so the masthead keeps `tankobanSeriesPrimaryAction` as the only
  way to reach `primaryRequested()`. This split matches the committed
  `tankoban-catalogue-smoke` scenario exactly: it presses `tankobanVolumeCard_1` for the
  shelved series (One Piece) and `tankobanSeriesPrimaryAction` for the shelf-less one
  (Berserk). `chapters`/`openChapterRequested`/`chapterDownloadRequested` do not exist on
  `MangaReadingRoom` at all — Slice 5 had already deleted them before this adoption.
- Fixed one real defect found only by running the harness: the arc candidate's synopsis text
  used `font.pixelSize: 13.5` (a real, not int) — `qml.exe` throws
  `Invalid property assignment: int expected` at construction. Corrected to `14`.

`tests/manga_reading_room_harness.qml` updated for the flow shape: grid-shape assertions
(`GridView` currentIndex, `stateWordFor`'s pre-v2.3 caption-word vocabulary) replaced with
flow-shape ones (`ListView` `flowCurrentIndex`/`liveVolumeTiles`/`bookHeight` — all read
inside a `Qt.callLater`, matching the flow's own deferred `centreFlow()`, since checking
synchronously in the same call stack as construction races the deferred
`positionViewAtIndex` and fails vacuously; `volumeNameFor`/`stateLineFor`'s v2.3 caption
vocabulary; a new assertion that the masthead's `tankobanSeriesPrimaryAction` button is
hidden for a shelved series and shown for a shelf-less one). Every zero-chapter/cover/
fetchThumb-never assertion is unchanged. Negative controls: the pre-existing fixed-height
and coveredCount ones, plus a new one on the flow's own centring
(`flowCurrentIndex === focusIndex + 1` deliberately wrong -> throws -> restored). Green:
`qml.exe -platform offscreen tests/manga_reading_room_harness.qml` -> `MANGA_READING_ROOM_OK`.

New gate `tests/manga_volume_flow_harness.qml` (ported from the arc package's own harness of
the same name, with the non-existent `chapters` construct property dropped) pins: resume
centring onto volume 7, the dynamic 190-276px never-cropped cover clamp
(`maxScaledVolumeHeight <= flowViewportHeight`), virtualization (`liveVolumeTiles` bounded),
Get/Read/Retry/percent state vocabulary, the name-caption redundant-collapse rule,
PageUp/PageDown/Home/End 10-volume jumps, first-click-centers/second-click-acts, and the
zero-volume defensive floor (`actionBarHeight === 0`). Registered
`colosseum.manga_volume_flow` (label `unit`) in `tests/CMakeLists.txt`, same
qml.exe/offscreen/powershell-wrapper shape as `colosseum.manga_reading_room`. Green:
`powershell -File tests/test_manga_volume_flow.ps1` -> `Tankoban series volume flow: OK`.

`qmllint` on both adopted files: exit 0, only the pre-existing class of inherited
context-property warnings (`TankobanVolumes`/`Progress`/`Downloads`/`WindowMode`/`Collection`
singletons, `LibraryButton` — resolved only inside the real app).

**Not run this pass:** `ctest -L unit` (the shared `native/build-msvc` out/ directory was
under an active concurrent build — `.ninja_lock` held, `cl.exe`/`ninja.exe` running — at
verification time; touching it would violate the one-build-per-out-dir rule and risk an OOM
collision on this RAM-constrained machine). The two harnesses above were run directly via
`qml.exe -platform offscreen`, which does not touch `build-msvc` and needed no rebuild (a
QML-only change runs against the existing app exe/tooling). `ctest -L unit` registration and
the full 71/72 (new count) regression pass are still owed once the concurrent build clears —
tracked as open verification debt, not claimed here.

**Native gate closed (runtime-verification pass, 2026-08-21).** The concurrent build cleared
(`.ninja_lock` released, `colosseum.exe` rebuilt by its holder ~14:59); claimed a short window
(`agents/chat.md`, posted+released), confirmed both new/changed registrations already at HEAD
(`colosseum.manga_reading_room` #44, `colosseum.manga_volume_flow` #46 — no configure needed,
both were already wired by ffd1eaa), then ran the full gate cleanly against the existing build:
`ctest --test-dir native/build-msvc -L unit --output-on-failure` → **100% tests passed, 72/72**
(`colosseum.manga_reading_room` 1.21s, `colosseum.manga_volume_flow` 1.01s). Full log basis:
this session's own ctest run, no other test touched. This closes the debt this entry left open
above — the 71/72 (new count) regression pass is done, not owed.

## Data-vault adoption Slice 1 — CatalogVaultClient (2026-08-22)

New class: `native/engine/CatalogVaultClient.{h,cpp}` — a QObject service that keeps the four
Colosseum-Data catalogue dbs (`mal_catalog.db`, `tankoban_catalog.db`, `comics_catalog.db`,
`imdb_catalog.db`) fresh in AppData from the public `kingoftheseas56/Colosseum-Data` GitHub
release (follows on from Hemanth's own slice-0 commit `8799772`, which extended
`publish_release.py`/`pull_data.py` to all four assets). `checkAndFetch()` throttles to zero
network when `state.json`'s `fetchedAt` is under 24h old and all four files are present;
otherwise it fetches `GET /releases/latest`, downloads only what's missing or tag-changed
(serial, one asset at a time — this machine is RAM/IO constrained), and lands each file via a
temp-then-rename swap. The rename is gated by a synchronous `aboutToReplace(name)` signal fired
before any pre-existing target is touched — the live-swap hook the next slice's hot-reload will
use to close a SQLite handle before Windows refuses to rename over an open file.

New harness: `tests/catalog_vault_client_harness.cpp`, registered as
`colosseum.catalog_vault_client_harness` (bare C++ harness, same shape as
`colosseum.update_release_client_harness` — no `.ps1` wrapper). A local `QTcpServer` fixture
stands in for the GitHub release API and asset downloads; the harness never touches the live
network. Six cases, all PASS + sentinel `CATALOG_VAULT_CLIENT_OK`:

- **(a) empty vault** — all four databases downloaded, bytes match the fixture, `state.json`
  carries the fetched tag, `allFresh` emitted, `fetching` flips true→false exactly once each.
- **(b) fresh state + files present** — zero network requests (request counter stays 0),
  `allFresh` emitted from cache, `fetching` never toggles.
- **(c) new upstream tag past the 24h throttle** — all four re-downloaded; `aboutToReplace(name)`
  fires before each pre-existing target's replacement (checked per-name, by event order); no
  `.downloading` residue left behind.
- **(d) manifest unreachable + full local cache** — no `fetchFailed`, cached files byte-for-byte
  untouched (the documented silent cache-keep path).
- **(e) manifest unreachable + empty vault** — `fetchFailed("manifest", ...)` emitted, no
  `state.json` written.
- **(f) truncated download mid-stream** — the truncated target is never landed, its
  `.downloading` temp is cleaned up, `fetchFailed` emitted for that asset, `state.json` left
  unwritten for the pass.

**Negative control performed:** flipped case (b)'s assertion to
`require(server.requestCount != 0, ...)`, rebuilt (`build-target.bat
catalog_vault_client_harness`, zero `error C`/`ninja: build stopped`), reran the harness —
exactly `FAIL: (b) NEGATIVE CONTROL — expect nonzero request count` red, case (a) still green
before it, cases (c)–(f) never reached (harness exits on first FAIL). Restored the real
assertion, rebuilt, reran — all six PASS + sentinel again.

**Full gate:** `ctest --test-dir native/build-msvc -L unit --output-on-failure` →
**72/73 clean, 1 flake** (`colosseum.qttest.profile_activity_isolation`, a known pre-existing
flake per this ledger's own prior entries — reran it alone via `ctest -R
profile_activity_isolation`, green in isolation, confirming it is unrelated to this change).
`colosseum.catalog_vault_client_harness` itself: green, 0.92s. Baseline was 72; this
registration brings the total to 73.

Build-slot note: this slice's CMakeLists.txt edits collided once with a concurrent Agent 0
sub-exec pass that stashed/rebuilt/popped the same 6-file tracked WIP set mid-edit — the
`native/CMakeLists.txt` hunk was swept out during that race and had to be re-applied before
building (`agents/chat.md` carries both the original and resume claims). No foreign hunks in
either CMakeLists.txt were touched by this slice's own edits, verified via `git diff` on each
file immediately before commit.

## Data-vault adoption Slice 2 — catalog reopen + main.cpp vault wiring (2026-08-22)

**ready/reopen()/closeForSwap() contract, all four catalogue seams.** `MalCatalog`,
`TankobanCatalog`, `ImdbCatalog`, and `ComicsCatalog` each gained `Q_PROPERTY(bool ready READ
ready NOTIFY readyChanged)`, `Q_INVOKABLE bool reopen(const QString& dbPath)`, and
`Q_INVOKABLE void closeForSwap()`. Each constructor's inline open logic was refactored into a
shared private `openAt(path)` helper (byte-identical behavior — same dev-path ladder, same
`QSQLITE_OPEN_READONLY` connect options) so the constructor and `reopen()` share one code path.
`reopen()` closes any existing connection (`QSqlDatabase::close()` + `removeDatabase()`, object
out of scope first — the standard Qt pattern, now guarded everywhere with `contains()` before
`removeDatabase()`, including the destructors), reopens at the new path, and emits
`readyChanged()` when the ready state flips or a reopen while already-ready succeeds (a
conservative superset of "flipped OR path changed" — harmless extra signal, no missed one).
`closeForSwap()` closes the connection, forces `ready()` false, and emits `readyChanged()` if it
was previously ready — this is what `CatalogVaultClient::aboutToReplace`'s slot calls so Windows
can rename over the file. `ComicsCatalog` keeps its per-instance connection name and its lack of
a `../../` dev fallback ladder (both pre-existing, unchanged); the other three keep MalCatalog's
exact ladder.

**CatalogVaultClient gained `setManagedNames(QStringList)`** (Q_INVOKABLE, filters against the
existing `knownAssets()` list, unknown names ignored) plus an internal `m_managedNamesSet` flag
so "never called" (manage all four, Slice-1 default) is distinguishable from "called with an
empty list" (manage nothing — `checkAndFetch()` now short-circuits to `emit
allFresh(m_currentTag)` with zero network in that case, since main.cpp calls this whenever every
catalog resolved via its dev override).

**main.cpp wiring.** A `resolveCatalogPath` lambda, run once per catalog before construction,
tries the existing relative dev path then the `applicationDirPath()/../../` ladder (MalCatalog's
constructor logic, now lifted to the caller) and falls back to
`QStandardPaths::AppDataLocation + "/catalog-vault/" + assetName` only if neither dev path
exists — so a dev machine with all four `data/*.db` files present is byte-identical to before
this slice. The four catalogs construct at their resolved paths; `CatalogVaultClient` constructs
with the existing `updateNam` (line ~602, already alive at this point in boot) and the AppData
vault directory, then `setManagedNames()` is called with only the non-dev-overridden asset
names. `aboutToReplace(name)`/`databaseUpdated(name, path)` are connected by name to the matching
catalog's `closeForSwap()`/`reopen(path)`. `CatalogVault` is exposed as a QML context property
(for Slice 3). A `QTimer::singleShot(0, ...)` kicks `checkAndFetch()` once, off the cold-launch
critical path.

**Build-graph gap found and fixed:** Slice 1 registered `CatalogVaultClient.{h,cpp}` only in the
`catalog_vault_client_harness` target's own source list — the `colosseum` app target's source
list (`native/CMakeLists.txt`, the `add_executable(colosseum ...)` block) never got the file, so
the harness and unit gate were green while the real app would not link. Caught by this slice's
own app relink (`LNK2019: unresolved external symbol CatalogVaultClient::...` x6, `LNK1120`
fatal). Fixed by adding `engine/CatalogVaultClient.cpp`/`.h` next to the other catalog entries in
that block; the app then linked clean.

**Harness coverage.** `tests/mal_catalog_rows_harness.cpp` gained a "reopen-contract" case
(documented as standing in for all four seams, since TankobanCatalog/ComicsCatalog/ImdbCatalog
share the identical `openAt`/`reopen`/`closeForSwap` shape by inspection): construct at a missing
path → not ready; `reopen(realFixture)` → ready true, `readyChanged` fired exactly once, a real
query (`mangaById`) answers correctly; `closeForSwap()` → ready false, `readyChanged` fired again,
queries return empty; `reopen()` again → recovers, `readyChanged` fires a third time, queries
answer again. `tests/catalog_vault_client_harness.cpp` gained two cases for `setManagedNames`:
**(g)** a 2-of-4 managed subset downloads only those two names (request-and-file-presence
checked for all four); **(h)** an explicitly empty managed set makes zero network requests and
emits `allFresh` immediately.

Both harnesses: all cases **PASS + sentinel**
(`PASS MalCatalog ready/reopen()/closeForSwap() vault-reopen contract` /
`CATALOG_VAULT_CLIENT_OK`, 8 cases (a)-(h) in the vault-client harness, all PASS).

**Negative control performed:** flipped `mal_catalog_rows_harness.cpp`'s post-reopen
`require(swapCat.ready(), ...)` to `require(!swapCat.ready(), ...)`, rebuilt — exactly
`FAIL: reopen-contract: ready() reflects the reopen` red, nothing else. Restored, rebuilt — all
three PASS lines again (`animeCatalog`, `mangaById`, and the new reopen-contract line).

**Full gate:** `ctest --test-dir native/build-msvc -L unit --output-on-failure` →
**100% tests passed, 73/73** (the previously-flaky `profile_activity_isolation` ran clean this
pass — no rerun needed).

**App relink:** `colosseum` target rebuilt clean after the CMakeLists.txt fix above
(`[4/4] Linking CXX executable colosseum.exe`, no errors).

**Dev-override status on this machine:** all four `data/*.db` files are present under the repo
root, so all four catalogs resolved via the dev ladder (`devOverridden == true` for all) and
`CatalogVaultClient::setManagedNames({})` was called — `checkAndFetch()`'s post-boot kick is a
zero-network no-op here, matching the comment left in `main.cpp`.

Evidence logs (gitignored, left on disk): `Colosseum/artifacts/data-vault/slice2/` — build logs
for both harnesses and the app relink, both harness run logs, the negative-control red + restored
logs, and the full `ctest` log.
