# P06-C packet report

Worker: `P06-C` (GPT-5.6 Luna); parent: `P06`; branch: `codex/server1-p06-c`.

## Result

P06-03 is `PASS`. The deterministic P06-A/P06-B corpus is converged and self-tested across payload, discovery, corruption, choke, delay, disconnect, cancellation, and unauthorized-delivery profiles. The unauthorized case is rejected on the wire: the attempted delivery is marked `authorized: false` and emits no piece bytes. Ledger presence alone is not treated as rejection.

## Source authority

Required base consumed: `0166565e4e9cc860d70c09ad6476d611d9d4299c`.

Planning-pack oracle: `C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js`, 6,676,503 bytes, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

| Authority | Range | SHA-256 |
|---|---:|---|
| M303 | lines 27748-27805; bytes 1590216-1594283 | `eb8b00c36b67354e28185cc83098831121a3510c31f9d01700d2991f353d6026` |
| M843 | lines 74473-74529; bytes 4227467-4230650 | `520011da2b4a68a3c71f967416cda41badb047ade587672ef031b1edce9efa30` |
| M851 | lines 74856-74884; bytes 4249770-4251508 | `2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041` |

The source hash command and exact outputs are preserved in `SOURCE-HASHES.raw.txt`.

## Changed files and commit

Owned source changed: `tools/server_lab/tests/test_fixtures.py`.

Packet case: `tools/server_lab/cases/P06-C.json`.

Packet evidence and receipt files are listed in `PACKET-RECEIPT.json`. No P06-A/P06-B fixture implementation, shared/native/CMake/state/CI file, or unrelated file was edited. The implementation commit SHA is recorded in `PACKET-RECEIPT.json` after commit; push status is `NOT PUSHED`.

## Case and fixture evidence

The literal deterministic seed is `P06-C-literal-corpus-v1`. Single-file infohash is `938822d490b34f7fd0e2bfdd67cf55440725e9ae`; multi-file infohash is `162dae3eff2d775bae50d0c8b410d3048af998ff`. The packet self-test preserves both identities, payload goldens, peer scripts, cancellation order, and unauthorized-delivery order in `SELF-TEST.json`.

P06-03 request order is `[handshake, interested, request(0,0,1), deliver(0,0,16384, unauthorized), disconnect]`. The client receives EOF and no piece frame; the peer ledger records `authorized: false`. This is the required RED/GREEN rejection behavior.

## Commands and exits

| Command | Exit | Evidence |
|---|---:|---|
| `python -m unittest -v tools.server_lab.tests.test_fixtures` | 0 | `P06-C-GREEN.raw.txt`; 10 tests passed |
| initial RED profile-contract test | 1 | `P06-C-RED.raw.txt` |
| `python -m unittest -v artifacts/server1/P06/P06-A/test_p06_a.py` | 0 | `P06-A-REGRESSION.raw.txt`; 8 tests passed |
| `python artifacts/server1/P06/P06-B/self_test.py` | 0 | `P06-B-REGRESSION.raw.txt` |
| `python -m py_compile ...` | 0 | `PYCOMPILE.raw.txt` |
| `python -m unittest -v tools.server_lab.tests.test_reference_identity` | 0 | `REFERENCE-ADAPTER.raw.txt`; 1 pass, 8 qualified-environment skips |
| source oracle/module hash verification | 0 | `SOURCE-HASHES.raw.txt` |
| candidate source/case hash inventory | 0 | `CANDIDATE-HASHES.raw.txt` |
| `git diff --check` | 0 | `DIFF-CHECK.raw.txt` |

## Adapter qualification

Existing Stremio and Server 0.1 adapters were inspected and probed only through their existing APIs. Stremio oracle identity passed and WSL was present; the reference tests skipped runtime qualification because required environment variables/inputs were not supplied. The `colosseum01` toolchain probe was complete. Neither existing adapter exposes controlled P06 corpus injection/consumption, so both controlled-corpus results are honestly `UNSUPPORTED`, not `PASS` or fabricated parity.

## Interfaces, risks, and integration

Implementation commit: `c236d58385b07d026243a11c1d6fdb92b88ba0e3` (`test(server1): converge P06-C fixture corpus`). The follow-up receipt-only commit records this implementation SHA. `WIRING-REQUEST.json` requests no shared-file wiring. Open risks are limited to differential consumption remaining unsupported through the existing adapters and parent/integration acceptance remaining pending at `B-W1C`; the packet does not claim Stremio/0.1 parity or integrated state.

Push: `NO`.
