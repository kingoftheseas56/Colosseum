# P06-A packet report

Base: `ce9961ea0666bea2d2c3563117ddcceb516f7a08`; branch: `codex/server1-p06-a`.

P06-01 provides canonical bencode, deterministic single-file and multi-file payload corpora, SHA-1 infohash and piece geometry validation, and a loopback-only scripted peer with handshake, bitfield, choke/unchoke, delay, corruption, disconnect, and block delivery actions. The ledger records peer, piece, begin, length, order/sequence, monotonic timestamp, authorized status, and loopback scope.

Source grounding: the planning-pack oracle SHA-256 is `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`; M851 L74856-74884 SHA-256 is `2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041`. The prior M303/M843 entries carried prefixes only; no full hashes are present in the checked-in P06-A evidence, so they are not repeated as authority claims here.

Changed files: `tools/server_lab/fixtures/torrents.py`, `tools/server_lab/fixtures/peer_server.py`, `artifacts/server1/P06/P06-A/test_p06_a.py`, `artifacts/server1/P06/P06-A/P06-01-TEST-OUTPUT-20260907.txt`, and this report. The pre-existing untracked `.superpowers/sdd/PARALLEL-EXECUTION-PLAN/P06-A-brief.md` is preserved and excluded from the commit.

Verification: `python -m unittest discover -s artifacts/server1/P06/P06-A -p test_*.py -v` passed 8 tests, exit 0; raw output is `P06-01-TEST-OUTPUT-20260907.txt`. `python -m py_compile tools/server_lab/fixtures/torrents.py tools/server_lab/fixtures/peer_server.py artifacts/server1/P06/P06-A/test_p06_a.py` passed, exit 0. `git diff --check` passed, exit 0. The full P04+P02 regression ran 20 tests with 8 skips, 1 failure, and 1 error, exit 1; raw output is `P04-P02-REGRESSION-20260907.txt`. The failure and teardown error are diagnosed in the P04 runner path: Windows tree termination races the redirected `stderr.txt` handle (`WinError 32`), while the same interleaved run observes missing `run-a\ownership.json`. P04 shared runner/test files are outside P06-A ownership and were not changed. Parent P06 acceptance and Stremio/0.1 consumption remain NOT_RUN pending P06-C/later environment.

State: authored and tested; compiled not applicable; runtime-verified loopback fixture self-test; regression failure preserved and diagnosed; committed; pushed no.
