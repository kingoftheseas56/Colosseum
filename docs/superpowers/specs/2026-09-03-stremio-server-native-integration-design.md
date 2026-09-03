# Native Stremio Server Integration Design

**Date:** 2026-09-03
**Target:** Arc 44 Colosseum native server integration
**Authority:** Stremio Server 4.20.17 oracle, SHA-256 `567A397BB11B788571BF1750FD05DD78927F97BEC0C9DDEAA6D9CC1ECCEE3922`, Wave-0 source-port matrix, and the Arc 44 takeover handoff.

## Goal

Replace the external `stremio-runtime server.js` playback dependency with an embedded native C++ server that preserves the observable behavior required by the frozen Stremio 4.20.17 server surface, including real torrent bytes, HTTP Range/HEAD semantics, cancellation, lifecycle, and application cutover.

## Non-negotiable behavior

The native server must preserve these contracts:

1. `EngineFS → torrent scheduler → FileStream → HTTP Range response` remains the critical playback path.
2. W06 remains the sole authority for selection, reservation, critical pieces, hotswap, choke pressure, and request pressure.
3. A response with a fixed `Content-Length` may deliver data incrementally, but its body bytes are raw media bytes and never HTTP chunk frames.
4. GET, HEAD, invalid-range fallback, open-ended ranges, `external`, `download`, `subtitles`, `enginefs-prio`, DLNA headers, and stream lifecycle match module 172.
5. `/proxy` preserves the frozen module 805 behavior, including the plain-HTTP HTTPS-agent quirk, unless an explicit product deviation is recorded.
6. W08 process work cannot block unrelated requests on the W01 server worker.
7. Remote archive paths retain bounded ranged reads and client cancellation; whole-volume materialization is not accepted as final parity.
8. The default HTTP listener binds loopback and retries ports 11470 through 11474. The HTTPS listener is functional on 12470 when enabled.
9. Shutdown cancels request work, closes streams, retires engines, stops listeners, and leaves no external server process.

## Architecture

`ColosseumServer` owns the shared W01 HTTP router and loopback listener. A new runtime composition owner constructs the W02 settings/cache services, W03 EngineFS control plane, W04/W05 torrent metadata/storage adapters, W06 scheduler and FileStream objects, W07 torrent HTTP surface, W08 media services, W09 network/app services, and W10 archive services. The composition owner mounts route adapters into the shared router and owns all service lifetimes.

W06 emits scheduler-owned block decisions. A concrete libtorrent transport executes peer-specific requests through a libtorrent-owned-thread command seam, using `peer_connection::add_request()`/cancellation where the installed headers and runtime prove the operation safe. The transport reports verified block bytes back to W06. Completed pieces flow into the storage/verification path and notify FileStream. A response bridge turns W07's read plan into incremental `HttpResponse` writes while propagating disconnect cancellation and stream-close accounting.

The application creates this embedded runtime during `StreamServer` startup, exposes the existing QML `Stream` contract, and no longer launches `stremio-runtime` or `server.js`. Existing non-server `TorrentEngine` consumers remain intact unless a narrowly scoped adapter is required to provide the server backend.

## Component ownership

### W01 HTTP and listener

Own fixed-length progressive framing, chunked framing, request cancellation, response completion, loopback HTTP, and the shared router. The wire encoder decides chunk framing from transfer mode, not merely from whether `write()` was called.

### W03–W05 control and storage

Provide concrete production implementations of `IEngineFsBackendFactory`, `IEngineFsBackend`, torrent metadata readiness, virtual-piece mapping, normal/circular storage, verification, and cache paths. Unit fakes remain test-only.

### W06 scheduler and transport

Keep `SchedulerSpine` authoritative. Add the real libtorrent peer/block transport and explicit cancellation completion semantics. Hotswap, peer retirement, late callbacks, and shutdown must not leave stale outstanding requests.

### W07 HTTP surface

Keep route behavior in `TorrentHttpSurface`, but add the adapter that maps `TorrentHttpRequest`/`TorrentHttpReply` to W01 `HttpRequest`/`HttpResponse`. The adapter owns progressive FileStream creation, fixed response lengths, byte-range boundaries, and lease teardown.

### W08–W10 route families

Complete route mounting for media, network/app, proxy, YouTube, casting, local addon, certificate, archive, FTP, and NZB services. Blocking subprocess and archive operations execute outside the W01 worker and marshal results back safely.

### Application cutover

Add the native server sources and required Qt/libtorrent/OpenSSL links to the real application target. Replace the external child-process path in `native/player/streamserver.cpp` while preserving QML signals, URL shape, readiness, errors, prefetch, and stats behavior.

## Verification contract

Every phase must have a focused test and a user-visible or wire-level check where execution is available. The final evidence set must include:

- exact oracle hash verification;
- W01 fixed-length multi-write wire test with raw byte count and hash;
- real libtorrent/torrent fixture producing bytes through W06, W07, and W01;
- GET, HEAD, bounded, open-ended, invalid, and multi-request Range behavior;
- disconnect cancellation proving transport and FileStream teardown;
- peer request, hotswap, failure, late callback, and shutdown tests;
- asynchronous media concurrency test;
- proxy, circular storage, archive-range, and HTTPS listener parity tests;
- application build and runtime proof that no `stremio-runtime` child process is launched;
- W37/W38 lifecycle, W39 route matrix, and W40 playback regression evidence;
- `scripts/validate-directives.ps1` when present.

Passing packet/unit tests without this end-to-end evidence cannot upgrade the implementation to integrated or complete.

## Explicit non-goals

- Replacing the frozen Stremio oracle with a “cleaner” protocol or different defaults.
- Moving scheduler authority into libtorrent defaults or the existing application stream heuristics.
- Buffering an entire torrent range merely to avoid implementing progressive delivery.
- Merging or pushing branches as part of this work without explicit direction.
