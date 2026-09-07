# P06-B packet report

Worker: P06-B. Branch: `codex/server1-p06-b`. Repair base: `23339fed5c0356eeb3cee372dac44c8e4a766d46`. No push performed.

## Scope and source identity

The owned implementation is limited to the two packet-local fixtures, their packet self-test, and the case definition:

| Input | Exact source range | SHA256 |
|---|---:|---|
| `tools/server_lab/fixtures/tracker_server.py` | lines 1-183 | `9A54ECA68CCAD7D1865F31A38BA01A5385DA4C034A712DBD096F4CA05BA1C565` |
| `tools/server_lab/fixtures/http_server.py` | lines 1-148 | `6FB096B478C165142FC9E21F32373B5C2B6BA93913166FC9B6DA81B7A5EF7A91` |
| `artifacts/server1/P06/P06-B/self_test.py` | lines 1-196 | `DD6E1EF5F2BA37C9A9B0049A53940CFB93281FE48670B68344EFEBDD650655C6` |
| `tools/server_lab/cases/P06-B.json` | lines 1-29 | `A541493CAA347A97769CB401BB999092DD32715FDC51479167F6A14833980709` |

The changed-file list is: `tools/server_lab/fixtures/tracker_server.py`, `tools/server_lab/fixtures/http_server.py`, `artifacts/server1/P06/P06-B/self_test.py`, `artifacts/server1/P06/P06-B/SELF-TEST.json`, `artifacts/server1/P06/P06-B/RED.raw.txt`, `artifacts/server1/P06/P06-B/GREEN.raw.txt`, `artifacts/server1/P06/P06-B/P06-B-REPORT.md`, `artifacts/server1/P06/P06-B/WIRING-REQUEST.json`, and `tools/server_lab/cases/P06-B.json`. The pre-existing uncommitted `.superpowers/sdd/PARALLEL-EXECUTION-PLAN/P06-B-brief.md` was preserved and is not owned by this packet.

## Repaired behavior

- Sequence allocation now occurs inside the tracker and HTTP ledger locks; request numbering and cancellation snapshots are synchronized.
- Duplicate compact peers are asserted by exact compact bencoded bytes, not by absence of textual `127`.
- HTTP `delayed_metadata_ms` is exercised and its delay event is asserted.
- A barrier-forced concurrent ledger test proves unique ordered sequences for both fixture classes; a concurrent HTTP request run checks the live path as well.
- Shutdown joins are checked with `thread.is_alive()`. A remaining server thread records `shutdown_timeout` and raises instead of being reported as clean. Self-test probes that no listener remains after each stop.

## Preserved RED evidence

Command: `python artifacts/server1/P06/P06-B/self_test.py`

Exit: `1`.

Raw failure: `artifacts/server1/P06/P06-B/RED.raw.txt`. The failure is the intended pre-fix concurrent ledger assertion (`AssertionError` at `self_test.py` line 72), proving the test caught the unlocked sequence allocation. The original import RED evidence remains in `RED.txt` and the raw file was not replaced with a green result.

## Replay and verification evidence

Concrete replay template, from repository root:

```text
python artifacts/server1/P06/P06-B/self_test.py
```

The fixture configurations are constructed in `self_test.py` and the executed tracker/origin configurations are recorded in `SELF-TEST.json`; this avoids hidden defaults or an untracked config file. `SELF-TEST.json` records packet checks, exact replay command, and distinct authored/tested/executed/integrated states.

Recorded command results:

| Command | Exit | Result |
|---|---:|---|
| `python -m py_compile tools/server_lab/fixtures/tracker_server.py tools/server_lab/fixtures/http_server.py artifacts/server1/P06/P06-B/self_test.py` | 0 | PASS |
| `python artifacts/server1/P06/P06-B/self_test.py` | 0 | PASS |
| `python -m unittest -v tools.server_lab.tests.test_runner tools.server_lab.tests.test_reference_identity` | not reached | P04 predecessor run stopped after exact failures/timeouts recorded below |
| `python -m unittest -v tools.server_lab.tests.test_reference_identity` | 0 | PASS; 9 run, 8 skipped, 1 source-authority test passed |
| `git diff --check` | 0 | PASS |

The complete green raw output is in `GREEN.raw.txt`; the packet replay produced no stdout/stderr and exited 0. Parent P06 acceptance: `NOT_RUN`. Cross-engine consumption: `NOT_RUN`. Integration: `NOT_RUN`. No production wiring was requested; see `WIRING-REQUEST.json`.

The combined predecessor command began with P04 `test_broken_subjects_have_nonzero_exit_for_all_p04_01_failures` and reported `(mode='timeout') ... FAIL`; it then reported `test_cleanup_failure_cannot_pass ... FAIL`, `test_interleaved_runs_isolate_cache_ports_events_and_cleanup_orphans ... FAIL`, and `test_raw_protocol_divergence_cannot_be_normalized_into_pass ... ERROR` before the session was intentionally stopped. No P04 pass is claimed. The separate P02 command completed with exit 0 and 8 explicit environment skips.

## State boundary

Authored: PASS. Compiled: PASS via `py_compile`. Tested: PASS via the packet self-test and predecessor regressions. Executed: PASS for the concrete replay command. Runtime-verified: packet-local loopback fixture scope only. Integrated: NOT_RUN. Committed: pending this repair commit. Pushed: NOT_RUN by instruction.
