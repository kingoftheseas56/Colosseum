# K11-A round-2 repair handoff to Agent 4 (2026-09-23)

Producer: `[Agent (Claude), K11-A repair producer]`. Reviewer and integration authority: Agent 4 (Codex).

## Result: COMPLETE

The committed-only partial-restore defect is repaired. The candidate is committed, pushed, reproducible, and ready for Agent 4's independent review. This is not B-W3E acceptance. No B-W3E, K12, H01, aggregate CMake, STATE, K06, master, or official-feature change was made.

## Git state

| Item | Value |
|---|---|
| Worktree | `C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired` |
| Branch | `sol/server1-w3-engine-v3-20260915` |
| Accepted feature base | `b28e2dfe8562a5f942d569ab981d97a0530113bf` |
| Reviewed input HEAD | `0194b7ffa131ea51eab86616e83189741c09e891` |
| Repaired candidate | `173f9a98c9951f67a4faaf2b2aba9c7e76126bab`, pushed; `origin/sol/server1-w3-engine-v3-20260915` equals it |
| This handoff | one documentation-only commit on top of `173f9a98` |
| History | additive commits only; no rewrite, no force-push |
| Official feature | `C:\b\Colosseum-Server-1.0` still at `b28e2dfe`, with the three deleted build documents (`docs/build/linux.md`, `macos.md`, `windows.md`) still deleted, unchanged |
| Dirty state | none in the K11 worktree before this handoff |

## Finding

`PersistentBackend::restored()` returns committed virtual pieces individually. K06 restore can keep one virtual component of a real verification piece and invalidate another (backing file truncated). `install()` then advertised the real piece on the first restored component, which violates P08-T5/K10-H.

## RED reproduction

The regression is `regressionPartialRestore` (`R6`, also run inside K11-02), and it uses the real persistent store:

- 524289-byte file, one 1 MiB real piece split into virtual pieces 0 (524288 bytes) and 1 (1 byte);
- fully committed, removed, backing file truncated to 524288 bytes, engine recreated.

Against production code at `0194b7ff` (`raw/repair2/red-R6.*`):

```
R6 -> exit 1: K11-PARTIAL advertised a real piece whose virtual component was not restored
```

The same regression exposed two more defects on the way to green:

- **After only the advertise/upload guard** (`raw/repair2/red-R6-after-advertise-guard.*`), R6 exits 1 with "recommitted group did not become readable and advertised exactly once". K06 `verify` requires every group member to be staged, while restore leaves the surviving component committed but unstaged, so the group can never complete.
- **After restaging, before the group-completion selection** (`raw/repair2/red-R6-corrupt-survivor-before-group-selection.txt`, transcribed from the console), R6's corrupt-survivor cycle exits 1 with `requests=2 tail=0 have=0`. The failed group reset, but a reader whose selection starts inside the group never requests the rest of it.

## Code changes (`0194b7ff..173f9a98`)

`native/colosseum_server_v1/src/policy/TorrentEngine.cpp` (+125/−14):

- **`install()`** inserts the whole restored set into the committed mirror before any `notifyCommitted` advertisement decision.
- **`realPieceGroup` / `realPieceCommitted`** compute a real piece's virtual range and require every component to be committed.
- **`notifyCommitted`** advertises a real piece only when `realPieceCommitted` holds.
- **`acceptUpload`** aborts unless the requested real piece is complete. `StoreBackend::uploadRead(piece, groupStart, groupEnd)` re-checks `isCommitted` for the whole group on the work lane.
- **`PersistentBackend::restageRestored`**: when a staged component leaves only committed, unstaged survivors missing, it stages their durable bytes and re-verifies. The whole real piece is re-hashed and re-committed as one K06 group; K06 production code is unchanged.
- **`stageResult` failure path**: K06 resets a failed group, including restaged survivors. K11 drops the group from the committed mirror and re-demands all of it.
- **`ensureGroupSelection` / `releaseGroupSelection`** add one engine-owned scheduler selection over a real piece when a missing member lies outside every selection, and release it when the real piece commits. Committed-only visibility makes a reader depend on its whole real piece, which the source's precommit reads never needed.

`native/colosseum_server_v1/tests/test_engine_registry.cpp` (+170): R6 `regressionPartialRestore`, wired into K11-02 and the `R6` entry point.

## What R6 proves

1. After partial restore there is no HAVE for real piece 0, while virtual piece 0 stays individually readable (524288 exact bytes).
2. An upload request for real piece 0 is aborted with no bytes served.
3. The re-download requests only the missing component (`[0, 524288, 1]`). After it is verified and committed, there is exactly one HAVE, the tail reader receives `[200]`, and uploads serve exact bytes: 16384×65 at offset 0 and `[200]` at 524288.
4. A corrupt surviving component fails the restaged group hash. The group is reset, virtual piece 0 is refetched, and exactly one HAVE follows. The repaired upload serves the correct bytes, never the corrupt survivor.

