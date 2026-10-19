# K12 raw peer identity and actual DNS admission

Date: 2026-10-19. Exact source oracle SHA256: 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

## Implemented and executed

- M818 add gate uses JavaScript Number of the second colon-separated component, rather than rejecting nonliteral IPv4 addresses. Raw text is retained as source peer identity and projected wire address; actual resolved endpoint remains separate diagnostic data.
- Source model owns actual admission and monotonically increasing attempt tokens. Resolver completion, native dialing, failure and timeout cancellation are generation/attempt guarded. Timeout before native socket identity cancels pending admission. A stale timeout cannot close a replacement.
- Native asynchronous resolver runs on the libtorrent I/O lane. Resolving 127.1 to the real controlled 127.0.0.1 peer is executed, not simulated. Engine close cancels resolver ownership before session destruction.
- Already source-admitted work continues during a later pause, matching net.connect having already started. New direct adapter actions still obey pause. Both paths have real-wire regression coverage.
- Source per-wire speed meters begin at admission, including DNS/handshake elapsed time. Controlled clock fixture exercises the delayed first payload.
- Canonical endpoint collision never steals another live native peer's identity. Unsupported second alias reports actual rejection, not successful fabricated counters.

## Evidence

Release scratch build uses pinned libtorrent 2.0.11, Qt 6.11.1, Boost 1.83. 27/27 focused tests passed, covering 22 registry cases, source model, incoming handshake, two real statistics wire cases, and pause/generation. Separate statistics projection/live-meter test passed. Twenty resolver/close cycles passed. Exact pinned extracted source differential passed 36 swarm lifecycle transitions and 23 raw address gate cases. Logs accompany this receipt.

## Explicit qualification gaps

This is not a declaration of full torrent transport parity. Source permits simultaneous distinct raw address strings resolving to one endpoint; native libtorrent owns one connection per canonical endpoint. The second live alias is explicitly rejected, so pure source identity is exact but concurrent live alias behavior is not. Source accepts fractional ports at its add gate and Node net.connect can then throw synchronously; native rejects invalid integral port work normally rather than reproducing process-level exception behavior. Hostname result ordering, family preference and resolver cancellation latency are not claimed byte-for-byte equivalent to the pinned Node runtime.

Incoming pre-association absolute handshake timing remains qualified as in INCOMING-20261019. No native vendor patches or version upgrades were made. All changes are in the recreation/adapter and tests.

## ABI profile

The imported Linux libtorrent package offers only RELEASE but enables TORRENT_USE_ASSERTS for Debug consumers, changing internal class layout. A Debug consumer links but is not an ABI-valid test configuration. Release is the qualified profile; main integration owns a fail-fast configuration guard. Windows locked-kit handling is unchanged. AddressSanitizer results are recorded separately when complete; LeakSanitizer is unavailable in this sandbox (ptrace restriction), so no leak-clean claim is made.

## AddressSanitizer result

Matching Release ABI + AddressSanitizer build passed the same 27/27 tests, separate live statistics/meter test, and 20 real resolver/close cycles (ASAN_OPTIONS=detect_leaks=0). No address-sanitizer diagnostic was emitted. Initial pre-build CTest invocation could not locate the still-compiling registry executable; the final recorded invocation ran all 27 after build completion. Earlier Debug/Release mismatched-profile payload failures are not counted as production test results.

The exact extracted M818 add/drain/shift methods executed with the installed Node net module throw ERR_SOCKET_BAD_PORT for ports 1.5 and 0.5 after tries increments, leaving the peer retained and no connection. check_fractional_port.js and trace preserve this known mismatch. This uses installed Node's net implementation, not an assertion that every historical Node runtime behaves identically.

## Alias boundary design constraint

Pinned libtorrent source src/bt_peer_connection.cpp:3537-3555 additionally calls torrent::find_peer(pid) and disconnects a duplicate remote peer ID before extension on_handshake at line 3577. Merely forwarding each alias through a distinct loopback endpoint does not establish source parity: both proxy endpoints still present the same remote peer ID. A valid solution needs isolated native ownership or source-owned wire handling, not only a cosmetic endpoint rewrite. No workaround or vendor modification was applied.

## Integration regression correction

Full main integration exposed K10-02's old synchronous submission-only endpoint mapping assertion. It expected submitting two peer identities to a closed port to immediately replace the mapping, without waiting for real native connection work. That is incompatible with source-owned asynchronous dial lifecycle and would permit live identity stealing. Reproduced red in scratch. Replaced only the test with an actual loopback TCP listener: accept peer 90, close its socket, wait for native endpoint cleanup, then accept peer 91 on the same endpoint and assert real replacement ownership. The revised K10-02 passes (1.12s). Production adapter behavior unchanged. Logs and refreshed test hash accompany this correction; the original ASan 27-case run predates this test-only correction and did not include K10-02.
