# K10 real-wire readiness flake: root cause (INT-W3, B-W3E assembly, 2026-09-23)

Author: `[Agent (Claude), INT-W3 B-W3E assembler]`. Diagnosis only. No K10 file was changed. K10 is owned by K10-A/K10-G/K10-H, and a repair needs its own packet.

## Summary

The recurring K10 failure `peer readiness failed connected=0 unchoked=0` is a deterministic phase race between libtorrent's uTP-first connect policy and the tests' fixed 5-second readiness deadline. It is not caused by K11, ports, or the controlled peer.

1. libtorrent assumes every peer supports uTP. In libtorrent 2.0.14, `torrent_peer.cpp:183` sets `supports_utp(true) // assume peers support utp`. In `torrent.cpp:7642-7648`, when `enable_outgoing_utp` is on (the default, and the adapter does not change it), the first outgoing connection goes over uTP. The locked binary is 2.0.11. This logic is long-standing, and the observed behaviour below matches it.
2. `controlled_peer.py` listens on TCP only. The uTP SYN goes unanswered, and libtorrent logs `udp_error ... forcibly closed by the remote host` (the ICMP port-unreachable) but cannot attribute it to the connection. The attempt therefore runs to `utp_connect_timeout` (about 3.05 s). The TCP retry comes on a later connect tick, about 1.03 s after that.
3. The first attempt is made on a connect tick about 515 ms or about 1020 ms after the session starts. The TCP attempt therefore lands at about 4.6 s (pass, about 0.4 s of margin) or about 5.1 s (after the 5 s `waitReady` / `readyDeadline`, so it fails with no HANDSHAKE ever logged by the peer).

Load changes which tick the first attempt lands on, which is why failures cluster in time. Examples: repair2 round 03:14-03:18, and this session's baseline runs 3-7.

## Evidence

### Diagnostic method

The diagnostic ran in an isolated copy outside the repository (`<diag-copy>`), built from the candidate sources. `diag-adapter.patch` shows the only changes: alert categories are widened, and each alert is printed with an epoch-ms timestamp. The `enable_outgoing_utp=false` line was applied only for the causal-control run. The copy used `run_sequential.ps1`, `controlled_peer.py`, and the unchanged test source.

### uTP on (production setting): 20 iterations, port 49851, `diag-utp-on/`

Times are relative to `listen_succeeded`.

| First uTP attempt | uTP timeout | TCP attempt | Result | Count |
|---|---|---|---|---|
| 501-526 ms | 3546-3614 ms | 4550-4631 ms | PASS, HANDSHAKE logged | 17 |
| 1019-1031 ms | 4068-4104 ms | none before exit | FAIL, readiness `connected=0`, no HANDSHAKE | 3 |

All 20 first attempts were uTP.

### Causal control, outgoing uTP off: 20 iterations, `diag-utp-off/`

20/20 PASS. The first attempt was TCP at 513-530 ms in every iteration, and no uTP attempt was made. Removing the uTP-first attempt removes the failure and restores about 4.5 s of margin.

### Baseline, 10 × `ctest -L K10` on the B-W3E aggregate build, `baseline-10x/`

| Runs | Result |
|---|---|
| 1, 2, 8, 9, 10 | exit 0, 19/19 |
| 3 | have-wire, live-reuse, K10-03 |
| 4 | K10-H caps: `per-peer admission cap mismatch` |
| 5 | K10-G |
| 6 | have-wire |
| 7 | sequential-wire, have-wire, live-reuse, failure-drain |

Every readiness failure in these runs has the same signature: the peer logs `LISTEN`, no `HANDSHAKE` follows, and readiness fails at 5 s. Runs 3 and 4 overlapped an accidental concurrent diagnostic loop, so treat them as loaded. Runs 5-7 did not overlap it.

### Aggregate repeats, `../aggregate-repeat-*.stdout.txt`

| Repeat | Result | Failing tests |
|---|---|---|
| 1 | 67/67 | none |
| 2 | 64/67 | K10-01 submit-after-ready (`single peer did not become ready before submit`, 5 s deadline), K10-02 sequential-wire, K10-G |
| 3 | 64/67 | K10-03 thread-guard, K10-G, K10-E source-infohash |

Transcripts are in `repeat-failure-transcripts/`.

## Signatures not explained by this mechanism (open, not root-caused)

- **K10-H caps (baseline run 4, loaded).** Peer 0 received only `HAVE piece=0` of five advertisements. The caps wait is 12 s, so the uTP delay alone does not explain it. It occurred once.
- **K10-E transport-source-infohash (aggregate repeat 3).** The test failed at `test_native_transport.cpp:1099`: `expect(transport->poll().empty(), "K10-E real libtorrent metadata repeated")`. That assertion fails on *any* observation 250 ms after readiness, so the label may overstate it. A late peer-lifecycle observation from the libtorrent seeder is plausible but unproven. It occurred once, and no raw directory is kept for this direct-exe case beyond the ctest log.

## Environment hazard (separate, latent)

K10 uses fixed ports 49610-49613 and 49810-49820. They sit in a band where Windows/Hyper-V reserves blocks of 100 ports (`excluded-port-ranges.txt`: currently 49669-49768 and 49869-49968). A reserved port makes `controlled_peer.py` fail to bind with WinError 10013. The runner does not check for `LISTEN`, so this yields the same `connected=0` signature. It was observed only in the diagnostic harness on port 49911, not in the recorded K10 failures, where `LISTEN` is always present.

## Repair options for the K10 owner (not applied)

1. **Test-only.** Raise the readiness deadlines (`waitReady`, the K10-01 `readyDeadline`, and the K10-G active-peer wait) from 5 s to at least 10 s. This keeps production behaviour and accepted contracts unchanged, but hides the roughly 4 s first-connect latency to TCP-only peers.
2. **Production.** Set `enable_outgoing_utp=false` in `LibTorrent2Adapter`. The source's peer-wire swarm dials over TCP, so this is closer to source parity, and it removes about 4 s of first-connect latency to TCP-only peers. It changes accepted K10 transport behaviour, so it needs Agent 4 acceptance as a K10 repair.
3. **Test harness hardening, either way.** The runners should fail fast when `LISTEN` never appears, which covers the port-exclusion hazard.

Option 2 has product consequences (connect latency and source parity), so it is flagged for Agent 4 rather than chosen here.
