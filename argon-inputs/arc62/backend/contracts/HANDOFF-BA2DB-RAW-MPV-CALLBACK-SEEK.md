# Arc 62 Vinyl - BA2-D-B Raw mpv Callback + Seek Completion Handoff

> Continuation status (2026-10-06): BA2-D-B1, BA2-D-B2, and BA2-D-B3 are complete and verified. Wave 2 is sealed locally. The original broad BA2-D-B handoff below is retained as historical pre-B1 context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md`.

Date: 2026-10-06
From: BA2-D-A Tokenized Player Bridge Core
State: BA2-D-A complete and verified; BA2-D-B not started
Production Colosseum: read-only through BA2-D-A

## Entry point

Continue Arc 62 Vinyl backend with **BA2-D-B Raw mpv Callback + Seek Completion Adoption**.

Read completely:
- `BA2D-MPV-ADOPTION-CONTRACT.md`;
- `BA2B-ENGINE-ADAPTER-CONTRACT.md`;
- `BA2C-QUEUE-SESSION-COORDINATION-CONTRACT.md`;
- `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md`;
- `ba2d-mpv-adoption-prototype/`.

Re-inspect current production player seams read-only:
- `native/player/mpvitem.h`;
- `native/player/mpvitem.cpp`;
- installed/current MpvQt controller/event API as needed.

BA2-A/B/C and BA2-D-A are frozen upstream unless BA2-D-B proves a concrete incompatibility.

## Verified BA2-D-A result

BA2-D-A now provides and verifies:
- tokenized plain load;
- tokenized header-bearing load;
- deterministic play -> paused=false;
- deterministic pause -> paused=true;
- full PlaybackSeekRequest command forwarding;
- exact-token stop;
- tokenized loaded/pause/position/duration/EOF/failure callback routing;
- stale old-token rejection after replacement;
- stale synchronous callback during stop;
- no queue/provider/persistence/history/QML ownership leakage.

Verification:
- exact Qt 6.11.1 MSVC kit confirmed;
- current bridge/test C++ sources compiled into the inspected local artifacts;
- direct harness execution by Preflight: exit 0;
- CTest executed independently by Preflight: 1/1 PASS;
- source leakage scans: zero relevant hits.

## Current concrete gap

Production `MpvItem` signals are not tagged with Vinyl work identity.

Current raw observations include:
- fileLoaded;
- pauseChanged;
- positionChanged;
- durationChanged;
- endFile(reason);
- playbackError(code, message);
- boolean coreSeeking / coreSeekingChanged.

A lower bridge that simply reads "current token" when these signals fire is invalid.

Late work from load A after load B begins must still be attributed to A so BA2-A rejects it as stale.

### New raw-mpv evidence

BA2-D1 inspected the installed MpvQt/libmpv surface and ran a local negative-control probe.

Confirmed:

- installed libmpv `MPV_EVENT_START_FILE` carries `mpv_event_start_file.playlist_entry_id`;
- `mpv_event_end_file` carries the matching `playlist_entry_id` and the raw mpv error code when applicable;
- installed libmpv exposes `MPV_EVENT_SEEK` and `MPV_EVENT_PLAYBACK_RESTART`, with PLAYBACK_RESTART documented as the event used to detect seek completion;
- current MpvQt/MpvItem public signals discard those raw load identities.

The rapid-load probe issued two successful `loadfile ... replace` commands back-to-back. Only `START_FILE entry=2` was observed; the first issued load was replaced before START_FILE.

Therefore **FIFO association of issued Vinyl load tokens to later START_FILE events is falsified and forbidden**.

A follow-up probe intended to test immediate synchronous retrieval of the new playlist-entry ID timed out during its build setup before execution. No verdict about that property is claimed.

The first bounded BA2-D-B task is to settle that concrete command-to-playlist-entry association using the smallest probe path. Do not repeat the failed broad setup blindly and do not fall back to FIFO.

## Seek gap

`MpvItem::seekExact(seconds)` sends an exact seek command.

The current seam observes:
- position changes;
- boolean mpv `seeking`.

It does not expose a completion callback carrying the originating `PlaybackSeekRequest`.

BA2-B requires exact completion identity:
- PlaybackWorkToken;
- per-generation seek sequence;
- target seconds.

Do not treat target proximity alone as exact completion.

Do not treat a generic `seeking=false` alone as exact completion when multiple seeks can overlap/supersede.

## BA2-D-B objective

Determine the smallest honest mechanism that lets the existing mpv runtime satisfy the tokenized BA2-D-A port.

At minimum resolve:

1. exact historical load-token attribution for fileLoaded;
2. token ownership for pause/position/duration observations;
3. token ownership for EOF/failure;
4. replacement-load stale callback handling;
5. exact seek-request completion identity;
6. duplicate request-header conversion across BA2-B vector -> current QVariantMap seam;
7. whether a small production MpvItem extension is required.

## Decision discipline

Evidence decides the mechanism.

Possible valid outcomes:
- current MpvQt exposes lower-level event/command userdata that can be adopted;
- a small native wrapper can serialize/tag operations honestly;
- MpvItem requires a narrow tokenized API extension;
- BA2-B's seek-completion contract requires adjustment because the concrete engine cannot prove that fact.

Do not fabricate certainty merely to preserve the current contract unchanged.

## Production boundary

The current handoff authorizes BA2-D-B investigation/prototype work in Preflight-Architect.

It does **not** automatically authorize modifying production Colosseum.

If proof requires a production `MpvItem` mutation, stop at that authorization boundary unless Hemanth explicitly authorizes the adoption change.

## Guardrail

Re-check BA2-D-B size before implementation.

If raw callback attribution and seek completion are independently risky latency/implementation domains, split again and implement only the first independently verifiable checkpoint.

Do not chain a later sub-slice in the same turn.
