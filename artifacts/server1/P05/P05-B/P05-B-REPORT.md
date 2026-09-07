# P05-B packet receipt

Worker: `P05-B`; branch: `codex/server1-p05-b`; base: `44991721`.

P05-01 consumes all five mutation fixtures from `tools/server_lab/cases/P05-A.json`. The comparator rejects callback removal, duplicate terminal event, priority reorder, null replacing an omitted key, and wrong cancellation generation with the fixture-declared identity, path, and rule. Baseline comparison is accepted without normalization.

P05-02 executes the qualified Node runtime `v22.16.0` and preserves four distinct ordering classes: immediate, `process.nextTick`, Promise continuation, and timer.

The trace contract freezes the oracle SHA-256, module IDs and authority hashes, injected inputs, observation level, exact event fields, raw-versus-normalized separation, omission-versus-null semantics, cardinality, terminal uniqueness, generation, order/priority, and callback identity.

Verification evidence:

- RED: `TEST-RED.raw.txt` — comparator/probe symbols absent; 7 tests errored.
- GREEN: `TEST-GREEN.raw.txt` — 8/8 comparator, contract, and qualified-Node tests passed.
- P05-A regression: `P05-A-REGRESSION.raw.txt` — 5/5 passed under Node `v22.16.0`.
- Python syntax, JSON validation, and `git diff --check` recorded in `CASE-RECEIPT.json` / command log.

State boundary: authored, tested, and runtime-verified; not integrated, committed, or pushed at receipt creation. No production/shared/native/CMake/state/CI wiring is requested.
