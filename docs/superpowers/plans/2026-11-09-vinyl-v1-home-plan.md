# Vinyl v1 Home — implementation plan

**Spec:** `docs/superpowers/specs/2026-11-09-vinyl-v1-home-design.md` (approved by Hemanth 2026-11-09).
**Planner:** Agent 0 (Claude), under `brotherhood-writing-plans`. **Executor:** a fresh session or another brother under `brotherhood-executing-plans`. This plan stands alone.
**Branch model:** Rule 28. Work on a branch `vinyl/v1-home` cut from `origin/master`. Merge endpoint, declared now: everything here merges into `master` once Slice 7 is Runtime-validated and Hemanth has signed off the looks. Vinyl stays behind `COLOSSEUM_VINYL_IN_APP` until then. Releases ship in sync (desktop + Android), so this lands for the next shared version, not ad hoc.
**CI budget:** at most 2 GitHub runs per slice. Iterate with local MSVC builds on Laptop 1.

## Ground truth the plan is written against (checked 2026-11-09)

- **The real Vinyl is not on master.** It is in Codex snapshot `bb346e35a` (GitHub `archive/vinyl-discover-lastfm-jiosaavn-20261107`): 8 commits on a base that is 260 commits behind `origin/master`, touching 114 files (+12,349 / −2,068). It carries the recommendation engine, the Last.fm + JioSaavn Discover, the Artist page, the five widgets (`qml/vinyl/Vinyl*Widget.qml`) and a large Vinyl test estate:
  - `tests/qml/tst_vinyl_*.qml` (36 files);
  - `tests/vinyl-*` suites;
  - `tests/vinyl_*_test.cpp|py|mjs`;
  - `tests/lanista_scenarios/vinyl_{home,discover,album,song}_journey.json`.
- **Neither ledger knows Vinyl.**
  - `docs/colosseum-test-verification.md` has no Vinyl entries.
  - `docs/colosseum-lanista-verification.md` only lists `modePill_Vinyl`.
  - The Colosseum Harness map has no Vinyl domain (`inspect vinyl` → `INSPECT_NOT_FOUND`).
  - So no Vinyl test or scenario may be cited as existing until Slice 0 registers it.
- **Bridge facts used here, all from the ledger's AVAILABLE section:**
  - `lanista session run <scenario.json> --tag <t> --drive [--seed <dir>]` (disposable tagged app, unique pipe, isolation proven per run);
  - the commands `ping`, `get-state`, `qml-get`, `ui-query`, `ui-click`, `ui-scroll`, `ui-wait-for` (strict equality, the only wait) and `log-mark`;
  - window grabs via `"grab":{"target":"window"}`;
  - `lanista brief <arc>` for the eyes-on gallery.
- **Bridge facts NOT available (plan around them):**
  - typed event waits;
  - absence assertions;
  - secondary windows;
  - pixel sharpness verdicts;
  - interactive sessions.
- **Fixture build:** `COLOSSEUM_ENABLE_VINYL_TEST_FIXTURES` (snapshot `native/CMakeLists.txt`) plus `native/devtools/VinylTestProfileController.h` and `tests/vinyl-live/fake_provider.py` give a no-network Vinyl. Every runtime session below uses it. Live network never enters a deterministic gate.

## Slice order

0 Foundation (internal + registration) → 1 Shell and tabs → 2 Source pool and the four rules (internal) → 3 Square widgets → 4 Carousel → 5 The Crate and The Spin → 6 Home tab rows and cold start → 7 Library.

---

