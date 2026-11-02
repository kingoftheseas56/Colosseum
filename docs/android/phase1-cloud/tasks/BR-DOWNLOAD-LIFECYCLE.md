# BR-DOWNLOAD-LIFECYCLE — Beta preserves Alpha Android pinned torrent recovery and adds safe incomplete deletion

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Preserves paused bytes only for exact immutable torrent/file with valid206 offset, pauses safely without foreground/service lease, retains failed deletion evidence rather than losing job.

## Alpha behavior
Android lifecycle/partial hash+file recovery implemented globally in DownloadStore; incomplete owned roots no bulk owner cleanup.

## Beta behavior
Default Android policy AndroidPinnedTorrent preserves same recovery; desktop Legacy behavior separate; failed cancellation keeps canonical job; all native reading download owners refuse cleanup until stopped and indexed files removed.

## Actual owner/caller
Main1607 DownloadStore default->AndroidPinnedTorrent; BackgroundDownloadBridge1621 service lease->main1627 foreground coordinator->Stream/Download execution gates; DownloadsPage mutations route owning backend.

## Donor evidence
native/player/downloadstore.cpp:63,264-373,485-569; native/engine/MangaDownloader.cpp:385

## Beta evidence
native/player/downloadstore.cpp:66-76,181-197,987-1080; native/engine/MangaDownloader.cpp:388-403; BookDownloader.cpp:349; ComicDownloader.cpp:1177; AudiobookDownloader.cpp:682

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
Keep Beta; Alpha missing-file Continue recovery/UI transfer is separately PF-04(platform), not a reason to replace lifecycle owner.

## Existing tests
tests/auto/player/tst_android_download_recovery.cpp includes pinnedResume/changed identity/mismatched range/transient prefix/lifecycleLeaseAndManualPause/failedCancellation; Alpha lifecycle harness overlap adapt, not duplicate.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
Actual Android foreground-service/provider/network behavior requires device checks; no new run.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
