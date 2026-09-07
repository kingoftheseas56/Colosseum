# P04-A packet report

Worker: P04-A. Base: `2f196378a41e209d30598f72a7e388123015f2ea`.

## Cases

- P04-01: PASS. Byte, status, header, timeout, crash, and missing-evidence toy subjects produce nonzero CLI exits with `FAIL`, `ERROR`, or `INDETERMINATE` evidence and preserved raw protocol lanes.
- P04-02: PASS. `FAIL`, `ERROR`, `UNSUPPORTED`, `INDETERMINATE`, and `NOT_RUN` remain distinct and cannot serialize as `PASS`.
- P04-03: PASS. Concurrent run IDs use OS-assigned loopback listeners, isolated cache/event roots, structured token/process/creation leases with controller identity, process-group cleanup, startup cleanup evidence, dead-stale lease handling, and unrelated-PID mismatch protection.

## Exact verification

Command: `python -m unittest tools.server_lab.tests.test_runner tools.server_lab.tests.test_reference_identity -v`

Exit: `0`.

Raw output: `TEST-RUN.txt` in this packet. Result: 15 tests, 8 environment-gated skips, 0 failures, exit 0.

The P04 runner RED correction was first observed before implementation; the final run is the GREEN evidence above.

## Produced interfaces

- `LabRunner` in `tools/server_lab/lab.py`.
- `EvidenceSchema` in `tools/server_lab/evidence.py`.
- Run receipt schema in `tools/server_lab/schemas/run.schema.json`.

Raw protocol evidence stores exact Latin-1 text and byte hex, status, and headers before normalized JSON comparison. Receipts are validated against the complete local schema before writing.

## Changed files

Owned lab files: `lab.py`, `adapters/base.py`, `evidence.py`, `scenarios.py`, `tests/test_runner.py`, and `schemas/run.schema.json`.

Packet evidence: `P04-REPORT.md`, `TEST-RUN.txt`, and `WIRING-REQUEST.json`.

P02 adapters were consumed only and not edited. No unowned wiring was needed.

## Open risks

The surrounding P02 identity tests remain environment-gated when the supplied Stremio oracle and WSL are absent. This packet does not claim P02 runtime qualification.

Push: false. Commit required; no push.
