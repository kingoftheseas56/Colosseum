# Vinyl Backend BA2-D - Colosseum mpv Adoption Contract

Status: verified isolated bridge proof; production Colosseum adoption pending explicit authorization
Date: 2026-10-06
Owner: Arc 62 Vinyl
Depends on: BA2-A Playback Session Lifecycle, BA2-B Engine Adapter, BA2-C Queue / Session Coordination

## Purpose

BA2-D defines the smallest honest bridge from the frozen Vinyl playback port to Colosseum's existing mpv/libmpv machinery.

It does not redesign BA1, BA2-A, BA2-B, or BA2-C.

The verified dependency direction remains:

```text
BA1 queue authority
  -> BA2-C coordinator
  -> BA2-A lifecycle
  -> BA2-B engine-neutral port
  -> BA2-D Colosseum/mpv bridge
  -> existing MpvQt/libmpv core
```

Production Colosseum remained read-only during this checkpoint.

## Basis inspected

Frozen Arc 62 inputs:

- `BA2-PLAYBACK-SESSION-CONTRACT.md`
- `BA2B-ENGINE-ADAPTER-CONTRACT.md`
- `BA2C-QUEUE-SESSION-COORDINATION-CONTRACT.md`
- `ba2-playback-session-prototype/`
- `ba2-engine-adapter-prototype/`
- `ba2c-queue-session-coordination-prototype/`

Current Colosseum player evidence, read-only:

- `native/player/mpvitem.h`
- `native/player/mpvitem.cpp`
- `qml/AudiobookSession.qml`
- `native/CMakeLists.txt`

Installed dependency evidence:

- `C:/tools/mpvqt-feasibility/mpvqt-msvc-install/include/MpvQt/mpvcontroller.h`
- `C:/tools/mpvqt-feasibility/mpvqt-msvc-install/include/MpvQt/mpvabstractitem.h`
- `C:/tools/mpvqt-feasibility/libmpv-prefix/include/mpv/client.h`

## Current Colosseum player facts

The current `MpvItem` already provides the required command mechanics:

- plain load through `loadFile()`;
- header-bearing load through `loadFileWithHeaders()`;
- deterministic pause state through `setPause(bool)`;
- exact absolute seek through `seekExact(double)`;
- stop through the inherited mpv command surface;
- observed position, duration, pause, and `seeking` state.

It also exposes current QML-facing signals:

- `fileStarted()`;
- `fileLoaded()`;
- `positionChanged()`;
- `durationChanged()`;
- `pauseChanged()`;
- `endFile(reason)`;
- `playbackError(code, message)`.

Those signals are useful presentation/runtime evidence, but they are not sufficient as Vinyl ownership signals because they carry no `PlaybackWorkToken`.

## Core ownership law

BA2-D must preserve the identity carried into BA2-B.

A concrete engine callback may mutate Vinyl playback state only when it can be attributed to the exact original:

```text
QueueOccurrenceId
+
playback generation
```

represented by `PlaybackWorkToken`.

For seeks, the callback must additionally preserve:

```text
PlaybackSeekRequest.sequence
```

A bridge must not derive ownership from:

- current Recording MBID;
- queue index;
- current URL;
- current provider;
- whichever item happens to be active when a callback is delivered;
- mutable global player state alone.

## Why the existing high-level signals are insufficient

MpvQt's installed `MpvController` exposes `fileStarted()`, `fileLoaded()`, and `endFile(reason)` without the raw libmpv event payload.

libmpv itself provides stronger evidence:

- `MPV_EVENT_START_FILE` carries `mpv_event_start_file.playlist_entry_id`;
- `MPV_EVENT_END_FILE` carries the matching `playlist_entry_id`;
- `MPV_EVENT_END_FILE` also carries the coarse libmpv error value;
- `MPV_EVENT_PLAYBACK_RESTART` is documented as the event whose main purpose is allowing a client to detect when a seek request is finished.

The installed MpvQt controller does not expose a dedicated `PLAYBACK_RESTART` signal.

Therefore BA2-D explicitly rejects these shortcuts:

- treating `fileLoaded()` as belonging to the current Vinyl token merely because it arrived now;
- treating `positionChanged()` as seek completion;
- treating `coreSeeking == false` alone as exact per-request seek completion;
- treating `MPV_EVENT_COMMAND_REPLY` as proof that media seeking has settled.

Command completion and playback restart are different evidence.

## Weak-client sidecar decision

No MpvQt fork is required.

