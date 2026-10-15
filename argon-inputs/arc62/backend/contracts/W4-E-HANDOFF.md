# W4-E Durable Personal State Handoff

Date: 2026-10-06
Task: T-5 / W4-E
State: DONE
Authorship: Lead closeout from Agent-5 implementation plus independent clean verification

## Completed

- BA4 now uses host-supplied monotonic elapsed time for accounting while wall time remains descriptive.
- BA4 durable PlayInstanceId and continuationOf preserve one listening journey across same-occurrence source/video replacement.
- continuous qualified listening spans coalesce rather than producing one durable row per sample pair.
- BA4 durable schema v2 persists listen facts, continuation identity, monotonic timing, and non-authoritative display snapshots; v1 remains supported for in-memory migration.
- BA5 playlist state now includes durable user-authored name, createdAtMs, and item addedAtMs.
- BA5 Library + Playlist state has deterministic versioned durable schema v2 with v1 migration support.
- BE2 now uses host revision ordering rather than wall-clock monotonicity for durable mutation conflict resolution.
- BE2 has deterministic versioned durable schema v2 with v1 migration support.
- BA4, BA5, and BE2 consume the shared BA0 ProfileScopeId.
BA4 accounting now takes both descriptive observedAtMs and host-supplied monotonicElapsedMs. Sequence owns append order; monotonic elapsed owns qualified-time calculation. Backward wall-clock steps are accepted.

Continuous qualifying sample pairs coalesce into one listening_span row until a discontinuity. The durable BA4 schema deliberately omits PlaybackWorkToken and playback generation, while preserving QueueOccurrence, EnqueueOrigin, PlayInstanceId, continuation links, display fallback, sequence, wall/monotonic time facts, positions, duration, and source trace.

BA5 now consumes shared BA0 ProfileScopeId. Playlist is created with durable user-authored name + createdAtMs. PlaylistItem carries durable addedAtMs, which must not predate the playlist. Rename is explicit and validated.

BE2 now consumes shared BA0 ProfileScopeId. Explicit/suppression conflict order is positive host revision, not wall time. changedAtMs remains descriptive and may move backward when the revision advances.
## Verified

Lead ran fresh out-of-tree Visual Studio 2022 x64 / Qt 6.11.1 configure-build-test sweeps.

- BA4: 2/2 CTest PASS
  - vinyl_ba4_listen_event_ledger_contract
  - vinyl_ba4_listen_event_persistence_contract
- BA5: 3/3 CTest PASS
  - vinyl_library_harness
  - vinyl_playlist_harness
  - vinyl_personal_state_persistence_harness
- BE2: 2/2 CTest PASS
  - vinyl_feedback_taste_state_harness
  - vinyl_feedback_taste_persistence_harness
- all three clean configure/build commands exited 0.
- backward wall-clock behavior is explicitly covered by the BA4 harness.
- continuous span coalescing is explicitly covered by the BA4 harness.
- BA4 durable round-trip includes continuationOf and display snapshot.
- BA4/BA5/BE2 corruption paths return typed errors.
- BA4/BA5/BE2 expose MinimumSupportedVersion=1 and SchemaVersion=2.
- BE2 tests prove higher host revision applies despite backward descriptive wall time.

## Unresolved

- Production Colosseum adoption is not claimed.
- W4-G owns history/taste derivation and final BE3 policy.
- W4-H remains the composed cross-lane integration proof.

## Evidence

- BA4-LISTEN-EVENT-LEDGER-CONTRACT.md
- ba4-listen-event-ledger-prototype/
- BA5-LIBRARY-PLAYLISTS-CONTRACT.md
- ba5-library-playlists-prototype/
- BE2-FEEDBACK-TASTE-STATE-CONTRACT.md
- be2-feedback-taste-state-prototype/
- Lead clean verification builds under %TEMP%: vinyl-w4e-ba4-final, vinyl-w4e-ba5-final, vinyl-w4e-be2-final
## Durable formats

Three versioned reference formats are frozen:

1. BA4: `colosseum.vinyl.listen-event-ledger`, writer v2, supported reader v1..v2.
2. BA5: `colosseum.vinyl.personal-state`, writer v2, supported reader v1..v2.
3. BE2: `colosseum.vinyl.feedback-taste-state`, writer v2, supported reader v1..v2.

All three:
- emit deterministic compact JSON;
- reject malformed JSON and unsupported future versions with typed errors;
- fail closed on unknown fields in a declared version;
- prove supported-v1 restore and current-version rewrite.

BA5 canonicalizes non-semantic Library/playlist container ordering while preserving playlist-item order. BE2 canonicalizes entity and behavioral-observation ordering. BA4 preserves its authoritative append sequence.
## Public interfaces for W4-G

