# Windowed Developer Mode — Codex Review Packet

**Producer:** [Agent 5 (Claude), Library UX]
**Design:** `Brotherhood/docs/superpowers/specs/2026-07-15-colosseum-secret-windowed-developer-mode-design.md`
**Plan:** `Brotherhood/docs/superpowers/plans/2026-07-15-colosseum-secret-windowed-developer-mode.md`
**Branch:** `agent5/windowed-developer-mode`
**Commit range:** `4301a713d50016067c48239889010673390c9128..4f08b39c35dd15b31153d370837a42dbc496980f`

Commits (oldest first):

- `864261d` test(window): define safe desktop geometry policy
- `7437e97` feat(window): add persistent fullscreen and desktop states
- `dc1d5de` feat(shell): add secret F11 desktop window
- `1c3132b` docs(window): hand off verified desktop mode for review
- `4f08b39` fix(window): address Codex review — live-reload re-attach, behavioral + PiP coverage

Branched from `origin/master` (`4301a71`) in an isolated worktree; the shared checkout's dirty files were never touched. (`origin/master` has since fast-forwarded to `d8004f0` with Agent 1's comics work — a clean ancestor of this base, no file overlap.)

## Round 2 — Codex review findings addressed (all three P1)

**P1-1 — QML live reload lost the replacement window. FIXED + proven live.**
`initializeShell()` previously bailed whenever `m_window` was already set. Colosseum's dev reloader (`main.cpp` `reload()`, lines 183-190) loads the replacement root *before* deleting the old one, so the new `Main.qml` never got `applyBaseMode` and stayed `visible:false`. Fix: `initializeShell()` now detaches the old root (`disconnect`) and adopts the replacement; the `aboutToQuit` capture is hooked once in the constructor so re-attach can't register it twice. Evidence:
- `window_shell_gui_harness` Test A models the exact load-before-delete lifecycle (init first → init replacement while first still registered → replacement must be shown): **OK**.
- Live in the real app (dev reloader on): startup fullscreen; on a watched-file change the root window handle changed `17958542 → 10881140` (the reloader genuinely replaced it) and the new window was `(0,0)+1280x720` **visible**; `[dev] reloaded` logged once.

**P1-2 — Interaction harness was an unconditional green. FIXED.**
`window_behavior_harness.qml` is now a QtTest `TestCase` (run under `qmltestrunner`) that drives the real drag, double-click, and edge-resize paths and asserts the fake-controller counters. While writing it, the double-click never fired: `WindowBehavior`'s move region used a `DragHandler` + `TapHandler` pair that contended for the pointer grab and dropped the double-tap. That region is now a single `MouseArea` (drag past a 6px threshold → `startSystemMove`; `onDoubleClicked` → `toggleMaximized`), which disambiguates cleanly. Evidence: `qmltestrunner` → **6 passed, 0 failed** (preconditions, double-click→maximize, edge→resize, drag→move).

**P1-3 — PiP round-trip unproved. FIXED with automated native tests.**
`window_shell_gui_harness.cpp` (offscreen QPA) drives the native service directly:
- fullscreen → PiP → fullscreen: base restored, always-on-top dropped — **OK**;
- windowed → PiP → windowed: base restored, persisted normal geometry uncorrupted, windowed minimum (not the PiP 360x240) restored, always-on-top dropped — **OK**;
- F11-while-in-PiP: exits PiP first, then flips the base — **OK**.
(The interactive UI cannot enter PiP — see Known gaps — but the state machine is now covered.)

## Files changed

- `A native/player/windowstatepolicy.h` / `.cpp` — pure geometry policy: default/min size, centered default, meaningful-visibility (≥96px), clamp/recenter of untrusted saved geometry against all screens.
- `M native/player/windowmodestore.h` / `.cpp` — the single shell authority. Adds `shellWindowed`/`savedNormalGeometry`/`savedMaximized` + `initializeShell`/`toggleShellMode`/`toggleMaximized`/`startSystemMove`/`startSystemResize`; keeps the PiP public API verbatim but restores PiP onto the persisted base mode. `initializeShell` supports the live-reload re-attach lifecycle; `aboutToQuit` hooked once in the ctor.
- `M native/CMakeLists.txt` — compiles `windowstatepolicy.*` into `colosseum`; adds `window_state_policy_harness` and `window_shell_gui_harness` targets.
- `M qml/Main.qml` — hidden-until-initialized startup, `initializeShell`, application-scoped `F11` → `toggleShellMode`, removal of the forced-fullscreen restore, `WindowBehavior` install. TopBar.qml untouched.
- `A qml/WindowBehavior.qml` — chrome-free desktop interaction: one `MouseArea` (reparented behind TopBar controls, z:-1) for unused-space drag + double-click maximize, plus eight invisible edge/corner native resize zones with cursors. Self-disables outside normal windowed mode.
- `A tests/window_state_policy_harness.cpp` — deterministic geometry-policy + persisted-state load coverage.
- `A tests/window_shell_gui_harness.cpp` — offscreen-QPA GUI harness: PiP round-trips + F11-in-PiP + live-reload re-attach.
- `A tests/test_shell_windowed_mode_p0.ps1` — static cross-file wiring/anti-regression contract.
- `A tests/window_behavior_harness.qml` — QtTest behavioral TestCase driving drag/double-click/resize and asserting the controller.

