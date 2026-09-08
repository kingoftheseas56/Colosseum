# Arc 41 final Astra re-review repair — 2026-09-09

## Scope and identity

This repair continues on `codex/arc41-directional-scroll` in `C:/b/colosseum-arc41-scroll`.
The unrelated unstaged deletions `docs/build/linux.md`, `docs/build/macos.md`, and
`docs/build/windows.md` were preserved exactly. Existing receipts and artifacts were preserved.
The host-visible execution identity is Codex / GPT-5 Codex; the requested Luna route is not
independently observable.

The source changes stay at existing keyboard owner boundaries. `KeyboardSectionCoordinator` is
owned by `WorldPage` and is injected into `ContinueRow`/`FeaturedCarousel`; it stores only the
immediately preceding owner identity and offset. No global history service, delegate scan, or
account-page change was added. Direct `KeyboardScrollController` owners that now forward release
include Settings, Extensions, Calendar, Downloads, Comic archive/series/torrent owners, Shortcuts,
Theatre, Vault, Wallpaper, WatchParty, ComicSeries, and the Universe/Era/Galaxy/Locg/Saga/Studio/
UniverseExtension page shells. Account-owned pages and the specialized reader controller remain
outside this scoped repair.

## Findings and evidence

- **W1/P1 Main return coverage — Implemented; Test-reported.** Main now exposes explicit aliases to
  the actual route Loader owners and `_bookReturnCovered()` checks active, visible, opacity, and
  loaded-item visibility/opacity. `tst_main_book_return.qml` instantiates real `Main` and proves a
  covered background cannot receive a restore focus write. Receipt:
  `artifacts/arc41-directional/wave4-main-green2.txt`.
- **W3/P1 return offsets — Implemented; Test-reported.** Main restores a valid unchanged owner
  offset, then invokes the owner reveal seam only when the resolved identity is clipped after
  reorder/removal/shrink. The real-ish Main fixture covers intervening offset change, reorder,
  removal, bounded shrink, clamping, and reveal count. Receipt:
  `artifacts/arc41-directional/wave4-main-green2.txt`.
- **W2/P1 transform fallback — Implemented; Test-reported.** Destination transform validation
  rejects unsupported rotated chains before both reveal-plan and raw scroll fallback writes. The
  maintained RED guard-removal receipt is `artifacts/arc41-directional/red-n2-target-visible.txt`;
  the restored implementation is covered by `wave4-directional-final.txt`.
- **W3/P1 independent rails — Implemented at the existing owner boundary; Test-reported.** A real
  two-Flickable fixture uses separate rail owners, a shorter destination rail, owner-local offsets,
  reorder while away, stable identity return, removed-peer fallback, and reset anchors. The
  corrected RED run is `artifacts/arc41-directional/wave4-red-rails-final.txt`; the maintained
  GREEN run is `artifacts/arc41-directional/wave4-continuity-final.txt` (20 passed / 0 failed).
- **W4/P1 lifecycle — Implemented for direct production owners; Test-reported source/Qt Quick.**
  Actual `Keys.onReleased` forwarding was added to the remaining synchronous controller owners,
  including the required Settings and Extensions owners. No repeat timer was added. Native
  QKeyEvent/navigation acceptance remains verification pending because no configured native build
  tree is present.

## Verification

Focused Qt Quick evidence is **Test-reported** green:

- directional scroll: 23 passed / 0 failed (`wave4-directional-final.txt`);
- directional continuity: 20 passed / 0 failed (`wave4-continuity-final.txt`);
- keyboard primitives: 8 passed / 0 failed (`wave4-primitives-final.txt`);
- keyboard scroll focus: 7 passed / 0 failed (`wave4-scroll-focus-final.txt`);
- Main return: 4 passed / 0 failed (`wave4-main-green2.txt`);
- Biblio library API/page harness: `BIBLIO_LIBRARY_OK` (`wave4-biblio-final.txt`);
- existing ScrollGlide, K03, world lifecycle, and Continue receipts remain PASS.

`qmlformat` returned zero for changed QML, including the new coordinator and Main fixture.
`qmllint -I qml` returned zero (`wave4-qmllint-final2.txt`). `git diff --check` returned zero
(`wave4-diff-check-final2.txt`).

Native Qt Test is **verification pending**: this worktree has no configured `native/build-msvc`
tree, and no C++/CMake path changed. Lanista is **Bridge blocked**: no candidate executable/QML
pair is available for an isolated tagged session. Bare Home Escape was not used as a return test;
ShellBackPolicy was not changed. The six journey families, real production Main route journey,
and human aesthetic verdict remain verification pending.

Overall status: **Bridge blocked**. The scoped source and maintained tests are Implemented and
Test-reported green; native, Lanista, and visual acceptance remain pending. The requested Luna
identity is not claimed.
