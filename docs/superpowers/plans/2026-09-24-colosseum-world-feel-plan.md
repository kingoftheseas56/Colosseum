# Colosseum World Feel — Build Plan

**Date:** 2026-09-24

**Spec:** `docs/superpowers/specs/2026-09-24-colosseum-world-feel-design.md`, approved by Hemanth on 2026-09-24.

**Planner:** Agent 0 (Claude).

**Executes under:** `brotherhood-executing-plans`.

**Branch:** `master` (Rule 28). No worktree unless Hemanth permits one.

## How to read this plan

There are 24 slices in five phases. Each slice lands on its own and carries its own proof. A slice is closed only at the status its Completion criterion names, using the vocabulary in `docs/colosseum-lanista-verification.md`, "Status vocabulary". A green unit suite alone is `Test-reported`, never `Runtime-validated`.

**Working-tree discipline.** Other agents keep uncommitted work in `qml/Main.qml`, `TopBar.qml`, `Taskbar.qml`, `ShellBackPolicy.js`, `TheatreSeries.qml`, `MangaSeries.qml`, `VaultPage.qml` and other files.

- Before each slice, read `git diff` for every file it touches.
- Preserve unrelated hunks.
- Commit with an explicit pathspec (`git commit -- <paths>`), per the Colosseum shared-index rule.

**Standard gates.** Name these in every slice, in addition to the slice's own checks. `ctest` is not on the plain shell PATH. Use a VS developer shell or `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe`. Verified 2026-09-24: `colosseum.qml`, `colosseum.shell_back_arbitration_p0`, `colosseum.back_action_p0` and `colosseum.qttest.gui_responsiveness_probe` are registered (165 tests total).

- `G-unit`: `ctest --test-dir native/build-msvc -L unit --output-on-failure`
- `G-qml`: `ctest --test-dir native/build-msvc -R "^colosseum\.qml$" --output-on-failure`. This opens real windows, so run it on the desktop. For a single file: `native/build-msvc/colosseum_qml_tests.exe -input tests/qml/<file>.qml`.
- `G-lint`: `C:/Qt/6.11.1/msvc2022_64/bin/qmllint.exe <touched .qml files>`. New warnings in touched lines fail the slice.
- `G-back`: `node tests/shell_back_policy_test.mjs` and `ctest ... -R "colosseum\.(shell_back_arbitration_p0|back_action_p0|taskbar_immersive_readers_p0)"`.
- `G-warn`: the warning gate (`tests/warning_gate.ps1`) over each Lanista session's logs. Known-noise classes must be cited by name.

**Isolation.** Every runtime proof uses `native/build-msvc/lanista.exe session run <scenario> --drive --exe native/build-msvc/colosseum.exe --qml qml/Main.qml --tag <slice-tag> --seed tests/lanista-seeds/world-feel-v1 --keep-going --verbose`. Never use the daily app or the default pipe.

**Known bridge limits.** These come from the Lanista ledger; the affected slices plan around them.

- No hover command. Hover behaviour is proven with Qt Quick Test (`mouseMove`) and by a human witness.
- No mouse-Back-button injection. `ui-click` is primary-button only. Covered by Qt Quick Test `mouseClick(..., Qt.BackButton)` plus a human witness.
- No pixel-colour verdicts; pixel checks are dHash only. Focus-ring appearance is proven by Qt Quick Test assertions on the frame item's properties, grabs, and Hemanth's eyes.
- No absence assertions. Where a slice must prove something did not happen, it adds a named counter seam and asserts the counter equals a value with `ui-wait-for` or `qml-get`.
- `ui-wait-for` supports strict equality only. Every wait below waits on an exact value.
- Timing-dependent input such as wheel acceleration cannot be paced through the bridge. It is proven with Qt Quick Test using an injectable clock, plus a human witness.

**Layout verdicts.** Where a slice needs "not covered or not clipped", it uses the runner-local `layout_verdict` step with a checkpoint JSON of `actionableNonzero`, `contained` or `noPeerOverlap` rules. Checkpoints live in `tests/lanista_layout/world-feel/<slice>.json`.

## Phase 0: Prerequisites

### Slice 0: World-feel seed and baseline scenarios

- **Purpose:** Give every later slice a reproducible, committed fixture and a recorded "before" state. Internal; the user sees nothing.
- **Dependencies:** None.
- **Implementation guidance:**
  - Create `tests/lanista-seeds/world-feel-v1/` with a `seed.json` (placement to `roaming`), in the shape of `continue-home-qualification-v1`. Use a synthetic local profile at `profiles/local/`:
    - `collection.ini`: at least 15 Theatre, 6 Tankoban and 6 Biblio entries.
    - `progress.ini`:
      - One Piece (`tt0388629`) mid-series.
      - Two movies mid-way.
      - A manga with chapter progress.
      - A book with page progress.
    - `search-history.ini`: three entries per world.
  - No real user data, because the repo is public. Do not seed `torrent-engine/` or `videos/queue.json`; a resumed queued job crashes boot.
  - Add `tests/lanista_scenarios/world_feel_baseline.json`:
    1. Boot and continue local.
    2. Open Theatre and wait `theatreTab_discover.visible == true`.
    3. Press `Down` 8 times. After each press, `qml-get theatreWorld` reads `automationFocusedObject` and `automationFocusedObjectFullyVisible`.
    4. Grab the window at press 1, press 3 and press 8.
    5. Press `Escape` on the Theatre world and grab.
    6. Press `Escape` on Home, then `ping`. Today this fails with `NO_PIPE`, which is the recorded baseline.
  - Scenarios never hard-code user paths.
- **Behavior to preserve:** The existing seeds and scenarios are unchanged.
- **Baseline:** The baseline scenario itself. Expected today, as observed live on 2026-09-24:
  - Press 1 focuses `Home`. Later presses land on `Genre` or on a TopBar control, with `automationFocusedObjectFullyVisible == false`.
  - The first Escape reaches Home.
  - The second Escape quits the app.
- **Focused tests:**
  - Qt Test: n/a. There is no C++ change.
  - Qt Quick Test: n/a. There is no QML change.
  - Existing harnesses: `tests/test_lanista.ps1` stays green.
  - Negative control: n/a. This slice records a baseline and asserts no regression.
- **Test seam status:** not applicable.
- **Lanista actions:** `session run world_feel_baseline.json --verbose` with the seed.
- **Completion signal:** `ui-wait-for bootSplash.visible == false`, then `ui-wait-for theatreTab_discover.visible == true`.
- **State / events / probes:** The `automationFocusedObject` sequence, recorded verbatim into `docs/world-feel-baseline.md` (new, short).
- **Visual evidence:** Three grabs of the Theatre focus positions and one of the post-Escape Home.
- **Regression paths:** n/a.
- **Evidence artifacts:** `artifacts/lanista-sessions/<id>/` (untracked) and `docs/world-feel-baseline.md` (tracked).
- **Bridge status:** available.
- **Completion criterion:** The seed boots to a populated Home in an isolated session, and the baseline sequence is recorded. Done (internal).

### Slice 1: Preflight prototype intake

- **Purpose:** Decide, on evidence, which parts of Preflight's prototype `WorldPage.qml` become production. Internal decision.
- **Dependencies:** Slice 0, and Preflight's prototype being delivered.
- **Implementation guidance:**
  - Score the prototype against the spec's rules R1–R11 and against the Theatre halfway mock's "Compare with today" behaviours.
  - Record an adoption table in the plan's evidence log. Each prototype part is marked adopt, adapt or rebuild, with a reason.
  - Reject any part that re-introduces a nested scroller, hides the world pills on detail pages, uses a horizontal episode rail, or lets Escape quit from Home.
  - Nothing merges in this slice. Adopted parts land inside Slices 3–9.
- **Behavior to preserve:** n/a.
- **Baseline:** n/a.
- **Focused tests:**
  - Qt Test: n/a. This is a decision slice.
  - Qt Quick Test: if the prototype ships tests, run them with the `colosseum_qml_tests -input <file>` form and record the results.
  - Existing harnesses: n/a.
  - Negative control: n/a.
- **Test seam status:** not applicable.
- **Lanista actions:** Optional. Run `world_feel_baseline.json` with `--qml` pointed at a prototype export in the scratch area, for comparison.
- **Completion signal:** `ui-wait-for theatreTab_discover.visible == true`, if run.
- **State / events / probes:** As in Slice 0.
- **Visual evidence:** Prototype-versus-current grabs, if run.
- **Regression paths:** n/a.
- **Evidence artifacts:** An adoption table appended to this plan's execution log.
- **Bridge status:** not applicable.
- **Completion criterion:** Hemanth has seen the adoption table and ratified it.

### Slice 2: Named automation seams for the world feel

