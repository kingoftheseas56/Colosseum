# K10 predecessor repair for B-W3E: source trace

Producer: `[Agent (Claude), K10 repair producer]`, working on Agent 4's REQUEST-CHANGES of 2026-09-23 against the B-W3E candidate `29543f94`.

Frozen source: `stremio-service-v4.21.1-server-bundle/server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`. This is the accepted K10 oracle.

## 1. The source dials peers over TCP only

- **M814, lines 72453-72457** (torrent-stream engine). The swarm is constructed as `pws(infoHash, opts.id, {size: ..., handshakeTimeout: ..., utp: !1})`. uTP is hard-coded off; no caller option can enable it.
- **M818, line 72836** (peer-wire-swarm). `this.utp = options.utp || !1`.
- **M818, line 72885.** The swarm opens a uTP server only when `swarm.utp` is set. Its TCP server is always created.
- **M818, lines 72932-72934** (`Swarm.prototype._drain`). `this.utp && !peer.noUtp ? utp.connect(...) : net.connect(parts[1], parts[0])`. With `utp` false, every dial is `net.connect`, which is TCP.

libtorrent 2.0 does the opposite by default:

- `torrent_peer.cpp:183`: `supports_utp(true) // assume peers support utp`.
- `torrent.cpp:7642-7648`: dial uTP first when `enable_outgoing_utp` is set, which is the default.

The adapter never overrode this. A TCP-only peer is therefore reached only after the uTP connect timeout (about 3.05 s) plus the next connect tick (about 1.03 s).

**Repair.** `LibTorrent2Adapter` sets `enable_outgoing_utp=false`, so the transport dials TCP only, like M818 under M814.

**Not changed.** Incoming uTP stays enabled, although the source opens no uTP server (72885). No K10 test exercises inbound uTP, so that divergence is disclosed rather than repaired here.

## 2. The source requests nothing until a selection exists

- **M814** starts with `engine.selection = []` and requests pieces only for selected ranges.
- The accepted K10 trace says: "All pieces remain `dont_download`."
- **Before this repair.** `suppressAutonomy()` applies `dont_download` only when `torrent_file()` exists at add time. Infohash and magnet sources gain metadata later, so their pieces took libtorrent's default priority 4. Nothing lowered it, and libtorrent's picker could request autonomously. The write firewall blocked the wire write but reported `FailureObservation{"unowned request reached write firewall"}`. That was the K10-E `metadata repeated` failure: 4/100 on the base build, and 2/100 with the diagnostic build, which printed the late observations.
- **Repair.** `sourceParams()` adds `lt::torrent_flags::default_dont_download`, available in the locked 2.0.11 header `torrent_flags.hpp:294`. Pieces are `dont_download` the moment metadata arrives, and the behaviour no longer depends on timing.

## 3. Controlled-peer startup and uTP observation (test harness)

`controlled_peer.py` changes:

- It binds a UDP socket on the same port as its TCP listener and logs every datagram as `UDP_DATAGRAM`. A uTP dial is now observable on the wire.
- If the requested fixed port cannot be bound (Windows reserves 100-port blocks at run time in K10's fixed-port band), it logs `PORT_UNAVAILABLE port=... error=<tcp|udp> ...` and falls back to an OS-chosen port. This was added after stability round 1 hit `WinError 10013` on port 49813, which is outside every listed TCP reservation.
- It logs `BIND_FAILED` and exits 2 only when no port pair can be bound after 20 attempts.

`controlled_peer_startup.ps1` provides four helpers, used by the nine K10-A real-wire runners and K10-G:

- `Start-ControlledPeer` captures the peer's stderr.
- `Wait-ControlledPeers` fails fast when a peer exits or never logs `LISTEN`, and names the cause.
- `Get-ControlledPeerPort` reads the port actually bound, and the runners pass it to the candidate.
- `Assert-NoUtpDial` fails on any `UDP_DATAGRAM` (the P08 `tiny_peer.py` runners, `run_wire.ps1` and `run_lifecycle.ps1`, get the startup check only and keep fixed ports; P08 files are unchanged).

The 5 s readiness deadlines are unchanged. With TCP-first dialing, readiness lands in about 0.5 s, so the deadline keeps about 4.5 s of margin and still catches real connect latency.

## K10-H caps signature (one occurrence before repair)

The failing run `K10-H/raw/upload-20260923-132205130` wrote peer 0's only adapter byte (`HAVE piece=0`) at 13:22:47.67. The candidate started at about 13:22:29.3, and the failure was recorded at the same instant. Peer 0's connection was therefore established about 18 s late. Admission logic did not misbehave, because the HAVE replay began with piece 0 as designed.

This is consistent with the uTP-first dial plus heavy concurrent load: that run overlapped three accidental diagnostic loops. It was not reproduced, so it is not proven.

Supporting measurement: K10-H took 47.5-65.4 s in 15 base executions and 20.8-33.1 s in the 15 final-build executions, because every upload peer is a TCP-only listener. It passed 15/15 after the repair, but one pre-repair occurrence in about 20 executions is too rare for that to be proof.

## Test hook

`server1::transport::nativeWantedPieceCount(const TorrentTransport&)` is added in the same style as the existing test hooks, such as `autonomousNativeMutationCount`. It counts native pieces whose priority is not `dont_download`, and returns `SIZE_MAX` when the handle, metadata, or priorities cannot be read, so the K10-E check cannot pass vacuously. No port or frozen header changes. `TorrentTransport.h` blob `11ebfd13` is unchanged.
