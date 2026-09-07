# P06-A packet report

Base: `ce9961ea0666bea2d2c3563117ddcceb516f7a08`; branch: `codex/server1-p06-a`.

P06-01 provides canonical bencode, deterministic single-file and multi-file payload corpora, SHA-1 infohash and piece geometry validation, and a loopback-only scripted peer with handshake, bitfield, choke/unchoke, delay, corruption, disconnect, and block delivery actions. The ledger records peer, piece, begin, length, order/sequence, monotonic timestamp, authorized status, and loopback scope.

Source grounding: oracle M303 L27748-27805 SHA `eb8b00...`; M843 L74473-74529 SHA `520011...`; M851 L74856-74884 SHA `2d42fa...`. The planning-pack oracle SHA is `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Verification: packet-local tests passed, 4 tests, exit 0; raw output is `P06-01-TEST-OUTPUT-20260907.txt`. The full P04+P02 regression ran 20 tests with 8 skips, 1 failure, and 1 error, exit 1; raw output is `P04-P02-REGRESSION-20260907.txt`. The failure and teardown error are diagnosed in the P04 runner path: Windows tree termination races the redirected `stderr.txt` handle (`WinError 32`), while the same interleaved run observes missing `run-a\ownership.json`. P04 shared runner/test files are outside P06-A ownership and were not changed. `git diff --check` passed. Parent P06 acceptance and Stremio/0.1 consumption remain NOT_RUN pending P06-C/later environment.

State: authored and tested; compiled not applicable; runtime-verified loopback fixture self-test; regression failure preserved and diagnosed; committed pending final scope audit; pushed no.
