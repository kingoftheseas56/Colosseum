# Android Media3 PlayerItem Backend Design

**Date:** 2026-09-03

**Status:** Architecture approved in chat; written spec pending user review. Product implementation has not started.

**Planning branch:** `feature/android-media3-a02-planning`

**Design basis:** Android Foundation integrated tree `861c949ac5e48b28b05bfe73d5853b402e15053f`.

## Objective

Provide Colosseum's Android video, audiobook, and future music playback backend behind the existing `Colosseum.Player 1.0 / PlayerItem` QML type using AndroidX Media3/ExoPlayer, while preserving desktop `MpvItem` unchanged.

The Android backend owns Colosseum's portable playback state machine and QML contract. Media3 owns demuxing, codecs, A/V synchronization, buffering, Android audio focus integration, and Android media-source mechanics.

The design must preserve the existing shared `PlayerPage.qml` and `AudiobookSession.qml` instead of creating Android-specific player UIs.

## Existing authority

- `native/player/playerbackendcontract.h` is the host-facing backend contract.
- W04 already made shared QML instantiate `PlayerItem` rather than `MpvItem` by type name.
- W02 `PlatformRuntime` owns Android application lifecycle and surface-availability truth.
- W02 owns Android screen-on policy; A02 does not create another power-policy owner.
- A03 will own durable local-media identity and `content://` URI production.
- A04 will own in-process Colosseum Server lifecycle and dynamic loopback range URLs.
- W01 already excludes MpvQt/libmpv from the Android build graph.
## Locked product constraints

- Desktop playback remains `MpvItem`/MpvQt/libmpv.
- Android primary playback is Media3/ExoPlayer; libmpv is not an Android fallback requirement.
- No decoder, demuxer, A/V sync engine, or subtitle parser stack is written from scratch.
- No Android WebEngine and no PDF work enters this lane.
- No Stremio Node server/supervisor is ported to Android.
- No fixed localhost port is assumed.
- Local media is not copied into app storage by default.
- `content://` is a first-class playable source.
- Direct HTTP/HLS/DASH may bypass Colosseum Server.
- No background playback service, MediaSession notification, or Picture-in-Picture is enabled by this work.
- Unsupported mpv-only features must report false capability instead of simulating support.

## Dependency decision

Use AndroidX Media3 **1.11.0**, released 2026-08-05, pinned consistently across modules.

Required runtime modules for the first implementation:

- `androidx.media3:media3-exoplayer:1.11.0`
- `androidx.media3:media3-exoplayer-hls:1.11.0`
- `androidx.media3:media3-exoplayer-dash:1.11.0`

Do not add `media3-session`, Media3 UI widgets, FFmpeg decoder extensions, Transformer, Cast, or Compose modules in v1.

AndroidX Media3 source is Apache-2.0 licensed. Lead must include its license/notice obligations in the Android dependency inventory used for release qualification.
## Architecture

Android registers `AndroidMedia3Item` as `Colosseum.Player 1.0 / PlayerItem`. Desktop keeps registering `MpvItem` under the same neutral name and retains the legacy `MpvItem` registration.

`AndroidMedia3Item` is a `QQuickItem` plus `PlayerBackendContract`. It owns the QML-facing cached state, generation guards, JNI calls, and scene-graph video presentation. It does not own Android Activity policy or source discovery.

`Media3PlayerHost.java` owns one `ExoPlayer` instance. All player mutations are marshalled to the Android main looper inside this host even when JNI entered from a different Qt thread.

`Media3SurfaceBridge.java` owns the Java `SurfaceTexture` and `Surface` wrappers for a Qt-created `GL_TEXTURE_EXTERNAL_OES` texture. It never owns playback state.

The native render path wraps the external OES texture with `QNativeInterface::QSGOpenGLTexture::fromNativeExternalOES`. A small `QSGGeometryNode` uses the normal Qt texture material and updates four UV vertices with the latest `SurfaceTexture` transform matrix. This avoids a second Android view hierarchy and avoids decoded-frame copies.

