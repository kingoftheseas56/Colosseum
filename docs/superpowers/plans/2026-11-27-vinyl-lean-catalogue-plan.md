# Vinyl lean catalogue — execution plan

Spec: `docs/superpowers/specs/2026-11-27-vinyl-combined-catalogue-design.md` (830be095a). Branch `codex/vinyl-v1-140`.
Executor: Pi workers (checkout `C:\b\pi-colosseum\src`; build/test on Laptop 2 via `C:\b\pi-colosseum\pi-l2-build.ps1`);
Claude (Agent 0) reviews each slice, reruns decisive tests, commits. Ledgers read 2026-11-27:
`docs/colosseum-test-verification.md`, `docs/colosseum-lanista-verification.md`.

Ledger facts this plan relies on:
- Deterministic seam for the music engine: `vinyl_catalogue_source_test` (Qt Test, CTest `colosseum.vinyl_catalogue_source`)
  drives `scripts/vinyl/provider.py` through `tests/vinyl-catalogue/fake_transport.py` + `fixtures/`. No live network in
  the gate.
- Lanista AVAILABLE: `qml-get`, `ui-click` on named items, `ui-wait-for` (strict equality). **Gap:** the Discover wall
  GridView in `qml/DiscoverBrowser.qml` has no objectName, so `ui-scroll` cannot page it -> endless paging is proven by
  Qt Test, not Lanista (Bridge blocked for scrolling; not needed).
- Looks (a tab appears, a list shows) are `human-witnessed` by Hemanth with exact steps; the daily profile is never a
  fixture: runtime checks use the tagged test profile `vinylv1-real-mockmatch-20261113-a1` launcher only.

Order: Slice 1 -> (Slice 2 and Slice 3 in parallel, separate checkouts) -> Slice 4.

---