### Slice 0: The real Vinyl lands on the branch, and its tests become citable
Purpose: Vinyl on the branch is the snapshot's Vinyl (engine, Discover, Artist page, widgets), building and passing its own tests on current master. Nothing visible changes yet beyond what the snapshot already does.
Dependencies: none.
Implementation guidance:
- Cut `vinyl/v1-home` from `origin/master`.
- Cherry-pick or merge the 8 snapshot commits (`git log $(git merge-base bb346e35a origin/master)..bb346e35a`); resolve conflicts in favour of current master for non-Vinyl files.
- Register the snapshot's deterministic Vinyl tests under CTest label `unit`. Register first, convert nothing.
- Add a "Vinyl" section to `docs/colosseum-test-verification.md` listing each test as registered / bespoke / probe.
- Add a Vinyl domain to the harness intelligence map (`tools/colosseum-harness/intelligence/colosseum-map.json`).
- Audit every Vinyl store for `COLOSSEUM_APPDATA_TAG` isolation, following the ledger's rule: no hardcoded `QSettings("Brotherhood","Colosseum")`.
Behavior to preserve: every non-Vinyl CTest that is green on `origin/master` stays green; the Vinyl gate option still compiles Vinyl out.
Baseline: record `ctest --test-dir native/build-msvc -L unit --output-on-failure` totals on `origin/master` before the merge.
Focused tests:
  - Qt Test: snapshot `tests/vinyl_recommendation_service_test.cpp`, `vinyl_recommendation_history_test.cpp`, `vinyl_home_listening_test.cpp`, `vinyl_recommendation_contracts.cpp`, `tests/vinyl-catalogue/tst_catalogue_source.cpp`, `tests/auto/account/tst_vinyl_*`. Register each; record pass/fail honestly.
  - Qt Quick Test: snapshot `tests/qml/tst_vinyl_picker_widgets.qml`, `tst_vinyl_widget_integration.qml`, `tst_vinyl_home_page.qml`, `tst_vinyl_world_navigation.qml`, `tst_vinyl_discover_page.qml`.
  - Existing harnesses: `tests/vinyl-live/*` stays bespoke; list it in the ledger as bespoke and do not convert.
  - Negative control: break one assertion in `vinyl_recommendation_service_test.cpp` locally and confirm CTest goes red, then revert.
Test seam status: migration required (this slice performs the registration).
Lanista actions: not applicable (no new visible behavior).
Completion signal: CTest exit code.
State / events / probes: not applicable.
Visual evidence: not applicable.
Regression paths: full `-L unit` suite; app boots to Home with Vinyl compiled in and with it compiled out.
Evidence artifacts: CTest output saved under `artifacts/vinyl-v1/slice0/`; the updated ledger diff.
Bridge status: not applicable.
Completion criterion: branch builds on MSVC; all registered Vinyl tests pass (or each failure is listed with cause and owner); non-Vinyl totals equal the baseline; ledger and harness map updated; store-isolation audit recorded.

### Slice 1: Vinyl lives on the house shell with three tabs
Purpose: opening Vinyl shows the house WorldPage board with the tab bar Home · Discover · Library. Home opens first, and the old Vinyl shell is gone.
Dependencies: Slice 0.
Implementation guidance:
- Host Vinyl through `qml/WorldPage.qml` and the house tab bar (`WorldTabBar.qml`), as `TheatreWorld.qml` does.
- Retire `qml/vinyl/VinylLiveWorld.qml` / `VinylWorld.qml` from the route.
- Discover reuses the existing Discover page unchanged. Home and Library start as placeholders that the later slices fill.
- Give new surfaces objectNames: `vinylWorld` (with property `ready`), `vinylTab_Home`, `vinylTab_Discover`, `vinylTab_Library`, `vinylWorld.activeTab` (string).
Behavior to preserve: Theatre, Tankoban and Biblio worlds; `modePill_Vinyl`; the player bar; Discover content; the song, album and artist pages open from Discover.
Baseline: a `lanista session run` grab of today's Vinyl entry on the branch after Slice 0.
Focused tests:
  - Qt Test: not applicable (QML-only routing).
  - Qt Quick Test: new `tst_vinyl_shell_tabs.qml`. Three tabs in order, Home default, switching sets `activeTab`, old shell type not instantiated.
  - Existing harnesses: `tst_vinyl_world_navigation.qml` stays green or is updated with a reason.
  - Negative control: reorder the tab model and confirm the order test fails.
