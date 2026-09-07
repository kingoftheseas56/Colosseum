# P03-A repair report

Worker: P03-A; packet: P03; branch: `feature/colosseum-server-1.0`. Pre-repair HEAD: `39b2b9b7f00386302e01e22b72e92a838492d611`. Integration predecessor: `0d002c6d8af49ba3647d9f729f753aa4c7d3b532`. Selected immutable application base at creation: `81e750b65f3788af7be99392f16b86a9212ff2bd`. P03 has no oracle modules or module/range tuples; authenticated oracle identity is `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930`.

## Repair

Changed only `native/colosseum_server_v1/tests/CMakeLists.txt` and P03-owned report/evidence. The explicit test registry now prepends `$<TARGET_FILE_DIR:Qt6::Core>` through CMake `ENVIRONMENT_MODIFICATION`; it does not embed a developer path. This closes the Sol finding that clean-shell CTest returned `0xc0000135` before the host started. Sol independently reconfigured the fresh review build after this change and reported CTest 1/1 passing without pre-seeded PATH.

## Case states

P03-01 — PROVEN PASS. Fresh standalone configure exited 0 at `artifacts/server1/P03/repair-build-20260907`; fresh Ninja build exited 0 and produced `server1_host.exe`. Controlled missing-lock configure exited 1 and named `C:/tools/libtorrent-2.0-msvc/lib/does-not-exist.lib`. Exact commands, exits, and raw output are in `P03-01-CONFIGURE.txt`, `P03-01-BUILD.txt`, and `P03-01-MISSING-DEPENDENCY.txt`.

P03-02 — PROVEN PASS. Direct host execution exited 0 with Qt `bin` derived from the locked `QtCore/qglobal.h` path and reported `streaming-ready=NO` on initialization and shutdown. CTest launched with PATH restricted to Windows system directories passed 1/1, exit 0, proving the packet-local registry supplies Qt runtime discovery. Exact commands and raw output are in `P03-02-HOST.txt` and `P03-02-CTEST.txt`; the pre-fix Sol failure is retained there as contrast.

P03-03 — PROVEN PROVENANCE, CURRENT SNAPSHOT REQUIRED. Reflog records `branch: Created from 81e750b...` before the accepted prerequisite commits through `0d002c6d`; current HEAD is not the selected base. The integration predecessor is `0d002c6d`, while the repair starts from `39b2b9b7`; neither is claimed to equal `81e750b`. The post-commit branch/worktree snapshot is in `P03-03-BRANCH-WORKTREE.txt`.

## Interfaces and no-change statement

Produced interfaces remain the packet-local `RuntimeSkeleton` and `PacketLocalBuildManifestContract`: `server1_runtime` (C++17 INTERFACE target), `server1_host` (QCoreApplication host), and the explicit P03 CTest registration. The only repair behavior is test-launch environment setup derived from `Qt6::Core`.

No routes, streaming transport, torrent/process ports, Server 0.1 files, root/shared CMake registration, `native/colosseum_server/**`, composition, or streaming-ready capability were added or changed.

## Exact artifact paths

Evidence root: `artifacts/server1/P03/`. Relevant files: `RED-CONTRACT.txt`, `P03-01-RED-GREEN.txt`, `P03-01-CONFIGURE.txt`, `P03-01-BUILD.txt`, `P03-01-MISSING-DEPENDENCY.txt`, `P03-02-HOST.txt`, `P03-02-CTEST.txt`, `P03-03-BRANCH-WORKTREE.txt`, `CASE-INPUTS.json`, `missing-dependency-lock.json`, and `WIRING-REQUEST.json`. Disposable build directories are local evidence inputs only and are not committed.

## Open failures and risks

The earlier pre-fix clean-shell CTest failure (`0xc0000135`, CTest exit 8) is resolved by the fresh clean-PATH pass and Sol's independent confirmation. Runtime verification proves only skeleton initialization/shutdown and Qt discovery; it does not prove streaming behavior, because no streaming implementation exists. The dependency lock contains machine-local raw-disk identities by design; the source contract consumes the lock and fails visibly when required inputs are absent. The receipt is a selected packet snapshot, not the entire repository. Push was not performed.

## Wiring

`artifacts/server1/P03/WIRING-REQUEST.json` remains packet-local and requests Sol review/integration of the standalone skeleton. No root/shared wiring is requested.