### Slice 1: The music engine can fetch "this week", Wikipedia chart lists, and YouTube's chart
Purpose: internal plumbing — the provider gains the three new evidence functions the screens will use.
Dependencies: none.
Implementation guidance: in `scripts/vinyl/provider.py` add operations (same envelope/validation/caching style as
`fetch_music_catalogue`, `_discover_http`): `chartWeek` (kind songs|albums|artists, scope global|<country>): Kworb
pages (a small fixed set; parse HTML tables with the stdlib `html.parser`), interleaved by rank with YouTube Music
`get_charts` entries, de-duplicated by normalised artist+title (`_catalogue_match_title` rules); 24 h cache; parser
fails closed with one log line and falls back to YouTube-only. `wikipediaList` (list key -> MediaWiki
`action=parse&prop=wikitext` of a page whose title is discovered from the API's category/list pages, never guessed;
extract songs/artists/years from the chart tables; 7-day cache; descriptive User-Agent; attribution "Wikipedia"
carried in the envelope). No request to billboard.com anywhere (add a test that greps provider.py).
Behavior to preserve: existing Last.fm/YouTube catalogue pages and all existing provider operations unchanged.
Baseline: `ctest -R "vinyl_catalogue_source$"` passes on 830be095a (record SUMMARY).
Focused tests:
  - Qt Test: extend `tests/vinyl-catalogue/tst_catalogue_source.cpp` with fixtures captured once from live pages
    (Kworb global songs/albums/artists, YouTube `get_charts("ZZ")` JSON, two Wikipedia wikitexts: Hot 100 #1s of
    2025, Mainstream Rock #1s of the 2020s): parse counts, interleave + de-dup, cache reuse, Kworb-layout-change
    fail-closed -> YouTube-only, Wikipedia title discovery, no-billboard grep.
  - Qt Quick Test: not applicable (no QML in this slice).
  - Existing harnesses: none.
  - Negative control: corrupt the Kworb fixture's table header -> the parse test must fail; restore.
Test seam status: available.
Lanista actions / Completion signal / State / Visual evidence: not applicable (internal).
Regression paths: Discover with today's Last.fm source still loads (`tst_vinyl_discover_page`).
Evidence artifacts: `C:\b\pi-colosseum\l2-runs\<RunId>\out\ctest.log`.
Bridge status: not applicable.
Completion criterion: new + existing `vinyl_catalogue_source` tests pass on Laptop 2; negative control failed then passed.

### Slice 2: Discover is one "Vinyl" catalogue: This week / Popular / New, genre filter, no JioSaavn
Purpose: Hemanth opens Discover and browses one endless English catalogue, filtered by genre.
Dependencies: Slice 1.
Implementation guidance: `qml/vinyl/VinylMusicDiscoverAdapter.qml` — `catalogs()` returns one source `vinyl`
("Vinyl"); List filter: This week (Slice 1 `chartWeek`), Popular (Last.fm, endless), New (YouTube Music new
releases); Genre filter = existing genre list + All; remove JioSaavn and the Language filter; index only an A-Z jump
for Artists if the house browser already supports it, otherwise none. Provider `fetch_music_catalogue` routes
`catalogueSource:"vinyl"`. Missing Last.fm key -> message "Vinyl's catalogue key is missing" in the log and a plain
retry state, not "Couldn't reach the catalogue". Reuse the house `DiscoverBrowser`; no new grid.
Behavior to preserve: card tap -> YouTube resolve on tap; Discover page state across tab switches; Theatre/Tankoban/
Biblio Discover unchanged (shared `DiscoverBrowser`).
Baseline: screenshot + `qml-get` of today's Discover (rail shows Last.fm + JioSaavn) in the test profile.
Focused tests:
  - Qt Test: Slice 1 tests (routing of `vinyl` source through `fetch_music_catalogue`).
  - Qt Quick Test: `tst_vinyl_discover_page` (update: one source, three lists, genre filter, no Language group),
    `tst_vinyl_catalogue_browse`; house: `tst_discover_picker`.
  - Existing harnesses: none.
  - Negative control: re-add a second catalog in `catalogs()` -> the one-source assertion fails.
Test seam status: available.
Lanista actions: `human-witnessed:` launch "Open Vinyl (Home fix)" -> Vinyl -> Discover: rail shows one source
"Vinyl"; corner "Now browsing Vinyl"; List filter shows This week / Popular / New; choose Genre "Metal" -> metal
cards; scroll far down Popular -> more cards keep loading; tap a song -> it plays.
Completion signal: Hemanth's confirmation recorded in the evidence note.
State / events / probes: provider log lines for `browseCatalogue` with `"source":"vinyl"`, `ok:true`.
Visual evidence: one grab per List value (Songs tab), Hemanth's eyes are the gate.
Regression paths: Home tab, Library tab, Theatre Discover opens and browses.
Evidence artifacts: `Brotherhood/agents/vinyl-lean-catalogue-evidence/slice2/` (grabs + log excerpt + verdict).
Bridge status: available for reads; scrolling the wall is a known gap (covered by Qt Test paging).
Completion criterion: suites pass on Laptop 2, Hemanth confirms the walkthrough.

### Slice 3: Explore tab — one shelf page of editorial rows
Purpose: Vinyl gets Home · Discover · Explore · Library; Explore shows Wikipedia and Kworb shelves.
Dependencies: Slice 1 (parallel with Slice 2: different files except `VinylHouseShell.qml` tab list, owned by Slice 3).
Implementation guidance: add an `explore` tab to Vinyl's `WorldTabBar` (order Home, Discover, Explore, Library);
page reuses the house shelf page pattern (`TheatreCatalogPage` / `BiblioExplorePage`, `RowIndexSidebar`) with house
shelves/cards (`VinylShelf`/`VinylItemCard`); rows: "This week worldwide" (Kworb), "Every #1 of 2025", "Global #1s",
"The year-end 100", "Rock's #1s this decade", "#1 hits of the '90s" (Wikipedia), "Most streamed ever" (Kworb); each
row loads independently with the house loading/retry states; songs, albums and artists mixed on one page (no
separate Songs/Albums/Artists tabs). Attribution line "Lists from Wikipedia" at the page foot.
Behavior to preserve: Home/Discover/Library behaviour and scroll positions per tab; full-window detail pages.
Baseline: `tst_vinyl_shell_tabs` passes with three tabs.
Focused tests:
  - Qt Test: Slice 1 `wikipediaList`/`chartWeek` tests.
  - Qt Quick Test: new `tests/qml/tst_vinyl_explore_page.qml` (rows render from fixtures, a failed row shows retry
    while others render, row index jumps); `tst_vinyl_shell_tabs` updated for four tabs; `tst_vinyl_v1_shell`.
  - Existing harnesses: none.
  - Negative control: drop the Explore tab from the tab model -> four-tab assertion fails.
Test seam status: available.
Lanista actions: `ui-click` `vinylTab_explore` (tabs are named `vinylTab_<key>`); `ui-wait-for` the Explore page
item's `visible` == true (objectName to add: `vinylExplorePage`); `qml-get` row count on `vinylExplorePage`.
Plus `human-witnessed:` Vinyl -> Explore: rows show real songs/albums; tap one -> plays/opens.
Completion signal: `ui-wait-for vinylExplorePage visible true`.
State / events / probes: `qml-get vinylExplorePage rowCount` >= 5.
Visual evidence: one full-page grab of Explore.
Regression paths: switch Explore -> Discover -> Explore keeps scroll; open an album from Explore and Back.
Evidence artifacts: `Brotherhood/agents/vinyl-lean-catalogue-evidence/slice3/`.
Bridge status: available (requires adding the `vinylExplorePage` objectName in this slice).
Completion criterion: suites pass on Laptop 2; Lanista wait + probe pass in the test profile; Hemanth confirms.

### Slice 4: Clean-up and the full Vinyl gate
Purpose: no dead JioSaavn paths left, everything green together.
Dependencies: Slices 2 and 3 merged.
Implementation guidance: remove now-unused JioSaavn UI paths (keep provider code if other callers exist; report);
update `VINYL-MAP.md` if present.
Behavior to preserve: everything above.
Baseline: Slice 2 + 3 merged commit.
Focused tests: full Vinyl QML suite set used for today's merges + `vinyl_catalogue_source`, `vinyl_native_facade`,
`vinyl_live_world_native`, `vinyl_v1_home_actions_native`; Negative control: n/a (no new test) — state why.
Test seam status: available.
Lanista actions: not applicable beyond Slices 2-3.
Bridge status: not applicable.
Completion criterion: all pass on Laptop 2; committed and pushed by Agent 0.

## Risks called out
- Kworb layout change breaks "This week" -> fails closed to YouTube's chart (tested).
- Wikipedia list pages differ in table shape per chart -> parser handles the two fixture shapes; new charts get a
  fixture before being added as rows.
- Last.fm key distribution for other users' installs is out of scope (launcher-only key today); flagged for packaging.