Test seam status: available after Slice 0.
Lanista actions: scenario `tests/lanista_scenarios/vinyl_v1_shell.json` run with `lanista session run ... --tag vinylv1 --drive` on the fixture build:
  - `ui-click modePill_Vinyl`;
  - `ui-wait-for vinylWorld.ready == true`;
  - `ui-click vinylTab_Library`, then `vinylTab_Discover`, then `vinylTab_Home`.
Completion signal: `ui-wait-for vinylWorld.activeTab == "Library"` (and likewise for the other tabs).
State / events / probes: `qml-get vinylWorld activeTab`; `get-state` shows the tagged roots.
Visual evidence: a window grab on each tab.
Regression paths: switch to Theatre and back to Vinyl; restart the session and confirm Home is the default.
Evidence artifacts: `artifacts/lanista-sessions/<id>/` manifest and grabs.
Bridge status: available.
Completion criterion: Runtime-validated. The scenario passes, the Qt Quick test passes, and the grabs show the house board.

### Slice 2: One merged source pool that always fills the rows (internal)
Purpose: (no direct visible change) every Vinyl surface draws from one de-duplicated pool built under the spec's four rules.
Dependencies: Slice 0.
Implementation guidance:
- Extend the snapshot's catalogue/recommendation services (`VinylRecommendationService`, `VinylRecommendationHome.cpp`, the catalogue source) with:
  - (a) MusicBrainz alias resolution, native script included, merged per artist;
  - (b) ListenBrainz similar-artists (`labs.api.listenbrainz.org/similar-artists/json`) and top-recordings-by-MBID, no token;
  - (c) Apple chart feed client (`rss.marketingtools.apple.com/api/v2/<cc>/music/most-played/<n>/{albums,songs}.json`) with rank-weighted multi-storefront merge;
  - (d) Last.fm `chart.getTopTracks/getTopArtists`;
  - (e) MusicBrainz release-group dates for "new" (12 months);
  - (f) mixes from YouTube radio plus similar artists' songs, never `track.getSimilar` alone.
- Everything goes behind the existing transport seam so tests use fixtures (`tests/vinyl-catalogue/fake_transport.py`, `tests/vinyl-live/fake_provider.py` patterns).
- The Last.fm key is read from config at runtime and never committed.
Behavior to preserve: existing ranker, feedback (like, dislike, hide, snooze, exclude artist), exposure rotation, profile-scoped history; Discover's current results.
Baseline: run the snapshot's recommendation tests and record the current candidate counts per row from the fixture profile.
Focused tests:
  - Qt Test: new `tst_vinyl_source_pool.cpp` with recorded fixtures (including a 竹内まりや alias fixture), asserting:
    - alias merge returns similar artists for the English-name query;
    - de-duplication across sources;
    - chart merge order;
    - "new" window;
    - New-from-your-artists widening to 10 nearest similar artists when under-filled;
    - Rediscover empty without history.
  - Qt Quick Test: not applicable (no QML in this slice).
  - Existing harnesses: `vinyl_recommendation_contracts.cpp` stays green.
  - Negative control: disable alias merging and confirm the English-name fixture test fails.
Test seam status: available (fake transport exists in the snapshot).
Lanista actions: not applicable.
Completion signal: CTest exit code.
State / events / probes: not applicable.
Visual evidence: not applicable.
Regression paths: Discover tests (`tst_vinyl_discover_page.qml`, `tests/vinyl-live/test_discovery_contract.py` as bespoke evidence).
Evidence artifacts: CTest output. Optionally rerun a non-gating live probe with the 5-seed set (Mariya Takeuchi, Björk, Ilaiyaraaja, Daft Punk, SZA) and attach the counts.
Bridge status: not applicable.
Completion criterion: all new and existing pool tests green under `-L unit`. The optional live probe shows each of the six day-one rows at or above one screen-width of items (≥ 8).

### Slice 3: The five widgets at one square size
Purpose: song, album, artist, mix and station widgets all render at the same square size with aligned captions, each still recognisable at a glance.
Dependencies: Slice 0.
Implementation guidance:
- Edit `qml/vinyl/Vinyl{Song,Album,Artist,Mix,Radio}Widget.qml` to the spec §6 forms:
  - song: square jukebox, title strip over the cover;
  - album: unchanged jewel case;
  - artist: record only, no glass frame, name moved to the host caption;
  - mix: J-card behind the cassette;
  - radio: square set.
