# W4-C Handoff - Source Resolution Session + Selection + Ordered Fallback + Expiry

AGENT: Agent-3  
TASK: T-3 / W4-C  
STATE: VERIFIED  
DATE: 2026-10-07

## COMPLETED

- Added BB5 as the explicit owner of source selection, asynchronous resolution-session fencing, ordered fallback, expiry re-resolution, typed exhaustion, and profile-scoped source memory.
- Audio selection admits only BB2 Exact results. Ambiguous/Rejected/InsufficientEvidence fail closed.
- Video selection admits only BF2 direct Audio | Video-qualified mappings.
- Added deterministic selection with explicit cross-kind order, injected source-kind scorer, optional provider preference, user pin, known-wrong/dead exclusion, and stable-key final tie-breaker.
- Provider arrival order is never selection/fallback authority.
- Added exact source-session/request identity with QueueOccurrenceId + RecordingMbid + candidate + transport kind + session sequence + request sequence + reason.
- Late resolver success and failure are both rejected by the same fence.
- Added finite ordered fallback for resolver failure, playback failure, invalid resolved transport, expiry, and exhaustion.
- Fallback never resurrects already-attempted candidates in the current chain.
- Before a next asynchronous fallback is dispatched, BB5 re-arms W4-B `awaitInput(expectedOccurrenceId, playWhenReady)`, preserving explicit wait/restore intent.
- Added typed `SourceFallbackTerminal` for exhausted candidates.
- W4-A ResolvedTrackSource expiry is explicit. Unknown expiry remains unknown. Expiry re-resolution retains QueueOccurrenceId and uses a fresh request identity.
- An immediately expired refresh is bounded and falls through rather than looping forever.
- Same-occurrence accepted replacement reaches playback only through W4-B `restartActive(expectedOccurrenceId, input)`.
- Added optional W4-E PlayInstanceId continuation ancestor to replacement facts so BA4 can link transport changes into one logical listening journey.
- Added deterministic versioned profile source-memory representation with user pin and known-wrong/dead state; transient failure does not automatically mutate it.
- Refactored BF3 to consume the general BB5 request lifecycle. BF3 now dispatches exact BB5 video requests and converts external resolver output into W4-A ResolvedTrackSource.
- Removed BF3's private resolution sequence, private pending request, separate stale-result lifecycle, direct BA2 handoff, and direct beginOccurrence replacement path.
- Audio -> Video -> Audio preserves RecordingMbid and QueueOccurrenceId for same-occurrence replacement.

## DETERMINISTIC REFERENCE POLICY

Cross-kind order:

1. LocalFile
2. PeerToPeer
3. Http
4. ProviderMediated
5. ExternalResolver
6. Unknown

Within an allowed kind, the injected scorer owns source-kind quality facts. The reference scorer uses lossless, bitrate, sample rate, and bit depth for audio. Explicit provider preference is a separate policy input. SourceCandidateKey stable ordering is only the final deterministic tie-breaker.

Profile-memory exclusions beat a user pin until explicitly cleared.

## SCRIPTED RACES PROVEN

The native W4-C harness explicitly reproduces:

- skip during audio resolve;
- skip during video resolve;
- Play Now during fallback;
- remove-active during resolve;
- stop during resolve.

It also proves:

- late success from an older resolver request is stale;
- late failure from an older resolver request is stale;
- video request supersession uses the same stale fence;
- expiry produces a fresh request;
- playback failure re-arms W4-B AwaitingInput before async fallback.

After every tested state-changing race step:

`BA2 bound occurrence is NONE OR BA2 QueueOccurrenceId == BA1 active QueueOccurrenceId`.

## ADVERSARIAL ACCEPTANCE MATRIX

The BB5/BF3 native harnesses cover the requested W4-C controls:

1. normal async resolution succeeds;
2. two Exact candidates order deterministically;
3. candidate/provider arrival permutation produces identical selection order;
4. Ambiguous/nonexact candidates cannot enter selection/playback;
5. late old success is rejected;
6. late old failure is rejected;
7. skip during audio resolve invalidates old work;
8. stop during resolve invalidates old work;
9. remove-active during resolve invalidates old work;
10. Play Now/active replacement cannot cross-wire the result;
11. expiry issues a fresh request identity;
12. resolve failure falls through to the next candidate;
13. playback failure triggers ordered async fallback;
14. exhaustion is typed;
15. retry/fallback has a finite stop condition;
16. same-occurrence fallback preserves QueueOccurrenceId;
17. replacement path is W4-B restartActive;
18. stale replacement cannot bind BA2 to a different BA1 active occurrence;
19. video obeys the same BB5 stale-request law;
20. Audio -> Video -> Audio preserves RecordingMbid;
21. known-wrong and known-dead source memory alter selection deterministically;
22. profile A source memory does not affect profile B;
23. user pin is an explicit deterministic policy input;
24. transient failure does not automatically become durable wrong/dead state;
25. source-memory v1 serializes/restores deterministically.

## VERIFIED

