# Arc 41 repair wave report

Date: 2026-09-09 (Asia/Calcutta)
Workspace: `C:\b\colosseum-arc41-scroll`
Branch: `codex/arc41-directional-scroll`
Base candidate before repair: `d0a9f095d3c81741c2864c6b4133119b378dacc1`
Requested route: Luna; host-visible execution identity: Codex/GPT-5 Codex. The requested model string does not independently prove backend identity.

## Repair scope

The review-characterized RED probes were made maintained regressions before production edits. The shared directional path now carries one finite reveal budget through owner selection and fallback, preserves `arrowScrolling: false`, retains the content owner after an over-budget step, and refuses Down export from an exhausted content viewport to unrelated chrome. Deferred landing is bounded and rejects an opt-out owner. `KeyboardViewport.applyPlan` measures nested movement in root coordinates and rejects rotated chains before writes.

Collection continuity now keeps geometric lane state without optional identity callbacks, preserves the original lane through multiple shorter sections, and retains stable identity across incremental reorder/removal when the caller supplies identity/index functions. `LibraryPage` and `ContinueRow` now supply those production seams. `KeyboardRegion.handleKey` preserves modifier filtering by routing through the event-aware navigator entry point. `KeyboardScrollController` unregisters and re-registers on Flickable rebinding, and Home/PageUp/PageDown plus `ScrollGlide` use origin and margin bounds. Main's Biblio detail route captures a local stable focus/scroll snapshot, validates identity, clamps offsets, and restores after the owned layer closes; Home and WorldPage direct navigators now receive release events.

The native QKeyEvent test is explicitly labeled as a metadata-only probe. It distinguishes initial press, auto-repeat press, and final release but does not claim real navigation-owner settlement. No repeat timer was added.

## RED evidence

The pre-repair RED receipt is `artifacts/arc41-directional/red-directional-scroll.txt` and `red-continuity.txt`. Before repair, the maintained assertions failed for external/fallback reveal, two-stage 90-pixel movement against the 72-pixel budget, opt-out fallback, visible bottom chrome export, default grid 3→6→2, multi-section lane return A3→B2→C2→A2, and KeyboardRegion modifier theft. Historical candidate receipts and Astra's corrected characterization remain preserved under `artifacts/arc41-directional/astra-review/`.

## Current focused evidence

All runs used Qt 6.11.1 `qmltestrunner.exe`, `QML_DISABLE_DISK_CACHE=1`, and serial execution.

- `green3-directional-scroll.txt`: 18 passed, 0 failed.
- `green5-continuity.txt`: 16 passed, 0 failed, including default-grid lane, multi-section return, modifier policy, and registry rebind/destruction.
- `green6-primitives.txt`: 8 passed, 0 failed, including nonzero origin/margins and corrected ragged-grid 3→6→3.
- `green6-focus2.txt`: 7 passed, 0 failed.
- `green6-glide.txt`: 3 passed, 0 failed.
- `green6-region.txt`: 9 passed, 0 failed.
- `green6-spatial.txt`: 7 passed, 0 failed.
- Relevant regressions: topbar spatial navigation, K01 reader keyboard, reader keyboard area events, player hotkey events, system focus containment, and vault crumb keys all passed in `artifacts/arc41-directional/aggregate-*.txt`.

`git diff --check` passed. `qmllint -I qml` exited 0; output contains the repository's existing unqualified-access and delegate-injection warning classes, with no new error. `qmlformat -n` exited 0 for changed QML files. A native CMake build and registered `tst_keyboard_key_events` run are verification pending because this worktree has no configured `native/build-msvc` tree; the existing candidate CMake target remains intact.

## Evidence matrix

```text
Qt Test: verification pending (native/build-msvc is not configured in this worktree)
Qt Quick Test: Test-reported (focused suites green; receipts above)
Existing harnesses: not run
Lanista: Bridge blocked (no candidate executable/QML pair in this worktree; do not use the canonical daily binary as candidate proof)
Human aesthetic verdict: pending
Overall: Bridge blocked
```

## Unresolved findings

The six populated Lanista journey families remain unproven: Home, long catalogue/detail, long settings/control surface, populated virtualized collection, reader precedence, and overlay/page return. The Main Biblio return wiring is Implemented but verification pending. Nested scale and rotation coverage is source-repaired by common-coordinate budgeting plus rotation rejection but needs a fresh discriminating fixture. Full native build, registered native event execution, normal packaged startup/manifest parity, and final Astra review remain open. No merge or master mutation was performed.
