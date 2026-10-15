# Vinyl Backend BA2-D1 - mpv Event Identity Evidence

Status: verified supporting evidence; BA2-D-B1, BA2-D-B2, and BA2-D-B3 complete
Date: 2026-10-06
Owner: Arc 62 Vinyl
Parent slice: BA2-D Colosseum mpv Adoption
Current checkpoint: BA2-D complete locally; Wave 2 sealed

## Purpose

BA2-D1 answers the load-bearing discovery question before any concrete bridge is authored:

> Can Colosseum's current public MpvItem signal surface preserve exact Vinyl playback generation identity for asynchronous load, seek, EOF, error, and observation callbacks?

Verdict:

**No. The current public MpvItem surface is not sufficient by itself for exact BA2 identity fencing.**

The underlying installed libmpv has stronger event identity than MpvItem currently exposes, so the gap is bridgeable without changing BA1/BA2-A/BA2-B/BA2-C.

Production Colosseum was inspected read-only and not modified.

## Frozen upstream

BA2-D1 found no incompatibility in:

- BA0 Domain Contract;
- BA1 Queue Core Contract;
- BA2-A Playback Session Lifecycle;
- BA2-B Engine Adapter;
- BA2-C Queue / Session Coordination.

Those contracts remain frozen.

## Current production MpvItem surface

Current production Colosseum HEAD inspected:

`a61c4564b088b2e1871829d4b7021ab16f7eace0`

Relevant read-only files:

- `native/player/mpvitem.h`;
- `native/player/mpvitem.cpp`;
- `qml/AudiobookSession.qml`.

MpvItem currently exposes:

Commands:
- `loadFile(file)`;
- `loadFileWithHeaders(url, headers)`;
- `seekExact(seconds)`;
- deterministic `pause` property writes;
- raw `command(...)` for stop and other mpv commands.

Signals/properties:
- `fileStarted()`;
- `fileLoaded()`;
- `endFile(reason)`;
- `playbackError(code, message)`;
- `pauseChanged()`;
- `positionChanged()`;
- `durationChanged()`;
- `coreSeekingChanged()`;
- `coreSeeking`.

The current C++ implementation observes mpv's `seeking` property and mirrors it into `coreSeeking`.

## Public-surface identity gap

The public MpvItem callbacks listed above carry no:

- `PlaybackWorkToken`;
- BA2 generation;
- mpv playlist-entry ID;
- load command ID;
- seek request ID.

Therefore a bridge that simply stores "current Vinyl token" and applies it to whichever callback arrives next is invalid.

A delayed callback from an older load could be mislabeled as the newer generation.

That would violate BA2's critical invariant even though BA2-A itself is correct.

## Installed MpvQt evidence

Installed MpvQt header inspected:

`C:\tools\mpvqt-feasibility\mpvqt-msvc-install\include\MpvQt\mpvcontroller.h`

The installed `MpvController` provides:

- synchronous `command(...)`;
- asynchronous `commandAsync(..., id)`;
- asynchronous property APIs with request IDs;
- public `mpv()` access to the underlying `mpv_handle*`.

Its emitted Qt signals are only:

- `propertyChanged(property, value)`;
- `asyncReply(data, mpv_event)`;
- `fileStarted()`;
- `fileLoaded()`;
- `endFile(reason)`;
- `videoReconfig()`.

Critically, MpvQt's public `fileStarted()` and `endFile(reason)` signals discard libmpv's playlist-entry identity.

MpvItem's `mpvController()` accessor is protected through MpvAbstractItem, so a native MpvItem extension/subclass can reach the stronger underlying event surface later.

## Installed libmpv evidence

Installed libmpv header inspected:

`C:\tools\mpvqt-feasibility\libmpv-prefix\include\mpv\client.h`

The installed API documents:

### START_FILE

`MPV_EVENT_START_FILE` carries `mpv_event_start_file`.

That struct contains:

`playlist_entry_id`

described as the playlist entry ID of the file being loaded.

