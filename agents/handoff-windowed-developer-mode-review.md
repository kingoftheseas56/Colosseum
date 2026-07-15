# Windowed Developer Mode — Codex Review Packet

**Producer:** [Agent 5 (Claude), Library UX]
**Design:** `Brotherhood/docs/superpowers/specs/2026-07-15-colosseum-secret-windowed-developer-mode-design.md`
**Plan:** `Brotherhood/docs/superpowers/plans/2026-07-15-colosseum-secret-windowed-developer-mode.md`
**Branch:** `agent5/windowed-developer-mode`
**Commit range:** `4301a713d50016067c48239889010673390c9128..dc1d5de69382e40f2a2b879523a43ef499daa72a`

Commits (oldest first):

- `864261d` test(window): define safe desktop geometry policy
- `7437e97` feat(window): add persistent fullscreen and desktop states
- `dc1d5de` feat(shell): add secret F11 desktop window

Branched from `origin/master` (`4301a71`) in an isolated worktree; the shared checkout's dirty files were never touched.

## Files changed

- `A native/player/windowstatepolicy.h` — pure geometry-policy interface: `defaultSize()`, `minimumSize()`, `centeredDefault()`, `isMeaningfullyVisible()`, `validatedNormalGeometry()`.
- `A native/player/windowstatepolicy.cpp` — centered default, meaningful-visibility test (≥96px overlap), clamp/recenter of untrusted saved geometry against all connected screens.
- `M native/player/windowmodestore.h` — extends the store into the single shell authority: adds `shellWindowed`/`savedNormalGeometry`/`savedMaximized` properties + `initializeShell`/`toggleShellMode`/`toggleMaximized`/`startSystemMove`/`startSystemResize` invokables. The existing PiP public API (`pipMode`, `enterPip`, `exitPip`, `pipEntered`/`pipExited`) is preserved verbatim; the old private snapshot struct is retired because PiP now restores onto the persisted base mode.
- `M native/player/windowmodestore.cpp` — settings load (untrusted-value normalization), 250ms geometry-persist debounce, fullscreen/windowed transitions, maximize toggle, native system move/resize, `aboutToQuit` final capture, and PiP coexistence (`exitPip` → `applyBaseMode`; `enterPip` snapshots stable normal geometry first).
- `M native/CMakeLists.txt` — compiles `windowstatepolicy.*` into `colosseum`; adds the `window_state_policy_harness` native target (Qt6::Core/Gui/Quick).
- `M qml/Main.qml` — hidden-until-initialized startup, `WindowMode.initializeShell(win)` in `Component.onCompleted`, application-scoped `F11` → `WindowMode.toggleShellMode(win)`, removal of the forced-fullscreen `onVisibilityChanged` restore, and the `WindowBehavior` instantiation. TopBar.qml untouched.
- `A qml/WindowBehavior.qml` — chrome-free desktop interaction: a backmost move/double-click region reparented behind TopBar's controls (z:-1) + eight invisible edge/corner native resize zones with cursors. Self-disables outside normal windowed mode.
- `A tests/window_state_policy_harness.cpp` — deterministic geometry-policy coverage + persisted-state load (windowed base, independent normal geometry, maximized flag).
- `A tests/test_shell_windowed_mode_p0.ps1` — static cross-file wiring/anti-regression contract (F11 door, application scope, native delegation, WindowBehavior shape, removal of the forced restore and the retired toggle).
- `A tests/window_behavior_harness.qml` — QML load + controller-call smoke for WindowBehavior against a fake controller.

## Automated evidence

Build (MSVC 2022 + Qt 6.11.1, Ninja, `native/build-msvc`):

- `cmake --build native/build-msvc --target colosseum window_state_policy_harness` → **PASS** (`BUILD_EXIT=0`; `colosseum.exe` links, harness builds; Qt license-server warning is non-fatal and pre-existing).

Focused suite (Task 4 Step 2):