Android must force the Qt Quick graphics API to OpenGL before the first scene graph is created. The ExternalOES path is not silently attempted under Vulkan or another RHI backend.

Default video presentation is aspect-fit inside the `PlayerItem` bounds. Android reports `videoTransform=false`; mpv-specific panscan, zoom, and arbitrary aspect overrides remain unavailable until separately designed.

## Thread ownership

- Qt GUI thread: QML properties, item lifetime, queued JNI command initiation, state notification.
- Qt scene-graph render thread: OES texture allocation/destruction, `SurfaceTexture.updateTexImage()`, texture transform acquisition, QSG node updates.
- Android main looper: ExoPlayer construction, player commands, listeners, track selection, source replacement, audio-focus/noisy behavior.
- Callback crossing: Java native callbacks carry a native handle plus source generation; C++ queues state changes onto the Qt object thread and drops stale generations.

No Java callback may directly mutate a QQuickItem or QSG node. Surface callbacks carry a separate surface generation so a late frame callback from a released `SurfaceTexture` cannot schedule work against its replacement.
## Required QML-facing compatibility surface

`AndroidMedia3Item` must expose the portable surface already consumed by shared QML. The implementation plan must add a contract test that fails if these members disappear.

Properties:

- `capabilities`, `mediaTitle`, `currentUrl`
- `position`, `duration`, `formattedPosition`, `formattedDuration`
- `pause`, `volume`, `mute`, `speed`
- `audioTrack`, `subtitleTrack`, `audioTracks`, `subtitleTracks`
- `chapters`
- `decodedWidth`, `decodedHeight`
- `cacheTime`, `cacheBufferingState`, `coreSeeking`
- compatibility defaults for `audioDelay`, `subDelay`, `panscan`, `videoZoom`, `videoAspect`
- compatibility default `gifEncoding=false`

Methods:

- all seven `PlayerBackendContract` methods
- one- and two-argument `loadSource`
- `seekExact`, `seekStep`, `addSubtitle`
- harmless compatibility methods currently called or discoverable from shared QML: `applyPlaybackProfile`, `refreshAudioOutput`, `playbackStat`, `frameStep`, `frameBackStep`, `setSubOption`, `setAudioNormalization`, capture/GIF methods

Optional-method implementations must be inert and side-effect-free when their corresponding capability is false. Shared QML must never need `typeof AndroidMedia3Item` checks.
Signals:

- property-change signals matching the properties above
- `fileStarted`, `fileLoaded`, `endFile(reason)`, `playbackError(code, message)`
- `videoReconfig`, `decodedDimensionsChanged`
- compatibility `gifSaved` / `gifFailed` may exist but cannot fire success when `gifCapture=false`

Android v1 capability map:

- `frameStepping=false`
- `frameCapture=false`
- `gifCapture=false`
- `audioDelay=false`
- `subtitleDelay=false`
- `videoTransform=false`
- `loudnessNormalization=false`
- `playbackStats=false`
- `subtitleStyling=false`
- `audioOutputRefresh=false`
- `pictureInPicture=false`
- `subtitleCueOverlay=true` (Android extension used only by the shared dynamic subtitle overlay)

This conservative map is intentional. Media3 having some lower-level mechanism is not enough to advertise a Colosseum feature before the shared UX and deterministic verification exist.

## Source contract

A02 accepts a source only through `loadSource(url, headers)`. It does not identify torrents, Vault rows, SAF providers, or Theatre providers itself.
Supported source classes:

1. `https://` direct progressive, HLS, or DASH provider sources.
2. `http://127.0.0.1:<dynamic-port>/...` from A04 Colosseum Server for the v1 Android contract.
3. `content://...` local/document-provider URIs from A03.
4. ordinary `file://` or local paths may remain supported for app-owned/downloaded files where existing flows still produce them.

