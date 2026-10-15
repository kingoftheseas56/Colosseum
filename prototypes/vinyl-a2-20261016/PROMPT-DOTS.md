# Dots: Vinyl A2 composed-service integration and Linux qualification

Integrate the supplied independently approved GLM compile repair into the supplied composed artist-service prototype, build it in your cloud environment, and qualify the composed service plus its five original harnesses. Produce a reviewable source commit and evidence. This is prototype integration, not production application wiring.

## Source authority

The attached packet is required: these prototypes are local-only and are not supplied by a reachable application Git commit. `baseline/` is the exact unmodified starting source for all six sibling directories; `BASELINE-MANIFEST.json` pins every source file by SHA-256. Verify those hashes before editing. Record the imported baseline Git commit before applying the patch. Preserve the sibling layout: the composed CMake file resolves dependencies through its parent directory.

Apply `evidence/vinyl-a2-compile-fixes.patch` from inside `baseline/artist-service-composed-20261015`. Patch SHA-256: `67E467CC89616994E05CBB08126BB9E9F175429580BAE4F41B243B02B0BD2DE3`. Do not substitute other historical prototype copies. There is no wider application-source dependency for this assignment. Packet source takes precedence over any similarly named Git paths.

The approved repair was recorded with its receiving review in Brotherhood coordination commit `c06e40dbb3007426b4e96244f4471ae20f334559`; that commit records the patch/review, not an application integration or the entire source baseline.

## Work

1. Verify source and patch integrity; create a practical isolated working branch/source location and record its baseline identity. You may create/use Colosseum task branches, the connected alternate Colosseum fork, and Actions/cache where useful. Do not make production-main changes or release the application.
2. Apply the patch. It changes only three compilation sites in two files: `lengthMs` is a plain int (positive becomes qlonglong QVariant; nonpositive stays invalid), and two `SourceBatch` values require aggregate initialization because ArtistMbid has no default constructor. Preserve strong types, statuses, source labels and snapshot IDs.
3. Configure/build with a supported Qt 6 SDK (CMake requires Qt >= 6.5 Core/Test, C++17). Record actual OS, compiler, CMake, Qt and generator versions. A separate build directory is required. Example from the packet root:

   cmake -S baseline/artist-service-composed-20261015 -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel 2
   ctest --test-dir build --output-on-failure
   ./build/test_artist_service_composed

   Adapt SDK discovery and executable locations to your environment. Keep the five original suites enabled. Record each command's exit code and preserve raw logs.
4. Expected baseline: six CTest suites pass; direct composed QtTest totals were 10 passed/0 failed/0 skipped on Windows. The ten include init/cleanup and eight behavior cases. Exercise artist entry/all lanes, redirect handling, invalid cursors, profile isolation, late-reply generation fencing, saved-state reload, playback selection/edition mismatch, and deterministic missing-artwork fallback through the actual harness. These use a fake source and do not establish provider-network or full-app playback behavior.
5. If Linux exposes an integration failure, make the smallest necessary repair, distinguish it from the approved two-file patch, and verify affected cases. Do not broaden into unrelated prototype defects. If an SDK/input is unavailable, report the precise blocker rather than inventing a result.
6. Commit the accepted integration and any required bounded repair on your working branch; push to a reachable task branch when practical. Keep the implementation where authored. Create one concise `kingoftheseas56/Dots-Colosseum` delivery entry pointing to the source, commits and evidence. If Git delivery is unavailable, return the changed source/patch and evidence as a packet instead.

## Existing evidence and boundaries

GLM's supplied Windows Qt 6.11.1/MSVC logs report build exit 0, CTest 6/6, direct composed QtTest 10/0/0, and no /W4 compiler warnings. Codex independently reviewed the patch, source contracts and logs and checked patch applicability; it did not rerun that Windows build. Reproduce your Linux results independently.

DeepSeek owns an independent QML GC-crash investigation. This assignment does not include QML/sidebar/scrollbar edits, production service registration, live-provider work, or full application wiring. Current restrictions reflect this assignment's file ownership, not a permanent restriction on Dots' capabilities. Linux results remain Linux-qualified; Windows qualification and production adoption remain separate.

## Return

Return your working repository/fork, branch, exact baseline and result commits, actual commands/exit codes, raw build/test logs and totals, final diff, environment versions, and one delivery entry with PASS/FAIL/BLOCKED/NOT RUN and known gaps. State separately what was authored, reviewed, built, tested, committed and pushed. No duplicate source ZIP/report is required when Git plus the delivery entry supplies everything.