- Visual reference: Brotherhood `agents/vinyl-home-mock/index.html` functions `wSongSq` / `wAlbum` / `wArtistSq` / `wMixSq` / `wRadioSq` (commit `9f2b302e`). Port the QML measurements; do not redraw from memory.
- Update `VinylItemCard.qml` so every type shows the same two caption lines.
Behavior to preserve: feedback badges (like/hide) on cards; widgets' placeholder art (artist initials, case backing); song/album/artist page opening from a card.
Baseline: Qt Quick grab of each widget at 130 px from the snapshot; record their implicit heights (today they differ).
Focused tests:
  - Qt Quick Test: update `tst_vinyl_picker_widgets.qml` / `tst_vinyl_widget_integration.qml`:
    - all five `implicitHeight == implicitWidth` at widths 130 and 196;
    - artist widget has no frame child;
    - song title text lives inside the widget, so the host hides its own title;
    - every card's caption block has the same y.
  - Qt Test: not applicable (presentation only).
  - Existing harnesses: none.
  - Negative control: restore the old song `implicitHeight` formula and confirm the square test fails.
Test seam status: available after Slice 0.
Lanista actions: `human-witnessed:` open the fixture build, Vinyl → Home, and look at one row of each widget type at 1280x700. Hemanth judges recognisability and taste. A bridge run is not ordered; this is a look.
Completion signal: Qt Quick Test exit code; Hemanth's recorded verdict.
State / events / probes: not applicable.
Visual evidence: a Qt Quick grab sheet of the five widgets at 130 px and 196 px, plus the eyes-on gallery via `lanista brief vinyl-v1`.
Regression paths: Discover cards, artist page discography rows.
Evidence artifacts: `artifacts/vinyl-v1/slice3/`.
Bridge status: available (human-witnessed look per the 2026-08-06 ratification; geometry is proven by the Qt Quick layer).
Completion criterion: square tests green, and Hemanth's "looks right" recorded in the evidence.

### Slice 4: The hero carousel
Purpose: the top of Vinyl shows four slides (#1 album, #1 song, #1 artist this week, then For you), each with its list beside it.
Dependencies: Slices 1, 2, 3.
Implementation guidance:
- Extend the house `FeaturedCarousel.qml` (or a Vinyl delegate inside it) with a side-list slot.
- Slide data comes from the Slice 2 pool (charts, For you pick with reason).
- About 7 s per slide, paused while hovered; blurred-artwork backdrop.
- Actions per spec §3. "Not for me" calls the existing hide feedback and advances to a new pick.
- Chart failure: keep the last good week; skip the slide if there's no cache.
- objectNames: `vinylHero` (with property `slideIndex` and `slideKind`), `vinylHeroDot_<n>`, `vinylHeroNotForMe`, `vinylHeroSideRow_<n>`.
Behavior to preserve: Theatre's carousel behaviour (if the house component is extended, Theatre's grabs must not change).
Baseline: the Slice 1 grab (no carousel).
Focused tests:
  - Qt Test: chart-cache fallback and skip logic in `tst_vinyl_source_pool.cpp` (fixture with a failing feed).
  - Qt Quick Test: new `tst_vinyl_hero.qml`:
    - exactly 4 slides in the spec's order;
    - each has 4 side rows (For you rows carry a type label);
    - no resume slide;
    - dot click sets `slideIndex`;
    - "Not for me" emits hide and changes the For you item.
  - Existing harnesses: any Theatre carousel Qt Quick test stays green.
  - Negative control: add a fifth slide and confirm the count test fails.
Test seam status: available after Slice 0.
Lanista actions (scenario `vinyl_v1_hero.json`, fixture build):
  - `ui-click vinylHeroDot_2`;
  - `ui-wait-for vinylHero.slideIndex == 2`;
  - `qml-get vinylHero slideKind` (expect `topArtist`);
  - `ui-click vinylHeroDot_3`, then `ui-click vinylHeroNotForMe`.