For each load, Java creates a fresh immutable Media3 media-source factory carrying that load's HTTP headers. Provider headers are not stored in a process-global mutable HTTP factory.

`DefaultHttpDataSource.Factory` receives the copied headers for HTTP(S). `DefaultDataSource.Factory` wraps that HTTP factory so the same Media3 source graph also handles `content://`, file, and resource schemes.

`DefaultMediaSourceFactory` uses that immutable data-source factory and infers progressive/HLS/DASH from the MediaItem URI/content type. HLS and DASH module dependencies are therefore mandatory even though direct progressive playback is the first smoke gate.

A same-URL reload is still a new source generation. Headers, decoded-frame state, tracks, chapters, subtitle attachments, errors, and readiness are reset before preparing the replacement source.

The A03 contract is: return a permission-valid durable `content://` URI and persistable access semantics when required. A02 must not require a copied path or filesystem translation.

The A04 contract is: return a dynamically bound loopback HTTP URL whose GET/HEAD/range behavior is sufficient for Media3 seeking. A02 must not assume a port, torrent identity, file index, or server implementation detail.

## Header safety

Only caller-supplied non-empty string header names and scalar string values are forwarded. Reject embedded CR/LF in names or values. Empty maps mean no provider headers.

Header state is source-generation scoped. A later plain load must not inherit Referer, Origin, Cookie, Authorization, or any other prior source header.
## Video surface lifecycle

The OES texture is created only after the Qt Quick scene graph has an active OpenGL context. `Media3SurfaceBridge` creates a `SurfaceTexture(textureId)` and `Surface` for that texture and sends the Surface to `Media3PlayerHost`.

`SurfaceTexture.OnFrameAvailableListener` does not render. It only signals the native item that a frame is pending. The native item schedules a Qt Quick update on the GUI thread.

During the next scene-graph update, the render thread calls `updateTexImage()`, retrieves the latest transform matrix, updates texture coordinates, and renders the external texture through Qt Quick.

Surface loss sequence:

1. W02/Lead reports surface unavailable or Qt scene-graph invalidation begins.
2. A02 calls/enters `releaseVideoSurface()`.
3. Java clears ExoPlayer's matching video Surface before that Surface is released.
4. Native decoded dimensions reset to zero and `playerReady` therefore becomes false for video.
5. `Surface`, `SurfaceTexture`, wrapped `QSGTexture`, and OES name are released in owning-thread order.
6. The ExoPlayer instance and current MediaItem remain alive unless host lifecycle policy separately requires release.

Surface restore sequence creates a new OES/Surface pair and calls `restoreVideoSurface()`. Playback position/source identity stay on the existing ExoPlayer. `decodedWidth/decodedHeight` become positive again only after a new `onRenderedFirstFrame` for the restored surface.

A rotation, Activity recreation, or Qt surface recreation must never keep a stale Java Surface bound to Media3.

## Decoded-frame truth

`onVideoSizeChanged` updates pending source width/height but does not itself make the QML player ready.

On every new load and surface loss, exported `decodedWidth` and `decodedHeight` reset to zero.

`onRenderedFirstFrame` for the current generation publishes the latest positive Media3 video size as decoded dimensions. Audio-only playback intentionally leaves both values at zero.
## Playback state mapping

`loadSource` increments the native source generation, resets source-scoped state, updates `currentUrl`, and emits `fileStarted` before Java prepares the new MediaSource.

`fileLoaded` fires once per generation on the first Media3 `STATE_READY`. It means the source is prepared enough for duration/tracks/selection logic; it does not imply that a video frame has rendered or playback has advanced.

`STATE_BUFFERING` updates buffering state but does not create a false end/error transition.

`STATE_ENDED` emits `endFile("eof")` once for the active generation.

`onPlayerError` maps Media3 failures into a bounded Colosseum code family: `network`, `http`, `source`, `permission`, `decoder`, `render`, or `unknown`. The original safe Media3 diagnostic string may be included in the message, but QML recovery policy must not depend on unstable Media3 exception class names.