### BA4
- `ListenContext::create(ProfileScopeId, ListenSessionId, QueueOccurrence, PlaybackWorkToken, PlayInstanceId, continuationOf?, displaySnapshot?)`
- recorder observations carry `observedAtMs` plus `monotonicElapsedMs`
- `PlayInstanceId` is durable identity; `PlaybackWorkToken` is live fencing only
- `continuationOf` links same-occurrence source/video replacement attempts
- QueueOccurrence still carries W4-D EnqueueOrigin for downstream behavioral derivation
- `VinylListenEventPersistence` exposes durable history without BA2 generation/work-token fields

### BA5
- shared BA0 `ProfileScopeId`
- `Playlist::create(profileId, playlistId, name, createdAtMs, provenance)`
- `PlaylistItem::create(itemId, recording, addedAtMs, releaseContext?)`
- `VinylPersonalStatePersistence` persists Library + ordered Playlists

### BE2
- shared BA0 `ProfileScopeId`
- explicit feedback: `applyExplicitFeedback(canonical, feedback, revision, changedAtMs, providerRef?)`
- suppression setters take `revision, changedAtMs`
- `FeedbackTastePersistence` persists explicit, suppression, revisions, descriptive timestamps, provenance, and behavioral evidence
## Verification

Final Release rebuild and CTest sweep under MSVC 19.44 / Qt 6.11.1:

- BA4: **2/2 PASS**
  - vinyl_ba4_listen_event_ledger_contract
  - vinyl_ba4_listen_event_persistence_contract
- BA5: **3/3 PASS**
  - vinyl_library_harness
  - vinyl_playlist_harness
  - vinyl_personal_state_persistence_harness
- BE2: **2/2 PASS**
  - vinyl_feedback_taste_state_harness
  - vinyl_feedback_taste_persistence_harness

All three persistence harness binaries were also executed directly with exit code 0.
Adversarial/static checks:
- no `ListenProfileId` remains in BA4 prototype;
- no `TasteProfileScopeId` remains in BE2 prototype;
- no BA5-local `class ProfileScopeId` remains;
- BA4 persistence source contains no `generation` or `workToken` field;
- BE2 contract contains no equal-timestamp ordering law;
- BA5/BE2 contracts no longer defer their storage formats.

The BA4 harness explicitly proves a backward wall-clock step with forward monotonic time. Its persistence harness simulates a process restart where BA2 generation returns to 1 for the same occurrence and proves a new PlayInstanceId remains distinct.
## Changed

- BA4-LISTEN-EVENT-LEDGER-CONTRACT.md
- ba4-listen-event-ledger-prototype/
  - VinylListenEventLedger.h/.cpp
  - VinylListenEventPersistence.h/.cpp
  - test_vinyl_listen_event_ledger.cpp
  - test_vinyl_listen_event_persistence.cpp
  - CMakeLists.txt
- BA5-LIBRARY-PLAYLISTS-CONTRACT.md
- ba5-library-playlists-prototype/
  - VinylLibrary.h/.cpp
  - VinylPlaylist.h/.cpp
  - VinylPersonalStatePersistence.h/.cpp
  - test_vinyl_library.cpp
  - test_vinyl_playlist.cpp
  - test_vinyl_personal_state_persistence.cpp
  - CMakeLists.txt
- BE2-FEEDBACK-TASTE-STATE-CONTRACT.md
- be2-feedback-taste-state-prototype/
  - FeedbackTasteState.h/.cpp
  - FeedbackTastePersistence.h/.cpp
  - test_feedback_taste_state.cpp
  - test_feedback_taste_persistence.cpp
  - CMakeLists.txt
## Unresolved

None inside W4-E.

Production Colosseum adoption is not claimed. Concrete production file/database placement, privacy retention/reset UI, cloud sync, and external tracker delivery remain later adoption/product concerns, not missing W4-E reference semantics.

## Next

W4-G may consume the public interfaces above together with W4-D EnqueueOrigin and W4-F catalogue features.

W4-H should use the BA4 durable ledger during the composed Play journey and verify that same-occurrence fallback/source replacement links one listening journey rather than fabricating a replay.


## 2026-10-07 W4-H integration repair

Hemanth explicitly authorized Agent-8 to repair the BA4 ownership blocker exposed by W4-H.

- BA4 now has distinct `ListenEventKind::Failure` and `ListenEventRecorder::failure(...)`.
- Failure uses the same exact PlaybackWorkToken stale fence and qualified-time termination path as other terminal facts.
- BA4 durable writer advances from v2 to v3. Reader support remains v1..v3; v1/v2 historical rows remain readable.
- Persistence round-trip proves `failure` does not collapse into `stop`.
- Stale failure callbacks mutate nothing.
- W4-G derivation explicitly regression-tests a failed play: factual Start remains history, while Failure does not fabricate completion, skip, early-skip, or passive-autoplay evidence.
- Fresh BA4 Release CTest: 2/2 PASS.
- Fresh BE-DERIVATION Release CTest: 1/1 PASS.
- Production Colosseum remained untouched.

This resolves `W4-H-BLOCKER-BA4-FAILURE-CAUSE.md`; T-8 may resume at H3.