- **Purpose:** Add the read-only status seams that later slices need to prove "didn't happen" and "restored". Internal.
- **Dependencies:** Slice 0.
- **Implementation guidance:** Add invisible status Items in the `localLaunchState` pattern, with exact-value properties.
  - `shellReturnState` (Main):
    - `homeTopReached` (bool)
    - `quitRequested` (int count)
    - `lastRestoredObject` (string objectName)
    - `lastRestoredContentY` (int)
  - `worldFeelState` (Main):
    - `pinnedTabsVisible` (bool)
    - `hoverFocusMoves` (int)
    - `layoutShiftCount` (int, carousel geometry changes after first paint)
  - `thumbnailQueueState`, a Q_PROPERTY projection on `MangaDownloader`:
    - `requested`, `cancelled`, `diskHits`, `inflight`

  All names are unique and world-neutral; they're root singletons, not shared stems. Register them in the Lanista ledger's "Named automation surfaces".
- **Behavior to preserve:** No visual change. No existing objectName is renamed.
- **Baseline:** The names don't exist yet: `qml-get shellReturnState` returns `NO_SUCH_ITEM`.
- **Focused tests:**
  - Qt Test: a new `tst_thumbnail_queue_state` covers counter increments on enqueue, cancel and cache hit. It is registered as `colosseum.qttest.thumbnail_queue_state` (label `unit`).
  - Qt Quick Test: new `tst_world_feel_state.qml` checks that the default values render.
  - Existing harnesses: `manga_downloader_cancel_harness` stays green.
  - Negative control: flip one expected counter and see exactly one named red; then restore it.
- **Test seam status:** available once added.
- **Lanista actions:** Boot, then `qml-get` on each seam.
- **Completion signal:** `ui-wait-for shellReturnState.quitRequested == 0`.
- **State / events / probes:** All defaults read as documented.
- **Visual evidence:** None; the seams are invisible by design.
- **Regression paths:** Boot, then Home.
- **Evidence artifacts:** Session manifest and ledger entry.
- **Bridge status:** available. `qml-get` and `ui-wait-for` are listed.
- **Completion criterion:** `Test-reported` plus the `qml-get` probes green in an isolated session. Done (internal).

## Phase 1: World shell (Theatre first, then Tankoban and Biblio)

### Slice 3: One loud focus look (R1)

- **Purpose:** Wherever focus is, you can see it: a gold ring, plus a lift on tiles.
- **Dependencies:** Slices 1 and 2.
- **Implementation guidance:**
  - `KeyboardAction.qml`: replace the two "quiet aura" rectangles with one 3 px `Theme.gold` ring at full opacity, outset 3 px.
  - Add `focusStyle`: `"control"` (default), `"tile"` or `"primary"`.
    - `tile` adds scale 1.05 and a lift shadow, 200 ms `Easing.OutCubic`.
    - `primary` draws a 3 px `#06070b` gap under the ring and scales to 1.04.
    - Reduced motion (the existing `reducedMotion` plumbing, if present; otherwise a new `Theme.reducedMotion`) drops scale and lift.
  - Adopt the style in `CataloguePosterCard`, `ContinueTile`, `WorldTabBar`/`TheatreTabBar` pills, `FeaturedCarousel`/`CarouselSlide` buttons (Watch/Read as `primary`), genre tiles and Discover cards.
  - Remove any competing per-card halo, frame or `keyboardSelected` border so there is one look.
- **Behavior to preserve:**
  - `KeyboardAction` activation semantics: Enter activates, Space is opt-in, context menu on Menu/Shift+F10.
  - Accessibility names.
  - The `focusOnPointer` behaviour.
- **Baseline:** Slice 0's grabs show a 1 px 28%-alpha ring, and focused gold buttons show no change.
- **Focused tests:**
  - Qt Test: n/a. QML-only.
  - Qt Quick Test: new `tst_focus_look.qml` against production `KeyboardAction`, `CataloguePosterCard` and `ContinueTile`:
    - the ring's `border.width == 3`
    - the ring's `border.color == Theme.gold` with alpha 1
    - the ring is visible only on `activeFocus`
    - `tile` scale reaches 1.05, and 1.0 under reduced motion
    - `primary` shows the dark gap
  - Existing harnesses: `tst_keyboard_primitives_events.qml`, `tst_topbar_spatial_navigation.qml`, `tst_vault_browse_page.qml` (its focus-ring cases) and `tst_biblio_library_focus.qml` stay green.
  - Negative control: set the ring alpha to 0.28 and see exactly the colour case go red; then restore.
- **Test seam status:** available.
- **Lanista actions:** Boot, click `modePill_Theatre`, wait for the tabs, press `Down` 3 times, grabbing the window after each press.
- **Completion signal:** `ui-wait-for theatreTab_discover.visible == true`.
- **State / events / probes:** `qml-get theatreWorld.automationFocusedObject` after each press, which records where focus is.
- **Visual evidence:** Three grabs, each showing an unmistakable gold ring on the focused item. The Watch button's focus must be distinguishable.
- **Regression paths:** Home (`focusHomePrimary`), the Vault grid ring, the Biblio library focus and the account centre.
- **Evidence artifacts:** Session dir and an eyes-on gallery (`artifacts/eyes-on/world-feel/slice3/`).
- **Bridge status:** available.
- **Completion criterion:** `G-qml` green including the new file, the session grabs captured, and `human-witnessed:` Hemanth opens Theatre, presses ↓ three times, and confirms he can always see where focus is. `Runtime-validated` after his confirmation.

### Slice 4: Hover moves the focus (R2)

- **Purpose:** The mouse and keyboard share one focus. There are never two highlighted things.
- **Dependencies:** Slice 3.
- **Implementation guidance:**
  - In `KeyboardAction`, a `HoverHandler` point-motion change (real pointer movement, not a hover that merely starts under a still cursor) calls `forceActiveFocus(Qt.MouseFocusReason)` without scrolling.
  - Suppress hover-focus for 250 ms after any key press, so a still cursor never steals focus back.
  - Remove the separate hover visuals on focusable items: the pill `hot` state, the poster hover lift, the genre-tile hover outline. The focus look is the only highlight.
  - Increment `worldFeelState.hoverFocusMoves`.
- **Behavior to preserve:** Clicks still activate immediately. Right-click context menus, drag in the Vault, and text-field focus are unchanged.
- **Baseline:** The 2026-09-24 walk captured a hover highlight on "Shoujo" while keyboard focus sat on "Magic".
- **Focused tests:**
  - Qt Quick Test: new `tst_hover_focus.qml`:
    - `mouseMove` onto B while A holds focus gives B `activeFocus` and A none.
    - A `keyClick(Qt.Key_Right)` right after moving the pointer continues from B.
    - A still pointer after a key press doesn't reclaim focus.
    - Exactly one item has a visible focus frame.
  - Existing harnesses: `tst_keyboard_spatial_navigator.qml` and `tst_world_keyboard_journey.qml` stay green.
  - Negative control: disable the hover-focus call and see the first case go red.
- **Test seam status:** available.
- **Lanista actions:** None for hover (no hover command).
- **Completion signal:** n/a at runtime. Proven by Qt Quick Test and human witness.
- **State / events / probes:** n/a.
- **Visual evidence:** A human-witnessed recording is optional.
- **Regression paths:** Vault grid drag, the tab bar, the Home widgets and search chips.
- **Evidence artifacts:** `G-qml` log and Hemanth's verdict note.
- **Bridge status:** bridge blocked for runtime hover. The missing capability is `ui-hover` (not in AVAILABLE). Verified by the human witness instead, which is legitimate under the ledger's human-witnessed rule.
- **Completion criterion:** `G-qml` green, and `human-witnessed:` Hemanth moves the mouse across a Theatre row, presses →, and focus continues from the hovered poster with only one highlight ever visible. Then `Runtime-validated (human-witnessed)`.

### Slice 5: Chrome is entered on purpose; system menu; Quit confirms (R5)

- **Purpose:** ↓ can never land in the top bar or on Quit. Minimize, fullscreen, wallpaper and Quit live in one menu, and Quit asks first.
- **Dependencies:** Slice 3.
- **Implementation guidance:**
  - In `WorldPage`'s `KeyboardSpatialNavigator` routing, exclude TopBar descendants from ↓, ←/→ at the content boundary, and ↑ targets except from the first content region. From there, ↑ goes to the pinned tab row (Slice 8), then the current world pill (`modePillFocus_<World>`).
  - TopBar: collapse minimize, fullscreen, wallpaper and power into `systemMenuButton`, a popup inside the root window with items `systemMenuMinimize`, `systemMenuFullscreen`, `systemMenuWallpaper` and `systemMenuQuit`.
  - `systemMenuQuit` opens `quitConfirmDialog` (`quitConfirmYes`/`quitConfirmNo`, focus on No). `Ctrl+Q` still quits directly.
  - Keep `colosseumTopbarUpdateButton`, the account button and `topBarStremioButton` beside the menu.
- **Behavior to preserve:**
  - `F11`, `Ctrl+Q`, the account flyout, Stremio sync, the Update badge.
  - Existing objectNames `modePill_*`, `modePillFocus_*`, `topBarSearch*` and `topBarStremioInput`.
