# Arc 41 final Astra fix round — 2026-09-09

## Scope and identity

This round continues from `f4307c3598786b006718978cbf113c9a94e48e7b` on
`codex/arc41-directional-scroll` in `C:/b/colosseum-arc41-scroll`. The three concurrent
`docs/build/{linux,macos,windows}.md` deletions were preserved exactly and remain unstaged.
The requested Luna override is not independently observable here; host-visible execution identity
is Codex / GPT-5 Codex.

The changed page files are all existing owners of `KeyboardScrollController` and receive only the
matching `Keys.onReleased` forwarding needed for N4: `BiblioBook`, `BiblioExplorePage`,
`BiblioGenreIndex`, `BiblioGenrePage`, `BiblioSearch`, `GenreIndex`, `GenrePage`,
`SearchSurface`, `TheatreGenreIndex`, and `TheatreGenrePage`. No account page or unrelated route
was changed. `BiblioLibraryPage`, `ContinueRow`, and `FeaturedCarousel` receive owner-local
identity/reveal seams because Main's existing return path resolves through those owners; no global
history service or delegate/model scan was added.

## Findings and evidence

- **N1/P1 return visibility — Implemented; Test-reported for the owned seam; verification pending for a real Main route journey.** `BiblioLibraryPage` now binds the existing stable identity/index/revision seams on its `KeyboardCollectionController` and exposes an owner-local `keyboardRevealIndex`. Main skips stale collection offsets, reapplies the resolved identity reveal as the final geometry operation, validates clipping/visibility before focus, and advances route generation when covered taskbar/search routes open. `ContinueRow` and `FeaturedCarousel` expose their existing owner-local reveal hooks. The Biblio harness covers 16-card scroll, reorder, stale offset, surviving identity, and final reveal. Receipt: `artifacts/arc41-directional/final-biblio-library.ps1.txt`.
- **N2/P2 transform fallback — Implemented; Test-reported.** `KeyboardSpatialNavigator` rejects an unsupported destination transform before raw owner fallback. The maintained rotated external-entry assertion is green in `artifacts/arc41-directional/final-tst_keyboard_directional_scroll.txt`. A temporary guard-removal run produced the expected RED (`Actual 50`, `Expected 0`) in `artifacts/arc41-directional/red-n2-target-visible.txt`; the guard was restored before verification.
- **N3/P1 production identity/rails — Partially Implemented; Test-reported for bounded owner APIs.** The Biblio owner now supplies `identityForIndex`, `indexForIdentity`, and `modelRevision`; independent controller rail state and reset-anchor behavior are maintained by `test_independent_grid_rails_keep_lane_state_and_reset_at_home`, and the continuity suite covers surviving reorder/removal identities. ContinueRow and FeaturedCarousel have owner-local reveal seams. A real Lanista page-to-overlay round trip and full independent multi-rail production journey remain verification pending; no global scan/history mechanism was introduced.
- **N4/P1 release/runtime — Implemented at direct owner wiring; Test-reported source/Qt Quick.** Existing `KeyboardScrollController` users now forward `Keys.onReleased` in the ten Biblio/Genre/Search owners listed above. The synchronous controller remains the movement authority; no repeat timer was added. Native QKeyEvent/navigation acceptance is verification pending because no configured native build tree is present.

## Verification

Maintained Qt Quick receipts are **Test-reported** green:

- directional scroll: 23 passed / 0 failed;
- directional continuity: 19 passed / 0 failed;
- keyboard primitives: 8 passed / 0 failed;
- keyboard scroll focus: 7 passed / 0 failed;
- Biblio library page/API harness: `BIBLIO_LIBRARY_OK`;
- ScrollGlide aggregate: PASS (harness, wheel delivery, and math);
- K03 keyboard contract: PASS;
- keyboard region, registry, and reader Python contracts: PASS.

`qmllint -I qml` and `git diff --check` are green. `qmlformat` parses all changed files; the
pre-existing `BiblioExplorePage.qml` also returns normalize status 1 at the baseline revision, so
that style result is recorded as verification pending rather than attributed to this fix.

Native Qt Test is **verification pending**: this worktree has no configured `native/build-msvc`
tree and no C++/CMake path changed in this round. Lanista is **Bridge blocked**: no candidate
executable/QML pair is available for an isolated tagged session. The supported return scenario
must open a real page/overlay before Escape; bare Home Escape was not used, and ShellBackPolicy was
not changed. The six journey families and human aesthetic verdict are verification pending.

Overall status: **Bridge blocked**. The scoped source and maintained tests are implemented and
Test-reported green, while real Main/Lanista/native runtime acceptance remains pending.
