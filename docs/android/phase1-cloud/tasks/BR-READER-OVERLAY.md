# BR-READER-OVERLAY — Beta keeps Android reader popovers operable across native-window boundaries

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Prevents lost Escape/Tab/focus and excessive keyboard avoidance when device density is greater than1.

## Alpha behavior
Host-centered key navigation and DPR-divided keyboard overlap.

## Beta behavior
Surface/window-correct key forwarding and focus targets; IME rectangle treated as logical window coordinates.

## Actual owner/caller
ReaderOverlay moves actual visual surface to native overlay window over Android WebView.

## Donor evidence
qml/reader2/ReaderOverlay.qml:23; SelectionMenu.qml:38-48; DictCard.qml:39-43; FootnoteCard.qml:35

## Beta evidence
qml/reader2/ReaderOverlay.qml:26; SelectionMenu.qml:38-48,66-75; SearchSheet.qml:54-65; DictCard.qml:30,41-43; FootnoteCard.qml:27,36

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
Preserve these Beta improvements while adapting Alpha hit areas; no older file replacement.

## Existing tests
tests/qml/tst_android_reader_overlay_focus.qml and tst_android_beta_reader_touch.qml; supplied host-pass evidence belongs integration receipts, no new runs.

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