- **Baseline:** Slice 0: `Down` × 8 reaches the power control.
- **Focused tests:**
  - Qt Quick Test:
    - Extend `tst_topbar_spatial_navigation.qml`: ↓ from content never focuses a TopBar child; ↑ from the first region reaches the world pill.
    - New `tst_system_menu.qml`: menu items trigger the existing signals; Quit shows the confirm dialog; Esc or No cancels.
  - Existing harnesses: `tst_world_keyboard_journey.qml`, `tst_keyboard_directional_continuity.qml`, `tst_system_focus_containment.qml`.
  - Negative control: re-admit TopBar items as ↓ candidates and see the new case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_chrome.json`:
  1. Open Theatre.
  2. `Down` × 20, then `qml-get theatreWorld.automationFocusedObject` and `automationFocusedObjectFullyVisible`.
  3. `ui-click systemMenuButton`, wait `systemMenuQuit.visible == true`, click it, wait `quitConfirmDialog.visible == true`, click `quitConfirmNo`, then `ping`.
- **Completion signal:** `ui-wait-for quitConfirmDialog.visible == true`, then `== false` after No.
- **State / events / probes:**
  - After each ↓, `automationFocusedObject` never equals `Home` or any TopBar name, and `automationFocusedObjectFullyVisible == true`.
  - `shellReturnState.quitRequested == 0` after No.
- **Visual evidence:** Grab with the system menu open, and grab with the confirm dialog.
- **Regression paths:** `keyboard_only_shell_smoke.json`, `keyboard_playstation_home_spatial.json` and `keyboard_catalogue_theatre_completion.json`, rerun green. Their TopBar steps may need updating to the new menu names; update them in this slice.
- **Evidence artifacts:** Session dirs.
- **Bridge status:** available.
- **Completion criterion:** All of the above green in an isolated session. `Runtime-validated`.

### Slice 6: Back never quits; Home top; Backspace and mouse Back (R6, part 1)

- **Purpose:** Esc on Home takes you to the top of Home and stops. Backspace and the mouse Back button work like Esc.
- **Dependencies:** Slices 2 and 5.
- **Implementation guidance:**
  - `ShellBackPolicy.actionFor`: the terminal `"quit"` becomes `"homeTop"`.
  - `Main.handleEscape`: `homeTop` scrolls `homePageFlickable` to 0, focuses `focusHomePrimary()`, and sets `shellReturnState.homeTopReached`.
  - Add shell-level `Backspace` routing (ignored when an editable item has focus; use the navigator's `_isEditable` logic) and a root `MouseArea`/`TapHandler` for `Qt.BackButton`. Both route into the same `escapeCommand.invoke`.
  - Update `KeyboardGuidePage` to list Backspace and the mouse Back button.
- **Behavior to preserve:** Every higher-precedence ShellBackPolicy owner (player, readers, sheets, flyouts) keeps its order. Vault's own Backspace ascend is unchanged.
- **Baseline:** Slice 0: Esc on Home ends the session (`NO_PIPE`).
- **Focused tests:**
  - Existing harnesses: `tests/shell_back_policy_test.mjs` updated so the final state expects `homeTop`, never `quit`; `test_shell_back_arbitration_p0.ps1` updated for the new action.
  - Qt Quick Test: new `tst_back_inputs.qml`: `keyClick(Qt.Key_Backspace)` outside a text field invokes Back; inside a TextInput it doesn't; `mouseClick(root, x, y, Qt.BackButton)` invokes Back.
  - Negative control: restore `"quit"` and see the matrix test go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_back_home.json`:
  1. Boot, then `ui-scroll homePageFlickable dy -1800`.
  2. `ui-keypress Escape`, `ui-wait-for shellReturnState.homeTopReached == true`, `ui-wait-for homePageFlickable.contentY == 0`.
  3. `ui-keypress Escape` again, then `ping`.
  4. `ui-keypress Backspace` on Home, then `ping`.
- **Completion signal:** The waits above.
- **State / events / probes:** `shellReturnState.quitRequested == 0` and `ping` replies after each press.
- **Visual evidence:** A grab of Home at the top after Esc.
- **Regression paths:** Esc closes the player, the book reader, the comic reader, Settings, Extensions and Vault in `keyboard_universe_utilities_completion.json` and `arc41_overlay_focus_containment.json`.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available for Esc and Backspace. The mouse Back button is bridge blocked (`ui-click` is primary-only), so it is covered by Qt Quick Test plus `human-witnessed:` Hemanth presses the mouse Back button on a Theatre title page and returns.
- **Completion criterion:** All green, with the human witness recorded. `Runtime-validated`.

### Slice 7: One scroller; gallery-size walls (R3, R11)

- **Purpose:** Discover and Library scroll with the page, not inside a box, and show about eleven posters per row.
- **Dependencies:** Slices 3 and 5.
- **Implementation guidance:**
  - `DiscoverPage`/`DiscoverBrowser` (Theatre and Tankoban), `BiblioDiscoverPage`, `LibraryPage`, `BiblioLibraryPage` and Tankoban's library tab: replace fixed-height self-scrolling GridViews with content laid into `WorldPage`'s board.
  - Grids keep virtualization by using the board's viewport. Use the existing `viewportContentY`/`viewportHeight` seam that `LazyPosterShelf` already consumes, and materialize only rows near the viewport.
  - Page more items when focus or viewport comes within two rows of the end, using a skeleton row at tile geometry.
  - Tiles use `CatalogueVisualMetrics.gallery`: 148×222, 12 px radius, 13 px title.
  - Biblio's Discover wall spans the full content width.
  - Keep `discoverCard_<id>` names.
- **Behavior to preserve:**
  - The NOW BROWSING picker, the lenses, the genre filter and the explicit-content gate.
  - Library ledger filters, the ⋮ menu, library search.
  - `biblioLibraryCard_*` names and `biblioDiscoverPage.freshness`.
- **Baseline:** Grab of the Discover wall scrolled inside its own box (two scrollbars). `qml-get theatreDiscoverPage.height` reads a fixed value, `theatre.height - 200`.
- **Focused tests:**
  - Qt Quick Test:
    - New `tst_wall_single_scroller.qml`: the wall contains no descendant Flickable that is interactive and vertical, and its height equals its content height.
    - Paging triggers within two rows of the end.
    - Materialized delegates stay under 60 for a 600-item model (the virtualization assertion, as in `tst_vault_browse_page.qml`).
  - Existing harnesses: `tst_biblio_library_focus.qml`, `tst_keyboard_directional_scroll.qml`, `tst_keyboard_scroll_focus.qml`.
  - Negative control: restore the fixed height and see the no-nested-scroller case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_walls.json`:
  1. Theatre: `ui-scroll theatreWorldScroll dy -3000` in steps, then `qml-get theatreWorldScroll.contentY` (increases monotonically) and `discoverCard_*` visibility via `ui-query`.
  2. Repeat for `tankobanWorldScroll` and `biblioWorldScroll`.
  3. Keyboard: from the Genre filter, `Down` enters the first wall card (`automationFocusedObject` starts with `discoverCard_`).
  4. A `layout_verdict` with `contained` rules checks that wall cards stay inside the board viewport.
- **Completion signal:** `ui-wait-for biblioDiscoverPage.freshness == "fresh"` before Biblio assertions. For Theatre and Tankoban, wait for `discoverCard_<first id>.visible == true`.
- **State / events / probes:** `worldFeelState` n/a; `theatreWorldScroll.contentHeight` exceeds the viewport by the wall height.
- **Visual evidence:** Walls at gallery size, about 11 per row at 1920.
- **Regression paths:** Tankoban Discover depth (`tankoban_discover_depth` journey), `biblio_scroll_probe*`, `arc41_virtualized_collection.json` and `biblio_catalog_source_smoke.json`.
- **Evidence artifacts:** Session dirs and eyes-on gallery.
- **Bridge status:** available.
- **Completion criterion:** All green, and `human-witnessed:` Hemanth wheels through Theatre Discover and confirms one scroll and one scrollbar. `Runtime-validated`.

### Slice 8: Rows park; tab row pins; one tab bar (R4, Locked 3)

- **Purpose:** The page moves to show what you focus. The tab row stays pinned under the top bar.
- **Dependencies:** Slice 7.
- **Implementation guidance:**
  - Move Theatre from `TheatreTabBar` onto the shared `WorldTabBar`, keeping the `theatreTab_<key>` objectNames through `tabPrefix: "theatreTab"`.
  - `WorldPage` renders a pinned copy, `<world>TabDock` (for example `theatreTabDock`, 46 px tall), under the TopBar once the in-flow bar's top passes y 96. It mirrors `currentTab` and moves focus between the two copies.
  - `KeyboardScrollController`/`KeyboardViewport.js`: "reveal" becomes "park" — the section heading sits 14 px below the chrome (96, plus 64 when pinned).
  - Grids park per row. Rails keep focus at least one tile from the edge. Rails show ‹ › edge buttons on hover and scroll 75% of their width per click.
  - Focus may land only on items fully visible after parking.
  - Set `worldFeelState.pinnedTabsVisible`.
- **Behavior to preserve:**
  - `theatreTab_*`, `tankobanTab_*` and `biblioTab_*` names.
  - The tab-change semantics.
  - Explore's drag-to-reorder (`biblioExploreDrag_*`).
  - `ScrollGlide` wheel motion.
- **Baseline:** Scrolling past the tab bar leaves no tab reachable. In the 2026-09-24 walk, clicks meant for a scrolled-off tab landed on the TopBar.
- **Focused tests:**
  - Qt Quick Test:
    - New `tst_pinned_tabs.qml`: the dock becomes visible when the bar scrolls past 96; a dock click switches tab; focus migrates between the copies.
    - Extend `tst_keyboard_scroll_focus.qml`: the focused row's heading is at the park line ±2 px.
  - Existing harnesses: `tst_keyboard_directional_continuity.qml`, `tst_scroll_glide_wheel.qml`.
  - Negative control: disable the dock and see the pinned-tabs case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_pin_park.json`:
  1. Theatre: `ui-scroll theatreWorldScroll dy -3000`, then `ui-wait-for worldFeelState.pinnedTabsVisible == true`.
  2. `ui-click theatreTabDock_movies` (pinned copy name `<world>TabDock_<key>`), then `ui-wait-for theatreWorld.activeTab == "movies"`.
  3. Keyboard: `Down` through Movies rows. After each press, `automationFocusedObjectFullyVisible == true`.
  4. `layout_verdict` `actionableNonzero` on `theatreTabDock_movies` while scrolled.