### END_FILE

`mpv_event_end_file` also contains:

`playlist_entry_id`

documented as the same value as the corresponding START_FILE event.

It additionally exposes the real mpv error code when end reason is error.

Current MpvQt/MpvItem discards those fields and reports only a coarse reason string.

### Seek completion

The installed libmpv API documents:

`MPV_EVENT_SEEK`
as the event emitted when a seek is initiated.

It documents:

`MPV_EVENT_PLAYBACK_RESTART`
as the event that usually follows when the seek has finished, with the explicit purpose of allowing a client to detect seek completion.

Current MpvQt does not surface SEEK or PLAYBACK_RESTART as Qt signals.

Current MpvItem instead exposes the observed `seeking` property.

## Empirical rapid-load probe

A temporary read-only runtime probe was built against the exact installed:

- MSVC 19.44 toolchain;
- `C:\tools\mpvqt-feasibility\libmpv-prefix\include`;
- `C:\tools\mpvqt-feasibility\libmpv-prefix\lib\mpv.lib`;
- `libmpv-2.dll`.

It issued two synchronous `loadfile ... replace` commands back-to-back using two existing Colosseum test fixtures.

Both load commands returned success.

Observed event output included:

`EVENT start-file entry=2`

No START_FILE for the first issued load was observed before the second replacement won.

## Consequence: FIFO binding is rejected

This empirically falsifies the tempting implementation:

1. enqueue Vinyl token A when load A is issued;
2. enqueue Vinyl token B when load B is issued;
3. bind the next START_FILE to the oldest queued token.

The first load can be replaced before START_FILE.

The next START_FILE may therefore belong to B while a FIFO still points at A.

A FIFO of issued load commands is not a valid identity fence.

## Second probe and guardrail boundary

A second temporary probe was started to test whether the synchronous load command makes the newly assigned mpv playlist-entry ID queryable immediately through playlist properties.

That probe did not reach execution.

Its CMake configure/build process exceeded the bounded 120-second qualification window and was terminated.

No result about immediate playlist-property availability is claimed.

Under the active Preflight oversized-slice rule, BA2-D1 stops here rather than retrying another slow build in the same turn.

## Required BA2-D2 design property

BA2-D2 must establish an exact mapping:

`PlaybackWorkToken <-> concrete mpv load identity`

without relying on:

- FIFO load issuance;
- current queue index;
- Recording MBID;
- source URL;
- current MpvItem URL;
- whichever token is current when a callback lands.

Acceptable evidence-backed mechanisms may include:

1. capturing the actual mpv playlist-entry ID immediately after a successful synchronous load if the next probe proves that property is reliably available;
2. using a bridge-controlled command/reply handshake that yields or deterministically resolves the concrete playlist-entry ID;
3. a minimal native MpvItem/MpvController extension that surfaces the raw libmpv entry ID alongside START_FILE/END_FILE and gives the bridge a safe command-to-entry association.

Evidence must choose among these.

## Required seek rule

BA2-D2 must preserve the full BA2-B `PlaybackSeekRequest`.

Current evidence supports two honest completion sources:

- raw `MPV_EVENT_SEEK` + `MPV_EVENT_PLAYBACK_RESTART`;
- the observed `seeking` property's true -> false lifecycle if the bridge proves exact request association.

Do not infer completion merely from a large `positionChanged` jump.

Back-to-back seeks require explicit proof that an older request cannot be reported as the newer one.

If necessary, the concrete bridge may serialize/coalesce physical seek commands while BA2-A continues to reject superseded logical requests.

## Load/header mapping remains straightforward

Current MpvItem already exposes:

- plain `loadFile`;
- `loadFileWithHeaders`.

BA2-B's opaque resource + request-header input can map onto these without importing BB provider/ranking policy.

This part of BA2-D has no discovered architecture incompatibility.

## Deterministic play/pause/stop mapping remains straightforward

Current MpvItem exposes a writable pause property.

