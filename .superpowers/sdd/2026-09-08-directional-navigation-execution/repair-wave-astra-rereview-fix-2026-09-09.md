# Arc 41 Astra rereview fix round

Date: 2026-09-09 (Asia/Calcutta)
Workspace: `C:\b\colosseum-arc41-scroll`
Branch: `codex/arc41-directional-scroll`
Base: `4002094e343368bea4c27b906538526236e15bc8`
Requested route: Luna. Host-visible execution: Codex/GPT-5 Codex; no independent backend telemetry proves a model override.

## Scope and RED-first evidence

The Astra rereview report was read in full. Maintained RED assertions were added before the production repair for nested inner opt-out, scaled budget, rotated owner no-write, forward multi-section lane, and revision-preserved identity. The pre-fix receipts are `artifacts/arc41-directional/red2-directional_scroll.txt` and `red2-continuity.txt`: the three scroll probes failed with inner `contentY` 50, rotated `contentY` 72, and scale over-budget movement; the two lane probes failed with C2 and A1. The outer-only continuation regression was added alongside the nested negative case.

## Implemented repairs

- `KeyboardSpatialNavigator` authorizes every reveal-plan owner before writes, blocks outer writes through a nested opted-out chain while allowing an outer-only target, rejects rotated source/target chains before reveal or pure-scroll fallback, and converts local fallback movement and directional budgets into root-coordinate units. Auto-repeat releases no longer cancel the active navigation generation.
- `KeyboardCollectionController` preserves semantic column intent through forward short sections, reconciles pending identity across incremental model revisions, and resets lane intent at lateral, Home, End, PageUp, and PageDown anchors. Real Library and Biblio library filters invalidate lane state for replacement views.
- Main's Biblio return path now recognizes route owners with model identity/index seams, resolves surviving IDs or clamps to a nearest peer, realizes through `positionViewAtIndex`, clamps origin/margin offsets, and cancels stale delayed restoration through a route generation. BiblioLibraryPage, ContinueRow, and FeaturedCarousel expose route-owner identity seams. KeyboardRegion now validates identity before reusing a reference and clamps restored offsets. KeyboardScrollController exposes release forwarding.
- The Biblio page harness covers selected-book identity, surviving identity resolution, and removed-book detection.

## Verification

All Qt Quick runs used Qt 6.11.1 `qmltestrunner.exe`, `QT_QPA_PLATFORM=offscreen`, `QML_DISABLE_DISK_CACHE=1`, and serial execution.

- Corrected Astra probes: **Test-reported**, 7 passed / 0 failed, receipt `artifacts/arc41-directional/astra-rereview/probes-round2.txt`.
- Maintained directional scroll: **Test-reported**, 22 passed / 0 failed, receipt `artifacts/arc41-directional/round2-tst_keyboard_directional_scroll.txt`.
- Maintained continuity: **Test-reported**, 18 passed / 0 failed, receipt `artifacts/arc41-directional/round2-tst_keyboard_directional_continuity.txt`.
- Maintained primitives, ScrollGlide, scroll focus, region, and spatial navigator: **Test-reported**, 8/8, 3/3, 7/7, 9/9, and 7/7; receipts under `artifacts/arc41-directional/round2-tst_*.txt`.
- Relevant aggregate QML suites (topbar, reader, reader-area, player, system focus, vault): **Test-reported**, all passed; receipts under `artifacts/arc41-directional/round2-aggregate-*.txt`.
- Biblio library API/page harness: **Test-reported**, `BIBLIO_LIBRARY_OK`.
- `qmlformat -n`: **Implemented**, exit 0 for changed QML. `qmllint -I qml`: **Implemented**, exit 0 with existing warning classes and no errors; receipt `artifacts/arc41-directional/round2-qmllint-final.txt`. `git diff --check`: **Implemented**, passed.

## Evidence matrix and open gates

```text
F1 nested owner opt-out: Test-reported (corrected probe green)
F2 transform units/rejection: Test-reported (corrected probes green)
F3 revision identity: Test-reported (corrected probe green)
F4 forward lane: Test-reported (corrected probe green)
F5 route-owner identity seam: Test-reported (Biblio harness); real Main open/return journey verification pending
Qt Test/native event owner: verification pending (no configured native/build-msvc tree)
Existing harnesses: Test-reported for Biblio library; other full-shell harnesses not run
Lanista: Bridge blocked (no current-tree candidate executable/QML pair; bare Home Escape remains unsupported)
Human aesthetic verdict: pending
Overall: Bridge blocked
```

The six populated Lanista journey families remain verification pending: Home, long catalogue/detail, long settings/control surface, populated virtualized collection, reader precedence, and overlay/page return. A real native auto-repeat/final-release navigation owner run, normal packaged startup/manifest parity, and full-shell Biblio open/return remain open. No repeat timer was added. The concurrent deletions of `docs/build/linux.md`, `docs/build/macos.md`, and `docs/build/windows.md` were preserved exactly and excluded from staging.
