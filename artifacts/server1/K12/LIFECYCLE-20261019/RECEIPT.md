# K12 source-owned connection lifecycle checkpoint

Pinned oracle SHA256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Implemented M818 queue/capacity/retained endpoints/attempt admissions, single-step drain, pause/resume, 3-second connect timeout, configured handshake timeout (default25 seconds), 4/8/12-second post-handshake reconnection waits and failed-peer removal/rediscovery. M814 forwards only handshakeTimeout, not connectTimeout or speed. Source timers run against engine monotonic time, driven by polling; production composition polls every5ms. Timeouts submit generation- and connection-identity-bound native disconnects. Source-owned counters now describe these operations, not unrelated libtorrent counters. PeerSearch consumes actual queued count, excluding active dials. Swarm-cap speed uses source speedometer.

The libtorrent2.0.11 adapter disables its autonomous connection-speed loop. Admitted dials call the already version-pinned internal torrent add_peer/connect_to_peer APIs on the native network lane. The existing synchronous cleanup barrier is preserved before replacement admission. No vendor source changes or dependency upgrades. Native lifecycle callbacks report actual connection creation, TCP connect, handshake, and disconnect, including pre-handshake failures. Failed native admission reports a real rejected connection rather than leaving source counters stuck. Native counters remain separate diagnostics. Native callback identities prevent a stale close or timeout from affecting replacements.

Validation:
-25/25 isolated regression tests pass:22 registry tests, two real-wire statistics tests, pause/generation.
-Real-wire tests now assert Started/Connected/Ready ordering for two peers, exactly-once disconnect, stale identity timeout no-op, and actual native close from the current timeout action.
-Live engine statistics test asserts capped queue and real admission counts, retained retry suppression, failed-peer rediscovery,3-second timeout boundary, generation isolation and identity binding.
-Pinned M818 extraction differential compares25 transitions:capacity, one-step drain, pause/resume, retries, failure removal, rediscovery, teardown. Pure tests additionally check connect/handshake deadline cancellation, incoming unique exclusion and delayed admission clock.
-Existing discovery test was corrected from an invalid assumption that active connections remained queued. New capacity-one fixture exercises actual queued backlog and wire-event hysteresis.

Qualification boundaries, not a whole-server parity claim:
-Unknown incoming endpoints still lack a native plugin association at this checkpoint. The pure source model already distinguishes incoming connections from retained outbound unique endpoints, but native incoming acceptance needs integration and real-wire tests.
-Discovery endpoint parser still validates literal IPv4/bracketed IPv6 and ports1..65535, whereas source PWS uses raw colon split and JavaScript numeric-port semantics, including rejecting65535. DNS/raw-address quirks remain open.
-Statistics projection remains the previously pinned M172 mapping with13 differential cases, including uploadSpeed=downloadSpeed, indexed wires:null, progress>1 and zero-length null progress. Network timing need not be identical to Node event scheduling, and unavailable behavior is not zero-filled to claim parity.