Therefore:

- BA2 play -> `pause = false`;
- BA2 pause -> `pause = true`.

Current AudiobookSession already contains deterministic `play()` precedent rather than toggle-only semantics.

Stop can map to mpv `stop`.

BA2-B already invalidates lifecycle before engine stop, so stop-triggered late callbacks remain stale if BA2-D preserves their real old identity.

## Failure evidence

Current MpvItem explicitly documents that installed MpvQt exposes only the coarse END_FILE reason string.

It emits:

`playbackError("unknown", reason)`

for coarse error/other end reasons.

BA2-D must preserve that honesty unless it deliberately drops below MpvQt to raw `mpv_event_end_file.error`.

No provider/network/codec classification may be invented by the bridge.

## Production adoption implication

The final production bridge should be native C++.

The current public QML-facing MpvItem signal set should not become Vinyl lifecycle authority.

The likely production seam is one of:

- a small native MpvItem extension;
- a native subclass;
- a tightly-scoped companion that reaches the protected MpvController/raw libmpv handle.

The final choice belongs to BA2-D-B's exact command-to-playlist-entry proof. BA2-D-A's abstract tokenized bridge core was authored concurrently and is now verified; this evidence artifact strengthens the raw mpv attribution work that remains below it.

## BA2-D1 completion

BA2-D1 is complete when:

- current MpvItem command/callback surface is re-inspected;
- installed MpvQt capabilities are inspected;
- installed libmpv load and seek identity capabilities are inspected;
- naïve FIFO load-token binding is empirically disproven;
- the failed second probe is recorded without manufacturing a verdict;
- the exact unresolved proof needed for BA2-D-B is durable.

All conditions are satisfied.

## BA2-D-B1 raw API qualification

A concurrent bounded BA2-D-B1 probe added `mpv_api_probe.cpp` to the existing isolated BA2-D build.

The compiled probe verifies against the installed MpvQt/libmpv headers and libraries that:

- `MpvController::mpv()` exposes the shared `mpv_handle*`;
- `mpv_create_weak_client`, `mpv_request_event`, `mpv_wait_event`, `mpv_set_wakeup_callback`, and `mpv_destroy` are available;
- raw START_FILE, FILE_LOADED, END_FILE, SEEK, and PLAYBACK_RESTART event IDs exist;
- START_FILE and END_FILE payload structs expose playlist-entry identity;
- END_FILE preserves raw libmpv error evidence.

Independent verification in this conversation:

- current `mpv_api_probe.cpp` source inspected;
- compiled probe executable exists;
- direct execution of that exact probe: exit 0;
- CTest at 14:52 IST: 2/2 PASS, covering both the tokenized bridge contract and raw API probe.

BA2-D-B1 proves the raw API needed for a lower identity bridge is reachable. It does **not** yet prove how a particular `loadfile replace` command is associated with its concrete playlist-entry ID at runtime.

The configured `build-ba2d-vs2` tree is intentionally retained as non-authoritative scratch for the next bounded runtime probe so BA2-D-B2 does not pay another unnecessary full configure cost.

## BA2-D-B2 runtime playlist-entry attribution

BA2-D-B2 added the canonical runtime probe:

`ba2d-mpv-adoption-prototype/mpv_playlist_identity_probe.cpp`

and the CMake target/test:

`vinyl_ba2db2_playlist_identity_probe`

The proof uses an owner libmpv handle plus a weak client attached to the same core.

For each successful synchronous `loadfile ... replace`, it queries the core's documented `playlist` property immediately and records the current entry's concrete `id`.

Verified results:

- the concrete playlist entry ID is synchronously queryable immediately after a successful load command;
- loading the same resource twice creates two distinct playlist-entry IDs;
- rapid A -> B replacement produces two distinct concrete IDs even if A never reaches START_FILE;
- raw START_FILE for the winning replacement carries the captured B ID;
- raw END_FILE for the winning replacement carries that same captured B ID;
- any raw START/END observed during the proof resolves only to one of the concrete IDs already captured from successful load commands;
- no FIFO load-token assumption is required;
- URL/resource equality is not used as identity;
- the weak client can observe the raw identity events without taking ownership away from the player core.

