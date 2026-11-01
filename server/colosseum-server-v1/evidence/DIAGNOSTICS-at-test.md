# Colosseum M07 local verification

The bounded legacy-HLS slice is implemented and locally verified. Overall 1:1 Stremio server parity is still unproven. No commit, push, source export, or publication occurred; this is local working state, not a durable external checkpoint.

## Reference and result

- Public branch: feature/colosseum-server-1.0
- Baseline HEAD: f32f84c963cec05e0b0c253ac5829f410dfe7854
- Historical last documented tested checkpoint: 7022f98db72e5df44f2163783caeb8899aae5fc6, 150/150 tests.
- Fresh untouched f32 baseline: two complete runs each 149/150. First failure was regenerated reference-index metadata, subsequently corrected. Second was existing K10 failure-drain timing (gap_ms=-48); its isolated unchanged-binary rerun passed. No K10 code changed.
- Final reviewed candidate: 158/158 registered CTests pass, including the 150 existing tests and eight M07 gates.
- Focused evidence: 286/286 renderer comparisons plus original 32/32; 22/22 native index cases; 45/45 pinned index comparisons, split equally across synchronous local, production asynchronous local, and fragmented HTTP; four pinned probe runtime outcomes; PID-backed cancellation and production routing checks; actual synthetic-media stream tests.

Actual regressions were retained: renderer 52 mismatches, then two underflow mismatches; initial index 1/15 passing; resource checks 19/21; post-mdat checks 21/22 native and 43/45 oracle; two probe error-body failures. All targeted cases pass after correction.

## Exact identities

- Official oracle: https://dl.strem.io/server/v4.21.1/desktop/server.js
- Oracle: 6,676,503 bytes; CDN selector v4.21.1, embedded package v4.21.0.
- Oracle SHA256: 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f
- Candidate server1_host SHA256: 03d67ba1646f87428619c4089bb70f37047e3968dea0f5d06536fbd317114434
- Local working.patch SHA256: 7d44314edacfea86dec345883898644508a09d9ace2ef96bcd23c7a0a55b45b0
- Local manifest.json SHA256: 43909ca3d160d474fe4397ecb373e1a0673f1670d1e711ca7e5ba64f75f252db

## Changed paths

All paths below are under native/colosseum_server_v1/:

- CMakeLists.txt
- include/server1/http/LegacyMediaRoutes.h
- src/ServerComposition.cpp
- src/http/LegacyMediaRoutes.cpp
- src/media/LegacyHlsService.cpp
- src/media/LegacyMkvIndex.cpp
- src/media/LegacyMp4Index.cpp
- tests/cmake/M07.cmake
- tests/fixtures/legacy-render-parity.json
- tests/legacy-media-index-oracle.cjs
- tests/legacy-runtime-fixture.py
- tests/run_legacy_composition.py
- tests/run_legacy_media.py
- tests/run_legacy_render_oracle.cjs
- tests/run_legacy_runtime_oracle.cjs
- tests/test_legacy_media_index.cpp
- tests/test_legacy_render.cpp
- tests/test_legacy_runtime.cpp

## Remaining gaps and next scope

- M07 keyed preparation coalescing remains open. A follow-up must preserve cancellation when one of several consumers disconnects.
- The pinned waitForSubs preparation path has a module-scope defect; faithful bug preservation versus intentional deviation needs an explicit decision.
- The candidate now caps retained ordinary MP4 metadata atoms/buffer at 256 MiB. This prevents the new buffering regression but deliberately rejects oversized inputs that the pinned oracle may retain without that ceiling. The existing moov cap/error was preserved. This is a documented parity limitation, not an exact-match claim for unbounded inputs.
- Real ffsplit execution, Windows, comprehensive malformed inputs, remote conversion, and other server families are not qualified by this slice.
- Resolve a safe authorized durable checkpoint/publication route before expanding into more service families. The next proposed bounded implementation is M07 preparation coalescing with isolated differential and cancellation tests.

Full local receipt: logs/m07-ctest-all-reviewed.log. Build and source hashes, reference provenance, earlier failures, and reproduction recipe are retained beside this report. The source patch remains local and is not included in this diagnostic report.
