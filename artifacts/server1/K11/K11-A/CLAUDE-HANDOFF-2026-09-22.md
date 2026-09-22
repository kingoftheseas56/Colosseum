# K11-A repair handoff to Agent 4 (2026-09-22)

Producer: `[Agent (Claude), K11-A repair producer]`. Reviewer and integration authority: Agent 4 (Codex).

## Result: COMPLETE

The repaired K11 candidate is committed, pushed, and reproducible from the recorded commands. It is ready for Agent 4's independent review. This does **not** mean B-W3E is accepted. No K12, H01, B-W3E, aggregate, STATE, master, or official-feature change was made.

## Git state

| Item | Value |
|---|---|
| Worktree | `C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired` |
| Branch | `sol/server1-w3-engine-v3-20260915` |
| Accepted base | `b28e2dfe8562a5f942d569ab981d97a0530113bf` |
| Rejected K11-v3 (input) | `81abb58d512d61b42fa0f7fdc376446fbf5831d5` |
| Repaired candidate (code + evidence) | `d115f80117a37992e45749e8897f7d3891024e6e`, pushed; `origin/sol/server1-w3-engine-v3-20260915` equals it |
| This handoff | one documentation-only commit on top of `d115f801` (it cannot name its own SHA) |
| History | additive commits only; no rewrite, no force-push; `5cb92001` (K11-v2) untouched |
| Official feature | `C:\b\Colosseum-Server-1.0` and `origin/feature/colosseum-server-1.0` still at `b28e2dfe`; the three user-owned deletions `docs/build/linux.md`, `macos.md`, `windows.md` are unchanged |

### Preserved dirty state

The uncommitted 25-line retained-reader regression in `test_engine_registry.cpp` was kept. Its assertions and messages are unchanged. The only additions are a `LaneWorker` work executor and `settle()` calls in place of bare `poll()/dispatch()`, both required by the new asynchronous work lane. It is now committed inside `runK1103`. FileReader production code was not changed.

## Changed files (vs `81abb58d`)

- Production: `native/colosseum_server_v1/include/server1/policy/EngineRegistry.h`, `src/policy/EngineRegistry.cpp`, `src/policy/TorrentEngine.cpp`
- Tests and manifests: `tests/test_engine_registry.cpp`, `cmake/packets/K11.cmake` (adds the `server1_k05_swarm_metadata` link for M814 rechoke)
- Evidence: `artifacts/server1/K11/K11-A/`
  - `CMakeLists.txt` (registers K05 for the packet-local build)
  - `run_oracle.js` (rewritten) and `run_mutations.py` (new)
  - `SOURCE-TRACE.md`, `MUTATIONS.md`, `TEST-COMMANDS.txt`, `PACKET-RECEIPT.json`, `SELF-REVIEW.md`, `WIRING-REQUEST.json`, `BLOB-SEALS.json`
  - `raw/*` and `raw/repair/*`
  - v3 evidence moved with `git mv`, or restored from `81abb58d`, into `raw/v3-rejected/`
- Untouched: native and tests aggregate `CMakeLists.txt`, `docs/server1/STATE.json`, `TorrentTransport.h`, every accepted predecessor source.

## Findings and dispositions