For parity with current `MpvItem`, an error emits `endFile("error")` first and then `playbackError(code, message)`. `PlayerPage` does not recover from the error-shaped `endFile`; the typed `playbackError` remains the single recovery trigger.

Position/duration/buffer snapshots are sampled on the Android main looper while a source is active and delivered no faster than roughly 10 Hz to QML. Cached native values may update more often internally, but QML notification rate remains bounded.

`cacheTime` is Media3 buffered position in seconds. `cacheBufferingState` is Media3 buffered percentage when known, otherwise `-1`.

`pause` maps to `playWhenReady=false/true` without destroying the source. `speed` maps to `PlaybackParameters.speed`. QML volume remains the current Colosseum 0-100 contract and maps to Media3 0.0-1.0; values above 100 are clamped on Android rather than pretending to implement mpv amplification.

`mute` preserves the last non-muted volume and presents an independent bool to QML.
## Seek semantics

`seekExact(seconds)` clamps to the known seekable media range, sets native `coreSeeking=true`, and asks Media3 to `seekTo` the absolute millisecond target.

The Java host tracks the pending target. `coreSeeking` clears after Media3 reports the seek discontinuity and either the sampled position is within 0.5 seconds of Media3's accepted post-seek position or the player has returned to `STATE_READY`; error/source replacement also clears it. A 3-second safety timeout clears a wedged seeking flag without claiming successful arrival.

`seekStep(delta)` resolves through the cached current position and the same absolute seek path.

## Track enumeration and selection

Media3 `Tracks` is normalized into the existing QML row shape. Each audio/subtitle row contains at least:

- `id`: source-generation-local opaque string (`a:<group>:<track>` or `s:<group>:<track>`)
- `title`: label, language, or codec-derived fallback
- `lang`: normalized BCP-47/language value when known
- `selected`: current Media3 selection truth
- `external`: true for A02-added standalone subtitle configurations
- `forced`: derived only from Media3 role/selection flags, never from title guessing when role data exists

The native/QML id is not a durable media identity. Java retains a generation-scoped map from the opaque id to Media3 `Tracks.Group` + track index.

Setting `audioTrack` or `subtitleTrack` applies an exact `TrackSelectionOverride`. Empty subtitle selection disables text tracks for the active item. Overrides are rebuilt for each new source rather than leaking track groups across media items.

## Chapters

Media3 1.11 exposes `androidx.media3.extractor.metadata.Chapter` metadata, including ID3 `CHAP` and QuickTime chapter data.

A02 listens for active-source metadata entries implementing `Chapter`, ignores entries Media3 marks hidden, converts their period-relative times to current-window-relative seconds as required by Media3, sorts/deduplicates them, and publishes the existing `{title, startSec}` QML shape. Missing titles normalize to `Chapter`.

If a container/extractor does not surface chapter metadata, `chapters` is honestly empty. A02 does not parse arbitrary containers independently to manufacture chapter parity.
## Subtitle architecture

Media3 remains the subtitle parser. It supports standalone WebVTT, TTML, SubRip, and SSA/ASS; A02 must not implement a parallel parser.

Android adds one backend-specific QML property, `subtitleCues`, plus `subtitleCuesChanged`, and advertises `subtitleCueOverlay=true`. Desktop does not need to expose this property because the shared overlay receives the player as `property var` and is instantiated only when that capability is true.

`Media3PlayerHost` listens to Media3 cue updates and converts the active `Cue` set into generation-scoped plain data: text, ordering/z-index, position/line anchors, cue size, alignment, text-size hints, window/background color, and supported text-style runs extracted from Android spans.

New shared component `qml/Media3SubtitleOverlay.qml` renders those cue maps as Qt Quick text/items at z=1 above the video item and below Colosseum player chrome/loaders. It contains no source fetching and no playback state.