- **Completion signal:** The waits above.
- **State / events / probes:** `theatreWorld.activeTab`, `worldFeelState.pinnedTabsVisible`.
- **Visual evidence:** Grab of the pinned dock over the Movies shelves.
- **Regression paths:** `keyboard_catalogue_{theatre,tankoban,biblio}_completion.json`, `keyboard_directional_scroll_continuation.json`.
- **Evidence artifacts:** Session dirs.
- **Bridge status:** available.
- **Completion criterion:** All green in all three worlds. `Runtime-validated`.

### Slice 9: No layout shift, true first frame, transitions (R10)

- **Purpose:** Nothing jumps under the cursor, the clock is right on the first frame, and layers glide in.
- **Dependencies:** Slice 8.
- **Implementation guidance:**
  - `FeaturedCarousel` reserves its final height from first paint and shows a placeholder slide.
  - Rails and walls reserve skeleton geometry.
  - `TopBar`: initialise `clock`/`ampm`/`date` from `new Date()` in `Component.onCompleted`, before first paint; remove the "8:29 PM / Wednesday, June 24" literals.
  - Layers (detail, search, utility) use a 180 ms opacity plus 8 px rise. World switches use a 220 ms cross-fade.
  - Reduced motion makes all of these instant.
  - Set `worldFeelState.layoutShiftCount` when the carousel's scene y changes after first paint.
