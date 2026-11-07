# BR-READER-PRESENTATION — Phone opacity/title/compact styling is excluded from direct backport

Class F; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Explains visual/control differences without declaring the working reader broken. Copying phone styling wholesale would reverse approved desktop composition; lost system-font consumption remains independently actionable through the worlds mechanism.

## Alpha behavior
Uses Android-opaque reader surfaces, scaled-font callsites, hides duplicate title toast and treats short landscape as compact icon command bar.

## Beta behavior
Keeps desktop-style translucent surfaces/title toast and width-only compact threshold with raw font sizes; reader commands remain present. Font-scale consumption is a separate cross-cutting worlds finding.

## Actual owner/caller
ComicReaderShell instantiates ComicReaderHud/CommandBar and reader popovers on Android; this is active presentation, not a missing native reader engine. Phone visual composition is intentionally excluded from direct transfer.

## Donor evidence
qml/comicreader/ComicReaderHud.qml:101-105,373-385,563-580; ComicReaderCommandBar.qml:32-36,207-210; ComicReaderImagePopover.qml:99-102 and fontSize callsite hunks; reader popover/settings diffs retained.

## Beta evidence
qml/comicreader/ComicReaderHud.qml:101-105,565; ComicReaderCommandBar.qml:32-36; reader popovers/settings font and opacity diffs

## Evidence layer
committed-alpha-beta

## Dependencies
ComicReaderShell/Hud overlay state, command-bar width/reader viewport, shared Theme/fontScale policy; current chosen layout preferences.

## Reuse/adaptation
exclude wholesale phone presentation; extract independently justified input/accessibility mechanism only

## Files and integration points
No presentation backport planned. Coordinate fontScale consumption with worlds/shared controls; short-landscape command fit may be previewed at actual viewport before any separate adaptation.

## Preserve
Keep approved desktop composition, all commands and chosen comic layouts, Beta pixel/strip continuity; retain larger-target/accessibility work as separate mechanism rather than phone visual transplant.

## Bounded unit
N/A: visual transplant excluded. Any accepted font/input adaptation belongs its shared policy unit.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Do not copy Alpha phone visual composition. Lost fontScale consumption is separately worlds C mechanism; short-landscape compact command default is a source difference and optional viewport usability check, not proven missing command.

## Existing tests
Comic reader chrome/overlay/migration harnesses and shared font/target tests; no physical command-fit test executed in audit.

## Required host checks
Proposed bounded check only: actual narrow/short viewport command visibility, no clipped action, explicit preference persistence and desktop visuals unchanged. Do not use font-size changes as proof of touch target size.

## Local device acceptance (pending)
If needed, preview command accessibility/fit on OnePlus portrait/landscape at actual installed density and enlarged font scale; separately qualify NokiaT20. No current failure inferred.

## Recovery
No product change made. Future bounded presentation adjustment reverts QML without modifying progress/reader settings/profile data.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
No actual native viewport measurement or fresh command clipping reproduction; styling deltas alone do not prove unavailable controls.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