The first implementation targets strong practical SSA/ASS fidelity for dialogue positioning, alignment, colors, emphasis, and ordering. It does not claim libass-perfect karaoke timing, vector drawing, complex clipping, animated transforms, or every embedded-font edge case.

Because Android reports `subtitleStyling=false`, the existing mpv subtitle appearance editor must not be offered on Android. Lead owns the small shared-QML capability guard needed to hide/disable that entry.

`addSubtitle(url, title, lang, select)` asks Media3 to attach a standalone subtitle configuration. The host preserves the current source URI, HTTP headers, playback position, play/pause intent, selected audio track, and source generation while rebuilding/replacing the Media3 source as needed.

A subtitle attachment revision is not a new Colosseum source load: it must not emit `fileStarted`, change `currentUrl`, or reset progress identity. Temporary buffering caused by Media3 re-preparation remains visible through buffering state.

Multiple externally added subtitles are retained for the active source. A new `loadSource` clears them.

## Audio focus and noisy-output policy

Each Java host configures Media3 audio attributes with usage `USAGE_MEDIA` and enables Media3-managed audio focus. W02 must not create or drive a second Android `AudioManager` focus owner.
Media3 `setHandleAudioBecomingNoisy(true)` is enabled so unplugging wired/Bluetooth output pauses rather than unexpectedly moving playback to speakers.

`setAudioFocusState` remains implemented for host-policy tests and future coordination, but normal production focus transitions originate from Media3. The host hook may force a pause/loss state but must not auto-resume playback that the user paused.

One ExoPlayer instance exists per `AndroidMedia3Item`, matching the current one-backend-instance-per-QML-item shape. No process-global player singleton is introduced.

## Host lifecycle policy

No background playback is approved in this phase.

`setHostLifecycleState("hidden")` or `"suspended"` pauses active playback and records whether the pause was host-induced. `"active"` may resume only when the host itself paused that same generation and no user pause, source change, stop, or terminal audio-focus loss occurred meanwhile.

`"inactive"` is treated as foreground/transient because W02 defines Active and Inactive as foreground. It does not by itself stop playback.

This policy lets a future MediaSession/background-audio design replace only the host lifecycle policy. It does not require replacing the QML contract, source graph, or Media3 owner.

Destroying the `AndroidMedia3Item` releases its ExoPlayer on the Android main looper after detaching the video surface. Activity/surface recreation alone does not destroy the player.

## Cleartext loopback policy

The Android app's base network-security policy remains cleartext-denied.

For FIRST-APK, A04 should publish Colosseum Server URLs on numeric IPv4 loopback `127.0.0.1` with a dynamic port. Lead adds an Android Network Security Configuration exception for `127.0.0.1` only and wires it through the manifest.

Do not use blanket `android:usesCleartextTraffic="true"` or a base-config cleartext allow. Remote direct sources remain HTTPS unless separately justified by product policy.

If A04 later requires IPv6 loopback, that is an explicit cross-lane policy update rather than silently widening the cleartext exception.
## Codec and fallback policy

Use `DefaultRenderersFactory.setEnableDecoderFallback(true)` so Media3 may try a lower-priority device decoder when the preferred codec initialization fails.

Do not bundle FFmpeg/software decoder extensions in the first implementation. Device codec capability is treated as truth, not hidden by CPU-heavy universal fallback.

Unsupported-container/codec failures normalize to the A02 decoder/source error family so the existing higher-level source retry UX can choose another candidate where one exists.

FIRST-APK qualification must record actual device support, not infer it from Media3's general format table. At minimum exercise:

- H.264/AAC MP4 progressive
- HLS with seeking and track selection
- DASH with seeking and track selection
- one MKV with multiple audio/subtitle tracks
- one SSA/ASS anime fixture
- one `content://` video supplied by A03
- one audiobook/local audio URI
- one A04 loopback range stream