- `window_state_policy_harness.exe` → **PASS** — `window_state_policy_harness: PASS` (default 1280x720, min 1024x640, centered (320,160,1280,720), secondary-screen preserve, off-screen recenter, undersized→default, partial-clamp (640,320,1280,720), plus persisted windowed/normal-geometry/maximized load).
- `test_shell_windowed_mode_p0.ps1` → **PASS** — `test_shell_windowed_mode_p0: PASS`.
- `test_player_pip_p0_parity.ps1` → **FAIL (pre-existing baseline red)** — dies on `PlayerPage must expose a PiP toggle.` Every WindowModeStore/native-PiP assertion (lines 42–69: WindowStaysOnTopHint, FramelessWindowHint, showNormal, setGeometry, availableGeometry, 480, 320, pipEntered, pipExited, class/Q_PROPERTY/enterPip/exitPip/QQuickWindow) **passes**. The failing assertions are stale `PlayerPage.qml` grep-contracts. Proof it is not from this branch: `git diff origin/master -- qml/PlayerPage.qml tests/test_player_pip_p0_parity.ps1` is empty. See Known gaps.
- `test_player_hotkeys_wiring_p0.ps1` → **PASS** — `Player hotkey wiring checks passed.` (still proves player `F` does not toggle the shell).
- `test_player_minimize_keepalive_p0.ps1` → **PASS** — `Player minimize keep-alive contract checks passed.`
- `test_player_pause_on_minimize_p0_parity.ps1` → **PASS** — `Player pause-on-minimize P0 parity contract checks passed.`

QML load smoke (Task 3 Step 6):

- `colosseum.exe tests/window_behavior_harness.qml` → **PASS** — `qml: [window-behavior-harness] PASS`, no fatal QML diagnostic (also confirmed under `qml.exe`).

## Windows eyes-on matrix

Dev display is **1280x720** at (0,0) with a ~48px taskbar (available height 672). Qt/D3D pixels are uncapturable, so visual rows are marked PENDING for Hemanth/Codex eyes; where behavior is observable through Win32 window geometry + the persisted `HKCU\Software\Brotherhood\Colosseum\window` registry keys, it was driven live and is marked PASS. The exact 1280x720 centering and 1024x640 minimum are proven deterministically by `window_state_policy_harness`; on this small screen the windowed default correctly bounds to the 1280x672 available area.

