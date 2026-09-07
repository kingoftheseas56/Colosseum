# P04-A packet report

Worker: P04-A. Base: `51d990bf866c2b603235da61bcc84a81079b1903`; repair commit follows.

Plan: `C:/b/Colosseum-Server-1.0-workers/P04-A/.superpowers/sdd/PARALLEL-EXECUTION-PLAN/P04-A-brief.md`, SHA256 `CA350B16412B78E44D407DADBDC8D63A12D3656D039413A4720F56EC300719AD`, lines 1-32.
SPEC: `C:/Users/Suprabha/Downloads/Colosseum-Server-1.0-Execution-Plan-Pack-v2.1/Colosseum-Server-1.0-Execution-Plan-v2.1/evidence-pack/SPEC.md`, SHA256 `63171D227C639D6D9E23B5F1AFAD45D66140595BE7950B32FD73C24207FD84FB`, section 8 at line 110.

## Cases

- P04-01: PASS. Byte, status, header, timeout, crash, and missing-evidence toy subjects produce nonzero CLI exits with `FAIL`, `ERROR`, or `INDETERMINATE` evidence and preserved raw protocol lanes.
- P04-02: PASS. `FAIL`, `ERROR`, `UNSUPPORTED`, `INDETERMINATE`, and `NOT_RUN` remain distinct and cannot serialize as `PASS`.
- P04-03: PASS. Concurrent run IDs use OS-assigned loopback listeners, isolated cache/event roots, structured token/process/creation leases with controller identity, process-group cleanup, startup cleanup evidence, dead-stale lease handling, and unrelated-PID mismatch protection.

Progression: P04-01 RED -> GREEN; P04-02 RED -> GREEN; P04-03 RED -> GREEN. Mutation evidence is in `MUTATION-EVIDENCE.json`; inputs are in `CASE-INPUTS.json`; sample machine-readable evidence is in `SAMPLE-RUN.json`.

## Exact verification

Command: `python -m unittest tools.server_lab.tests.test_runner -v`

Exit: `0`.

Raw output: `TEST-RUN.txt` in this packet. Result: 19 tests, 8 environment-gated skips, 0 failures, exit 0. The pre-repair RED run had 1 failure and 1 error from the newly added contracts.

The P04 runner RED correction was first observed before implementation; the final run is the GREEN evidence above.

## Produced interfaces

- `LabRunner` in `tools/server_lab/lab.py`.
- `EvidenceSchema` in `tools/server_lab/evidence.py`.
- Run receipt schema in `tools/server_lab/schemas/run.schema.json`.

Raw protocol evidence stores exact Latin-1 text and byte hex, status, and headers before normalized JSON comparison. Receipts are validated against the complete local schema before writing.

## Changed files and interfaces

Owned lab files: `lab.py`, `adapters/base.py`, `evidence.py`, `scenarios.py`, `tests/test_runner.py`, and `schemas/run.schema.json`.

`LabRunner.run` now writes events after final result calculation and records a full replayable lab CLI command. `EvidenceSchema` emits nullable torrent/infohash/selected_file and response/byte/peer/resource arrays.

Packet evidence: `P04-REPORT.md`, `TEST-RUN.txt`, `CASE-INPUTS.json`, `SAMPLE-RUN.json`, `MUTATION-EVIDENCE.json`, and `WIRING-REQUEST.json`.

P02 adapters were consumed only and not edited. No unowned wiring was needed.

## Open risks

The surrounding P02 identity tests remain environment-gated when the supplied Stremio oracle and WSL are absent. This packet does not claim P02 runtime qualification.

No integration claim: no merge, push, or root wiring was performed. Cleanup behavior remains process/platform dependent outside the deterministic toy fixtures.

Push: false. Commit required; no push.

State: authored in the owned P04 source and packet paths; compiled: not applicable (Python); tested: 19 tests, 8 skips, exit 0; executed: runner and replay fixture exercised; runtime-verified: toy-subprocess scope only; integrated: no.
