# 1.1.9 core device-state carry-over verification

Date: 2026-10-14. Candidate source: `031d4c8725bbce0c5db45e27e91b29e20a30c8c1`, branch `codex/d1-119-no-account`. Scope is verification of existing same-device Progress/Collection/History adoption and account isolation. No application code, migration plumbing or tests were added or changed.

## Accepted Biblio limitation

Hemanth accepted the Biblio compromise and stopped further plumbing. Exact Biblio reading position, bookmarks and annotations are **not promised to carry over** into an account profile. `Reader2Bridge.cpp:104-111` changes the reader root between legacy/local/account routes; current adoption snapshots omit reader JSON. These files and path-derived identities were not migrated or given cloud sync. This limitation does not negate the core state that the existing snapshot does carry.

## Fresh focused candidate build

A separate temporary standalone CMake build compiled the candidate's existing production dependencies and the unmodified `tst_account_adoption`, `tst_account_shared_pc`, and `tst_account_attachment_coordinator` sources using MSVC 2022 / Qt 6.11.1. The dependency declarations were extracted from the existing test target definitions and their existing tracker-runtime helper, then shared in one static library to avoid repeated compilation. This does not build the application or add repository targets.

Build invocation: `cmake --build <focused-build> --config Release --target tst_account_adoption tst_account_shared_pc tst_account_attachment_coordinator --parallel 2`. Configure and build exited 0. Configure warned that optional Vulkan headers and a QML asset-downloader plugin dependency were unavailable; compilation and the selected runtime checks succeeded. Older primary-tree binaries were queried only for function metadata and were not used as pass evidence.

Tests ran offscreen with Qt's `-o <temporary-log>,txt`. Each root process was waited through `Start-Process -Wait -PassThru -WindowStyle Hidden`; all root exit codes were 0.

| Existing executable | Selected cases | QtTest result, including setup/cleanup |
| --- | --- | --- |
| `tst_account_adoption` | `populatedFirstAccountCommitsBeforeActivation`, `cleanRestartKeepsCommittedAdoption`, `ordinarySignInAdoptsLegacyLocalState`, `committedAccountSessionMergesResidualLocalOnlyState`, `corruptRestartPreservesAccountAndEvidence`, `retryIntentReAdoptsOnLaterSignIn`, `explicitLocalPreparingAdoptionResumesFromLocalSource`, `existingCachedAccountKeepsSourceForAttachment` | 10 passed, 0 failed, 0 skipped; 3,826 ms |
| `tst_account_shared_pc` | `accountAToBToASealsConcreteStoreObjects`, `historyNeverCrossesAccountBoundary`, `directCrossAccountOpenFailsUntilCurrentProfileIsSealed` | 5 passed, 0 failed, 0 skipped; 829 ms |
| `tst_account_attachment_coordinator` | `happyPathOrchestrationOrderUnionAndRetirement`, `commitResponseLossFailsClosedThenRecoversIdempotently`, `verificationFailureKeepsSourceAndRecordsFailure`, `crashRestartMatrixUsesDurableReceiptAndEngineState` | 6 passed, 0 failed, 0 skipped; 878 ms |

The result is **15 selected substantive cases passed**, with 21 QtTest passes when setup/cleanup are counted. No selected case failed or skipped, and no test launch was denied.

## Source and assertion boundaries

`tests/auto/account_adoption/tst_account_adoption.cpp:60-185` seeds core Progress/Collection/History in the existing snapshot. Its populated-adoption case at `832-939` checks destination semantic readback, committed adoption, backup evidence and exact local source preservation until attachment proof; restart/retry cases exercise legacy and explicit-local sources. Account-isolation assertions at `tests/auto/account_shared_pc/tst_account_shared_pc.cpp:293-452,526-609,612-653` check concrete store sealing, history separation and refusal to switch without sealing. Attachment recovery assertions at `tests/auto/account_attachment_coordinator/tst_account_attachment_coordinator.cpp:2522-2654,2656-2763,3463-3693` exercise lost responses, source preservation and durable restart recovery.

Production adoption remains `FirstAccountProfileCoordinator`; it uses existing local/account profile paths and does not change application identity. These component results establish the selected existing core paths on this candidate. They do not establish installer/edition-upgrade persistence, full-application acceptance, live Neon service operation, all-media completion, cross-device identity/sync, or automatic Biblio carry-over. Initial existing-account progress conflicts still use the current `updatedAt` merge rule; these selected tests do not qualify skewed-clock or intentional-rewind conflict resolution. No new conflict policy was implemented.

Astra personally reviewed the candidate assertions and all three fresh QtTest logs and accepts the selected core carry-over component evidence only. The installer, live Neon, clock-conflict and release gates remain as listed above. No merge, tag, release, deployment or endpoint activation is authorized by these results.