Where the physical device advertises HEVC Main/Main10, VP9, or AV1, add representative samples and record observed results. H.264 Hi10P and other common fansub edge codecs are qualification probes, not promised baseline support.

## Media title and metadata

`mediaTitle` prefers current Media3 media metadata title when present and otherwise uses a safe source-derived fallback. Metadata updates for the same generation may update title without changing source identity.

Chapter and title metadata callbacks are generation guarded because in-stream metadata may arrive after source replacement.

## Future MediaSession compatibility

The Java player owner must not depend on a QML Item for audio-only mechanics other than lifecycle commands/state callbacks. This permits a future foreground service/MediaSession to host or wrap the same playback owner.

No MediaSession, notification transport controls, lock-screen controls, Android Auto, or background audiobook behavior is included or advertised now.
## Future implementation files

A02 implementation-worker ownership:

- `native/player/androidmedia3item.h`
- `native/player/androidmedia3item.cpp`
- `native/player/androidmedia3state.h`
- `native/player/androidmedia3state.cpp`
- `native/player/androidmedia3videonode.h`
- `native/player/androidmedia3videonode.cpp`
- `native/platform/android/src/org/colosseum/player/Media3PlayerHost.java`
- `native/platform/android/src/org/colosseum/player/Media3SurfaceBridge.java`
- `qml/Media3SubtitleOverlay.qml`
- `tests/android_media3_state_harness.cpp`
- `tests/test_android_media3_player_contract.py`

`androidmedia3state.*` contains platform-neutral generation/state normalization that can be compiled and tested on desktop without Media3 or Android. JNI and QSG ownership stay out of it.

`androidmedia3videonode.*` owns only scene-graph geometry/texture wrapping and SurfaceTexture transform application. It does not contain player state.

Lead integration ownership:

- `native/main.cpp`: Android `PlayerItem` registration, Android OpenGL scene-graph selection, and W02 lifecycle/surface hookup.
- `native/CMakeLists.txt`: Android source/build wiring and focused harness registration.
- `native/platform/android/build.gradle`: Media3 dependency pinning while preserving the Qt 6.11 template/AGP contract.
- `native/platform/android/AndroidManifest.xml`: network-security resource reference if not already supplied by another Android lane.
- `native/platform/android/res/xml/network_security_config.xml`: loopback-only cleartext exception.
- `qml/PlayerPage.qml`: mount `Media3SubtitleOverlay` and guard Android-unsupported subtitle styling.
- `qml/SubtitleMenu.qml` only if its public inputs need a minimal style-capability flag.

No A02 implementation worker independently owns broad `main.cpp`, CMake, manifest, Gradle, or `PlayerPage.qml` integration edits.
## Test architecture

Implementation is TDD-oriented. Every state/contract behavior that can be proven off-device closes RED -> GREEN before physical qualification.

`tests/test_android_media3_player_contract.py` statically verifies:

- Android builds register `AndroidMedia3Item` as `PlayerItem`; desktop still registers `MpvItem`.
- Android graph never requires MpvQt/libmpv.
- Media3 modules are pinned to one version.
- shared QML still instantiates `PlayerItem` and contains no Android player fork.
- required Android QML properties/methods/signals exist.
- provider headers pass through the neutral `loadSource(url, headers)` route.
- `subtitleStyling=false` cannot leave the style action exposed on Android.
- cleartext policy is narrow and does not globally enable HTTP.

`tests/android_media3_state_harness.cpp` deterministically verifies:

- source-generation stale callback rejection
- same-URL reload resets source-scoped state
- first-frame readiness reset/publish semantics
- fileStarted/fileLoaded/end/error one-shot mapping
- position/duration/buffering normalization
- seek lifecycle and timeout/error reset
- track opaque-id generation and selection mapping model
- chapter sort/dedupe/window-offset conversion
- capability truth
- subtitle attachment revisions preserve source identity

The state harness must not fake codec/surface success; those belong to Android runtime gates.