- **Behavior to preserve:** Carousel autoplay and dots, the Home intro staging timers (the post-first-frame performance work in the test ledger's Responsiveness sections).
- **Baseline:** Cold Theatre grab with an empty carousel and content jumping after load. First-frame placeholder clock.
- **Focused tests:**
  - Qt Quick Test: new `tst_layout_stability.qml`: carousel height is constant from creation to slides-loaded; the TopBar's `clock` equals the current time at completion.
  - Existing harnesses: the Responsiveness-hardening Home staging tests stay green.
  - Negative control: bind the carousel height to slide count and see a red.
- **Test seam status:** available.
- **Lanista actions:** Boot, click `modePill_Theatre`, then `ui-wait-for worldFeelState.layoutShiftCount == 0` after `theatreTab_discover.visible == true`. Grab the window immediately at world open.
- **Completion signal:** The waits above.
- **State / events / probes:** `worldFeelState.layoutShiftCount == 0`.
- **Visual evidence:** First-frame grab with a correct clock and the carousel placeholder.
- **Regression paths:** `continue_home_qualification`, `arc41_world_featured_continue`.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available.
- **Completion criterion:** Green, and `human-witnessed:` Hemanth opens and closes a title page and switches worlds, and judges the motion. `Runtime-validated`.

## Phase 2: Back and search

### Slice 10: The Back ladder and region memory (R6 part 2, R7)

- **Purpose:** Back climbs one step and lands exactly where you were. Re-entering a world keeps your place.
- **Dependencies:** Slices 6 and 8.
- **Implementation guidance:**
  - The world ladder in `WorldPage`: content, then the pinned tab row (current tab), then Featured primary, then Home. Implement it as `ShellBackPolicy` sub-actions `worldTabs` and `worldTop`, emitted before `world`.
  - Return points: every layer open (`theatreSeriesLayer`, `seriesLayer`, `westernLayer`, `comicSeriesLayer`, `bookLayer`, genre and see-all layers, `searchLayer`, `worldSearchLayer`, and the utility layers) records `{world, tab, contentY, focusedObjectName}`. On close it restores them and sets `shellReturnState.lastRestoredObject` and `lastRestoredContentY`.
  - Extend `KeyboardSpatialNavigator`'s existing section-return memory to per-row memory.
  - World re-entry restores the retained world's tab, scroll and focus.
- **Behavior to preserve:**
  - Genre-page Back returning to the exact genre tile, which works today.
  - The Biblio detail return snapshot (`biblioBookRouteState`).
  - The player, reader and One Piece atlas escape owners.
- **Baseline:**
  - Esc from Discover goes to Home.
  - Manga series Back resets the Manga tab to the top.
  - Re-entering Theatre resets scroll. All captured 2026-09-24.
- **Focused tests:**
  - Existing harnesses: `shell_back_policy_test.mjs` matrix extended with `worldTabs`/`worldTop`; `tst_main_book_return.qml` stays green.
  - Qt Quick Test: new `tst_return_points.qml`: open a detail layer from focused tile X at contentY 1234, close it, and X regains focus at 1234 ±1. A world-ladder sequence reaches the expected focus names.
  - Negative control: skip the restore and see the return-point case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_back_ladder.json`:
  1. Theatre Movies: `Down` × 4 (focus on a poster, record the name via `qml-get`).
  2. `Enter`, then `ui-wait-for theatreSeriesPage.visible == true`.
  3. `Escape`, then `ui-wait-for shellReturnState.lastRestoredObject == <recorded name>`.
  4. `Escape`: tab row focused, `automationFocusedObject == theatreTabDock_movies` or `theatreTab_movies`.
  5. `Escape`: Featured primary. `Escape`: Home. `Escape`: Home top, then `ping`.
  6. Leave to Tankoban and return: `theatreWorld.activeTab == "movies"` and `theatreWorldScroll.contentY` equals the value recorded before leaving.
- **Completion signal:** The waits above.
- **State / events / probes:** `shellReturnState.*`, `theatreWorld.automationFocusedObject`.
- **Visual evidence:** Grabs at each rung.
- **Regression paths:** `arc41_biblio_poster_route_return`, `journey_open_manga`, `stremio_task4_theatre_panel`.
- **Evidence artifacts:** Session dirs.
- **Bridge status:** available.
- **Completion criterion:** All green. `Runtime-validated`.

### Slice 11: One search surface in the worlds (Siaran design)

- **Purpose:** Every world searches the same way. Results are rails, Enter does what you expect, and Back returns to your results.
- **Dependencies:** Slices 3, 4 and 10.
- **Implementation guidance:**
  - Converge `SearchSurface` and `BiblioSearch` into one component (keep the file `SearchSurface.qml`; Biblio's series and alternate-source groups become result groups). Layout follows the spec's Search section:
    - Detail frame placeholder until Slice 14 (keep `searchBack` for now).
    - Field with the gold ring.
    - Scope line.
    - Focusable Top Match sized to its content.
    - Grouped rails at gallery size.
    - Recent searches, Try a genre, Surprise me.
  - Remove the window-wide `Shortcut { Return/Enter }`. `Enter` is handled in the field (opens the Top Match once it exists; the hint strip names it) and on focused results.
  - `↓` from the field enters the Top Match. `Esc` clears a non-empty field first. Typed characters return focus to the field.
  - Search is a layer with a return point (Slice 10). Back from a result restores query, results, scroll and focused result.
  - Keep `searchSurfaceInput`, `biblioSearchInput`, `searchResult_<id>` and the recent-chip names.
- **Behavior to preserve:**
  - Per-world history scopes (`SearchHistory`), recent remove.
  - Request cancellation and generation guards; the Responsiveness "search cancellation" gate in the test ledger.
  - Surprise me, genre browse, and Biblio's series and LibGen results.
- **Baseline:**
  - Search → One Piece → Esc returns to the Theatre world, and the query is gone.
  - Arrow keys from the field show no focus.
  - Enter opens the Top Match whatever is focused.
- **Focused tests:**
  - Qt Quick Test: extend `tst_search_history_flow.qml` for the merged component, and resolve its known Biblio flake per the ledger note (the owner reconciles it; don't rerun until green). New `tst_search_surface_keys.qml`:
    - Enter in an empty-results field does nothing.
    - Enter on the third result opens that result, not the Top Match.
    - ↓ from the field focuses the Top Match.
    - Esc clears, then closes.
  - Existing harnesses: the Function 0010 search/discovery P0 contract tests (test ledger section "Function 0010 adoption").
  - Negative control: re-add the window Shortcut and see the Enter-on-third-result case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_search.json` in each world:
  1. Click `topBarSearch` (Theatre), `ui-text-input searchSurfaceInput "one piece"`, `ui-wait-for searchResult_tt0388629.visible == true`.
  2. `Down` twice, `Enter`, `ui-wait-for theatreSeriesPage.visible == true`.
  3. `Escape`, `ui-wait-for searchSurfaceInput.text == "one piece"`, then `qml-get theatreSearchSurface.resultCount`, which is unchanged.
  4. Tankoban and Biblio equivalents (`topBarSearch_Tankoban`, `topBarSearch_Biblio`, `biblioSearchInput`).
- **Completion signal:** The waits above.
- **State / events / probes:** `resultCount`, `searchSurfaceInput.text`, `shellReturnState.lastRestoredObject`.
- **Visual evidence:** Search in each world: Top Match plus rails.
- **Regression paths:** `ratings_reviews_frieren_production.json` (it drives search), `theatre_one_piece_virtualization.json`, `tankoban_catalogue_smoke`.
- **Evidence artifacts:** Session dirs and eyes-on gallery.
- **Bridge status:** available. Live catalogue network is used only at the runtime layer.
- **Completion criterion:** Green in three worlds, and Hemanth's eyes on the search look. `Runtime-validated`.

### Slice 12: Home search (three worlds), type-anywhere, `/` and Ctrl+F, hint strip (R8, R9, Locked 2)

- **Purpose:** Search from anywhere by typing. Home searches Tankoban, Biblio and Theatre, never Siaran. The bottom strip teaches the keys.
- **Dependencies:** Slice 11.
- **Implementation guidance:**
  - Home TopBar: `homeSearchPill` ("Search · or just start typing") beside the clock. The Update glyph stays.
  - Home search uses the same component with `searchMode: "Home"`. Its dispatcher fans out to the three worlds' existing providers and groups rails per world under the world names. No Siaran source.
  - Shell-level type-anywhere: a printable key (letter or digit) with no editable focus, outside readers, player and sheets, opens the current scope's search seeded with that character. `/` and `Ctrl+F` open it too, and `KeyboardGuidePage`'s Ctrl+F entry becomes true.
  - Hint strip: `worldHintStrip`, a 42 px pill at the bottom centre with 3–4 contextual keys. Drop it to 40% opacity during pointer use. A Settings toggle is added in Slice 22.
  - `[`/`]` switch tabs anywhere in a world. `1`/`2`/`3` on Home enter the worlds.
- **Behavior to preserve:** Existing shortcuts (`Ctrl+Shift+V/D/E/S`, `Ctrl+O`, `F11`), text inputs everywhere (the Vault search, library search, account forms) receive their characters untouched.
- **Baseline:** Home has no search. Typing on Home or in a world does nothing. `Ctrl+F` does nothing in the worlds.
- **Focused tests:**
  - Qt Quick Test: new `tst_type_anywhere.qml`:
    - A letter on the world root opens search with that letter.
    - A letter inside a TextInput doesn't.
    - `[`/`]` cycle tabs.
    - `1` on Home enters Tankoban.
  - Extend `tst_keyboard_guide_registry.qml` so every listed binding resolves to a registered command.
  - Existing harnesses: `keyboard_ignition_shortcuts.json` replayed.
  - Negative control: disable the TextInput guard and see a red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_home_search.json`:
  1. Boot, `ui-keypress O` on Home, `ui-wait-for searchSurfaceInput.text == "o"` (home scope), `ui-text-input` the rest of "one piece".
  2. Wait for grouped results: `qml-get homeSearchSurface.groupNames` equals `["Tankoban","Biblio","Theatre"]` in whatever order the spec's layout fixes. Record the order in the slice.
  3. `ui-keypress ]` in Theatre, then `ui-wait-for theatreWorld.activeTab == "movies"`.
- **Completion signal:** The waits above.
- **State / events / probes:** `groupNames`, and the hint strip's `hintText` (new property) for a poster focus.
- **Visual evidence:** Home search grab and hint strip grab.
- **Regression paths:** `keyboard_only_shell_smoke`, `arc41_keyboard_guide_live`, the account forms (`account-signin-happy-path` journey).
- **Evidence artifacts:** Session dirs.
- **Bridge status:** available. `ui-keypress` sends printable ASCII.
- **Completion criterion:** Green, and Hemanth's eyes on Home search ("as glorious as Siaran's"). `Runtime-validated`.

## Phase 3: One frame everywhere

### Slice 13: Shared frame component; pills everywhere (Locked 4)

- **Purpose:** Title, search and utility pages keep the Tankoban · Biblio · Theatre pills, search, and a Back pill that names where you are going.
- **Dependencies:** Slices 5, 10 and 12.
- **Implementation guidance:**
  - New `ShellFrame.qml`, a TopBar variant with `density: "world" | "detail" | "utility"`:
    - Detail: a `frameBackPill` labelled with the return point's destination ("‹ Theatre", "‹ Search · one piece", "‹ Magic"), compact world pills `modePill_*` (reuse the same Pill component and names, scoped so only the visible frame resolves; follow the ledger's DFS-collision warning and world-namespace where needed), search, account and `systemMenuButton`.
    - Utility adds a kicker.
    - World density is today's TopBar plus the slim-on-scroll behaviour (64 px after 40 px of scroll).
  - The frame's world pills are hidden only while `bookReaderLayer`, the comic reader (`embeddedComicReaderOpen()` or `vaultComicLayer`) or the player is active.
  - A pill click on a detail page closes the layer stack to that world, with the return point preserved, so Back from the world returns to the title.
- **Behavior to preserve:** The Stremio door on Theatre surfaces, the account flyout, the Update badge on Home.
- **Baseline:** The four chromes grabbed 2026-09-24 (world, detail black bar, utility chevron, book glass bar). Title pages have no pills.
- **Focused tests:**
  - Qt Quick Test: new `tst_shell_frame.qml`:
    - The three densities render the expected children.
    - The Back pill text follows the return point.
    - The pills are hidden when the `readerActive`/`playerActive` inputs are true.
  - Existing harnesses: `tst_topbar_spatial_navigation.qml`.
  - Negative control: force the pills hidden on detail and see a red.
- **Test seam status:** available.
- **Lanista actions:** Applied in Slices 14–15. This slice proves the component on the Theatre series page: open One Piece via search, then `ui-query modePill_Tankoban` (visible true, `clipChain` clear) and `layout_verdict` `actionableNonzero` on `modePill_Tankoban`, `frameBackPill` and `systemMenuButton`.
- **Completion signal:** `ui-wait-for theatreSeriesPage.visible == true`.
- **State / events / probes:** `frameBackPill.text == "‹ Search · one piece"`.
- **Visual evidence:** Series page with the frame.
- **Regression paths:** `ratings_reviews_frieren_production.json` (detail actions).
- **Evidence artifacts:** Session dir.
- **Bridge status:** available.
- **Completion criterion:** Green. `Runtime-validated`.

### Slice 14: Frame on every title page, art kept

- **Purpose:** Opening any title still looks like Colosseum: art washed in, pills present, facts that fit.
- **Dependencies:** Slice 13.
- **Implementation guidance:** Adopt `ShellFrame` density `detail` in these pages, removing each page's own Back and window controls:
  - `TheatreSeries` (series and movie)
  - `MangaSeries`/`MangaSeriesSharedHeader`
  - `ComicSeries`
  - `BiblioBook`
  - `GenrePage`/`TheatreGenrePage`/`BiblioGenrePage`
  - the genre index pages
  - `TheatreSeeAllPage`
  - `ContinueSeeAllPage`

  Content fixes:
  - Theatre pages wash the backdrop (`sourceBackdrop()`) over the wallpaper.
  - Facts wrap (no clipping).
  - The synopsis gains "More".
  - Cast shows photos when present.
  - Move `tankobanRatingsReviewsAction` out from behind `mangaSeriesModeSwitch`.
  - `BiblioBook`'s torrent list goes into `biblioSourcesDisclosure`, collapsed and labelled "Sources · N", with owned editions first. The primary reads "Read", "Continue · p. N" or "Get this book".
  - Tankoban Collection tiles without covers set the title in type.
- **Behavior to preserve:**
  - Every action on these pages (Watch/Resume, Library, Ratings & Reviews, Notifications, the Tankoban/Chapter mode switch, language, Select, Download, Read Vol. N).
  - The `biblioPrimaryRead`/`biblioPrimaryReadStatus`/`biblioBookBack` semantics (keep `biblioBookBack` as an alias of `frameBackPill` on that page for existing scenarios).
- **Baseline:**
  - The Dune: Part Two page with Country clipped and a black header.
  - The manga header overlap.
  - The book page's grey bar and raw torrent list.
- **Focused tests:**
  - Qt Quick Test: new `tst_detail_pages_frame.qml`: each page instantiates with a `ShellFrame` of density `detail`, and with a long Country fixture the facts' `implicitWidth` stays within the content width.
  - Existing harnesses: `tst_main_book_return.qml`, `tst_next_up_metadata.qml`, `tst_stremio_episode_metadata_bridge.qml`.
  - Negative control: restore `elide`-free single-line facts and see the width case go red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_detail_pages.json`:
  1. Open, through search, the Dune: Part Two movie (`searchResult_tt15239678`), One Piece series, the One Piece manga (Tankoban search), a Biblio book (`biblioSearchInput` "dune"), and a genre page (from Tankoban Manga's genre mosaic, using the page's named entry if one exists; otherwise human-witnessed).
  2. For each: `layout_verdict` with `contained` for the facts block within the content column and `noPeerOverlap` for `tankobanRatingsReviewsAction` against `mangaSeriesModeSwitch`, plus a grab.
