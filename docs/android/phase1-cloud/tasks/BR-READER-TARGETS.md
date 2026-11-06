# BR-READER-TARGETS — Reader actions remain present but lose Alpha larger targets and width safety

Class C; priority P2. Authorization/order comes from START.md, not classification alone.

## Why this matters
Actions are reachable in source and existing host tests exercise them, but small deletion/highlight/save areas reduce finger tolerance. Note card may exceed very narrow available width; that is conditional geometry risk, not proven clipping at current OnePlus density.

## Alpha behavior
SelectionMenu on narrow Android has48-unit swatch/action targets, phone width cap, note keyboard cap; LeftPanel reserves48-wide full-height delete gutter separate from jump row.

## Beta behavior
Touch delete remains visible and independent click handler, but uses text glyph+6 margin; SelectionMenu keeps20-wide swatches/24-high save and300-wide note card; native-overlay coordinate/focus fixes improve behavior.

## Actual owner/caller
ReaderShell SelectionMenu/ReaderChrome LeftPanel remain actual Android reader chrome. ReaderOverlay reparents popover surface to native overlay window; AndroidPaper WebView covered when full-screen panel open.

## Donor evidence
qml/reader2/SelectionMenu.qml:38-49,115-121,175-181,301-320; LeftPanel.qml:133-138,428-477

## Beta evidence
qml/reader2/SelectionMenu.qml:38-48,115-118,167-173,284-300; LeftPanel.qml:423-447; ReaderOverlay.qml:26; SearchSheet.qml:54-65

## Evidence layer
committed-alpha-beta

## Dependencies
Shared touch sizing work owned by platform/worlds; ReaderOverlay native window surface coordinates and IME logical rectangle; keyboard navigation.

## Reuse/adaptation
adaptation

## Files and integration points
SelectionMenu swatch/TextAction/save hit items and cardW; LeftPanel bookmark activation/delete target ownership. Coordinate with shared sizing implementation to avoid stacking redundant enlarged MouseAreas.

## Preserve
Beta nativeOverlay Keys.forwardTo, surface-based focus target and logical keyboard mapping; no note-dismissal on Android selectionCleared; desktop dimensions/style unchanged; dictionary/footnote keys.

## Bounded unit
One reader overlay/action reachability change, sharing policy with general controls; tests across short/narrow viewport and native-like reparented window.

## Risks
Preservation constraints: Beta nativeOverlay Keys.forwardTo, surface-based focus target and logical keyboard mapping; no note-dismissal on Android selectionCleared; desktop dimensions/style unchanged; dictionary/footnote keys. Dependencies: Shared touch sizing work owned by platform/worlds; ReaderOverlay native window surface coordinates and IME logical rectangle; keyboard navigation.

## Recommended change
Adapt input targets and width containment using the shared touch-target policy, preserving desktop presentation. Extract Alpha reserved non-overlapping delete hit region and viewport cap rather than copy its phone two-row composition.

## Existing tests
Alpha tests/qml/tst_phone_reader_controls.qml missing Beta; Beta tst_android_beta_reader_touch.qml covers IME, delete-vs-jump and note save at960x640, and tst_android_reader_overlay_focus.qml covers focus topology. Adapt tests, do not reinstate old Android DPR division.

## Required host checks
Re-use Beta fake IME; add narrow widths, short landscape, keyboard visible, color/note/copy/delete touch bounds and no row jump from delete gutter; reparented popover Escape/Tab, save/cancel/draft retention, no action on cancelled drag; desktop geometry assertions.

## Local device acceptance (pending)
OnePlus11R tap swatches/delete/save at normal and enlarged Android font settings; hold/select, open note keyboard, save/cancel, dictionary/footnote dismiss and Back both orientations, density1.15/1.20; NokiaT20 independently.

## Recovery
Revert reader-target commit+tests, keep QML asset/normal same-package version rollback; no database/schema/settings conversion and no data deletion.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
Exact installed native-window dimensions/target overlap have not been device measured. Enlarging target must keep one gesture owner and avoid stealing list scroll.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
