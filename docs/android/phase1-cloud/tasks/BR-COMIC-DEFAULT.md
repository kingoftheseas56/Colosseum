# BR-COMIC-DEFAULT — Android lane default differs; working reader observation does not establish a layout defect

Class H; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
User sees different starting page layout/controls. Corrected supplied observation says Beta reader works; this is not evidence of failed gestures or overflow.

## Alpha behavior
Unspecified Android lane default paired becomes single_page, explicit series/global choice wins.

## Beta behavior
Uses lane default paired_pages for unspecified preferences; explicit choices still win.

## Actual owner/caller
ComicReaderShell load preferences->layout; actual ComicReaderInput/native core identical; Main/Tankoban readers active.

## Donor evidence
qml/comicreader/ComicReaderShell.qml:1485-1493

## Beta evidence
qml/comicreader/ComicReaderShell.qml:1523-1529; ComicReaderState.js:migrateReaderPrefs

## Evidence layer
committed-alpha-beta

## Dependencies
ComicReaderState.migrateReaderPrefs, global/per-series saved layout, actual reader viewport; Beta progress/pixel/strip-restoration guards.

## Reuse/adaptation
investigate/adapt only if supported

## Files and integration points
No implementation planned pending concrete viewport preview. If evidence supports a change, adapt only unspecified lane fallback in ComicReaderShell preference loading; do not rewrite saved preferences or copy Alpha shell.

## Preserve
Preserve explicit global/per-series user choices, paired mode as an available choice, desktop defaults, Beta numbered-chapter resume and pixel/strip restoration progress guards.

## Bounded unit
None authorized now. A later accepted fallback adaptation would be one policy change with precedence/rotation checks, not a reader transplant.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Device-check current default with fresh preferences before changing. If needed, adapt lane default per actual reader viewport only; preserve paired mode and existing chosen settings.

## Existing tests
comicreader_migration_acceptance.qml; comicreader_shell_harness.qml; Alpha default-specific expectations must be distinguished from user setting preservation.

## Required host checks
Proposed bounded check, not executed: fresh unspecified preference versus explicit single/paired/strip and global/per-series precedence at narrow/wide and short/tall reader viewports; reopen/rotation must retain explicit choice and no provisional progress overwrite.

## Local device acceptance (pending)
Fresh test profile portrait/landscape manga and western comic, paired/single/strip; chosen mode survives reopen/rotation; inspect decode presentation and controls.

## Recovery
No implementation made. If later accepted, revert fallback code through same-package upgrade without modifying saved reader preferences/progress or clearing app data.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
No fresh device capture of page fit/current reader preference; no recommendation to forcibly disable paired mode.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
