# Colosseum Server: active-reader circular-cache protection

Completed bounded component repair, India calendar date **9 October 2026**. Work began after 00:00 IST (8 October 18:30 UTC). This continues the same reader-eviction slice checkpointed on 8 October; no second slice or unrelated queue item was started.

## Source and authority

Repository: `kingoftheseas56/Colosseum`, existing branch `feature/colosseum-server-1.0` only.
Initial assignment pin: `5b03cfbf6d4f6487bf991a7af834e6d1a3a176ef`.
Verified working baseline: `733d8affd493f2e71284298441479ec5a9f9a1de`, the already-delivered 7 October repeated-tick rechoke repair. Its two changed packet files were refreshed from that exact commit; all 108 native-subtree source blob identities were verified before editing. No unknown concurrent source drift was adopted. Branch tip was rechecked before publication.

The original source/intent packet, prior opinion and 8 October checkpoint were read. The current automation supersedes the old opinion packet's read-only authority. No user computer, BDC, Codex/Work or delegated coding was used. No other branch, master/main, workflow, PR, tag, release or repository setting changed.

## Reproduction and fix

The real `TorrentEngine`, `FileReader`, scheduler, metadata verification and `CircularPieceStore` were composed with the repository's existing deterministic fake transport and worker lane. With a stage queued first, two readers then acquire a cached piece; baseline stage execution evicts it before those queued reads execute. Both real readers terminate with **piece is unavailable**.

The repair admits individually tokened read reservations on the app lane, synchronized with the work lane. Circular staging consults the live reservations at execution time rather than a stale post-time snapshot. While reads are outstanding, staging retains the received bytes and defers through the existing app/work queues. Once reads have copied their bytes, staging resumes. No refetch or new parallel store is introduced. Cancellation removes only its own token; generation closure drains old reservations. Persistent-store staging/read behavior is unchanged.

This intentionally conservative policy defers circular staging for any admitted read, rather than computing an eviction candidate around active reads. It protects already-admitted reads; it is not a claim of permanent protection for all selected/unrequested pieces or a full cache-coherence redesign. Throughput under sustained load is unmeasured. The mutex covers circular memory-store operations; persistent disk operations are not put behind it.

## Executed evidence

- **PASS:** 20 named focused CTest invocations: 13 engine cases (8 existing plus 5 added), four unchanged FileReader cases and three unchanged CircularPieceStore cases. Some engine regression functions are also invoked by larger existing cases; these counts are invocations, not 20 distinct defects.
- **PASS:** all 8 selected existing engine invocations on unchanged baseline as well as candidate.
- **EXPECTED FAIL on baseline, PASS after restoration:** R7 queued-stage/two readers; R8 one-slot/full-pressure case; R9 cancellation of one sibling while the other retains ownership.
- **PASS on both:** R10 cancellation of both pending readers; R11 retirement/recreation while old read/stage callbacks are queued. These are preservation checks, not claimed newly repaired failures.
- **PASS:** ten repeated runs of each new case, 50 additional invocations, with bounded 20-second process timeouts.
- Tests assert exact bytes, no duplicate delivery, released reader locks, incoming-piece completion and no additional network request caused by pressure. Existing assertions were preserved; five cases were appended and registered in the existing K11 test file.
- Initial fixture setup attempts failed before reaching the intended race; logs preserve them. The corrected fixture fences before `registry.poll()`, which routes transport observations synchronously on the app lane. The definitive baseline failure is `red3.log` / `negative-R7.log`, not the preliminary setup failures.

## Environment and qualification boundary

Own-cloud Debian 13 x86_64; GCC 14.2.0; QtCore 6.11.1 official Linux package plus its ICU73 companion; CMake 4.4.3; C++17 Debug component harness. QtCore SDK acquisition, previously blocked, succeeded using the official Qt package URLs and published SHA-1 checks, with local SHA-256 recorded.

- Qtbase archive SHA-256: `1d6d82d765987d657d0df136fb347e98d6e29e99ef8d1cb433abf9f15c86ab6f`
- ICU archive SHA-256: `6ea4a612560b6eb39173bcfaa35abc08904fc124e4c0f93e1b39dd22368c33c8`
- QtCore binary SHA-256: `5f5abaaeb8c01d9ec53498ec2c54d81d1048e30522d71e76e223569a59749f11`
- Final focused engine executable SHA-256: `0f6255d514fa567637099efbc7ca0f35ca0eacbff3f8a790aaae3b47177793c4`

The focused harness links production engine/policy/storage source and the existing fake-transport fixture. A harness-only native-transport factory aborts if accidentally called; it does not fake a successful network transport. **Real libtorrent, wire behavior, public swarm, HTTP end-to-end streaming, player playback, full-server parity and Windows runtime are NOT RUN.** The normal full-server CMake configure was executed and is **BLOCKED** by its existing locked `C:/tools/libtorrent-2.0-msvc/include/libtorrent/version.hpp` dependency. The K11-03 whole case includes native transport and was not misrepresented as executed.

No CI/workflow was edited or dispatched. Current remote CI receipts, exact publication commit and file identities are recorded separately in `PUBLICATION.json`.

## Reproduce / receiving

Check out the baseline above, apply `baseline.patch`, then use the normal supported Windows locked-dependency build or the supplied focused Linux harness. Set `SOURCE_ROOT` to the candidate's `native/colosseum_server_v1`, point CMake at a QtCore6.11 kit, and provide its ICU runtime in the loader path when required. The ZIP includes complete changed files, the standalone harness, source manifest and raw logs. It excludes installed SDKs and transient build output.

This completes only the active-reader eviction component slice. The next queue item remains storage exceptions reaching terminal results. The smallest controlled end-to-end stream still needs the full transport/host dependencies and remaining bounded repairs; no parity or playback promise is made. Stop after this checkpoint.