## Automated evidence

- Build `colosseum window_state_policy_harness window_shell_gui_harness` (MSVC 2022 + Qt 6.11.1, Ninja) → **BUILD_EXIT=0** (Qt license-server warning is non-fatal, pre-existing).
- `window_state_policy_harness.exe` → **PASS** (default 1280x720, min 1024x640, centered (320,160,1280,720), secondary-screen preserve, off-screen recenter, undersized→default, partial-clamp (640,320,1280,720), persisted windowed/normal-geometry/maximized load).
- `window_shell_gui_harness.exe -platform offscreen` → **PASS** (fullscreen PiP round-trip, windowed PiP round-trip, F11-in-PiP ordering, live-reload re-attach).
- `qmltestrunner -input tests/window_behavior_harness.qml -platform offscreen` → **6 passed, 0 failed** (interactive-state preconditions, double-click→maximize, edge-press→resize, TopBar-drag→move).
- `test_shell_windowed_mode_p0.ps1` → **PASS**.
- `test_player_hotkeys_wiring_p0.ps1` → **PASS** (still proves player `F` does not toggle the shell).
- `test_player_minimize_keepalive_p0.ps1` → **PASS**.
- `test_player_pause_on_minimize_p0_parity.ps1` → **PASS**.
- `test_player_pip_p0_parity.ps1` → **FAIL (pre-existing baseline red)** — dies on `PlayerPage must expose a PiP toggle.` All WindowModeStore/native-PiP assertions pass; the failing assertions are stale `PlayerPage.qml` grep-contracts. `git diff origin/master -- qml/PlayerPage.qml tests/test_player_pip_p0_parity.ps1` is empty (not from this branch). See Known gaps.

## Windows eyes-on matrix

Dev display is **1280x720** at (0,0) with a ~48px taskbar (available 1280x672). Qt/D3D pixels are uncapturable, so purely visual rows are marked PENDING for Hemanth/Codex eyes; behaviors observable through Win32 geometry, the persisted registry keys, or the automated harnesses are marked PASS. Exact 1280x720 centering and the 1024x640 minimum are proven deterministically by `window_state_policy_harness`.