**1. App lane blocked by peer/disk work (Critical): REPAIRED.** Each engine has a serialized work lane (`Strand`). The following run only there: transport open, `ConnectAction` (the adapter's `connect` blocks on `handle.status()`), transport close, metadata parse with persistent store open and restore scan, stage/verify/commit, cache reads, upload reads, and store close.
- The lane runs on `workExecutor` only when that executor defers the task and does not run it on the app thread. Otherwise it falls back to a registry-owned worker thread.
- The app lane keeps registry state, scheduler/FileReader policy, a committed-piece mirror, and mailbox `poll`/`submit`.
- RED: `raw/repair/red-v3-R1.*` shows metadata install ran on the app lane under v3.
- GREEN: `regressionAppLane` fences the lane and proves that nothing completes there during poll/dispatch. It also shows create/poll/dispatch return in under 1 s while a transport open is blocked. The M814 scenario asserts that every work trace ran on the work-lane thread.

**2. Destruction invoked callbacks on the destroying thread (Important): REPAIRED.** Create and remove completions are recorded in a terminal ledger with their decided outcome.
- With `callbackExecutor`, destruction posts `finalize` through the executor. It runs on the app lane, delivers each remaining terminal exactly once (decided `Removed`/`SourceError`, else `Cancelled`), and drops stale events.
- Without an executor, destruction on the app thread is the final dispatch turn. Destruction from a foreign thread abandons callbacks rather than running them there (documented contract).
- RED: `raw/repair/red-v3-R2.*`.
- GREEN: `regressionDestruction` covers these cases:
  - held create destroyed from another thread;
  - queued removal destroyed from another thread;
  - fenced-lane destroy that forces `finalize` to deliver the decided `Removed`;
  - remove before transport open, during metadata install, and during an in-flight commit. Each terminates once, with no late HAVE, data, or Ready.

**3. Timer 500 ms vs M814 10,000 ms (Important): REPAIRED.**
- `kRechokeIntervalMs = 10000`, started when metadata installs (M814 `ontorrent`) and cancelled once in close.
- The tick runs M814 rechoke through accepted K05 `SwarmPolicy` (uploads slots per M814), emitting `ChokeAction`. Ticks after close are ignored.
- Swarm-cap evaluation moved to M172's real triggers (new wire, download) and binds to the creating call's `swarmCap`. PeerSearch ticks on `poll()`.
- RED: `raw/repair/red-v3-R3.*`.
- GREEN: `regressionTimer` checks interval capture, start after metadata, rechoke unchoke, exactly one cancel, and two raw post-close callbacks with zero effects.

**4. PeerSearch results never drained (Important): REPAIRED.** `TorrentEngine::discoverPeer(sourceIndex, address)` is the translation of M612's per-source `peer` event.
- `drainPeerAdds` validates each literal `host:port` (port 1–65535), deduplicates, allocates an engine handle, and submits a generation-owned `ConnectAction` on the work lane. The `queued` count feeds `onSwarmState` on wire and pause/resume changes.
- PeerSearch is constructed at `isNew` creation, from torrent announce for metainfo sources or configured sources otherwise, so magnets can find peers before metadata.
- RED: `raw/repair/red-v3-R4.build.txt` (no entry point or drain in v3).
- GREEN: `regressionPeerSearch` checks dedup across sources, 7 malformed rejections, min/max hysteresis with and without a swarm cap, swarm pause/resume, blocking connects off the app lane, and a closed generation never connecting or leaking into its replacement.

**5. M172 fixture gaps and hard-coded resume count (Critical evidence): REPAIRED.** The real M172 factory now runs with these creates:
- stream alias plus `path:false` plus shallow `peerSearch`;
- a second create;
- reordered continuations;
- ready, then a cached create with `path:""`;
- remove, then recreate.

It measures `swarm.resume()` calls, records fresh `spoofedPeerId()` ids, the emitted options snapshot (no id at emit time), and the PeerSearch constructor sources. Native now reproduces M172's callback order inside the scoped ready event: `ScopedReady, C2, C1, Ready, ScopedReady, Ready`.

**6. Native traces were constants (Critical evidence): REPAIRED.** The native `--trace` output is built from observed transport actions, `EngineTrace` work-lane records, events, callbacks, reader deliveries, and timer calls. `normalize172`/`events814`/`normalize814` apply the same rules to both sides, and the rules are listed in `raw/comparison.json`.
- The disclosed source precommit visibility is `{"kind":"source-precommit-visible","piece":1}` in the source projection and must be absent from native.
- RED: `raw/repair/red-v3-oracle-drift.txt`. A v3 build with a 750 ms timer drift passes v3's tests and v3's oracle (all parity true). The repaired oracle rejects that binary.

## Verification

| Check | Result | Raw |
|---|---|---|
| Focused `ctest -L K11 --repeat until-fail:3` | exit 0, 9/9 | `raw/repair/focused-repeat.*` |
| Stress, 30× each case | 90/90 | before final test strengthening |
| Differential `node run_oracle.js` | exit 0, match, 0 differences | `raw/comparison.json`, `raw/{source,native}-m{172,814}-{raw,projection}.json` |
| Drift controls | 24/24 detected | `raw/comparison.json` |
| Corruption negatives | corrupt bundle and corrupt factory both rejected | `raw/comparison.json` |
| Mutations `python run_mutations.py` | exit 0, 19/19 killed, sources SHA-restored | `raw/repair/MUTATION-RESULTS.json`, `mutants/` |
| Mutations, first run | 16/19; three test gaps fixed in tests only | `raw/repair/mutation-run-1-with-survivors.stdout.txt` |
| Base inventory | 64 | `raw/repair/inventory-base.txt` |
| Combined build | 82/82 targets | `raw/repair/combined-build.txt` |
| Combined inventory | 67 | `raw/repair/inventory-combined.txt` |
| Combined run, first attempt | exit 8, 56/67; 11 K10 real-wire tests Not Run because `pwsh` was not on PATH | `raw/repair/combined-67-no-pwsh.*` |
| Combined run, with PowerShell 7.5.3 on PATH | exit 0, 67/67 in 146.26 s, including K06, K08, K10 (real wire, G, H), P08, INT-W3 link and K11 | `raw/repair/combined-67.*` |
| Public paths | `PUBLIC_PATH_GUARD_OK` | |
| Blob seals | 19 staged blobs equal `HEAD:path` after commit | `BLOB-SEALS.json` |

Commands and exits are in `TEST-COMMANDS.txt`. The combined wrapper is `artifacts/server1/K11/K11-A/aggregate/CMakeLists.txt`.

## Remaining risks

- The registry-owned worker detaches when its last reference drops on itself, so a process exiting mid-close can cut short a libtorrent session teardown. Hosts that need orderly shutdown should inject `workExecutor` and join it.
- Without `callbackExecutor`, destroying the registry from a non-app thread abandons pending terminal callbacks by contract. Production hosts should supply `callbackExecutor`.
- `connectSourcePeer` now reports admission to the work lane, not the transport's synchronous verdict. Rejections are counted in `peerDiscovery().connectsRejected`.
- Rechoke uses accepted K05 `SwarmPolicy` (peer-handle salt, no optimistic persistence). Only the single-peer controlled rechoke is differentially proven.
- Nothing in production calls `discoverPeer` yet, because K09 DHT/tracker sources are stubs. The entry point and the drain are proven, and a live discovery runtime remains future work.
- `EngineRegistry.h` (the K11-authored F10 surface) gained additive members and a threading contract. Agent 4 should confirm this is acceptable before B-W3E.

## Exact B-W3E wiring request

This is also in `WIRING-REQUEST.json`. After Agent 4 accepts the candidate, INT-W3 alone should:

1. Fast-forward or merge `d115f801` (plus this handoff commit) onto `feature/colosseum-server-1.0`, preserving the three user-owned deletions in the official checkout.
2. In `native/colosseum_server_v1/CMakeLists.txt`, after the K10 registration (K05 is already registered earlier), add `include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K11.cmake")` and `server1_register_k11_packet()`.
3. In `native/colosseum_server_v1/tests/CMakeLists.txt`, include `tests/cmake/K11.cmake` and call `server1_register_k11_tests()`, which registers `K11-01/02/03-engine-registry` with labels `server1;native;K11`.
4. Rerun the aggregate. The expected inventory is 67, and the K10 real-wire tests need `pwsh` on PATH.
5. Record B-W3E and K11-A integration in `docs/server1/STATE.json` only on acceptance.

K12-A, then B-W3F, H01-A, H01-B and B-W3G follow only after B-W3E is accepted.