The installed libmpv API exposes `mpv_create_weak_client()`.

A weak client:

- connects to the same mpv core;
- has its own event queue;
- has its own requested-event state;
- has its own observed-property state;
- can be destroyed independently;
- receives core shutdown when the owning non-weak handles disappear.

The installed `MpvController::mpv()` method publicly exposes the underlying initialized `mpv_handle *`.

Therefore the **reference-proven adoption shape** is a Vinyl-owned native sidecar attached to the same core as the rendering `MpvItem`. BA2-D-B2/B3 and the W4-H real-libmpv journey prove the weak-client/event-identity mechanics in the reference backend; attaching that sidecar to Colosseum's production player remains production wiring.

The sidecar does not replace MpvQt rendering and does not become another playback engine.

## Production-side bridge shape

The intended native seam is conceptually:

```text
VinylPlaybackSession
  -> PlaybackEngineAdapter
  -> ColosseumMpvCommands
  -> Vinyl mpv sidecar client
  -> same libmpv core rendered by MpvItem

same sidecar event queue
  -> token ownership ledger
  -> ColosseumMpvCallbacks
  -> PlaybackEngineAdapter
  -> VinylPlaybackSession
```

A production bridge should keep command issuance and raw event ownership on one serialized bridge strand.

This avoids racing a new command against undrained old raw events.

## Command mapping

### Load

`PlaybackLoadInput.resource` remains opaque transport material.

With no headers:

```text
load(token, input)
  -> plain loadfile ... replace
```

With headers:

```text
load(token, input)
  -> validated http-header-fields
  -> loadfile ... replace
```

When `PlaybackLoadInput.playbackWindow` is present, BA2-D also emits per-file mpv bounds:

```text
PlaybackWindow.startMs -> loadfile start=<physical seconds>
PlaybackWindow.endMs   -> optional loadfile end=<physical seconds>
```

These are physical-media bounds. They do not become queue identity, canonical Recording identity, or resume position. A bounded load begins logical track time at zero and the bound's end becomes the logical EOF for that occurrence.

The existing Colosseum header formatting/injection guard remains the appropriate production helper.

BA2-D never decides provider, source rank, canonical identity, or fallback.

### Play

```text
play(token)
  -> pause=false
```

This is deterministic state-setting, not toggle behavior.

### Pause

```text
pause(token)
  -> pause=true
```

### Stop

```text
stop(token)
  -> stop
```

BA2-B already invalidates BA2-A before issuing this command.

Any synchronous or late stop-generated callback must therefore remain stale.

### Seek

The canonical Wave 4 engine-facing request is `PlaybackSeekCommand`: the complete frozen `PlaybackSeekRequest` plus track-relative integer-millisecond target.

BA2-D maps logical track time to physical media time before issuing mpv:

```text
physicalTargetMs = PlaybackWindow.startMs + trackRelativeTargetMs
```

With no playback window, physical start is zero.

Only at this concrete bridge is the physical millisecond target converted to mpv seconds for exact absolute seek. Command dispatch is still not completion evidence.

## Load-token ownership

The sidecar must record the Vinyl work token before issuing a load.

Because the production load command is synchronous and bridge command/event work is serialized on one strand, raw `START_FILE` events can bind the newly created mpv playlist entry to the pending Vinyl load token.

After binding:

- `FILE_LOADED` belongs to the current started entry;
- `END_FILE` resolves through its raw `playlist_entry_id`;
- old entry EOF/error remains attached to the old Vinyl token after a replacement;
- the replacement entry receives its own token.

A raw start event with no pending Vinyl load is not silently assigned to the current occurrence.

Unexpected playlist expansion/redirect behavior must fail closed or receive an explicit future mapping rule.

BA2-D does not guess.

## Seek-completion ownership

libmpv gives the bridge the correct completion evidence: `MPV_EVENT_PLAYBACK_RESTART`.

It does not give that event an arbitrary Vinyl request ID.

Therefore the concrete mpv adapter must not have multiple physically ambiguous seek requests in flight.

The safe mechanism is:

1. BA2-B may continue creating distinct `PlaybackSeekRequest` values immediately.
2. BA2-D retains the complete requests.
3. At most one physical mpv seek is dispatched at a time.
4. A newer Vinyl seek received while a physical seek is in flight replaces any queued-not-yet-dispatched seek with the newest request.
5. `MPV_EVENT_PLAYBACK_RESTART` completes only the one physically in-flight request.
6. BA2-D maps the observed physical mpv position back to track-relative integer milliseconds and returns the exact `PlaybackSeekCommand` to `engineSeekApplied(command, observedPositionMs)`.
7. The next retained request, if any and still in the same work token, is then dispatched.
8. Load replacement or stop invalidates all physical/queued seek ownership before new work begins.

