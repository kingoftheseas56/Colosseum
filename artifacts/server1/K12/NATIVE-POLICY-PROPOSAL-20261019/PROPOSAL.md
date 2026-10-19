# Read-only pinned-native policy proposal

No vendor source, dependency, binary, lock, production settings or build configuration was changed for this proposal. It is a scope decision, not an implemented patch or a new parity claim.

## Executed prerequisite: two same-ID wires really survive

Run `node prove_engine_alias_wires.js EXACT_ORACLE_PATH` from this directory. The script verifies SHA256 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f, captures the bundled webpack loader instead of starting the application, and loads actual M814, M818 and M822. Only the unused native uTP module is stubbed; exact M814 hardcodes utp:false. Filesystem storage, metadata parsing, bundled stream protocol, socket piping and handshakes are real. The bundled entrypoint is suppressed to avoid unrelated application side effects.

A metadata-ready engine opens two real TCP connections using distinct raw aliases with the same numeric port (plain decimal and leading zero). The controlled server replies to both with the identical 20-byte peer ID and correct info hash. One second after both complete handshakes, engine.swarm retains two wires and two connections, both unfinished, with M814 request handlers installed. alias-wire-trace.jsonl records the actual observation. M814's wire event listener at oracle line72707 installs exchange/bitfield behavior, and metadata-ready onwire setup attaches request handling, without later peer-ID deduplication. This is stronger than admission counters or a mocked Wire.

## Why a one-line bypass or proxy is insufficient

Pinned libtorrent2.0.11 src/peer_list.cpp1149-1226 merges add_peer by canonical endpoint; torrent::connect_to_peer at torrent.cpp7443 requires a distinct torrent_peer with connection==nullptr. bt_peer_connection.cpp3537-3555 then independently disconnects a duplicate remote peer ID, before peer_plugin::on_handshake at3577. peer_list::update_peer_port at906-975 can merge/disconnect peers when an extension updates its advertised listening endpoint. Incoming association also performs duplicate endpoint checks in peer_list::new_connection around630-825.

A loopback forwarding proxy only changes the first endpoint key and does not defeat the later same-ID check. Rewriting peer IDs would hide protocol evidence and is not proposed. Merely returning false from peer_plugin::can_disconnect also does not work: these unconditional disconnect paths do not consult that hook.

## Smallest coherent duplicate-policy fork to prototype, if authorized

1. Add an explicit, default-off source-owned admission mode scoped to the controlled adapter/torrent, not a blanket session-wide weakening for arbitrary peers. Thread it through torrent::add_peer and peer_list::add_peer with a force-distinct-instance path. Allocate a separate torrent_peer per admitted raw identity even at the same endpoint, preserving sorted peer-list bookkeeping and one connection per instance. Source generation/attempt identity stays in the adapter; mapping native peer-info pointer to source handle avoids endpoint-based ambiguity. Do not turn tracker/discovery observations into automatic duplicate connections.
2. Gate the bt_peer_connection same-peer-ID duplicate close only for explicitly source-owned instances. Keep normal default behavior, info-hash validation, invalid message checks, IP filtering and source self-connection behavior independently reviewed. Incoming source-owned association and advertised-port updates must preserve instance ownership rather than merging entries. The minimal change is therefore not just add_peer plus handshake: peer_list::new_connection/update_peer_port also require tests/audit.
3. Replace adapter single endpoint owner lookup for this mode with instance-aware identity; preserve diagnostic actual endpoint and projected raw peerAddress separately. Detach, requests, byte meters and retries must target exact connection identity/attempt, never an endpoint neighbor.

Likely affected native headers/implementation: torrent.hpp/torrent.cpp, peer_list.hpp/peer_list.cpp, a default-off source-owned state/flag carried to the connection, and bt_peer_connection.cpp. Exact final API and line count require a prototype; no claim that the existing private ABI can remain compatible. Rebuild the pinned library and all consumers with matching assertions/ABI, add a distinct fork revision/hash, and keep ordinary unmodified mode covered.

## Pre-association absolute handshake timer hook

M818 onwire creates an absolute handshake setTimeout at socket acceptance, before the info hash identifies the swarm. The existing native torrent extension hook is too late. session_impl::incoming_connection around3174-3203 constructs and starts bt_peer_connection before any torrent association. The smallest reusable hook is a default-empty session-level callback at that creation boundary, receiving a weak peer_connection_handle and creation timestamp on the native I/O lane. It lets the adapter schedule a steady timer, disconnect through the existing handle method if still handshaking, and cancel on completion/close without retaining the session. Source ownership/teardown generation is mandatory. An alternative native absolute timer policy is possible, but a one-second second_tick comparison is not the same timing boundary as the source timer.

Important additional finding: current adapter does not override native handshake_timeout. Pinned settings_pack.cpp274 defaults to10s, and peer_connection.cpp4939/4991-5003 measures time since last receive, not since accept. This can preempt the source-owned outgoing25s timer. Carrying source configured timeout to the adapter and making native inactivity timeout no earlier than it is a separable, non-vendor correction; it still does not establish pre-association absolute timeout under trickled input. Do not globally disable the native timeout without replacing incoming ownership first.

## Security and performance consequences

Allowing multiple source-admitted aliases removes native duplicate suppression as a resource guard. Raw aliases can multiply sockets, peer entries, buffers and handshakes to one host. Keep source connection capacity, timeout and attempt ownership authoritative; retain generation teardown, source dedup for identical raw strings, native allocation failures and bounded socket admission. Do not silently introduce an extra endpoint cap and call it source parity. The absolute accept timer reduces slow-handshake retention but adds one timer/weak handle per incoming socket; callbacks must remain native-lane-owned and be canceled during shutdown. The hook must not expose broad arbitrary raw-pointer ownership or cross-thread access.

## Required real regression before acceptance

- Same endpoint through two raw aliases, identical remote peer ID, both valid handshakes and independent payload requests/transfers concurrently; compare exact pinned M814/M818/M822 trace, not just peer count.
- Close/retry one alias while the other continues; stale timeout, failure, detach and old generation cannot close or bill the survivor. Exact identical raw duplicate still suppressed.
- Both peers advertise the same extension listening port; neither merges or disconnects in source-owned mode. Default native mode still performs its existing duplicate policy.
- Incoming silence and trickled partial handshake: source configurable deadline expires measured from acceptance even when bytes arrive. Handshake just before deadline survives; after deadline cannot associate. Wrong info hash still closes. Engine/session close cancels all callbacks.
- Real outgoing handshake after10s but before source25s stays alive; source25s timeout still wins independent of activity. Include configurable shorter/longer timeout cases.
- Existing source cap/pause/generation/request ownership/upload caps/metadata and 150-case integration suite; matching-ABI Release and ASan teardown runs. Assertion-enabled native library and consumer must be built together, not mixed with the existing Release-only artifact.

Approval would need to cover a reviewed, pinned libtorrent2.0.11 fork and rebuilt dependency artifacts only, not an upstream version upgrade, production merge or release. Until then these are documented qualification gaps and a read-only plan.