1. **PASS** — Clean settings → fullscreen. Live: no `window` key → `STARTUP rect=(0,0)+1280x720`, `baseMode=(unset)`.
2. **PASS** — F11 → windowed and back. Live: `F11 → (0,0)+1280x672 baseMode=windowed`; `F11 → (0,0)+1280x720 baseMode=fullscreen`. Exact centered 1280x720 by the policy harness.
3. **PARTIAL / PENDING eyes-on** — Resize stops at 1024x640. `applyWindowed` sets `setMinimumSize(1024,640)`; the constant is harness-proven; interactive resize-to-minimum needs eyes-on.
4. **PASS (behavioral) / PENDING eyes-on (pointer precedence)** — Unused TopBar drag calls the native move (`window_behavior_harness` test_3). That controls keep first claim over unused space is structural (move region reparented at z:-1 behind TopBar's children); the live "pills still click while empty space drags" feel is pending eyes-on.
5. **PASS (behavioral)** — TopBar double-click maximizes/restores. `window_behavior_harness` test_1 asserts `toggleMaximized` fires (the MouseArea fix). On the 1280x720 dev screen maximize ≈ windowed, so this is proven by the behavioral test, not live geometry.
6. **PASS (behavioral, edges) / PENDING eyes-on (cursors)** — Edge press starts a native resize (`window_behavior_harness` test_2); all eight zones exist with per-direction cursors (P0 asserts both corners). The cursor visuals are pending eyes-on.
7. **PENDING eyes-on** — Drag-to-edge and Win+arrow snapping. Native Qt `startSystemMove`/`startSystemResize` preserve OS snapping by construction; live snapping is pending.
8. **PASS (windowed) / PENDING (fullscreen)** — Minimize/restore preserves the base mode. Live (windowed): minimize → `iconic baseMode=windowed`; restore → `(0,0)+1280x672 baseMode=windowed` (no forced fullscreen). Fullscreen minimize/restore relies on native visibility memory; eyes-on recommended.
9. **PASS (fullscreen+windowed) / PENDING (maximized)** — Restart persistence. Live: closed windowed → relaunch `(0,0)+1280x672 baseMode=windowed`. Clean-start fullscreen is row 1. Maximized restore across restart is pending eyes-on.
10. **PASS** — Off-screen saved rectangle recenters. Live: injected `@Rect(5000 5000 1280 720)` → opened `(0,0)+1280x672` on-screen; registry self-corrected on close.
11. **PENDING eyes-on** — At 1024x640 every route stays usable through existing scrolling/clipping. Visual.
12. **PASS-by-architecture / PENDING behavioral eyes-on** — Route/Loader state across F11. `toggleShellMode` only changes the one root window's presentation; it never reloads `Main.qml`, replaces the root, clears a Loader, or restarts media. (The live-reload path, which *does* replace the root, now re-attaches correctly — P1-1.) Behavioral spot-check across a world/reader/video pending.
13. **PENDING eyes-on** — Active video stays playing inside the window across F11. Needs a live stream.
14. **PASS (native automated) / GAP (live UI entry)** — PiP returns to both base modes. `window_shell_gui_harness` proves fullscreen→PiP→fullscreen, windowed→PiP→windowed (geometry + minimum preserved), and F11-in-PiP. No shipped QML calls `WindowMode.enterPip`, so it cannot be entered from the running UI (pre-existing — see Known gaps).
15. **PARTIAL / PENDING eyes-on** — Fullscreen shows no new chrome/layout shift. `applyFullscreen` keeps the same frameless flags + `showFullScreen`; startup rect is the full 1280x720; WindowBehavior self-disables in fullscreen. Pixel-level confirmation is Hemanth's eyes.
16. **PASS** — No new fatal QML diagnostic. Live full-app run: no type/binding/assignment errors; WindowBehavior loads with no required-property warnings. Only environmental cover-art image 404s.

Eyes-on hygiene: all live drives launched the worktree exe in the dev lane (no self-update pull), closed gracefully via WM_CLOSE (no `taskkill`), reverted `Main.qml` after the live-reload probe, and deleted the test `HKCU\...\Colosseum\window` key so Hemanth's Colosseum returns to its clean fullscreen default.

## Scope changes

- `qml/WindowBehavior.qml`: the move/double-click region changed from a `DragHandler` + `TapHandler` pair to a single `MouseArea` (P1-2 fix — the pair dropped the double-tap). All P0-contract strings preserved.
- `tests/window_behavior_harness.qml`: rewritten from a load-smoke into a QtTest `TestCase` (P1-2). It keeps the earlier `import "../qml"` addition (needed to resolve WindowBehavior from `tests/`).
- `tests/window_shell_gui_harness.cpp` + its CMake target: new file (P1-3 native PiP/re-attach coverage), beyond the original plan File Map.
- `qml/Main.qml`: the minimize comment was updated to match the removed forced-fullscreen line (no logic beyond the plan's wiring edits).
- No other files outside the plan's File Map were modified. No minimum-size obstruction fix was required.

## Known gaps

- **PiP has no live UI entry point.** No shipped QML calls `WindowMode.enterPip`; the player's PiP controls were removed on `origin/master` before this branch. The native PiP↔base coexistence is now proven by `window_shell_gui_harness`, but a running-UI round-trip needs a PiP trigger restored — out of this feature's scope.
- **`test_player_pip_p0_parity.ps1` is baseline-red on `origin/master`** (stale `PlayerPage.qml` assertions for that removed PiP UI). All WindowModeStore assertions pass. Not fixed here (out of scope; green-washing it would mask the pre-existing player-UI gap). Flagged for a separate decision.
- **Purely visual eyes-on rows (3 resize-to-min, 6 cursors, 7 snapping, 11 small-size usability, 12 behavioral spot-check, 13 video, 15 fullscreen pixels) need Hemanth/Codex at the screen.** Automated geometry/logic evidence is provided where possible; the 1280x720 dev display is why exact centering, the minimum, and maximize-vs-windowed rest on the deterministic harnesses rather than live geometry.

## Review request

Review the branch against every Definition of Done item in the design. Focus on: saved-geometry validation + the debounce-vs-transition persistence rules; the live-reload re-attach lifecycle in `initializeShell`; PiP/base-mode interaction; the `MouseArea` move/double-click disambiguation and pointer precedence over TopBar controls; active-playback/Loader continuity across `F11`; and the scope notes above.