The first runtime draft stopped a file before START_FILE could be emitted and correctly failed its START_FILE assertion. The proof was tightened to wait for the captured winning START_FILE before issuing stop; this was a harness-timing repair, not a change to the identity mechanism.

Final local verification:

- existing configured `build-ba2d-vs2` tree reused;
- CMake regeneration with read-only `COLOSSEUM_ROOT`: PASS;
- `vinyl_ba2db2_playlist_identity_probe` compile/link: PASS;
- direct canonical probe execution: exit 0;
- full BA2-D CTest after adding B2: 3/3 PASS.

Therefore the load identity relation is now evidence-backed:

```text
successful synchronous load command
  -> immediately captured mpv playlist_entry_id
  -> stored PlaybackWorkToken <-> playlist_entry_id binding
  -> raw START_FILE / END_FILE resolve through that binding
```

A replacement that never emits START_FILE is harmless: its captured ID exists only as historical issued-load identity and cannot cause a later START_FILE for B to be mislabeled as A.

Production Colosseum remained read-only.

## BA2-D-B3 exact seek-completion attribution

BA2-D-B3 uses the isolated runtime probe:

`ba2d-mpv-adoption-prototype/mpv_seek_completion_probe.cpp`

The proof keeps the complete `PlaybackSeekRequest` attached to every physical seek. One physical seek is allowed in flight at a time. While that seek is active, newer logical seeks replace the queued request so only the newest pending request can become the next physical seek.

Raw libmpv evidence is used as follows:

- `MPV_EVENT_SEEK` marks that the exact active physical seek actually began;
- `MPV_EVENT_PLAYBACK_RESTART` may complete only that active physical request;
- completion is returned to BA2-B with the original work token, seek sequence, and observed position;
- BA2-A remains final authority for rejecting a completion that was logically superseded.

The adversarial three-seek case proves:

1. seek1 becomes the only physical in-flight seek;
2. seek2 is queued and then superseded by newer seek3 before physical dispatch;
3. seek1 completes physically but BA2-A rejects its completion as `SupersededSeek`;
4. seek2 is never physically dispatched;
5. seek3 becomes the second physical seek and its exact completion is accepted;
6. the accepted completion clears BA2-A pending seek state and publishes the observed final position;
7. stop clears both active and queued physical seek ownership before returning;
8. late raw seek/restart events after stop cannot manufacture completion or revive the lifecycle.

Final independent verification on 2026-10-06:

- clean out-of-tree CMake configure against Qt 6.11.1: PASS;
- all 14 BA2-D build steps: PASS;
- `vinyl_ba2d_mpv_bridge_contract`: PASS;
- `vinyl_ba2d_mpv_api_probe`: PASS;
- `vinyl_ba2db2_playlist_identity_probe`: PASS;
- `vinyl_ba2db3_seek_completion_probe`: PASS;
- full CTest: **4/4 PASS, 0 failed**;
- direct B3 probe execution against Colosseum's read-only `tests/lanista_capture/assets/Colosseum.mp4`: exit 0.

The Qt configure still reports the known optional/private TaskTree/QML plugin warning. It does not prevent configuration, build, linking, or any BA2-D test.

## BA2-D closeout

BA2-D-A, BA2-D-B1, BA2-D-B2, and BA2-D-B3 are complete and verified.

The exact load and seek identity relations are now evidence-backed without FIFO attribution, URL identity, queue index identity, or mutable-current-token guessing.

Production Colosseum remained read-only and unchanged. Production adoption remains a later explicit integration task, not an unresolved Wave 2 backend contract.

**Wave 2 is sealed locally.** The next parallel backend wave is Wave 3 convergence: BA3, BA4, BB3, BB4, BE2, and BF3.