- **Completion signal:** `ui-wait-for <page>.visible == true` for each (`theatreSeriesPage`, `mangaSeriesPage`, `biblioBookDetail`).
- **State / events / probes:** `biblioSourcesDisclosure.expanded == false` by default.
- **Visual evidence:** A grab per page with the art visible.
- **Regression paths:** `biblio_downloaded_epub_read_journey.json` (it uses `biblioBookBack`), `journey_open_manga`, `tankoban_chapter_migration`.
- **Evidence artifacts:** Session dirs and eyes-on gallery.
- **Bridge status:** available. The genre-page entry may be human-witnessed if no named tile exists; if so, name the gap.
- **Completion criterion:** All verdicts green, and Hemanth's eyes on the gallery. `Runtime-validated`.

### Slice 15: Frame on utility pages; the taskbar pill (Locked 5)

- **Purpose:** Vault, Extensions, Settings, Downloads, the Keyboard Guide and Sync look like the same app. The taskbar never sits on anything, and it hides while you scroll down.
- **Dependencies:** Slice 13.
- **Implementation guidance:**
  - Adopt `ShellFrame` density `utility` in `VaultPage`, `ExtensionsPage`, `SettingsPage`, `DownloadsPage`, `KeyboardGuidePage` (turned into a sheet over the current page) and `TrackerSyncCenterPage`. Remove each page's floating chevron and window controls.
  - Fix the Vault hero so only one title is drawn during the cross-fade.
  - The Extensions pane tabs become `WorldTabBar`-style pills, keeping `extensionsPaneTab_<key>`. A focused well reorders with `Alt+↑`/`Alt+↓`.
  - `Taskbar.qml`:
    - The closed state becomes a 36 px pill.
    - A root binding hides it after 24 px of downward scroll on the active page's scroller, and shows it on upward scroll, at the page end, or on focus entering it.
    - Expanded icons show labels.
    - Pages reserve bottom padding equal to the pill height plus 16 px.
    - `colosseumTaskbar` gains a `pillHidden` property.
- **Behavior to preserve:**
  - All taskbar entries: Home, Open Media plus Open Recent, Watch Party, Downloads, Extensions, Settings, Keyboard Guide, Sync, session tiles.
  - `tst_open_media_control.qml`, `tst_watch_party_taskbar.qml`, `tst_tracker_sync_center_taskbar.qml`.
  - The Vault `/` and Ctrl+F search.
- **Baseline:** The taskbar covering "Last watched", Genre and the book page's Library button (2026-09-24 grabs). Utility chevrons over the kickers.
- **Focused tests:**
  - Qt Quick Test: new `tst_taskbar_pill.qml`: `pillHidden` becomes true after a simulated 30 px downward scroll, false after an upward one; expanded labels are visible. The taskbar suites above stay green.
  - Existing harnesses: `tst_vault_browse_page.qml`, `tst_vault_detail_sheet.qml`, `tst_tankoyomi_configuration_page.qml`.
  - Negative control: disable the hide binding and see a red.
- **Test seam status:** available.
- **Lanista actions:** A new `world_feel_utilities.json`:
  1. `Ctrl+Shift+V/E/S/D` in turn, each with `ui-wait-for <layer>.active == true` (`vaultLayer`, `extensionsLayer`, `settingsLayer`, `downloadsLayer`).
  2. On each: `layout_verdict` `noPeerOverlap` of `colosseumTaskbar` against every `actionableNonzero` control in the page root, plus a grab.
  3. Theatre Library: `ui-scroll theatreWorldScroll dy -600`, then `ui-wait-for colosseumTaskbar.pillHidden == true`. `ui-scroll dy 240`, then `== false`.
- **Completion signal:** The waits above.
- **State / events / probes:** `colosseumTaskbar.pillHidden`.
- **Visual evidence:** A grab per utility page.
- **Regression paths:** `keyboard_universe_utilities_completion.json`, `vault_launch_smoke.json`, `journey-vault-browse`, `arc35_sync_center_route`, `update-*` journeys.
- **Evidence artifacts:** Session dirs.
- **Bridge status:** available.
- **Completion criterion:** Zero overlap verdicts on all pages, the pill hides and returns, and Hemanth confirms "never on a button". `Runtime-validated`.

## Phase 4: Long lists and the carousel

### Slice 16: Image loading fix for episode stills (decode size and disk cache)

- **Purpose:** One Piece episode pictures appear quickly and stay loaded.
- **Dependencies:** Slice 2.
- **Implementation guidance:**
  - `TheatreSeries.qml` episode `Image`: add `sourceSize` at display size (256×144 device-independent pixels, times the device pixel ratio).
  - Route episode stills through the same disk-cached image path that posters use (the native launcher/image-cache path used by `RoundedPosterImage`/Bookshelf covers; locate the exact provider during the slice and name it in the commit).
  - Load an image only after its row has stayed on screen for 150 ms.
- **Behavior to preserve:** The backdrop fallback when there is no thumbnail, the "E N" text fallback, and the `theatre_one_piece_virtualization` delegate cap.
- **Baseline:** In the 2026-09-24 walk, rows 9 and below showed blank stills after scrolling. The image has no `sourceSize` (code read).
- **Focused tests:**
  - Qt Quick Test: new `tst_episode_still_loading.qml`: the delegate's `Image.sourceSize` equals the display size, and the source stays unset until the dwell elapses (injectable timer).
  - Existing harnesses: `theatre_one_piece_virtualization.json` (runtime).
  - Negative control: remove the dwell and see a red.
- **Test seam status:** available.
- **Lanista actions:** Replay `theatre_one_piece_virtualization.json`, plus 12 `ui-scroll theatreSeriesScroll dy -480` steps and one grab after each. `invoke-read BiblioImageDiag.recentRows` is Biblio-only and does not apply; see the probe line.
- **Completion signal:** `ui-wait-for theatreSeriesPage.loading == false`.
- **State / events / probes:** `qml-get theatreSeriesPage.liveEpisodeDelegateCount` stays within the virtualization cap.
- **Visual evidence:** Grabs after each scroll, showing stills appearing.
- **Regression paths:** `ratings_reviews_frieren_production`.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available for behaviour. There is no image-network probe for Theatre (the ledger lists `BiblioImageDiag` only), so load latency is human-witnessed.
- **Completion criterion:** Green, and `human-witnessed:` Hemanth scrolls One Piece episodes on his connection and the stills keep up. `Runtime-validated (human-witnessed latency)`.

### Slice 17: The long-list ledger component; Theatre episodes on it

- **Purpose:** One Piece shows about eight episodes per screen with a slim header and a range strip for jumping through 1,179 episodes.
- **Dependencies:** Slices 8, 14 and 16.
- **Implementation guidance:**
  - Extract `LongListLedger.qml` from `TheatreSeries`' virtual-space episode window (keep its virtualization math).
  - Sticky bar: 56 px, holding order (`Absolute|Seasons`, only when `animeOrder.absoluteComplete`), the season picker (the existing dropdown past ten seasons), `ledgerGoTo` (number field), count and "Next: N".
  - Rows: 96 px, containing a 128×72 still, number, title, a two-line synopsis, a progress bar, a status only when it's not the default, and Play and Download.
  - Range strip: `ledgerRangeStrip`, ranges of 100, click and drag, with PgUp/PgDn/Home/End and digit-typing into Go-to.
  - It opens parked on Next Up or the last watched episode.
  - Fix the heading so it never says "Season 1" in Absolute order.
  - Keep `theatreEpisodeVirtualSpace`, `theatreEpisodeWindow` and `theatreEpisodePlay_<id>`.