Before the full backend is built out, the implementation plan must contain an early physical vertical gate that proves Media3 -> `SurfaceTexture` -> ExternalOES -> Qt Quick composition with ordinary QML drawn above the video. If that gate fails on the target Qt 6.11/OpenGL stack, stop and re-review the surface architecture instead of implementing the remaining backend around an unproven compositor.

## Build gates

Before physical-device acceptance:

1. Existing desktop Player backend contract tests stay green.
2. Existing PlayerPage/AudiobookSession QML parse/lint remains green.
3. Desktop full app still links with MpvQt after shared-QML integration.
4. Android arm64 configure succeeds with no MpvQt/WebEngine dependency.
5. Android Java compilation resolves Media3 from Gradle at the pinned version.
6. Android `apk` target packages successfully and dependency inspection shows the required Media3 modules.
## Physical-device acceptance

A02 is not implementation-complete until a physical ARM64 Android device proves all of these through the production QML route:

1. Progressive HTTPS video renders a real frame and advances position.
2. Required Referer/Origin headers are observed on wire by a controlled HTTP fixture and do not leak into the next headerless source.
3. HLS and DASH start, seek, pause/resume, enumerate/select tracks, and recover from a surface recreation.
4. `content://` video plays without app-storage duplication; persisted SAF access survives process restart where A03 contract promises persistence.
5. A04 `127.0.0.1` dynamic-port range stream starts and supports forward/back seeks; requests include valid byte ranges. Device proof must also show Android cleartext policy permits `127.0.0.1` while a non-loopback HTTP control destination remains denied.
6. Rotation portrait -> landscape -> portrait preserves source, position, controls, and produces a new first-frame readiness event without black-video lockup.
7. Home/background for at least 30 seconds pauses under the approved no-background-playback policy and returns without stale surface or decoder loss.
8. Wired/Bluetooth noisy-output transition pauses as expected.
9. Audio-focus transient interruption/loss does not create dual focus owners or unauthorized auto-resume.
10. Multiple audio tracks and embedded subtitles enumerate and select correctly.
11. Online SRT/ASS attachment preserves source identity/position and renders through the Qt subtitle overlay.
12. Representative anime ASS cues are visually inspected for positioning/style, with unsupported advanced effects recorded rather than hidden.
13. Audiobook playback supports seek, speed, pause/resume, local/content URI load, foreground lifecycle, and progress callbacks without requiring a video surface.
14. Unsupported codec input produces bounded `decoder`/`source` error truth and leaves the UI/retry ladder responsive.
15. Stop/back navigation releases playback and surface resources without orphan audio.

For every video gate, acceptance requires decoded-frame truth (`decodedWidth > 0 && decodedHeight > 0`) in addition to page/open/load success.

Waydroid/emulators may assist compile/launch/QML smoke but do not qualify codec, OES composition, audio focus, Bluetooth/noisy behavior, or physical lifecycle gates.

## Cross-lane contract published to A03

A03 must provide URI strings that Android's ContentResolver can open under the permission lifetime A03 advertises. A02 requires no filesystem path and will pass the URI directly into Media3's `DefaultDataSource` graph.

A03 should surface MIME type when it knows it, but A02 does not require MIME to be encoded into a new player API; Media3/content resolver inference remains available. If an unusual provider needs explicit MIME to disambiguate playback, Lead may extend source metadata later without replacing the URI contract.
## Cross-lane contract published to A04

A04 must provide a complete playable URL using `http://127.0.0.1:<dynamic-port>/...` for FIRST-APK. The URL is ephemeral runtime transport identity, not a durable media identity.

For ordinary file playback the server must support Media3's byte-range behavior, including a truthful total length when known, valid `206 Partial Content` responses, correct `Content-Range`, and seekable reconnects. HEAD support is desirable where Media3/probes use it but GET+Range correctness is the hard playback contract.

