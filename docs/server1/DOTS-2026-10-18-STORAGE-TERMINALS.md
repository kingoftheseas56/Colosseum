# Storage failure terminalization checkpoint

Baseline: `849ffca59c3ebd0767838f54940e3df0c1ced8fb`, branch
`feature/colosseum-server-1.0`. The 124 native/server-document files were checked
against their Git blob identities before editing. Original Stremio/specification
archives were independently recovered and matched EVIDENCE-LOCK.json.

## Production repairs

- Convert stage/read/upload storage exceptions into existing app-lane terminal
  paths, preserving read tokens and upload ownership. Worker catch-all containment
  no longer silently loses these operation completions.
- On nonretryable stage failure, terminate affected readers and retire internal
  demand, group selections, requests and piece buffers. Fence older unsuccessful
  stage results so they cannot recreate canceled demand. Valid late commits remain
  usable. Fresh reader demand can retry after the underlying storage recovers.
- Publish committed state and release staged bytes only after successful bitmap
  persistence. Roll back tentative in-memory bits on persistence failure.
- Atomically replace the bitmap using QSaveFile with direct-write fallback disabled;
  do not truncate the last-good bitmap on an unsuccessful replacement.
- Report storage failure as `store-error`, not `verify-success`, in trace output.

## Executed regression evidence

Linux, GCC 14.2.0, Qt 6.11.1, CMake 4.4.3, C++17 Release. Production policy/storage
components are linked to the actual static libtorrent adapter. The new engine
fault cases use the established deterministic fake transport to isolate disk and
callback behavior; these cases are not wire/playback tests.

Six new cases:

| Case | Before | After |
|---|---|---|
| K06-F3 bitmap persistence failure/commit retry | committed state incorrectly published | PASS |
| R12 stage exception | reader remains pending | PASS |
| R13 cached read exception | reader remains pending | PASS |
| R14 upload read exception | matching abort missing | PASS |
| R15 multi-virtual-piece fatal cleanup | internal selection remains live | PASS |
| R16 queued old failure and fresh-reader recovery | demand resurrected; subsequent attempted repair retained flushed buffer | PASS |

R12 initially had a fixture setup error (missing parent directory), retained
separately from the corrected failing-before run. Both R16 failing stages are
retained. No existing assertions were removed.

Commands after rebuilding:

```sh
ctest --test-dir build-server1-linux -R 'K06-|K11-|R[0-9]+-' \
  --output-on-failure --timeout 30
ctest --test-dir build-server1-linux -R 'K06-F3|R1[2-6]-' \
  --repeat until-fail:20 --output-on-failure --timeout 30
```

Results: **18/18 focused invocations PASS**, then **120/120 repeated new-case
invocations PASS**. These totals count test invocations, not independent defects.

Tested executable SHA256:
- engine registry: `6e85a665018c9f021ff938aa13eea0cb693072ec1dcb1f2da6c71405bab2612f`
- persistent store: `396290df3426836031e985d436be43fa0598acacb039ec2193c9f8a592b03288`

## Other findings and limits

An earlier surrounding Linux run executed 65 cases: 61 passed, four failed;
twelve PowerShell-driver cases were NOT RUN. Failures: M00 discovery crash,
M00 graceful-stop fixture/lifecycle behavior, two native metadata-seeder startup
cases. These are retained open findings, not erased by the focused green run.
That surrounding run predates the final two adverse-state regressions and does
not qualify the complete final suite. Broader repairs and requalification continue.

The Linux build adaptation is separate from storage behavior. Windows, full
server composition, actual streaming/player operation and complete differential
parity remain unqualified. No production merge, release or CI dispatch occurred.