Completion signal: `ui-wait-for vinylHero.slideIndex == <n>` per step.
State / events / probes: `qml-get vinylHero slideKind`; For you item id before and after "Not for me" must differ.
Visual evidence: a window grab per slide at 1280x700.
Regression paths: Theatre home carousel; app restart keeps the cached chart week.
Evidence artifacts: session manifest and grabs.
Bridge status: available.
Completion criterion: Runtime-validated, plus Hemanth's look at the four grabs.

### Slice 5: The Crate and The Spin
Purpose: below the carousel, The Crate shows 16 square tiles of what you return to, and The Spin shows picks for right now with a Songs · Albums · Artists switch.
Dependencies: Slices 2, 3, 4.
Implementation guidance:
- The Crate: 2 rows × 8 at house card size, with paging. Balanced selection counts listening at album and artist level as well as song level.
- Pin and unpin persist in a tag-safe store.
- Album tiles resume at the last track (caption "Resume · track N of M").
- Artist tile click starts artist radio; a hover control opens the artist page.
- The Spin: switch `vinylSpinSwitch` (property `view`: songs / albums / artists); songs are a 4×3 paged grid; albums and artists are widget rows with reason lines.
- objectNames: `vinylCrate` (property `count`), `vinylCrateTile_<n>` (with properties `kind`, `pinned`, `resumeTrack`), `vinylCrateTileOpen_<n>`, `vinylSpinSwitch_<songs|albums|artists>`.
- Cold start: The Crate shows the optional taste card (`vinylCrateTasteCard`), and The Spin uses chart picks.
Behavior to preserve: player bar, queue, existing Continue/restore session behaviour (`tests/vinyl-native-session-bridge/tst_continue_restore.qml`).
Baseline: the Slice 4 grab (carousel only).
Focused tests:
  - Qt Test: crate balancing and resume-position mapping (new cases in the recommendation/home-listening tests); pin store routes through the tag (store-isolation pattern).
  - Qt Quick Test: new `tst_vinyl_crate_spin.qml`:
    - 16 tiles;
    - an album tile with history shows the resume caption;
    - the switch changes `view` without changing page scroll;
    - the taste card appears only with an empty profile.
  - Existing harnesses: `tst_continue_restore.qml` stays green.
  - Negative control: drop album-level counting and confirm the balance test fails.
Test seam status: available after Slice 0.
Lanista actions: scenario `vinyl_v1_crate.json` with `--seed` of a fixture profile that has history:
  - `ui-click vinylCrateTile_0` (an album);
  - `ui-wait-for` the player's track index to equal the fixture's resume track (use the player's existing named property recorded in Slice 0's ledger entry; if none exists, this step is Bridge blocked and Slice 0 must add a named read surface);
  - `ui-click vinylSpinSwitch_albums`;
  - `ui-wait-for vinylSpinSwitch.view == "albums"`;
  - pin a tile, restart the session with the same seed, and `qml-get vinylCrateTile_<n> pinned`.
