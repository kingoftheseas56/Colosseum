# BR-COMIC-CONTINUITY — Beta guards saved progress until actual pixels and restoration settle

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Prevents a blank/failed open or premature close from replacing last successful read position; resumes same numbered chapter across source translation without rewriting source before presentation.

## Alpha behavior
Eager entry record and presented geometry can bank provisional opening anchor; no numbered cross-translation chapter cache.

## Beta behavior
Record blocked while strip restore pending or entry unpainted; strip presented waits real pixels/source dimensions; canonical manga numbered chapter fallback shares progress safely.

## Actual owner/caller
TankobanLibrary/Manga/ComicSeries embed ComicReaderShell, native core/provider added main919; reader shell writes canonical ProgressStore.

## Donor evidence
qml/comicreader/ComicReaderShell.qml:1028-1051,1072-1090; ComicReaderStripSurface.qml:668-671; native/ProgressStore.h:885

## Beta evidence
qml/comicreader/ComicReaderShell.qml:1060-1093,1114-1130,1653-1677; ComicReaderStripSurface.qml:670-681,902-938; native/ProgressStore.h:921-934,2095-2120

## Evidence layer
committed-alpha-beta

## Dependencies
N/A: no immediate transfer proposed; preserve existing owner/availability boundary.

## Reuse/adaptation
preserve Beta

## Files and integration points
N/A: preservation/exclusion or unresolved decision; no implementation selected.

## Preserve
Canonical title/edition/profile, exact current source until rendered; decimal chapter identity; strip anchor and max/page clamp.

## Bounded unit
N/A: no implementation before the stated evidence/decision gate.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve Beta; no Alpha shell or ProgressStore transplant.

## Existing tests
comicreader_resume_race_harness.qml; comicreader_sync_resume_shell_acceptance.qml; comicreader_strip_harness.cpp; source only not run.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
No additional source question recorded; runtime acceptance is separate.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
