# P06-A packet report

Base: `ce9961ea0666bea2d2c3563117ddcceb516f7a08`; branch: `codex/server1-p06-a`.

P06-01 provides canonical bencode, deterministic single-file and multi-file payload corpora, SHA-1 infohash and piece geometry validation, and a loopback-only scripted peer with handshake, bitfield, choke/unchoke, delay, corruption, disconnect, and block delivery actions. The ledger records peer, piece, begin, length, order/sequence, monotonic timestamp, authorized status, and loopback scope.

Source grounding: oracle M303 L27748-27805 SHA `eb8b00...`; M843 L74473-74529 SHA `520011...`; M851 L74856-74884 SHA `2d42fa...`. The planning-pack oracle SHA is `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Verification: packet-local tests passed, 3 tests, exit 0. P04 predecessor regression was run and ended with 20 tests, 8 skips, 1 failure, 1 error; the failure was an existing Windows interleaved-run lease/handle issue (`run-a\ownership.json` absent and `WinError 32` on `stderr.txt`), outside P06-A paths. `git diff --check` passed. Parent P06 acceptance and Stremio/0.1 consumption remain NOT_RUN pending P06-C/later environment.

State: authored and tested; compiled not applicable; runtime-verified loopback fixture self-test; committed pending final scope audit; pushed no.