Completion signal: the `ui-wait-for` equalities above.
State / events / probes: `qml-get vinylCrate count` (16); `kind` mix across tiles includes album, artist and song.
Visual evidence: window grabs of the first screen at 1280x700 (carousel and The Crate fully visible, The Spin's header visible) and after scrolling one notch.
Regression paths: scroll away and back; open an album from a tile and press back; restart.
Evidence artifacts: session manifest and grabs.
Bridge status: available, except the resume-track wait, which is conditional on a named player property (see above).
Completion criterion: Runtime-validated; spec acceptance criteria 2, 4 and 5 met.

### Slice 6: Home tab rows and cold start
Purpose: the Home tab shows the seven rows (Rediscover only with history), each full, with feedback badges; a brand-new profile is never blocked.
Dependencies: Slices 2, 3, 5.
Implementation guidance:
- Home rows in spec order:
  1. Mixes & radio;
  2. New from your artists;
  3. Albums for you (large editorial cards);
  4. Because you played…;
  5. Deep cuts;
  6. Rediscover;
  7. Artists like the ones you love.
- A row under one screen-width of items is hidden.
- objectNames: `vinylHomeRow_<key>` (with property `itemCount`), `vinylHome` (property `rowKeys`, joined string).
Behavior to preserve: snapshot feedback/exposure rotation; `tst_vinyl_home_page.qml` intent (update with reasons where the structure changed).
Baseline: the snapshot Home's row list (record `rowKeys` from the snapshot build).
Focused tests:
  - Qt Test: row assembly from the pool under the four rules (fixtures: 5 seeds incl. a native-script alias, no history → 6 rows; with history → 7).
  - Qt Quick Test: `tst_vinyl_home_page.qml` updated for order, hide-when-thin, and Rediscover hidden without history.
  - Existing harnesses: `tests/vinyl-live/PersonalizedHomeFlow.qml` remains bespoke evidence.
  - Negative control: lower the hide threshold to 0 and confirm the thin-row test fails.
Test seam status: available after Slice 0.
Lanista actions: two sessions on the fixture build.
  - **A, fresh tag, no seed:** `ui-wait-for vinylWorld.ready == true`, then `qml-get vinylHome rowKeys`; expect no gate and only chart-backed rows.
  - **B, seeded 5-artist profile without history:** expect `rowKeys` equal to the six-row string.
  - **C, seeded with history:** expect seven.
Completion signal: `ui-wait-for vinylHome.rowKeys == "<expected>"`.
State / events / probes: `qml-get vinylHomeRow_<key> itemCount` ≥ 8 for every shown row.
Visual evidence: a full-page grab per session.
Regression paths: hide an item via the feedback badge and confirm the row refills; switch tabs and come back.
Evidence artifacts: three session manifests.
Bridge status: available.
Completion criterion: Runtime-validated; spec acceptance criteria 7 and 9 met.

### Slice 7: Library tab
Purpose: Library is one calm scroll: Playlists, Saved albums, Liked songs, Artists you follow, Downloads, Recently played. Empty rows are hidden.
Dependencies: Slices 1, 3.
Implementation guidance:
- Back the rows with the snapshot's personal library and playlist stores (`ba5-library-playlists` lineage) and the listen-event ledger for Recently played.
- objectNames: `vinylLibrary` (property `rowKeys`), `vinylLibraryRow_<key>`.
Behavior to preserve: saving, liking and following from the song, album and artist pages; downloads playback offline.
Baseline: the snapshot's personal-library behaviour (`tests/vinyl-local-session/tst_personal_library.qml`).
Focused tests:
  - Qt Quick Test: new `tst_vinyl_library_tab.qml` (row order; hide empty; recently played reflects a fixture listen).
  - Qt Test: none new (stores are already covered; cite the registered snapshot tests).
  - Existing harnesses: `tst_personal_library.qml` stays green.
  - Negative control: show empty rows and confirm the hide test fails.
Test seam status: available after Slice 0.
Lanista actions: scenario `vinyl_v1_library.json` with a seeded library: `ui-click vinylTab_Library`, then `ui-wait-for vinylLibrary.rowKeys == "<expected>"`. A second run with the network disabled (fixture provider stopped) checks that Downloads still opens.
Completion signal: the `rowKeys` equality.
State / events / probes: `qml-get vinylLibraryRow_downloads` exists.
Visual evidence: a window grab of Library.
Regression paths: save an album from its page, return to Library, and confirm it appears.
Evidence artifacts: session manifests.
Bridge status: available.
Completion criterion: Runtime-validated; spec acceptance criteria 8 and 10 met. **The merge endpoint is reached** when Hemanth signs off the full-page looks.

## Risks, named plainly

1. **Slice 0 is the big one.** The snapshot sits 260 commits behind master across 114 files. Conflicts are likely in shell and CMake files.
2. **The resume-track check in Slice 5** needs a named player property. If none exists, Slice 0 must add one, or that step is Bridge blocked.
3. **Extending the house `FeaturedCarousel` could disturb Theatre.** Slice 4 re-grabs Theatre for that reason.
4. **Live sources stay outside every gate.** The only live check is the optional Slice 2 probe, which is evidence, not a verdict.
