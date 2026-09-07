# P05-A receipt

Worker: `P05-A`; parent: `P05`; base: `ce9961ea0666bea2d2c3563117ddcceb516f7a08`.

The loader extracts webpack module spans without rewriting source, hashes raw bytes, verifies `MODULE-INDEX.json`, and executes modules inside a VM context with injected deterministic `Date` and `Math.random`. The authenticated oracle hash was verified as `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`; extraction found 1312 modules. Assigned module hashes and authority ranges are recorded in `tools/server_lab/cases/P05-A.json`.

Evidence states: authored PASS; tested PASS; executed PASS; differential source hash PASS; P05-01 comparator DEFERRED_TO_P05-B by scope. TRACE-CONTRACT is not implemented here.

Command: `node --test artifacts/server1/P05/P05-A/test_p05_a.mjs` — exit `0`, 4/4 tests passed.