- **Behavior to preserve:** Everything in the spec's "Capabilities that must survive → Episodes", and `liveEpisodeDelegateCount`/`episodeContentHeight`, which the virtualization scenario reads.
- **Baseline:** Sticky chrome about 350 px, rows 156 px, 4.5 rows visible, "Season 1" in Absolute order (2026-09-24 grab).
- **Focused tests:**
  - Qt Quick Test: new `tst_long_list_ledger.qml` with a seeded 1,200-row model:
    - Row height is 96.
    - At least 7 rows are visible in a 1080 px-tall window.
    - Go-to 1071 focuses and parks row 1071.
    - Dragging the range strip to "1001–1100" parks row 1001.
    - The heading reads the range, not "Season 1", in absolute mode.
    - Fewer than 40 delegates are live.
  - Existing harnesses: `tst_next_up_metadata.qml`, `tst_stremio_episode_metadata_bridge.qml`.
  - Negative control: set row height 156 and see the rows-visible case go red.
- **Test seam status:** available.
- **Lanista actions:** Replay `theatre_one_piece_virtualization.json` extended:
  1. `ui-text-input ledgerGoTo "1071"`, `ui-keypress Enter`, then `ui-wait-for theatreSeriesPage.episodeKeyboardIndex == 1070`.
  2. `qml-get theatreSeriesPage.liveEpisodeDelegateCount` stays under the cap.
  3. `layout_verdict` `contained` of the focused row in the viewport.
- **Completion signal:** The waits above.
- **State / events / probes:** `episodeKeyboardIndex`, `effectiveEpisodeOrder`.
- **Visual evidence:** A grab of the ledger at episode 1071.
- **Regression paths:** Full-season download entry (the sources sheet opens), Next Up play, `ratings_reviews_frieren_production`.
- **Evidence artifacts:** Session dir and eyes-on gallery.
- **Bridge status:** available.
- **Completion criterion:** Green, and Hemanth's eyes. `Runtime-validated`.

### Slice 18: Comic issues on the ledger

- **Purpose:** A comic series lists its collections and issues the same way as episodes.
- **Dependencies:** Slice 17.
- **Implementation guidance:**
  - `ComicSeries` release table becomes `LongListLedger` with a pinned "Collections" group, then issues.
  - Read and Download on the focused row, keeping `comicReleaseRow_<id>`, `comicReadAction_<relId>` and `comicDownloadAction_<relId>`.
  - The filter and sort bar parks as the section header.
- **Behavior to preserve:** Pack mode, baked mode, the reader overlay, the collection-before-issue ordering.
- **Baseline:** The glass table, from the code; the live page stalled on 2026-09-24. Reproduce first. If the stall recurs, it goes to Slice P2 before this slice closes.
- **Focused tests:**
  - Qt Quick Test: new `tst_comic_series_ledger.qml`: collections render before issues; the names are preserved.
  - Existing harnesses: `comics_catalogue_intelligence_smoke.json`.
  - Negative control: reverse the grouping and see a red.
- **Test seam status:** available.
- **Lanista actions:** Open a comic series (Tankoban search or the comics tab; if no named entry exists, add `comicTile_<id>` names in this slice, world-namespaced), then `ui-wait-for comicReleaseRow_<first>.visible == true`, and grab.
- **Completion signal:** The wait above.
- **State / events / probes:** Row names present in order.
- **Visual evidence:** A grab of the ledger.
- **Regression paths:** Comic download and read (`comicReadAction_*`).
- **Evidence artifacts:** Session dir.
- **Bridge status:** available once the named entry exists (this slice adds it).
- **Completion criterion:** Green. `Runtime-validated`.

### Slice 19: Carousel wheel acceleration and left alignment (Locked 6)

- **Purpose:** A fast flick of the wheel races through volumes to the 80s. A gentle scroll still steps one.
- **Dependencies:** Slice 14.
- **Implementation guidance:** `MangaTankobanLibrary.qml` `flowWheel`:
  - Accelerate per the spec: notches under 90 ms apart double the step (1, 2, 4, 8, capped at 10), and the step resets after 250 ms idle.
  - `Shift` gives 10.
  - Pixel deltas give one step per 120 px, with the same acceleration.
  - Inject a `clock` function property for tests.
  - The header spacer aligns the first card to the left content margin instead of centring it. Keep resume-centring on the reading volume and `tankobanVolumeCard_<token>` names.
- **Behavior to preserve:** Keys (Left/Right step 1, PgUp/PgDn 10, Home/End), Select mode, the docked Read/Download bar, resume-centring.
- **Baseline:** One volume per notch. The left 40% of the screen is empty (2026-09-24 grab).
- **Focused tests:**
  - Qt Quick Test: new `tst_volume_flow_acceleration.qml` with an injected clock:
    - Notches at 0, 50, 100, 150 ms advance 1+2+4+8.
    - A notch after 300 ms idle advances 1.
    - Shift advances 10.
    - From index 0, a 2-second burst (a notch every 40 ms) reaches index 79 or above in the 113-volume fixture.
  - Existing harnesses: the Tankoban volume-flow gates (the "Tankoban series volume-flow" sections in both ledgers).
  - Negative control: remove the doubling and see the burst case go red.
- **Test seam status:** available.
- **Lanista actions:** Open the One Piece manga, `ui-scroll` 3 notches (bridge pacing is not controllable, so this proves step ≥ 1 only), then `qml-get volumeShelf.focusIndex`.
- **Completion signal:** `ui-wait-for tankobanVolumeCard_1.visible == true` (confirm the token format during the slice).
- **State / events / probes:** `volumeShelf.focusIndex`.
- **Visual evidence:** A grab at rest showing left alignment.
- **Regression paths:** `journey_open_manga`, Read Vol. N.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available for alignment. Acceleration timing is bridge blocked (no paced input), so it is proven by Qt Quick Test plus `human-witnessed:` Hemanth flicks from Vol. 1 and lands in the 80s within two seconds.
- **Completion criterion:** Qt Quick Test green, and Hemanth's witness. `Runtime-validated (human-witnessed feel)`.

### Slice 20: Chapter thumbnail pipeline (dwell, cancel, disk cache)

- **Purpose:** Chapter pictures load only for what you stop on, never for what you fling past, and are remembered.
- **Dependencies:** Slice 2.
- **Implementation guidance:**
  - `MangaDownloader`:
    - Add `cancelThumb(chapterId)` that removes the chapter from the queue or ignores an in-flight settle.
    - Persist thumbnails to a disk cache under the tagged cache root (`CacheLocation/chapter-thumbs/`), keyed by chapter id, with a size cap.
    - Serve disk hits synchronously.
    - Keep `THUMB_CONCURRENCY = 3` unless measurement argues otherwise.
  - `MangaChapterSeriesView`:
    - Request a thumbnail after a 150 ms on-screen dwell.
    - Cancel on delegate destruction or when the row leaves the viewport.
    - Decode at display size.
  - Update `thumbnailQueueState`.
- **Behavior to preserve:**
  - Downloaded chapters use their local first page.
  - The Tankoyomi network policy checks (`pageUrlAllowedBeforeDns`).
  - Chapter downloads.
  - `manga_downloader_cancel_harness`.
- **Baseline:** Thumbnails are fetched eagerly for each materialized row and cached in memory only (code: `MangaDownloader::fetchThumb`, `m_thumbCache` as a `QHash`).
- **Focused tests:**
  - Qt Test: extend `tst_thumbnail_queue_state` (Slice 2):
    - Enqueue 50, cancel 45: at most 5 fetch starts (fake transport).
    - A disk hit after restart emits `thumbReady` without a network start.
    - The cache cap evicts the oldest entries.
    - Uses a fake network; no live network.
  - Qt Quick Test: a delegate requests only after the dwell, and cancels on destruction.
  - Existing harnesses: `manga_downloader_cancel_harness`, `manga_image_host_resolver_harness`.
  - Negative control: disable cancel and see the 50/45 case go red.
- **Test seam status:** available once Slice 2 lands. The fake transport may need a seam; if `MangaDownloader`'s `QNetworkAccessManager` is not injectable, add injection here, as internal.
- **Lanista actions:** Open the One Piece manga, `ui-click mangaModeChapter`, then `ui-wait-for mangaChapterSeriesView.visible == true`. `qml-get thumbnailQueueState` before and after 10 fast `ui-scroll mangaChapterSeriesView dy -960`.
- **Completion signal:** `ui-wait-for thumbnailQueueState.inflight == 0` after the scroll burst.
- **State / events / probes:** `requested - cancelled` stays at or under the number of rows that dwelled.
- **Visual evidence:** A grab after settling.
- **Regression paths:** `tankoban_chapter_migration`, chapter download and read.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available (counter seam from Slice 2).
- **Completion criterion:** Qt Test green, and probe values within bounds in a session. `Runtime-validated`. This slice doesn't change the pages of ten; Slice 21 decides that.

### Slice 21: Chapter Mode continuous list, gated (Locked 7)