This makes completion identity deterministic without pretending that `PLAYBACK_RESTART` itself carries a sequence number.

A `PLAYBACK_RESTART` with no physical seek in flight is ordinary playback/startup evidence and is not reported as seek completion.

## Observation mapping

The sidecar may observe the engine properties needed by BA2:

- pause;
- playback position;
- duration.

Each observation is forwarded under the currently bound raw file entry's `PlaybackWorkToken`.

BA2-A remains the final stale-work fence.

No progress persistence, history accounting, scrobbling, or UI interpretation occurs here.

## EOF mapping

Raw `END_FILE` carries the playlist entry ID.

The sidecar resolves that entry ID back to the exact work token and forwards EOF under that token.

BA2-D never advances BA1.

BA2-C remains the only layer that converts one accepted current EOF into a single BA1 `advance()`.

## Failure mapping

Raw `mpv_event_end_file.error` is direct engine evidence and may be preserved as a bounded mpv failure code/detail.

The bridge must not reinterpret it as:

- provider failure;
- source mismatch;
- network policy;
- retry/fallback instruction.

Those meanings belong elsewhere.

The existing high-level `MpvItem::playbackError("unknown", reason)` remains valid coarse evidence for legacy consumers, but Vinyl's sidecar can retain the raw libmpv error without manufacturing additional semantics.

## Header ownership

Request headers remain transport data only.

They do not become provider identity.

The bridge must preserve BA2-B validation and Colosseum's existing header-injection protections.

A following plain load must clear prior header state before loading, preserving current `MpvItem` behavior.

## Queue boundary

BA2-D contains no queue policy.

It must never:

- call BA1 `advance()`;
- insert, remove, or reorder queue occurrences;
- implement Play Now or Play Next;
- select the next occurrence;
- clear/deactivate BA1.

BA1 + BA2-C already own those operations.

## Source boundary

BA2-D does not:

- discover candidates;
- perform exact-track resolution;
- choose provider;
- rank sources;
- implement source fallback;
- persist selected source.

BB remains the source owner.

## Persistence and history boundary

BA2-D does not:

- write queue/session snapshots;
- restore queue/session state;
- record listen history;
- compute listening time;
- scrobble.

BA3 and BA4 remain separate.

## Presentation boundary

No QML state is authoritative in BA2-D.

A future Vinyl UI may observe BA2 state, but QML does not own token attribution, seek completion, load ownership, or engine lifecycle.

## Isolated bridge prototype

Reference implementation:

`backend/ba2d-mpv-adoption-prototype/`

Files:

- `ColosseumMpvBridge.h`
- `ColosseumMpvBridge.cpp`
- `test_colosseum_mpv_bridge.cpp`
- `mpv_api_probe.cpp`
- `mpv_playlist_identity_probe.cpp`
- `mpv_seek_completion_probe.cpp`
- `mpv_bounded_window_probe.cpp`
- `CMakeLists.txt`

`ColosseumMpvCommands` adapts the frozen BA2-B commands to a minimal Colosseum player-facing port.

`ColosseumMpvCallbacks` routes token/request-bearing player observations back into BA2-B.

The player-facing port is deliberately stricter than current untagged QML signals.

That strictness is the adoption contract, not accidental abstraction.

## Deterministic harness coverage

The native bridge harness proves:

1. every header-bearing load retains the exact work token;
2. every plain load retains the exact work token;
3. load resource text remains opaque;
4. validated headers are preserved;
5. play maps to `paused=false`;
6. pause maps to `paused=true`;
7. position observation retains exact token ownership;
8. duration observation retains exact token ownership;
9. every seek retains the complete `PlaybackSeekRequest`;
10. command dispatch alone does not clear pending seek;
11. an older same-generation seek completion is rejected as `SupersededSeek`;
12. the latest exact seek completion is accepted;
13. an old-load loaded callback is stale after replacement;
14. old-load position/duration callbacks are stale after replacement;
15. old-load failure/EOF callbacks are stale after replacement;
16. same-occurrence retry creates a new generation;
17. callback emitted synchronously during stop is already stale;
18. current EOF reaches BA2-A without queue mutation;
19. coarse current failure evidence reaches BA2-A without provider interpretation;
20. PlaybackWindow maps to deterministic per-file mpv start/end options;
21. physical position maps to track-relative integer milliseconds;
22. bounded duration is end-start rather than the backing file duration;
23. track-relative seeks are offset by the physical window start;
24. late physical position beyond a bounded end clamps to logical track end;
25. Qt runtime is exactly 6.11.1.

