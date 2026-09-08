# Arc 41 Astra final re-review 2 repair — 2026-09-09

## Scope and identity

This repair stays on `codex/arc41-directional-scroll` in `C:/b/colosseum-arc41-scroll` from `8ab52cbf3080e72c66c29a78486fed7b690574d7`. The unrelated unstaged deletions `docs/build/linux.md`, `docs/build/macos.md`, and `docs/build/windows.md` were preserved exactly. Existing receipts and artifacts were preserved. Host-visible execution identity is Codex / GPT-5 Codex; the requested Luna route is not independently observable.

The scoped implementation touches only the existing keyboard navigation and Main return seams plus their focused QML tests. No account-page or broad page-family changes were made, and no global history service or delegate/model scan was introduced.

## Findings and repairs

- **S1/P1 WorldPage rail crossings — Implemented; Test-reported.** `KeyboardSpatialNavigator` now records section transitions after a successful nested-owner land and after the outer Flickable owner path succeeds. Semantic collection owners select the intended delegate index while retaining owner focus. The maintained WorldPage-like fixture uses two actual nested rail Flickables, delivered key events, a shorter destination rail, inverse Up return, local offsets, and a lateral re-anchor. Receipt: `artifacts/arc41-directional/wave5-final-continuity.txt`.
- **S2/P1 lateral reset — Implemented; Test-reported.** `KeyboardCollectionController` clears lane and section intent before every accepted Left/Right path, including successful movement. The same fixture proves A→B, lateral movement in B, then Up selects the newly anchored A identity rather than stale return state. Receipt: `artifacts/arc41-directional/wave5-final-continuity.txt`.
- **S3/P2 pre-write validation — Implemented; Test-reported.** `_restoreSectionReturn` validates route/root and owner lifecycle, containment, opacity, transform chain, scroll opt-out, target eligibility, and target transform before writing `currentIndex` or offsets. WorldPage clears its coordinator on lifecycle, visibility, or enabled loss. The hidden-owner test proves rejected restore leaves index and offset unchanged. Receipt: `artifacts/arc41-directional/wave5-final-continuity.txt`.
- **S4/P2 unrealized target — Implemented; Test-reported.** Main distinguishes a null collection delegate from a non-collection owner, invokes the owner `keyboardRevealIndex`/`positionViewAtIndex` seam, re-queries realization, and focuses only the realized visible target. The real Main fixture proves a null target requests one reveal before focus. Receipt: `artifacts/arc41-directional/wave5-final-main.txt`.

Prior N2 transform no-write, W1 coverage, W3 offset, and W4 release repairs remain in the candidate and were not regressed by this wave. The corrected RED receipts are preserved at `artifacts/arc41-directional/wave5-red-s1s2.txt` and `artifacts/arc41-directional/wave5-red-s4-main.txt`; they show the maintained regressions before the corresponding repairs. The final focused runs are Test-reported green: directional 23/0, continuity 21/0, primitives 8/0, scroll-focus 7/0, Main return 5/0, and Biblio harness `BIBLIO_LIBRARY_OK`.

## Verification and remaining gates

`qmlformat` returned zero for all six changed QML/test files. `qmllint -I qml` returned exit 0 with existing warnings; `git diff --check` returned zero. Raw static output is `artifacts/arc41-directional/wave5-static-final.txt` and Biblio output is `artifacts/arc41-directional/wave5-biblio.txt`.

Native Qt Test is **verification pending**: this worktree has no configured `native/build-msvc` tree and this wave changes no C++/CMake path. Lanista is **Bridge blocked**: no candidate executable/QML pair is available for an isolated tagged session. Bare Home Escape was not used as a return test and `ShellBackPolicy` was not changed. The six required journey families, full real production WorldPage route journey, native QKeyEvent acceptance, and human aesthetic verdict remain verification pending. Overall status is **Bridge blocked**. The source and maintained Qt Quick tests are Implemented and Test-reported green; runtime, native, Lanista, and visual acceptance remain unresolved.

## Changed files

- `qml/KeyboardCollectionController.qml`
- `qml/KeyboardSpatialNavigator.qml`
- `qml/Main.qml`
- `qml/WorldPage.qml`
- `tests/qml/tst_keyboard_directional_continuity.qml`
- `tests/qml/tst_main_book_return.qml`
- this report and the append-only `progress.md` ledger entry

No unrelated docs/build deletion was staged or modified.