## Verification

| Check | Result | Raw |
|---|---|---|
| Focused `ctest -L K11 --repeat until-fail:3` | exit 0, 9/9 | `raw/repair2/focused-repeat.stdout.txt` |
| R6 direct | 3/3 | |
| R1–R5 direct | all exit 0 | |
| K06 persistent store (`ctest -R K06-` in combined build) | exit 0, 4/4 | `raw/repair2/k06-restore.stdout.txt` |
| K11 verbose in combined build | every sub-check PASS, including K11-PARTIAL | `raw/repair2/combined-k11-verbose.txt` |
| Oracle `node run_oracle.js` | exit 0, match, 0 differences | `raw/repair2/oracle.*` |
| Drift controls | 24/24 detected | `raw/comparison.json` |
| Corruption negatives | bundle and factory rejected | `raw/comparison.json` |
| Mutations `K11_MUTATION_OUT=repair2 python run_mutations.py` | exit 0, 24/24 killed on the first run, sources SHA-restored | `raw/repair2/MUTATION-RESULTS.json`, `raw/repair2/mutants/` |
| Aggregate build | exit 0 | `raw/repair2/combined-build.txt` |
| Inventories | base 64, combined 67 | `raw/repair2/inventory-*.txt` |
| Combined run 1 | exit 8, 65/67; K10 real-wire flake (below) | `raw/repair2/combined-67-run1-k10-flake.*` |
| Combined run 2 | exit 0, 67/67 in 157.50 s | `raw/repair2/combined-67.*` |
| Public paths | `PUBLIC_PATH_GUARD_OK` | |
| Blob seals | 20 seals (round 2): staged blob OID and SHA-256 all equal `HEAD:path` at `173f9a98` | `BLOB-SEALS.json` |

**Oracle.** Regenerating the raw evidence produced files byte-identical to the committed ones. Observed trace behavior did not change, so the oracle evidence and normalization rules were not modified.

**Mutations.** The five new mutants are listed below, with their killing checks:

| Mutant | Killed by |
|---|---|
| `advertise-without-group-completeness` | R6, K11-02 |
| `upload-without-group-completeness` | R6 |
| `restored-survivor-not-restaged` | R6 |
| `group-completion-selection-disabled` | R6 |
| `failed-group-stays-visible` | R6 |

`staged-bytes-uploadable` was re-anchored to remove the new real-piece guard as well as both work-lane guards.

**K10 flake in combined run 1.** K10-02 sequential-wire and K10-G failed at controlled-peer readiness (`connected=0 unchoked=0`, peer listening). An isolated rerun failed both again. Three further repeats passed, except one `have-wire` failure (`raw/repair2/combined-k10-flake-repeats.txt`). K10 sources and scripts are unchanged, K10 test binaries do not link K11, and v3 recorded the same K10-G readiness timeout. Run 2 was fully green.

**Toolchain.** MSVC 14.44 via vcvars64, Qt 6.11.1 msvc2022_64, CMake 3.30 and Ninja from `C:/Qt/Tools`, node, Python 3.11. `pwsh` is not on this shell's PATH, so PowerShell 7.5.3 from the local vcpkg tools copy was prepended for the K10 real-wire tests. Exact commands are in `TEST-COMMANDS.txt` (Round 2 section).

## Updated evidence

- `SOURCE-TRACE.md`: new "Real-piece completeness" section
- `MUTATIONS.md`: round 2
- `TEST-COMMANDS.txt`: round 2
- `PACKET-RECEIPT.json`: `round2`
- `SELF-REVIEW.md`
- `BLOB-SEALS.json`: round 2
- `run_mutations.py`: 24 mutants, `K11_MUTATION_OUT` output selection
- `raw/repair2/`: all round-2 raw outputs

Round-1 evidence under `raw/repair/` and `raw/v3-rejected/` is untouched. `WIRING-REQUEST.json` is unchanged, and its B-W3E request stands for the new candidate SHA.

## Remaining risks

- `restageRestored` reads the surviving component from disk and trusts K06 `read`. A corrupt survivor is caught by the group hash, but its bytes stay individually readable until the group is re-verified, because K06 restore does not re-hash.
- The engine-owned group-completion selection counts in `selectionCount()`. It exists only while a real piece is incomplete and a member lies outside every reader selection.
- The K10 real-wire peer-readiness flake is environmental and outside K11. Agent 4 may see it on a combined rerun.
- The round-1 risks still stand: the registry-owned worker detaches at shutdown, and without a `callbackExecutor`, destruction from a foreign thread abandons terminal callbacks.