1. **PASS** — Clean settings → fullscreen. Live: with no `window` key, `STARTUP rect=(0,0)+1280x720`, `baseMode=(unset)` → started fullscreen covering the whole screen.
2. **PASS** — F11 → windowed and back. Live: `F11 → rect=(0,0)+1280x672 baseMode=windowed`; `F11 → rect=(0,0)+1280x720 baseMode=fullscreen`; `F11 → windowed` again. End-to-end chain (application shortcut → native authority → mode flip → registry persist) confirmed. Exact centered 1280x720 verified by the policy harness.
3. **PARTIAL / PENDING eyes-on** — Resize stops at 1024x640. `applyWindowed` calls `setMinimumSize(1024,640)` and the constant is proven by the harness; interactive resize-to-minimum needs eyes-on.
4. **PENDING eyes-on** — Empty TopBar drag works; pills/icons still click. Move region is reparented behind TopBar's controls (z:-1) and `startSystemMove` is wired (smoke-proven); tactile pointer-precedence check pending.
5. **PENDING eyes-on** — TopBar double-click maximize/restore. `TapHandler.onDoubleTapped → toggleMaximized` wired (smoke-proven). On a 1280x720 screen maximize ≈ windowed, so geometry cannot distinguish it live.
6. **PENDING eyes-on** — All four edges + four corners resize with correct cursors. Eight `ResizeZone`s call `startSystemResize` with per-direction cursors (P0 asserts both corners; smoke-proven call). Cursor/resize is visual/tactile.
7. **PENDING eyes-on** — Drag-to-edge and Win+arrow snapping. Uses native Qt `startSystemMove`/`startSystemResize`, which preserve Windows snapping by construction; live snapping check pending.
8. **PASS (windowed) / PENDING (fullscreen)** — Minimize/restore preserves the base mode. Live (windowed): minimize → `iconic=True baseMode=windowed`; restore → `rect=(0,0)+1280x672 baseMode=windowed` (NOT forced fullscreen — validates removing the `onVisibilityChanged` line). Fullscreen minimize/restore relies on native Qt visibility memory; eyes-on pass recommended.
9. **PASS (fullscreen+windowed) / PENDING (maximized)** — Restart persistence. Live: closed while windowed → relaunch `RESTART_STARTUP rect=(0,0)+1280x672 baseMode=windowed`. Clean-start fullscreen is row 1. Maximized-state restore across restart is pending eyes-on.
10. **PASS** — Off-screen saved rectangle recenters. Live: injected `normalGeometry=@Rect(5000 5000 1280 720) baseMode=windowed` → `OFFSCREEN_STARTUP rect=(0,0)+1280x672` (on-screen, never at 5000,5000); registry self-corrected to `@Rect(0 0 1280 672)` on close.
11. **PENDING eyes-on** — At 1024x640, Home/all worlds/search/downloads/extensions/overlays/both readers/player remain usable through existing scrolling/clipping. Visual.
12. **PASS-by-architecture / PENDING behavioral eyes-on** — Route/Loader state across F11 round-trips. `toggleShellMode` only changes the one root window's presentation; it never reloads `Main.qml`, replaces the root, clears a Loader, or restarts media. Behavioral spot-check across a world/reader/video pending.
13. **PENDING eyes-on** — Active video stays playing inside the window across F11. Needs a live stream; single-window design keeps the mpv surface intact across the transition.
14. **GAP — not live-reachable** — PiP round-trips. PiP entry has no UI trigger anywhere in the shipped QML (`WindowMode.enterPip` is called nowhere; PlayerPage's PiP controls were removed on master before this branch). Native coexistence is implemented per spec (`exitPip` → `applyBaseMode` restores the base mode and resets the PiP min/flags; F11-in-PiP exits PiP then flips the base; `enterPip` snapshots stable normal geometry first) and is code-reviewable, but cannot be exercised until a PiP trigger is restored. See Known gaps.
15. **PARTIAL / PENDING eyes-on** — Fullscreen shows no new chrome or layout shift. `applyFullscreen` keeps the same `Qt.Window | Qt.FramelessWindowHint` + `showFullScreen`; startup rect is the full 1280x720; WindowBehavior self-disables in fullscreen. Pixel-level confirmation is Hemanth's eyes.
16. **PASS** — No new fatal QML diagnostic. Live full-app run: no type/binding/assignment errors; WindowBehavior loaded with no required-property warnings. The only `.qml Error` lines are environmental cover-art image 404/BadRequest (`TheatreStrip.qml`, `BootSplash.qml`), present in baseline.

Eyes-on hygiene: all live drives launched the worktree exe in the dev lane (`colosseum.exe qml\Main.qml`, no self-update pull), closed gracefully via WM_CLOSE (no `taskkill`), and the test `HKCU\...\Colosseum\window` key was deleted afterward so Hemanth's Colosseum returns to its clean fullscreen default.

## Scope changes

- `tests/window_behavior_harness.qml`: added one line — `import "../qml"` — beyond the plan's literal snippet. The harness lives in `tests/` but `WindowBehavior` lives in `qml/`; without the relative import the harness cannot resolve the type and the plan's own GREEN outcome is unreachable. Verified loading under both `qml.exe` and `colosseum.exe`.
- `qml/Main.qml`: updated the minimize comment (it described the now-deleted forced-fullscreen snap-back) to state the new behavior. No logic beyond the plan's five wiring edits.
- No files outside the plan's File Map were modified. No minimum-size obstruction fix was required.

## Known gaps

- **PiP round-trip is not live-verifiable (row 14).** No shipped QML calls `WindowMode.enterPip`; the player's PiP UI was removed on `origin/master` before this branch. The native PiP↔base coexistence contract is fully implemented and preserved, but a live PiP entry point would be needed to exercise it. Restoring a PiP trigger is outside this feature's scope.
- **`test_player_pip_p0_parity.ps1` is baseline-red on `origin/master`.** It fails on stale `PlayerPage.qml` assertions (`function togglePipMode`, `WindowMode.enterPip(...)`, `icon: "pip"`, `Qt.Key_P`) that describe a player PiP UI no longer present. Every WindowModeStore assertion passes. Not fixed here: it targets `PlayerPage.qml` (untouched, out of scope) and "green-washing" it would mask the pre-existing player-UI gap. Flagged for a separate decision.
- **Tactile/visual eyes-on rows (3, 4, 5, 6, 7, 11, 12-behavioral, 13, 15) need Hemanth/Codex at the screen.** Geometry/logic evidence is provided where automatable; the 1280x720 dev display means exact 1280x720 centering, the 1024x640 minimum, and maximize-vs-windowed distinctions rest on the deterministic `window_state_policy_harness` rather than live geometry.

## Review request

Review the branch against every Definition of Done item in the design. Pay special attention to:

- saved-geometry validation and the debounce-vs-transition persistence rules in `windowmodestore.cpp` (`captureStableWindowState` only overwrites normal geometry when base is windowed, PiP inactive, visibility exactly `Windowed`);
- PiP/base-mode interaction (`exitPip` → `applyBaseMode`; the F11-in-PiP order; no PiP min/`WindowStaysOnTopHint` leak into developer mode);
- pointer precedence on TopBar controls (the reparented z:-1 move region vs the pills/system icons);
- active-playback and Loader continuity across `F11` (single root window; no scene recreation);
- the two scope notes above.
