# Discover Sidebar — Build Plan

Spec: `docs/superpowers/specs/2026-09-29-discover-sidebar-design.md` (approved by Hemanth 2026-09-29, `5ab41fc3`).
Written under `brotherhood-writing-plans` by Claude (Agent 0 lane). Executed under `brotherhood-executing-plans`.

## How to read this plan

Seven slices. Slices 0–1 are internal seams (no visible change); Slices 2–5 are user-visible and each carries its
own proof; Slice 6 is the four-world acceptance sweep. A slice is closed only at the status its Completion
criterion names (vocabulary: `docs/colosseum-lanista-verification.md`). A green Qt Quick suite alone is
`Test-reported`, never `Runtime-validated`.

**Working-tree discipline.** Other agents keep uncommitted work in the main checkout (`qml/ExtensionsChainPage.qml`,
`qml/ExtensionsHousePage.qml`, `tests/__*`, extensions tests). Read `git diff` for every file a slice touches,
preserve unrelated hunks, commit with explicit pathspecs. The encyclopedia pre-commit hook needs
`scripts/code_encyclopedia.py --accept <file>` for guide-listed files (local state, gitignored).

**Standard gates** (name them in every slice besides the slice's own checks):
- `G-qml`: `native/build-msvc/colosseum_qml_tests.exe -input tests/qml/<file>.qml` for touched files, and the
  registered `ctest --test-dir native/build-msvc -R "^colosseum\.qml$" --output-on-failure` (real windows; run on
  the desktop). `ctest` is not on the plain PATH: use
  `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe`.
  Quick local loop: `C:/Qt/6.11.1/msvc2022_64/bin/qmltestrunner.exe -input tests/qml/<file>.qml -platform offscreen`
  (offscreen cannot render shader effects; never use it for glass/blur claims).
- `G-keys`: the keyboard set must stay green: `tst_keyboard_spatial_navigator`, `tst_keyboard_directional_continuity`,
  `tst_keyboard_directional_scroll`, `tst_world_keyboard_journey`, `tst_world_rows_park`, `tst_topbar_spatial_navigation`,
  `tst_keyboard_scroll_focus`, `tst_scroll_glide_wheel`.
- `G-lint`: `C:/Qt/6.11.1/msvc2022_64/bin/qmllint.exe <touched .qml>`; no new warnings versus `git show HEAD:<file>`.
- `G-warn`: `tests/warning_gate.ps1` over each Lanista session's logs; known-noise classes cited by name.
- Pre-existing red on master (not this plan's): `tst_account_data_privacy` (1), `tst_main_book_return` (1),
  `tst_one_piece_east_blue_native_atlas` (5), `tst_player2_progress` (1), `tst_vault_detail_sheet` (1).

**Isolation.** Every runtime proof uses
`native/build-msvc/lanista.exe session run tests/lanista_scenarios/<scenario>.json --drive --exe native/build-msvc/colosseum.exe --qml qml/Main.qml --tag <slice-tag> --keep-going --verbose`.
Never the daily app or default pipe. Scenarios start like `tankoban_discover_depth.json`: `ping`, `get-state`
(appDataRoot contains `Colosseum-dltest`), `ui-wait-for bootSplash.visible == false` (60 s), `ui-click
accountWelcomeContinueLocal` (no-op when onboarding is already clear), then the world's `modePill_<World>` and its
Discover tab (`theatreTab_discover`, `biblioTab_discover`, `tankobanTab_discover`; Comics via `tankobanTab_comics`
where the comics wall lives — confirm the comics Discover route in Slice 0 and record it here).

**Known bridge limits this plan designs around** (ledger UNAVAILABLE / HUMAN-ONLY):
- `ui-wait-for` is strict equality only → Slice 0 adds string/int automation properties to wait on.
- No absence assertions → "old picker gone" is proven by a counter seam (`automationLegacyPickerCount == 0`)
  plus the Qt Quick Test layer.
- No window resize command → the width breakpoints are proven in Qt Quick Test (component width) plus a
  `human-witnessed:` step.
- No hover command → hover plates are Qt Quick Test (`mouseMove`) plus human witness.
- Whether it *looks* right is Hemanth's eyes; grabs are the evidence package, never the verdict.

---

### Slice 0: Named seams for the sidebar (internal)

Purpose: give every sidebar surface a stable, world-namespaced name and the automation properties the runtime
proofs wait on, before any UI moves.
Dependencies: none.
Implementation guidance:
- `DiscoverBrowser.qml`: add `property string automationPrefix` (set by each host page: `theatre`, `biblio`,
  `tankoban`, and the comics host's own prefix — never a bare shared stem; ledger naming rule). Name the browser
  `<prefix>DiscoverBrowser` and the (current) wall `<prefix>DiscoverWall`.
- Read-only automation properties on the browser: `automationCatalogKey` (== `currentCatalogKey`),
  `automationType` (== `currentType`), `automationFilterSummary` (stable string, e.g. `Genres=Action;Formats=Omnibus`,
  groups sorted, empty when none), `automationItemCount` (== `items.length`), `automationLegacyPickerCount`
  (count of legacy picker instances: 2 today — catalogue menu + filter picker; 0 after Slices 2–3).
- Declare (for Slice 2 to implement) `automationSidebarPinned` on the sidebar: true when the rail's top sits on its
  pinned line (dock bottom + 12, or the section top when that is lower) within 1 px — the bridge cannot compare two
  readings, so the pin is proven by waiting on this bool.
- Hosts set the prefix: `DiscoverPage.qml` (Theatre), `BiblioDiscoverPage.qml`, `TankobanDiscoverPage.qml`
  (and the comics host, if separate).
Behavior to preserve: every existing `discoverCard_*` / `mangaDiscoverCard_*` name; `tankoban_discover_depth.json`
still passes.
Baseline: `lanista.exe session run tests/lanista_scenarios/tankoban_discover_depth.json ...` green before the change.
Focused tests:
  - Qt Test: not applicable (no C++ contract).
  - Qt Quick Test: new `tests/qml/tst_discover_sidebar.qml` — instantiate production `DiscoverBrowser` with a stub
    adapter (2 types, 3 catalogues in 2 sections, 2 filter groups, synchronous `fetchPage`); assert the names and
    that `automationFilterSummary`/`automationCatalogKey` track state changes.
  - Existing harnesses: `tankoban_discover_depth.json` scenario.
  - Negative control: break the summary sort order → the summary case goes red.
Test seam status: available (new file under the registered `colosseum.qml` runner).
Lanista actions: `tankoban_discover_depth.json` replay; `qml-get tankobanDiscoverBrowser automationLegacyPickerCount`.
Completion signal: `ui-wait-for <prefix>DiscoverBrowser.automationType == <world's default type>` (movie / book /
manga), THEN `loading == false` — `loading` alone is false before the browser initialises (found in execution).
State / events / probes: `automationLegacyPickerCount == 2` (pickers still present).
Visual evidence: none needed (no visible change).
Regression paths: Discover in all three worlds opens and pages as before.
Evidence artifacts: `artifacts/lanista-sessions/<id>/`, G-qml log.
Bridge status: available.
Completion criterion: G-qml green incl. the new file; the replay green; names resolvable by `qml-get` in each world.
`Test-reported` + replay green = done (internal slice).

### Slice 1: Filter state becomes one-per-group, with combine vs replace (internal)

Purpose: the browser can hold one filter per group and ask the source whether groups combine, without changing
what the user sees yet (the old picker keeps single-selection semantics until Slice 3).
Dependencies: Slice 0.
Implementation guidance:
- Replace `filterGroup/filterKey` with `filters` (map `{group: key}`) plus helpers `setFilter(group, key)`,
  `clearFilter(group)`, `clearFilters()`. Per-type memory (`typeStates`) stores the map.
- Adapter contract: optional `combinesFilters(type, catalogKey)` (default false). When false, `setFilter` on a new
  group replaces the map; when true, it adds. `fetchPage` receives `state.filters` (map); keep passing the legacy
  single pair for adapters not yet migrated during the slice, then remove it.
- `TankobanDiscoverApi.js`: answer true for manga and comics catalogues; build the local DB query with every
  active group (genre + demographic; genre + publisher + format + availability).
- `BiblioDiscoverApi.js`: answer per catalogue from what its query supports (record the answer in the slice log).
- `DiscoverApi.js` (Theatre/Stremio): answer true only when the catalogue's manifest `extra` declares more than one
  non-`skip`/non-`search` extra and the request builder sends them together; otherwise false.
Behavior to preserve: today's single-filter picker behaviour, pins (`resolvePin`), per-type memory, the
catalogue-missing fallback notice, offline/downloading notices.
Baseline: record current `fetchPage` requests for one filtered Tankoban comics page and one Theatre genre page
(via the slice's Qt Quick stub adapter capturing calls).
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: `tst_discover_sidebar.qml` cases — combine=true keeps both groups and `fetchPage` receives
    both; combine=false replaces; clearing one group keeps the other; per-type memory restores the map.
  - Existing harnesses: `node` JS tests that cover the adapters, if any exist for `TankobanDiscoverApi`/
    `DiscoverApi` (check `tests/*.mjs`; run the ones found).
  - Negative control: force combine=false for the comics stub → the combine case goes red.
Test seam status: available.
Lanista actions: `tankoban_discover_depth.json` replay (single filter path unchanged).
Completion signal: as the replay's own waits.
State / events / probes: `automationFilterSummary` after picking a genre via the old picker equals `Genres=<key>`.
Visual evidence: none.
Regression paths: Theatre genre filter, Tankoban genre filter, Biblio filter each still filter the wall
(one scenario step per world: pick via the old picker, wait on `automationFilterSummary`).
Evidence artifacts: session dirs, G-qml log.
Bridge status: available.
Completion criterion: G-qml, G-lint green; three-world regression steps green. Done (internal).

### Slice 2: The sidebar appears — type switch and catalogues (user-visible)

Purpose: Discover shows the pinned glass rail with the type switch and catalogues grouped by source; one click
switches the wall; the "Now browsing ▾" dropdown is gone; the summary line shows the current catalogue.
Dependencies: Slices 0–1.
Implementation guidance:
- New `DiscoverSidebar.qml` (inside `DiscoverBrowser`), fed only by the adapter: `types()`, `catalogs(type)`
  (`section`, `title`, `attribution`; Theatre descriptors carry `addonName`/transport for `AddonLogo`). Sections
  become groups with a logo header (fallback: the section name's initial in a round plate).
- Layout: rail 184 px + 28 px gap on the left of the Discover section; wall takes the rest. Style from the concept
  (`worldnav` CSS: 46 px rows, 12 px radius, inkDim → ink, `.on` plate + 3 px gold bar with glow), glass panel via
  `Glass` (fixed mask, `9f29b622`).
- Pinned: the rail is positioned against the world page viewport (like `WorldPage`'s dock): its top = max(section
  top on screen, dock bottom + 12); it does not become a second scroller for the wall; its own list scrolls when
  taller than the window.
- Remove the catalogue popup (`catalogMenu`) and move "Now browsing" to the summary line (catalogue title +
  attribution). The type chips above the wall go (the switch is in the rail). `automationLegacyPickerCount` → 1.
- Names: `<prefix>DiscoverSidebar`, `<prefix>DiscoverType_<key>`, `<prefix>DiscoverCatalog_<sanitised key>`,
  `<prefix>DiscoverSummary`.
Behavior to preserve: Slice 7 page flow (wall scrolls with the page, one scrollbar), Slice 8 dock and row parking,
per-type memory, catalogue-missing notice, card click opens the title.
Baseline: grab Theatre Discover with the dropdown open (Hemanth's screenshot state: Cinemeta + 9 addon catalogues,
list overflowing the screen); record `automationCatalogKey`.
Focused tests:
  - Qt Quick Test: `tst_discover_sidebar.qml` — rail rows equal the stub's catalogues grouped by section in order;
    clicking a catalogue row sets `currentCatalogKey` and emits one page request; the type switch swaps the
    catalogue list; single-type stubs hide the switch; the current row has the active state.
  - Existing harnesses: `tst_world_rows_park`, `tst_world_keyboard_journey` (world page intact).
  - Negative control: make a click not update `currentCatalogKey` → the click case goes red.
Test seam status: available.
Lanista actions (new `tests/lanista_scenarios/discover_sidebar_catalogues.json`, Theatre): open Theatre →
`theatreTab_discover` → `ui-wait-for theatreDiscoverSidebar.visible == true` → `qml-get theatreDiscoverBrowser
automationCatalogKey` → `ui-click theatreDiscoverCatalog_<a second catalogue>` → `ui-wait-for
theatreDiscoverBrowser.automationCatalogKey == <that key>` → `ui-click theatreDiscoverType_series` →
`ui-wait-for theatreDiscoverBrowser.automationType == "series"` → `ui-scroll theatreWorldScroll dy -1200` →
`ui-wait-for theatreDiscoverSidebar.automationSidebarPinned == true` (rail still beside the wall, on its pinned line).
Completion signal: the `ui-wait-for` equalities above.
State / events / probes: `automationLegacyPickerCount == 1`; `theatreDiscoverSummary` text contains the catalogue
title.
Visual evidence: window grabs — rail at rest; after switching catalogue; scrolled with the rail pinned beside
row ≥3. Hemanth's eyes on the look.
Regression paths: switch world away and back (Discover keeps catalogue and type); open a title and Back.
Evidence artifacts: session dir, grabs, G-warn output.
Bridge status: available.
Completion criterion: all waits green, G-qml/G-keys/G-lint/G-warn green, and `human-witnessed:` Hemanth opens
Theatre Discover, switches two catalogues and Movie → Series with one click each, scrolls the wall and sees the
rail stay. Then `Runtime-validated (human-witnessed)`.

### Slice 3: Filters live in the sidebar, with the summary line (user-visible)

Purpose: filter groups sit under the catalogues; picking combines or replaces per the source; the summary line
shows each active filter with ✕; long groups fold to eight + "Show all (N)", with search when N > 40; an empty
result offers Clear filters. The Genre/Filter picker is gone.
Dependencies: Slices 1–2.
Implementation guidance:
- Filter block per group in `DiscoverSidebar.qml` from `filterGroups`; a group header per group; options as rows;
  chosen row = active style; clicking the chosen row clears it.
- Eight rows then `Show all (N)` / `Show less`, expanding in place; group > 40 options shows a search field at the
  top of the expanded list (Esc clears, second Esc leaves).
- Summary line: catalogue part (no ✕) then one chip per active filter with ✕ → `clearFilter(group)`.
- Empty result: the wall's empty state gains a `Clear filters` button → `clearFilters()`.
- Remove `filterPicker` and the FILTER row. `automationLegacyPickerCount` → 0.
- Names: `<prefix>DiscoverFilter_<group>_<key>`, `<prefix>DiscoverFilterShowAll_<group>`,
  `<prefix>DiscoverFilterSearch_<group>`, `<prefix>DiscoverSummaryClear_<group>`, `<prefix>DiscoverClearFilters`.
Behavior to preserve: Slice 1 semantics; explicit-content filtering (`showExplicit`) in Tankoban/Biblio options.
Baseline: grab Comics Discover with the old single "Filter" picker showing Publishers; record that only one filter
can be active.
Focused tests:
  - Qt Quick Test: group rendering from stub; eight + Show all; search appears only when N > 40 and filters rows;
    combine stub adds, replace stub replaces; ✕ in the summary clears only its group; empty result shows Clear
    filters and it clears.
  - Negative control: render all rows instead of eight → the fold case goes red.
Test seam status: available.
Lanista actions (new `discover_sidebar_filters.json`, Tankoban Comics then Theatre): Comics Discover →
`ui-click tankobanDiscoverFilter_Publishers_<Marvel key>` → `ui-click tankobanDiscoverFilter_Formats_<Omnibus key>`
→ `ui-wait-for tankobanDiscoverBrowser.automationFilterSummary == "Formats=<k>;Publishers=<k>"` →
`ui-click tankobanDiscoverSummaryClear_Formats` → wait summary `== "Publishers=<k>"`; Theatre Discover →
two genres in sequence → wait summary equals the second only.
Completion signal: the summary equalities.
State / events / probes: `automationLegacyPickerCount == 0`; `automationItemCount > 0` after each filter
(use `expect ... > 0` on the `qml-get` reply).
Visual evidence: grabs of Comics with two filters active and the summary line; Publishers expanded with search.
Regression paths: type switch keeps each type's filters; world away and back; Back from a title keeps filters.
Evidence artifacts: session dir, grabs, G-warn.
Bridge status: available.
Completion criterion: waits green, gates green, `human-witnessed:` Hemanth sets Marvel + Omnibus in Comics, clears
one from the summary, and expands Publishers. `Runtime-validated (human-witnessed)`.

### Slice 4: Retract to a logo strip, remembered; narrow windows (user-visible)

Purpose: the edge toggle retracts the rail to a 52 px strip (type initials + source logos), the wall and the docked
tab bar slide left and gain a column, the choice survives restarts; narrow windows start retracted or use an
overlay. Reference: `colosseum-theatre-sidebar-concept (1).html` (`.worldnav-toggle`, `body.worldnav-collapsed`).
Dependencies: Slice 3.
Implementation guidance: toggle = 28 px round glass button at the rail's right edge (`right:-14`, `top:18`),
chevron rotates 180° when retracted; width animates 184 ↔ 52 with the concept's easing; labels fade, rows become
44 px icon buttons, gold bar stays. While the sidebar is present the dock centres over the content column.
Retract state and group-fold state in the existing Settings store, per world (`<prefix>`). Strip items: type initials, one logo per source (click → expand at that group). Breakpoints on the
browser's width: < 1250 start collapsed; < 900 rail hidden, summary line's catalogue part opens the rail as an
overlay (Esc/click-outside closes).
Behavior to preserve: summary line always visible; wall page flow; keyboard reachability of the strip.
Baseline: record wall `columnCount` with the rail open (qml-get on the wall seam).
Focused tests:
  - Qt Quick Test: retract toggles width 184 ↔ 52 and wall columns +1; chevron rotation 0 ↔ 180; setting write happens through an injected
    record layer (ledger learning: never race the Settings batch timer); width 1200 starts collapsed; width 880 hides
    the rail and the summary opens the overlay.
  - Negative control: skip the persisted read → the restore case goes red.
Test seam status: available.
Lanista actions: `discover_sidebar_collapse_a.json` (Theatre: click `theatreDiscoverSidebarCollapse`, wait
`theatreDiscoverSidebar.collapsed == true`, read wall `columnCount`) then `discover_sidebar_collapse_b.json` with
the **same `--tag`** (new process): wait `theatreDiscoverSidebar.collapsed == true` at boot.
Completion signal: the two waits.
State / events / probes: wall `columnCount` = baseline + 1 when collapsed.
Visual evidence: grabs open vs collapsed.
Regression paths: expand from a logo lands on that group; other worlds keep their own state.
Evidence artifacts: two session dirs, grabs.
Bridge status: available for collapse and persistence; the width breakpoints are proven by Qt Quick Test plus
`human-witnessed:` (Hemanth narrows the window below ~1250 and ~900 px) — the bridge has no resize command.
Completion criterion: waits green, gates green, human witness of collapse look and the narrow-window behaviour.
`Runtime-validated (human-witnessed)`.

### Slice 5: Keyboard in and out of the sidebar (user-visible)

Purpose: ← from the wall's first column enters the rail at the current catalogue; ↑/↓ move; Enter selects; →
returns to the card focus left; ↓ never reaches the TopBar; one focus look.
Dependencies: Slice 3 (Slice 4 for the strip).
Implementation guidance: rail rows are `KeyboardAction` stops (FocusRing). The wall's GridView ignores Left at
column 0 (bubbles); `DiscoverBrowser` handles it: focus the rail at `<prefix>DiscoverCatalog_<current>` (region
memory). Right from the rail restores the wall's `currentIndex` and focus. Use the shared navigator for ↑/↓ (nearest
row, lane) so behaviour matches the rest of the app; Up at the rail top goes to the docked tab row, never the TopBar
(world-feel K1 rule; if K1 has not landed, stop at the rail top).
Behavior to preserve: G-keys; Discover filter-row → wall entry (`enterWall`) now becomes rail → wall.
Baseline: from the wall's first card, Left today goes to the page (record `automationFocusedObject`).
Focused tests:
  - Qt Quick Test: Left at column 0 focuses the current catalogue row; Right returns to the same card index;
    Enter on a row selects it; Down past the last rail row stays in the rail/goes to the wall, never outside.
  - Negative control: drop the Left handler → the entry case goes red.
Test seam status: available.
Lanista actions (`discover_sidebar_keys.json`, Theatre): enter Discover, `ui-keypress Down` into the wall
(existing path), `ui-keypress Left` → `ui-wait-for theatreWorld.automationFocusedObject == "theatreDiscoverCatalog_<current>"`,
`ui-keypress Down`, `ui-keypress Return` → wait `automationCatalogKey` equals the next catalogue, `ui-keypress Right`
→ wait focus object equals the recorded card name.
Completion signal: the focus/catalogue equalities.
State / events / probes: `automationFocusedObjectFullyVisible == true` after each move.
Visual evidence: grab with the ring on a rail row.
Regression paths: G-keys; Tankoban Comics rails walk (Down/Up) unaffected outside Discover.
Evidence artifacts: session dir.
Bridge status: available.
Completion criterion: waits green, gates green. `Runtime-validated`.

### Slice 6: Four-world acceptance sweep and the eyes-on gallery (user-visible)

Purpose: prove the one design works in Theatre, Biblio, Tankoban Manga and Tankoban Comics, and hand Hemanth the
evidence to judge the look.
Dependencies: Slices 2–5.
Implementation guidance: one scenario per world (`discover_sidebar_<world>.json`) replaying: rail visible, switch
catalogue, apply a filter (two in Manga/Comics), summary equality, collapse/expand, Left into the rail and Right out.
Then `lanista.exe brief` gallery of the grabs.
Behavior to preserve: everything in G-keys; `tankoban_discover_depth.json`.
Baseline: Slice 2–5 baselines.
Focused tests: G-qml full run (`colosseum.qml`), G-keys.
  - Negative control: covered per slice.
Test seam status: available.
Lanista actions: the four scenarios (all `session run`, unique tags).
Completion signal: each scenario's waits.
State / events / probes: `automationLegacyPickerCount == 0` in all four; summary strings as seeded per world.
Visual evidence: per world — rail at rest, filtered, collapsed; Theatre scrolled with the rail pinned under the
glass dock.
Regression paths: world switching round-trip; restart with the same tag keeps collapse per world.
Evidence artifacts: four session dirs, the brief gallery.
Bridge status: available.
Completion criterion: four scenarios green, all gates green, and Hemanth's verdict on the gallery and a live
look recorded in the plan's execution log. `Runtime-validated (human-witnessed)`; the spec's acceptance items 1–9
each map to a green step or a recorded witness.

## Risks named plainly

- **Theatre combine rule:** most Stremio catalogues declare only `genre`; Theatre will mostly stay single-filter.
  That is the approved behaviour, not a defect.
- **Pinned rail and the page flow:** the rail must track the page viewport without becoming a second scroller or
  re-triggering the Discover wall window's binding loop (a `windowTop` binding-loop warning appears in today's
  logs; G-warn will surface it — fix it where it lives if this plan's code touches it).
- **Glass cost:** one more Glass panel on Discover; the blur is visible since `9f29b622`. Watch frame pacing in
  Slice 2's scroll step (`COLOSSEUM_FRAME_PROBE=1` in the session env; compare with the world-feel scroll numbers).

## Execution log

Append per-slice gate numbers, session ids, witness verdicts and status changes here.

- 2026-09-29 — Slice 0 (seams). Baseline: `tankoban_discover_depth.json` 17/18 (tag ds0base, session
  20260929-204205-bb3c2337) — the rank-11 wait fails BEFORE any sidebar change (today's wheel fix scrolls 168 px per
  notch and Codex's `cacheBuffer: 0` wall window, so the scripted scrolls pass rank 11; rank 18 still reached).
  Comics route confirmed: Tankoban Discover, type `comics` (one `DiscoverBrowser`, prefix `tankoban`).
  Implemented: `automationPrefix` + `<prefix>DiscoverBrowser`/`<prefix>DiscoverWall`, automation properties,
  legacy-picker counter (named `discoverCatalogMenu`/`discoverFilterPicker`); hosts set theatre/biblio/tankoban.
  Qt Quick Test `tst_discover_sidebar.qml` 3/3 (registered runner `colosseum_qml_tests.exe`, real windows);
  negative control (summary separator) red. Lanista `discover_sidebar_seams.json` 28/28 (tag ds0seams6, session
  20260929-204939-2ede66ce); `WARNING_GATE_OK`. Plan corrected: `loading == false` is true before init, so the
  completion signal waits on `automationType` first. qmllint unchanged. Status: done (internal).

- 2026-09-29 — Slice 1 (one filter per group). Reality vs plan: the manga, comics and Biblio native queries each
  took ONE axis, so "build the query with every active group" needed native work: new
  `discoverPageFiltered(catalogId, [{axis,key}], …)` on `MalCatalog`, `ComicsCatalog`, `BiblioCatalog`
  (+ `BiblioCatalogStore::pageFiltered`); the old single-filter calls delegate to it. Manga keeps its driving
  join for the first facet and ANDs the rest with a non-correlated `IN` (the LOWER() match cannot use the index);
  comics/Biblio AND bound conditions. Browser: `filters {group: key}`, `setFilter` combines or replaces per
  `adapter.combinesFilters`, `clearFilter(group)`, per-type memory, pins; adapters: Tankoban (builtin catalogues
  combine), Biblio (built-in combine, addon catalogues single), Theatre (combine only when the catalogue declares
  >1 filterable extra; `selectionsForFilters`). Bug found by the page harnesses and fixed: a local named `active`
  in `requestPage` shadowed the browser's `active` (hoisted) and stopped every first page.
  Gates: Qt Quick `tst_discover_sidebar` 10/10 (negative control: combine forced off -> 2 red); native harnesses
  `mal_catalog_discover` / `comics_catalog_engine` / `biblio_catalog_store` OK with new combined-facet cases
  (negative control: extra-facet loop off -> "Action AND Seinen" red); existing `discover_api`,
  `tankoban_discover_api`, `biblio_discover_api`, `tankoban_discover_page` harnesses OK; `biblio_discover_page`
  1 failure identical on HEAD (fixedGalleryWidth, pre-existing). G-keys green. Lanista
  `discover_sidebar_filter_regression.json`: fresh tag ds1reg2 (session 20260929-224748-e0012303) Theatre +
  Tankoban 100%, Biblio's fresh-tag catalogue is empty (page too short to scroll); tag claude-scroll (session
  20260929-224904-de65a195) Biblio + Tankoban 100%, Theatre's only red is the scroll-settle wait (that tag keeps
  an earlier scroll) while its filter steps pass. WARNING_GATE_OK. Temporary seam: the legacy `DiscoverPicker`
  got `automationName` (`<prefix>DiscoverFilterPickerPill`/`Option_<i>`); qmllint +2 "unqualified" warnings on
  the option-row name line, accepted because Slice 3 deletes the picker. Status: done (internal).

- 2026-09-29 — Slice 2 (the rail: type switch + catalogues). Baseline: Theatre Discover at rest (tag ds2base,
  session 20260929-225857-1a625752): `Popular`/Cinemeta, 49 cards, legacy pickers 2; the dropdown's trigger had no
  name, so its open state is Hemanth's screenshot, not a grab. Implemented: `DiscoverSidebar.qml` (glass rail,
  184 px + 28 gap; segmented type switch hidden for one type; catalogues grouped by source — extension catalogues
  under their addon (`attribution`), built-ins under `section` — with `AddonLogo` headers; rows 46 px, gold bar
  when current; pinned against the world page viewport under the dock, fixed pinned height so the glass is not
  re-allocated per scroll frame, bottom clear of the taskbar's Colosseum button; its own list scrolls; clicks do
  not move focus, so no ring on a mouse pick). `DiscoverBrowser`: dropdown + type lens removed; masthead is the
  left-aligned summary line (`<prefix>DiscoverSummary`, catalogue then filters) + attribution byline; content
  column starts at `contentLeft`; `flowHeight` never below the rail's natural height; worlds pass `backdrop`.
  Reality vs plan: (1) a fresh profile has ONE Theatre catalogue per type, so the scenario uses a new seed
  `tests/lanista-seeds/discover-sidebar-addons-v1` (fresh defaults + the public Torrent Catalogs addon; Hemanth's
  Trakt addon URL carries a token and was left out); (2) Top seeded is a short wall, so the pinned check scrolls
  dy -480 (past the end the rail rides up with the wall by design); (3) the `contentY == 504` settle waits failed
  because ScrollGlide settled at 503.9999999999999 — fixed at the source (settle snaps sub-1e-6 residue to the whole
  pixel); (4) G-warn caught the plan's named `windowTop` binding loop — fixed where it lives (the wall's paging
  request from `onContentYChanged` is deferred with `Qt.callLater`). The docked tab bar centring over the content
  column stays with Slice 4 (its guidance).
  Gates: Qt Quick `tst_discover_sidebar` 15/15 (registered runner), negative control (row click no-op) red on the
  click case; G-keys all green (`tst_world_keyboard_journey` has an intermittent focus flake, 6/6 green both with
  and without this slice's glide change); qmllint `DiscoverSidebar` 0, `DiscoverBrowser` 39 -> 14, hosts equal or
  fewer; harnesses `discover_browser/page/picker/api`, `tankoban_discover_api/page`, `biblio_discover_api` OK,
  `biblio_discover_page` the known fixedGalleryWidth red; `test_scroll_glide_p0`, `scroll_glide_harness`,
  `scroll_glide_math_test` green; `ctest -L unit` 154/160, the 6 reds (reader2 runtime, startup deferral/
  responsiveness, manga downloader responsiveness, core_sync_adapters, video_source_handoff_p0) read none of this
  slice's files. Lanista `discover_sidebar_catalogues.json` 35/35 twice (tags ds2cat5/ds2cat6, sessions
  20260929-232413-ba8d4dfb, 20260929-232440-4cf98216), WARNING_GATE_OK both; `discover_sidebar_seams.json` 28/28
  (count now 1); `discover_sidebar_filter_regression.json` 49/49 fresh (tag ds2freg2, 20260929-232154-72ee5bc9),
  WARNING_GATE_OK. Grabs in `artifacts/discover-sidebar/slice2/`. Not bridge-driven: opening a title and Back
  (card names are live ids) — part of the witness. Status: Implemented, verification pending (awaiting the
  `human-witnessed:` pass: Theatre Discover, two catalogue switches, Movie -> Series, scroll with the rail staying).

- 2026-09-29 — Slice 2 REDONE to the concept after Hemanth's review of `9b1c323a` ("Absolutely not"; "Why can't
  you simply follow the mock"). His calls, recorded: (1) the type selector (Movie/Series/Anime, Manga/Comics) stays
  above the wall as the underlined lens — not in the rail; (2) the rail is ALWAYS closed by default (not
  remembered); (3) posters sized like the concept; (4) "mock look, catalogues in rail" (asked: the concept's rail
  holds Home/Watch Party/Activity/Stats; he chose the concept's look with our catalogues). Built: `DiscoverSidebar`
  now copies `#worldnav` from `colosseum-theatre-sidebar-concept (1).html` — no glass panel, 1 px fading right edge,
  shell padding 22/16/18/0, "WORLD" + world name heading, 46 px items with 19 px icon (source logo) + 14/600 label,
  .09 plate + 3 px gold bar, 184 open / 52 closed eased 240 ms, closed = one 44 px icon per source, 28 px round toggle
  at right:-14 top:18 (`<prefix>DiscoverSidebarCollapse`), pinned 12 below the board top like the concept; the
  docked tab bar sits right of the rail (`WorldPage.dockContentLeft`, concept `.dock` left = margin + rail + gap).
  Wall = the concept's grid: auto-fill minmax(148, 1fr), 20 px column gap, 26 px row gap (measured in the concept at
  1280 wide with the rail closed: 6 columns of 164 x 245; the app now lays 6 x 165). Every gallery wall counts
  columns this way (the fixed-width Biblio path too, so the two agree). Kept from the plan: catalogue grouping,
  automation names. Deviation: the rail's bottom stays 92 px above the window edge (the taskbar's Colosseum button;
  the concept has no taskbar, its bottom is 62). Also: ScrollGlide's whole-pixel snap now covers the path where the
  last take ends the glide (the wheel step itself was -503.99999999999994); three probes land on 504.
  Gates: `tst_discover_sidebar` 16/16 (closed by default, toggle and logo open it, lens not inside the rail); G-keys
  green; qmllint sidebar 0, WorldPage unchanged (5); harnesses as before (Biblio page's known red only); Lanista
  `discover_sidebar_catalogues.json` 41/41 twice (ds2z1/ds2z2, sessions 20260929-235451-c9de3c91,
  20260929-235519-1ab9a011; checks closed at start, toggle opens to 184, dock x = 266) WARNING_GATE_OK;
  `discover_sidebar_seams.json` 28/28 OK; `discover_sidebar_filter_regression.json` 49/49 on claude-scroll (the
  gate's flags there are all 16:00-17:45 profiling lines from earlier sessions on that profile, none from this run).
  Status: Implemented, verification pending (Hemanth's eyes).

- 2026-09-30 — Slice 2 REDEFINED by Hemanth after `02127773` ("there really is no proper definition behind what
  the sidebar is meant to do... understuff"). The definition now: the SIDEBAR lists the sources only (addons, plus
  the built-in source named as its addon: Colosseum Grand Database for Tankoban, Apple Books for Biblio), by real
  icon and name; one click switches the wall to that source's first category. The CATEGORY PICKER returns top right
  ("Popular ▾") and lists only the current source's categories. The FILTER dropdown stays for genres/years/languages.
  Rail widened to 240 (names fit; a trailing "Addon" is dropped from rail labels). Discover posters use a wall token
  of 132 px: seven across with the rail closed, six open, in all three worlds. Root cause found for "Language with
  no languages": `ExtensionsStore::slimManifest` discarded every extra's `options` and the catalog's `genres` since
  install, so installed addons showed no filters; it now keeps them (`slimVersion` 2) and re-fetches rows saved in
  the old shape once when the profile opens (live TMDB Language offers 72 languages). `tmdb.png` was bundled but not
  mapped in `AddonLogos.js`; mapped. Sidebar row index for Movies/Shows/Anime, Manga/Comics and Biblio Explore is
  the next piece (Hemanth: "an index to all the rows").
  Gates: `tst_extensions_first_run` 23/23 (two new cases; negative control red), `tst_discover_sidebar` 18/18
  (negative control red), G-keys green, harnesses as before (Biblio page's known red), JS contracts green.
  Lanista (new build, TARGET_BUILD_OK): `discover_sidebar_catalogues.json` 51/51 twice (ds3c1/ds3c2) incl. category
  picker, Language's filter options, 7/6 columns, dock x 322; `discover_sidebar_seams.json` 34/34 (three worlds'
  source names, 7 columns); `discover_sidebar_filter_regression.json` 52/52 on claude-scroll (fresh-tag Biblio
  catalogue empty as documented). WARNING_GATE_OK on all. Status: Implemented, verification pending (Hemanth's eyes).