Toolchain for both final clean trees:

- Qt 6.11.1 MSVC 2022 x64
- Visual Studio 17 2022 generator
- MSVC 19.44.35223.0
- Windows SDK 10.0.26100.0

Final clean BB5 tree:

`backend/W4-C-verified-bb5`

- clean configure: PASS
- Release build: PASS
- direct `vinyl_bb5_source_resolution_session_harness.exe`: exit 0
- CTest: 1/1 PASS, 0 failed

Final clean BF3 tree:

`backend/W4-C-verified-bf3`

- clean configure: PASS
- Release build: PASS
- direct `vinyl_bf3_video_playback_adapter_harness.exe`: exit 0
- CTest: 1/1 PASS, 0 failed

Adversarial/static audit:

- BF3 private lifecycle/direct BA2 spellings: 0 hits
- BB5 direct `beginOccurrence` calls: 0 hits
- BB5 source contains exactly one `m_coordinator.restartActive` replacement call
- BB5 source contains 3 `m_coordinator.awaitInput` call sites: initial wait, ordered fallback wait, expiry wait
- unbounded `while(...)` / `for(;;)` spellings in BB5: 0 hits
- QML/QtQml/BE3/autoplay/radio ownership spellings in BB5/BF3 source/CMake: 0 hits
- frozen W4-A/W4-B source mtimes remain pre-W4-C and were not rewritten
- production Colosseum received no Agent-3 write; its currently dirty files belong to unrelated in-flight work

Verification history:

- first BB5 build exposed a BF2 test-fixture aggregate initialization mistake; library compiled; fixture corrected
- next harness run exposed an explicit-provider-preference fixture using the wrong provider IDs; fixture corrected
- adversarial review then found a real W4-C semantic gap: playback-failure fallback did not re-arm W4-B pending input; implementation and regression test were repaired
- final clean BB5 and dependent BF3 builds/tests above are the acceptance evidence

## CHANGED

Canonical W4-C source:

- `BB5-SOURCE-RESOLUTION-SESSION-CONTRACT.md`
- `bb5-source-resolution-session-prototype/CMakeLists.txt`
- `bb5-source-resolution-session-prototype/SourceResolutionSession.h`
- `bb5-source-resolution-session-prototype/SourceResolutionSession.cpp`
- `bb5-source-resolution-session-prototype/test_source_resolution_session.cpp`
- `BF3-VIDEO-PLAYBACK-ADAPTER-CONTRACT.md`
- `bf3-video-playback-adapter-prototype/CMakeLists.txt`
- `bf3-video-playback-adapter-prototype/VideoPlaybackAdapter.h`
- `bf3-video-playback-adapter-prototype/VideoPlaybackAdapter.cpp`
- `bf3-video-playback-adapter-prototype/test_video_playback_adapter.cpp`
- `W4-C-HANDOFF.md`
- append-only Agent-3 CLAIM / PUBLIC_INTERFACE / HANDOFF messages in `WAVE4-AGENT-ROOM.md`

Generated evidence trees:

- `W4-C-verified-bb5/`
- `W4-C-verified-bf3/`

Earlier W4-C build trees are non-authoritative generated evidence only.

## IMPORTANT FINAL HASHES

- BB5 contract: `EAF13E94F679B1ECEB13131E38E7E17C632B603BD6288A598BD4D07F27B0355F`
- SourceResolutionSession.h: `276736EBA369F176AB843EAAB56435A3DDE3AD8BB4042D64B72C1237629A0A3C`
- SourceResolutionSession.cpp: `E7148CE1AB54FB203CA565649714DA187E50D29598F4D7B576125FA8166304A0`
- BB5 harness: `284FFBDB44779BFBFE90CA57108FDFB3749F7FEA62A80FB657D4FFC9D7235BAD`
- BF3 contract: `4A0936905E5CB0BEF93771122203F6A6310F5DDB98A6D21A19BC1F27F3D68751`
- VideoPlaybackAdapter.h: `A8979962492388ACDB60D2048F0C8379092E25539C77ADC83A6BB6ABC18DFE61`
- VideoPlaybackAdapter.cpp: `6563FBCF76B8C01BF39737F323EE9A67D53E78B8CD01B7F11C55FD6AA2DF8675`
- BF3 harness: `37DCA9F3FCB410BDBDB421F6053CDF1B1DDAB771F8371A719A76363EF846F9CC`

## UNRESOLVED

None inside W4-C scope.

W4-H still owns the composed end-to-end journey, BA3/BA4 cross-lane assertions, quit-during-resolve restore race, real-libmpv composed journey, and whole-Wave superbuild.

Production Colosseum adoption is not claimed.

Shared backend README, Arc 62 README, MEMORY.md, and final conversation recap remain Lead-owned per the Wave 4 room and were not modified by Agent-3.

No commit or push was performed.

## NEXT

Lead may independently accept T-3.

W4-C is ready for W4-H once T-7 is also complete/accepted.

Agent-3 must not start Slice 8.
