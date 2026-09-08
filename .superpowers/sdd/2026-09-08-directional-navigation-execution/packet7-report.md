# Packet 7 report — scoped candidate integration and durable closeout

Date: 2026-09-09 (Asia/Calcutta)
Workspace: `C:\\b\\colosseum-arc41-scroll`
Branch: `codex/arc41-directional-scroll`
Base/master: `32b742dd2955347415e5a3413a8e1eefb815101e`
Feature commit: `c34ad13748797d06f11f8ba4bdb286e2407ac1d3`
Feature push verification: `origin/codex/arc41-directional-scroll` resolved to the same `c34ad13748797d06f11f8ba4bdb286e2407ac1d3`
Execution identity: requested Luna route; host-visible execution was Codex/GPT-5 Codex, with no independently observable Luna identity.

## Scope and commit boundary

The feature commit contains exactly these 12 intended files:

- `qml/KeyboardCollectionController.qml`
- `qml/KeyboardRegion.qml`
- `qml/KeyboardScrollController.qml`
- `qml/KeyboardSpatialNavigator.qml`
- `qml/KeyboardViewport.js`
- `tests/CMakeLists.txt`
- `tests/auto/keyboard/tst_keyboard_key_events.cpp`
- `tests/lanista_scenarios/keyboard_directional_scroll_continuation.json`
- `tests/qml/tst_keyboard_directional_continuity.qml`
- `tests/qml/tst_keyboard_directional_scroll.qml`
- `tests/qml/tst_keyboard_primitives_events.qml`
- `tests/qml/tst_keyboard_scroll_focus.qml`

No generated artifacts, cached output, or unrelated files were staged. Existing Packet 1–6 evidence remains in the worktree under this SDD folder and `artifacts/arc41-directional/`; only this Packet 7 report is added as closeout documentation.

## Current verification

The exact committed candidate was checked with `QML_DISABLE_DISK_CACHE=1` and Qt 6.11.1 `qmltestrunner.exe`. All 14 load-bearing suites passed serially: **93 passed, 0 failed, 0 skipped**. Receipts are `artifacts/arc41-directional/packet7-<suite>.qml.txt` and the summary is `artifacts/arc41-directional/packet7-qt-suite-summary.json`.

`qmlformat -n` passed, `qmllint -I qml` passed, and `git diff --check` passed. The lint receipt contains only the existing warning classes recorded by Packet 6: implicit delegate `index`/`modelData` access, one `SignalSpy.count` type warning, and the existing unused `QtQuick.Controls` import in the scroll-focus fixture. Packet 4's temporary native key-event harness remains valid evidence at 3/3; a full candidate native build was not performed because the worktree has no configured `native/build-msvc` tree.

The mechanical status matrix is:

```text
Qt Test: pass (inherited Packet 4 temporary key-event target, 3/3)
Qt Quick Test: pass (93/93 current committed candidate)
Existing harnesses: not run in Packet 7
Lanista: bridge blocked; inherited Packet 5 attempt timed out
Human aesthetic verdict: pending
Overall: Bridge blocked
```

## Packet 5 blocker carried forward

Packet 5's isolated Lanista exploration used the canonical `colosseum.exe` with candidate QML because this worktree has no candidate executable pair. It reached 13 setup/interaction passes, then timed out on the final Home visibility/content read:

```text
INFRA ERROR: TIMEOUT: no reply from ColosseumLanista-20260909-011017-51bb4c37 within 10000 ms
```

That attempt is not runtime acceptance. Home crossing, a long Tankoban/Theatre/Biblio surface, Settings/Extensions, a populated virtualized list/grid, Reader precedence, and overlay/page return remain unproven in the running candidate. No second long-running Lanista session was started. Packet 6's eight-clause matrix is mechanically `MET` but runtime-qualified `PARTIAL`; Astra review and runtime unblock remain required before integration into master.

Master was not merged or pushed. `origin/master` remained at `32b742dd` during Packet 7.

## Handoff to Astra

Review the committed diff and the Packet 1–6 receipts against the approved design. Attack the plan's load-bearing cases: far-offscreen jumps, fixed-clip leakage, pure-scroll ownership, virtualized identity/index movement, ragged-row return after reorder, release overshoot, modifier/editable precedence, nested scroll ownership, bottom-to-taskbar escape, and overlay restoration. Treat this branch as a pushed, mechanically Test-reported candidate awaiting runtime evidence and Astra final acceptance.
