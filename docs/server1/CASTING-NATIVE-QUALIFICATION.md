# Native casting qualification

## Pinned source

Oracle: Stremio service v4.21.1 server bundle, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

The executable differential extracts modules through `tools/server_lab/oracle/module_loader.cjs` and rejects a different bundle hash. JavaScript is used only in the test oracle, never the native server runtime.

- M234: ffmpeg stderr stream descriptions and its unusual fractional HMS conversion (fraction is divided by 100, regardless of digit count).
- M942: transcode/convert argument selection, stream maps, H.264/stereo-AAC copying, fallback codecs, offsets, fragmented MP4 and Matroska, DLNA response headers and HEAD behavior.
- M945/M951/M960: first-observation device collection, passive mDNS record normalization and SSDP description parsing.
- M482: player parameter/body precedence, command selection and initial status.
- M996/M1006: Chromecast and DLNA state normalization.

## Implemented runtime surface

`CastingService` and `installCastingRoutes` register discovery listing, individual device lookup, both transcoding aliases and the player endpoint. Device inventory is populated through explicit passive ingestion. No discovery sockets are automatically opened.

Transcoding runs an owned native FFmpeg process through `ProcessPort`. Response streams apply output-pipe backpressure at 128 KiB and resume below 64 KiB; at most one 64 KiB pipe read can cross the high watermark. Response destruction cancels the owned process and releases buffered output. Probe, subtitle retrieval and subtitle alignment share response lifetime. Subtitles use the existing native retrieval/decoding/rendering implementation, an owned temporary directory, and FFmpeg alignment before burning. As in M942, subtitle preprocessing failure falls back to video without subtitles.

The native player endpoint parses query/form/JSON input and selects M482 commands. `setPlayerInvoker` is an explicit integration boundary for a platform/device implementation. Without one, existing-device control returns 501 rather than silently succeeding. Native `DlnaSoapClient` supports asynchronous SOAP actions against a configured HTTP(S) control endpoint; it preserves normal TLS validation, rejects redirects, limits responses and applies a timeout. It does not discover endpoints or launch applications. Cast protobuf framing is native, bounded and incrementally decoded, but is not connected to a production TLS/session transport.

## Registered tests

`tests/cmake/Casting.cmake` registers:

- `CAST-fixtures`: committed source-derived cases, passive discovery, Cast framing/size checks, real loopback SOAP, and async player-route integration with an injected invoker.
- `CAST-source-differential`: regenerate expectations directly from the pinned oracle, then run the native test executable.
- `CAST-real-media` (Unix with FFmpeg): generate lawful synthetic video/audio; transcode to fragmented MP4 and decode it; pause reading until the bounded producer watermark is reached, verify stable buffering, then cancel and verify zero processes/buffered bytes; retrieve loopback subtitle text, align/burn and decode; verify HEAD starts no output producer.

The HTTP fixture binds only to numeric loopback. No real casting device or external player application is accessed by these tests.

## Explicit residuals and divergences

This is **not full casting parity**. Remaining integration/qualification includes:

- Active mDNS/SSDP browse, discovery TTL/lifecycle and platform network interfaces.
- Full Chromecast TLS connection, receiver launch, session heartbeat, request correlation and real-device interoperability. Certificate validation has not been weakened.
- Production DLNA device-description service resolution, event subscriptions and complete delayed play/seek/subtitle/audio-track orchestration. The SOAP primitive and source command/state functions are available, but the default production invoker is intentionally not configured.
- External application discovery/launch and platform-specific player clients.
- Windows process backpressure and subtitle-filter filename escaping, GPU paths and physical-device testing.
- Source permits additional FFmpeg input protocols; this route accepts only `file`, `http` and `https` explicitly. No firewall, listener-address or security setting changes are made.
- Source's redundant remote metadata HEAD request is omitted: its length is calculated but never used in the emitted response or process command.
- Native probe capture uses the process layer's bounded diagnostic buffer and a 120-second deadline; malformed/very large diagnostics may differ from the source's unlimited capture.

No production merge, release or real LAN qualification is implied by passing these tests.
