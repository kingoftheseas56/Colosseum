# Linux laboratory runner portability checkpoint

Basis: `da8cd96dffb7906d2def3c1db7cc724a72914fd3` on
`feature/colosseum-server-1.0`. This repairs test infrastructure, not an application
route or a completed Stremio replacement. Native C++ sources/binaries are unchanged.

## Repairs and adversarial coverage

- Windows `Popen._handle` is used only on Windows. Linux records process creation
  through /proc and retains the known launched Python image identity.
- Linux start-time parsing handles spaces/parentheses in comm. Identity includes
  boot and PID namespace identities, avoiding accidental cross-boot/namespace
  equivalence. Executable identity is read between matching creation observations.
- Zombies count as terminated. A verified non-group-leader is signaled individually;
  it is not assumed to own a process group. Non-positive PIDs are rejected.
- Executable matching remains case-insensitive on Windows and is case-sensitive
  on Linux. A changed Linux audit creation identity prevents termination.
- Test-side process observations are independently platform-aware. Existing
  cleanup, unrelated-process preservation and raw-byte/status assertions remain.
- Historical Windows receipt paths are compared using PureWindowsPath when the
  tests execute on Linux; those historical receipts are not relabeled Linux proof.
- Tracker receipt inspection now waits for the actual response-recording event.
  A controlled held-recording test proves no premature success. The exact
  ready/request/response assertion remains unchanged after that bounded wait.

Identity checks and signaling remain separate OS operations. This narrows known
races but does not claim PID-reuse-proof signaling or pidfd-based ownership.

## Actual results

Focused runner/identity tests: 20/20 passed before the additional audit-creation
case; all six Linux identity cases subsequently passed. Fixture tests: 11/11 passed.

Final whole Python lab invocation: **73 run, 66 passed, 1 failed, 2 errors,
4 skipped**. No assertion was relaxed to hide the remaining gates:

- FAIL: reference heartbeat/settings test. The exact, unmodified JS oracle under
  official Node22.16.0 exits with `ERR_SYSTEM_ERROR`, syscall
  `uv_interface_addresses`, errno1. An isolated Node networkInterfaces probe
  reproduces the same failure. Teardown confirms exit1, alive_after=false.
  NO_NETWORK_INTERFACES does not suppress the oracle's listen-time ip.address call.
  No injected shim, oracle patch, namespace or security-setting change was used.
- ERROR (2): historical authority tests require PARALLEL-WORK-ITEMS.json with SHA256
  `777319421c8c8d50348492ace51d7c51eb8c6b14e8e225d5d1e8962bdb1c3dc7`.
  Exact committed files, supplied archives, and permitted Library/Drive handoffs
  were checked; that input remains unavailable and was not reconstructed.
- SKIPPED (4): WSL-specific qualification cases. This Linux environment is not WSL.

The reference fingerprint, missing-companion accounting and port-exhaustion
negative all executed and passed. A successful port-exhaustion check does not
qualify reference startup, streaming or media playback.

Original failures retained: wrong-cwd invocation, Windows _handle error,
Windows-only tasklist test helper, proc parsing/zombie/group/image/audit negatives,
tracker record race, missing inputs, and unmodified oracle startup failure.
Seventeen absent committed historical fixture/evidence files were recovered with
exact Git-blob checks. They are existing inputs, not newly fabricated evidence.

## Runtime and commands

Python3.12. Official Linux Node22.16.0 archive:
https://nodejs.org/dist/v22.16.0/node-v22.16.0-linux-x64.tar.xz
Archive SHA256 (matches official SHASUMS256.txt):
`f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e`.
Executable SHA256:
`8142d37c6f2f372ef040419e7a111a6baf17df89f8078d02447d6c639ae20c1d`.
This is a newly verified Linux executable, not the historical Windows executable
or the missing intended stremio-runtime companion.

From repository root:

```sh
P05_QUALIFIED_NODE=/path/to/node22 \
STREMIO_RUNTIME=/path/to/node22 \
STREMIO_SERVER_JS=/path/to/exact/server.js \
python -m unittest discover -s tools/server_lab/tests -p 'test_*.py' -v
```

K12 counter differential also passed all14 scenarios under Node22.16.0 against
the unchanged native counter executable. Earlier Node24 results remain separate.

See evidence/dots-2026-10-18/lab-linux-tests.txt for decisive logs. Full native
server route composition, integrated streaming and Windows qualification remain
pending. The 87 registered native/oracle checks from the preceding checkpoint
are separate from this Python suite; neither is an overall full-parity pass.
