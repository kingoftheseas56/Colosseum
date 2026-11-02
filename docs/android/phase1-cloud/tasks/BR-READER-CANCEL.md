# BR-READER-CANCEL — Beta cancels partial page drags and delayed image actions

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Prevents cancelled drags leaving partial page position and a pinch/cancel causing delayed image popover.

## Alpha behavior
No explicit touchcancel reset for paginator drag and image long-press timers.

## Beta behavior
Touchcancel discards pending action, restores paginated origin when safe; second contacts do not restart single-finger origin; delayed old callback cannot clear newer gesture.

## Actual owner/caller
Android WebView loads shared Foliate through paper_glue, not desktop-only WebEngine; native AndroidEbookHost transport remains.

## Donor evidence
resources/reader2/vendor/foliate-anx/src/paginator.js:843-1007; view.js:253-270

## Beta evidence
resources/reader2/vendor/foliate-anx/src/paginator.js:968-1036; view.js:253-281

## Evidence layer
committed-alpha-beta

## Dependencies
N/A: no immediate transfer proposed; preserve existing owner/availability boundary.

## Reuse/adaptation
preserve Beta

## Files and integration points
N/A: preservation/exclusion or unresolved decision; no implementation selected.

## Preserve
Keep current canonical state, saved preferences, security/lifecycle owners and approved desktop presentation.

## Bounded unit
N/A: no implementation before the stated evidence/decision gate.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve delivered touch mechanism; do not backport Alpha vendor files.

## Existing tests
tests/android_reader_gesture_cancel_test.mjs; tests/android_reader_selection_touch_test.mjs; source only, physical Android not qualified by host tests.

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
