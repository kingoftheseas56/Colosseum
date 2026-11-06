# BR-SOURCE-REPLACEMENT — Changing the source of a retained same-content session can resume its old stream

Class B; priority P2. Authorization/order comes from START.md, not classification alone.

## Why this matters
A user minimizes an episode, picks another source for the same episode, and receives the old retained source despite the selected descriptor now being saved. Already-active same-id open may not dispatch at all. Source-supported deterministic path; no device reproduction in this audit.

## Alpha behavior
SessionStore detects exact transport identity change without losing content position, emits sourceReplaced; Main invalidates warm session and activates new target.

## Beta behavior
Updates target but emits no replacement signal; same-id setActive returns early, or a parked reactivation takes warmPlayerSessionId branch into resumeFromMinimize without reading new target.

## Actual owner/caller
SourcesSheet.activateSource:90-95 -> TheatreSeriesView.onPlayRequested:2253 -> Main detail Loader.playRequested.connect(openMovieSession):5284 -> openMovieSession:3557 -> Sessions.openOrSwitch. Session activeChanged handler5534 -> activateSession -> activateMovieSession. Player resume has no descriptor argument/source reconciliation.

## Donor evidence
native/SessionStore.h:80-88 sourceKeyFor/openOrSwitch; qml/Main.qml:2221-2226 onSourceReplaced; tests/session_store_harness.cpp:150-164

## Beta evidence
native/SessionStore.h:77-86 openOrSwitch; :690-709 setActive; qml/Main.qml:3813-3848 activateMovieSession; qml/PlayerPage.qml:3102-3119 resumeFromMinimize

## Evidence layer
committed-alpha-beta

## Dependencies
SessionStore content-vs-transport identity; Main retained PlayerPage; canonical episode id; existing playback savedState; Media3/Nuvio demand lifecycle.

## Reuse/adaptation
adaptation

## Files and integration points
SessionStore.openOrSwitch same-content update branch and source identity helper/signal; Main existing targetReplaced Connections and activateMovieSession warm ownership.

## Preserve
Beta canonical contentKey, one-tab-per-show, browser/session generation, saved position; latest Nuvio foreground/download isolation and Media3 teardown. Compare source URL/localPath/hash/file identity deliberately; do not reload for title/art/candidate list changes. Capture/checkpoint the latest currently active player position before replacing transport; do not restore stale savedState. Integrate existing media request/demand/lifecycle generations so old callbacks cannot land.

## Bounded unit
One coupled native signal + QML warm invalidation change, plus native and host fake-player assertions in one coherent commit.

## Risks
Preservation constraints: Beta canonical contentKey, one-tab-per-show, browser/session generation, saved position; latest Nuvio foreground/download isolation and Media3 teardown. Compare source URL/localPath/hash/file identity deliberately; do not reload for title/art/candidate list changes. Capture/checkpoint the latest currently active player position before replacing transport; do not restore stale savedState. Integrate existing media request/demand/lifecycle generations so old callbacks cannot land. Dependencies: SessionStore content-vs-transport identity; Main retained PlayerPage; canonical episode id; existing playback savedState; Media3/Nuvio demand lifecycle.

## Recommended change
Adapt Alpha source-change invalidation into Beta, limiting reload to an actual transport/source change. Keep same episode savedState and metadata-only reuse. Do not replace SessionStore or Main wholesale.

## Existing tests
Alpha tests/session_store_harness.cpp:80-82,150-164 source-swap assertions absent Beta counterpart; both session harnesses exist; relevant player Android lifecycle/recovery suites remain.

## Required host checks
Reintroduce/adapt source identity test: same content different hash/file/url reloads exactly once, preserves position; metadata-only/case-normalized hash no reload; parked and already-active Main session path uses newly selected descriptor; switch different episode clears old resume; download prefetch unaffected.

## Local device acceptance (pending)
OnePlus11R portrait and landscape: pause/minimize same episode sourceA, choose sourceB, confirm actual route changes and resume position retained; repeat failed source and app-background transitions with video download running; NokiaT20 separately unqualified.

## Recovery
Revert signal+caller implementation together and restore prior APK through normal same-package upgrade only when signed/version-compatible; never clear data/uninstall. Snapshot task-owned test profile/canonical session+progress stores before qualification; persisted schema unchanged.

## Evidence confidence
high source-path confidence; Android runtime unverified

## Unresolved
No fresh physical reproduction. Source identity adaptation should include local/direct routes without treating expired URL re-resolution as a different content identity.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
