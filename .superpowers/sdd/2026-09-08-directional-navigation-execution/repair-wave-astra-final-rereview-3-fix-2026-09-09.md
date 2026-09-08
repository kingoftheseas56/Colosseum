# Arc 41 Astra final re-review 3 repair — 2026-09-09

## Scope and identity

This scoped repair continues from `a4bc96de8f0a411cb0a458f9b5417ec1a2310d8a` on `codex/arc41-directional-scroll` in `C:/b/colosseum-arc41-scroll`. The unrelated unstaged deletions `docs/build/linux.md`, `docs/build/macos.md`, and `docs/build/windows.md` and all existing receipts were preserved. Host-visible execution identity is Codex / GPT-5 Codex; the requested Luna route is not independently observable.

Only `KeyboardSpatialNavigator.qml` and the maintained directional continuity test changed in this wave. No account pages, global history service, delegate/model scan, master branch, or unrelated cleanup was touched.

## Findings and repairs

- **T1/P1 nested collection hierarchy — Implemented; Test-reported.** Collection identity now resolves from the spatial target up through its existing delegate hierarchy. It accepts the delegate index or the owner identity seam (`ContinueTile.entry.id` and `CarouselSlide.slide` identity) without scanning models or delegates. Managed landing and inverse section return now select the owner index and retain focus on the owner based on the owner/delegate contract, rather than one nested action's `focusEnabled` flag. The maintained fixture instantiates real `ContinueRow`/`ContinueTile` and `FeaturedCarousel`/`CarouselSlide` owners in one shared vertical viewport, exercises nested actions, currentIndex, owner activeFocus, shorter-rail crossing, inverse return, and Enter identity (`C1`/`F1`). Receipt: `artifacts/arc41-directional/wave6-green-continuity.txt`.
- **T2/P2 editable precedence — Implemented; Test-reported.** `activeItem()` checks the actual focused object for editable/native text policy before projecting focus to a collection's semantic selected delegate. A nested `TextInput` inside a collection owner now keeps arrow precedence, with no content mutation and focus retained. Receipt: `artifacts/arc41-directional/wave6-green-continuity.txt`.

RED-first evidence is preserved at `artifacts/arc41-directional/wave6-red-t1t2.txt`: the nested editable assertion moved spatially before the guard, and the real collection hierarchy landed with the wrong destination selection before the repair. The final continuity run is 23 passed / 0 failed.

## Verification

Focused Qt Quick evidence is **Test-reported** green:

- directional scroll: 23 passed / 0 failed (`artifacts/arc41-directional/wave6-green-directional.txt`);
- directional continuity: 23 passed / 0 failed (`artifacts/arc41-directional/wave6-green-continuity.txt`);
- keyboard primitives: 8 passed / 0 failed (`artifacts/arc41-directional/wave6-green-primitives.txt`);
- keyboard scroll focus: 7 passed / 0 failed (`artifacts/arc41-directional/wave6-green-scroll-focus.txt`);
- Main return: 5 passed / 0 failed (`artifacts/arc41-directional/wave6-green-main.txt`);
- Biblio API/page harness: `BIBLIO_LIBRARY_OK` (`artifacts/arc41-directional/wave6-green-biblio.txt`).

`qmlformat` returned zero for both changed QML files, `qmllint -I qml` returned exit 0 with existing warnings, and `git diff --check` returned zero (`artifacts/arc41-directional/wave6-static.txt`).

Native Qt Test is **verification pending** because this worktree has no configured `native/build-msvc` tree and no C++/CMake path changed. Lanista is **Bridge blocked** because no candidate executable/QML pair is available for an isolated tagged session. Native QKeyEvent acceptance, full production route capture/open/return, six required journey families, packaged startup/manifest parity, and human aesthetic verdict remain **verification pending**. Bare Home Escape was not used and `ShellBackPolicy` was not changed. Overall status is **Bridge blocked**. No runtime or visual acceptance is claimed.

## Changed files

- `qml/KeyboardSpatialNavigator.qml`
- `tests/qml/tst_keyboard_directional_continuity.qml`
- this report and the append-only `progress.md` entry

No unrelated docs/build deletion was staged or modified.