A04 owns server startup/shutdown and URL invalidation. If a loopback URL expires because the server restarted, the higher source layer must request a fresh URL; A02 does not guess a replacement port.

A02 does not consume infoHash, fileIdx, torrent progress, libtorrent handles, or server internals.

## Rejected alternatives

### Native SurfaceView/TextureView beside Qt

Rejected as the primary composition path. It creates a second Android view compositor with z-order, overlay, rotation, and lifecycle coupling against Qt Quick. It would make Colosseum's existing QML chrome and loaders harder to preserve.

### CPU/GPU copied decoded frames into ordinary Qt textures

Rejected as the primary path. It adds bandwidth, latency, battery use, and extra frame ownership solely to avoid ExternalOES integration. Keep it only as an emergency future fallback if measured device evidence proves ExternalOES unviable.

### Kotlin-only host

Rejected for v1. Kotlin would work technically, but the current Android package already uses Java and A02 needs no Kotlin-specific feature. Java avoids adding language/plugin/build complexity just for this bridge.

### Media3 PlayerView / SubtitleView as the product UI

Rejected. Colosseum owns playback UI in QML. Media3 UI widgets would fork presentation and create cross-compositor ordering problems.

### Android libmpv

Rejected by the room's product decision and build graph. Media3 is Android's primary backend.

### Bundled FFmpeg decoder extensions immediately

Rejected for FIRST-APK. Start with platform MediaCodec truth plus Media3 decoder fallback, measure the device/content gaps, then make any software-codec cost decision from evidence.
## Plan-sealing conditions

The A02 implementation plan may be drafted after this spec is approved, but it is not final until the latest A03 and A04 result packets are checked against these URI and loopback contracts.

If A03 or A04 returns an incompatible source requirement, resolve the cross-lane contract with Lead rather than adding storage/server knowledge inside A02.

No implementation worker launches until Lead cross-reviews all four Android Media Runtime specs.

## Authoritative external references checked 2026-09-03

- Media3 1.11.0 release notes: https://developer.android.com/jetpack/androidx/releases/media3
- Media3 source surface API: https://developer.android.com/reference/androidx/media3/common/Player
- Media3 surface choices: https://developer.android.com/media/media3/ui/surface
- Media3 DefaultDataSource URI schemes: https://developer.android.com/reference/androidx/media3/datasource/DefaultDataSource
- Media3 HTTP request headers: https://developer.android.com/reference/androidx/media3/datasource/DefaultHttpDataSource
- Media3 supported formats/subtitles: https://developer.android.com/media/media3/exoplayer/supported-formats
- Media3 standalone subtitle configuration: https://developer.android.com/reference/androidx/media3/common/MediaItem.SubtitleConfiguration
- Media3 cue model: https://developer.android.com/reference/androidx/media3/common/text/Cue
- Media3 chapter metadata: https://developer.android.com/reference/androidx/media3/extractor/metadata/Chapter
- Media3 track selection: https://developer.android.com/media/media3/exoplayer/track-selection
- Media3 audio focus/noisy builder APIs: https://developer.android.com/reference/androidx/media3/exoplayer/ExoPlayer.Builder
- Media3 decoder fallback: https://developer.android.com/reference/androidx/media3/exoplayer/DefaultRenderersFactory
- Qt ExternalOES QSG texture adoption: https://doc.qt.io/qt-6/qnativeinterface-qsgopengltexture.html
- Qt Android custom package template: https://doc.qt.io/qt-6/cmake-target-property-qt-android-package-source-dir.html
- Qt Android deployment/Gradle customization: https://doc.qt.io/qt-6/deployment-android.html
- Android Network Security Configuration: https://developer.android.com/privacy-and-security/security-config

## Completion boundary

This document is architecture only. It authorizes no product-code mutation by the A02 architecture lane. After user review, the next allowed artifact is the Superpowers TDD implementation plan. Actual backend implementation belongs to a later implementation worker after Lead approves the four-lane architecture package.