- **Purpose:** If thumbnails prove reliable on Hemanth's connection, Chapter Mode becomes one list. If not, it keeps the pages of ten with the new header and a "Go to chapter" field.
- **Dependencies:** Slices 17 and 20.
- **Implementation guidance:**
  - **Gate run first**, with no code change beyond Slice 20:
    - `human-witnessed:` on Hemanth's connection, with the One Piece manga in Chapter Mode, fling to about chapter 900 and stop. At least 90% of the visible thumbnails appear within 2 s, and all within 5 s.
    - The probe shows `requested` grew only for rows that dwelled.
    - Revisit chapter 900: the thumbnails show on first paint (`diskHits` increases).
  - **If the gate passes:** `MangaChapterSeriesView` renders all chapters through `LongListLedger`, with "Chapters 1–10" headings kept as section labels (`MangaChapterGrouping.genericGroups` output flattened into sections) and the range strip jumping by chapter number. "Download page" becomes "Download range…", with a range picker over the same download call.
  - **If the gate fails:** keep the pages of ten, adopt the ledger's sticky bar and row design, and add `chapterGoTo`. Record the failure numbers.
  - Never enable the exact-volume mode (Locked 8).
- **Behavior to preserve:** Language selection, chapter download, read-from-chapter, the Tankoyomi-off message, `mangaChapterPageSelector` (only if the gate fails).
- **Baseline:** Pages of ten, 120 pages for One Piece.
- **Focused tests:**
  - Qt Quick Test: new `tst_chapter_list_modes.qml`:
    - With `continuousChapters: true`, a 1,195-row model renders fewer than 40 live delegates and Go-to 900 parks row 900.
    - With it false, the pages-of-ten selector still works.
    - `MangaChapterGrouping.group` is never called with an exact record (spy).
  - Existing harnesses: the Tankoban catalogue gates in the test ledger.
  - Negative control: pass an exact record and see the spy case go red.
- **Test seam status:** available.
- **Lanista actions:** Open the manga, `ui-click mangaModeChapter`, `ui-text-input ledgerGoTo "900"` (or `chapterGoTo`), `ui-keypress Enter`, then `qml-get` the focused chapter number, plus the thumbnail probe.
- **Completion signal:** `ui-wait-for mangaChapterSeriesView.focusedChapterNumber == "900"` (new property added in this slice).
- **State / events / probes:** `thumbnailQueueState.*`.
- **Visual evidence:** A grab at chapter 900.
- **Regression paths:** Chapter download and the reader open.
- **Evidence artifacts:** Session dir and the gate numbers recorded in this plan's log.
- **Bridge status:** available, plus the human gate.
- **Completion criterion:** The gate is recorded, whichever branch is implemented is green, and Hemanth confirms. `Runtime-validated`.

## Phase 5: Polish and whole-app acceptance

### Slice 22: Settings, Downloads covers, Library filter groups, an honest Keyboard Guide

- **Purpose:** Settings becomes the home for preferences, and the small rough edges go away.
- **Dependencies:** Slices 12 and 15.
- **Implementation guidance:**
  - `SettingsPage` adds:
    - wallpaper (opens the existing wallpaper layer)
    - hint strip on/off (`settingsHintStripToggle`)
    - reduced motion (`settingsReducedMotionToggle`)
    - default world on launch
    - account and device (opens the existing account centre)
  - Downloads cards without covers set the title in type.
  - Library filter bars are split into labelled groups (Theatre: Sort · Type · Status; the equivalent groups in Tankoban and Biblio), so no two chips share a label.
  - `KeyboardGuidePage` lists only bindings registered for the surface it describes.
- **Behavior to preserve:** Explicit Content (`settingsExplicitToggle`), the Ratings & Reviews conversion editor, `downloadsShelfCard_*` names.
- **Baseline:** Settings has two sections. Theatre Library has two "All" chips. The Guide lists Ctrl+F while the worlds didn't bind it (fixed in Slice 12; verify here).
- **Focused tests:**
  - Qt Quick Test: new `tst_settings_world_feel.qml` (toggles persist through the injected settings record layer, per the ledger's conversion learning); extend `tst_keyboard_guide_registry.qml` so every listed binding exists.
  - Existing harnesses: `arc41_settings_overflow` scenario.
  - Negative control: list an unregistered binding and see a red.
- **Test seam status:** available.
- **Lanista actions:**
  1. `Ctrl+Shift+S`, then `ui-click settingsHintStripToggle`, then `ui-wait-for worldHintStrip.visible == false`.
  2. `layout_verdict` `actionableNonzero` on the new controls.
- **Completion signal:** The wait above.
- **State / events / probes:** The toggle state.
- **Visual evidence:** A Settings grab.
- **Regression paths:** `arc41-settings-overflow`, `ratings-reviews-delivery`.
- **Evidence artifacts:** Session dir.
- **Bridge status:** available.
- **Completion criterion:** Green. `Runtime-validated`.

### Slice 23: Whole-app acceptance replay

- **Purpose:** Prove the spec's 16 acceptance criteria together on the assembled app, and hand Hemanth one gallery.
- **Dependencies:** All earlier slices.
- **Implementation guidance:**
  - Compose `world_feel_acceptance.json` from the per-slice scenarios, one section per spec criterion, each opened with a `log-mark`.
  - Run it twice (cold seed, then warm), then run the Colosseum harness verify for the touched paths (`python tools/colosseum-harness/run.py --root . verify --path <each touched file> --run`).
- **Behavior to preserve:** Every regression path named above.
- **Baseline:** The Slice 0 baseline session, for before/after.
- **Focused tests:** `G-unit`, `G-qml`, `G-back` and `G-lint` all green in one run.
  - Negative control: not applicable to a replay slice. Each constituent carries its own.
- **Test seam status:** available.
- **Lanista actions:** `session run world_feel_acceptance.json` × 2.
- **Completion signal:** Each section's own waits.
- **State / events / probes:** Per criterion.
- **Visual evidence:** A before/after eyes-on gallery (`artifacts/eyes-on/world-feel/final/`), side by side with the 2026-09-24 audit grabs.
- **Regression paths:** The full keyboard scenario family (`keyboard_*`), `continue_home_qualification`, `journey_*`, `vault_*`, `ratings_reviews_*`, `stremio_task4_theatre_panel`, `update-*`.
- **Evidence artifacts:** Session manifests, gallery, and harness run receipts.
- **Bridge status:** available. Hover, mouse Back, acceleration feel and thumbnail latency remain human-witnessed, as recorded in their slices.
- **Completion criterion:** Every criterion green or human-witnessed-confirmed, `G-warn` clean with known noise named, and Hemanth signs the gallery. `Runtime-validated`.

## Parallel track: performance

This track runs alongside Phases 1–4 and is not gated by them.

### Slice P1: Profiling run

- **Purpose:** Rank the causes of jank with evidence, per the 2026-09-24 profiling handoff.
- **Dependencies:** Slice 0 (the seed).
- **Implementation guidance:**
  - Run Lanista sessions with the environment variables `QT_LOGGING_RULES=qt.scenegraph.time.renderloop=true;qt.scenegraph.time.renderer=true` and `QT_MESSAGE_PATTERN=%{time process} %{category}: %{message}`.
  - Segments, each opened with a `log-mark`: idle, Theatre Discover scroll, Movies scroll, keyboard runs, Biblio Explore scroll, and Tankoban → Comics then `Down` (the 2026-09-24 stall).
  - Parse the frame intervals. Rank these suspects: QML not compiled ahead of time, live Glass blur, per-frame bindings, nested scrollers (removed by Slice 7; measure before and after), and retained worlds.
- **Behavior to preserve:** n/a (measurement).
- **Baseline:** This slice *is* the baseline.
- **Focused tests:** The existing `colosseum.qttest.gui_responsiveness_probe` stays green. n/a otherwise.
- **Test seam status:** not applicable.
- **Lanista actions:** `session run` with `log-mark` segments, `ui-scroll` and `ui-keypress`.
- **Completion signal:** The waits per segment.
- **State / events / probes:** Stderr render-loop lines.
- **Visual evidence:** None.
- **Regression paths:** n/a.
- **Evidence artifacts:** A report at `docs/world-feel-perf-baseline.md`.
- **Bridge status:** available.
- **Completion criterion:** The ranked report is written. It proposes the frame target: under 5% of frames over 20 ms in scroll and focus segments, which Hemanth confirms or revises.

### Slice P2: Fix the top-ranked causes

The fixes come from P1's ranking and each gets its own sub-slice with the full field contract. Candidates:

- `qt_add_qml_module` plus qmlsc.
- One shared, pre-blurred wallpaper for all Glass panels.
- Removing per-frame bindings.
- The Comics-tab stall.

Each sub-slice is `Runtime-validated` by re-running P1's segments and meeting the confirmed target.

## Execution log

Append the Slice 1 adoption table, gate numbers, human-witness verdicts and status changes here.
