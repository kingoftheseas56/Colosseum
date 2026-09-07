# Colosseum Server 1.0 decisions

## D001: Independent alternate branch

This implementation lives on `feature/colosseum-server-1.0-preflight`, created directly from immutable Colosseum `master` commit `1548f6998e9dd852cda25a4c8950b55194a3ab7c` on 2026-09-07. The pre-existing `feature/colosseum-server-1.0` branch remains untouched and is not an ancestor, implementation source, or fallback.

## D002: Source-led parity boundary

The active behavioral oracle is the exact Stremio Service CDN v4.21.1 bundle, 6,676,503 bytes, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`, whose embedded `stremio-server` package version is 4.21.0. Exact bytes outrank labels, older plans, Server 0.1 behavior, Harbor, and convenience behavior from native libraries.

## D003: Server 0.1 is comparison-only

The archived Server 0.1 specimen is pinned to `a3fcaa96ec2650014e1dd94f603d76b2b1e48387`. It is non-blocking evidence only. No production code may be copied from it merely because it already uses libtorrent internals; salvage requires controlled A/B evidence plus causal diagnosis later.

## D004: Native substrate stays frozen

Server 1.0 remains C++17 and libtorrent 2.0. No libtorrent 2.1, WebTorrent, sync modernization, second torrent engine, or endpoint-only imitation enters this baseline. Existing Colosseum `colosseum_libtorrent` remains the dependency authority unless P01A proves it cannot provide a reproducible substrate.

## D005: Production implementation is gated

P00 closes evidence only. P01A must close the reproducible native substrate and P08A must demonstrate selected-peer exact-block control plus ordinary-picker suppression on actual wire traffic before the production Server 1.0 skeleton or bulk policy port begins.