## Installed API probe

A separate executable compiles, links, and runs against the actual local production dependency set:

- Qt 6.11.1 MSVC;
- installed MpvQt;
- installed libmpv import library/runtime.

It proves the exact dependency set exposes:

- public `MpvController::mpv()`;
- `mpv_create_weak_client`;
- `mpv_request_event`;
- `mpv_wait_event`;
- `mpv_set_wakeup_callback`;
- `mpv_destroy`;
- `MPV_EVENT_START_FILE`;
- `MPV_EVENT_FILE_LOADED`;
- `MPV_EVENT_END_FILE`;
- `MPV_EVENT_SEEK`;
- `MPV_EVENT_PLAYBACK_RESTART`;
- `mpv_event_start_file.playlist_entry_id`;
- `mpv_event_end_file.playlist_entry_id`;
- raw end-file error evidence.

This proves the sidecar design fits the exact locally installed APIs without an MpvQt fork.

## Verification

Verified locally on 2026-10-06.

Environment:

- Qt: 6.11.1 MSVC 2022 x64;
- compiler: MSVC 19.44.35223;
- Windows SDK: 10.0.26100.0;
- MpvQt prefix: `C:/tools/mpvqt-feasibility/mpvqt-msvc-install`;
- libmpv prefix: `C:/tools/mpvqt-feasibility/libmpv-prefix`.

Results:

- CMake configure: PASS;
- `vinyl_ba2d_mpv_bridge` compile/link: PASS;
- `vinyl_ba2d_mpv_bridge_harness` compile/link: PASS;
- `vinyl_ba2d_mpv_api_probe` compile/link: PASS;
- `vinyl_ba2db2_playlist_identity_probe` compile/link and runtime proof: PASS;
- `vinyl_ba2db3_seek_completion_probe` compile/link and runtime proof: PASS;
- W4-A full CTest after bounded-window integration: **5/5 PASS**;
- direct B3 seek-completion probe against the read-only Colosseum media fixture: PASS;
- real bounded-window libmpv probe against the read-only Colosseum media fixture: PASS;
- the bounded probe starts at the physical window, offsets exact seeks, reports track-relative observations, and receives EOF at the logical end;
- bridge prohibited dependency scan: zero hits.

One configure warning reported a missing optional/private Qt TaskTree dependency through QML plugin discovery. It did not prevent configuration, compilation, linking, or either test.

An earlier combined configure/build attempt timed out while generating a Visual Studio build tree. The resulting partial tree was discarded. A clean configure separated from build succeeded. A subsequent CTest launch initially failed with Windows `0xc0000135` because Qt runtime DLLs were not on the test PATH; the CTest environment was corrected, then the same binary passed.

These were environment/harness issues, not playback-contract failures.

## Production adoption status

Production Colosseum has not been modified.

Therefore BA2-D is:

- architecturally defined;
- source-grounded against current Colosseum;
- compiled;
- deterministically tested;
- dependency-API verified;
- not production-integrated;
- not runtime-qualified inside the real Colosseum player.

The weak-client sidecar, playlist-entry token attribution, serialized seek state, and callback fencing are reference-proven. **Production Colosseum Player integration owner** still must wire those proven owners into the real Colosseum player after explicit production write authorization; this contract does not claim that wiring has occurred.

## BA2 completion state

BA2-A, BA2-B, BA2-C, and BA2-D are locally complete.

BA2-D-B3 independently proves exact seek-completion attribution with full `PlaybackSeekRequest` identity over real libmpv SEEK / PLAYBACK_RESTART events. Newer logical seeks supersede queued work deterministically, only physically executed seeks can complete, and late events after stop cannot revive lifecycle state.

The remaining gap is production player wiring and runtime qualification in Colosseum. The BA2-D mechanics themselves, including real-libmpv event attribution and W4-H fallback/error composition, are reference-proven; this is not an unresolved BA2 domain contract.

BA2 is therefore complete for Wave 2. The next mock-agnostic convergence wave begins with BA3 Session Persistence, BA4 Listen Event Ledger, BB3 CUE / Whole-Album Sources, BB4 mpv Playback Adapter, BE2 Feedback + Taste State, and BF3 Video Playback Adapter.
