# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/player-state.json`

## Summary

- Total files: **26**
- Documented: **10**
- Undocumented: **16**
- Drifted: **0**

<a id="file-native-player-caststore-cpp"></a>
## `native/player/caststore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `06a0960be2050e2a1c27195542ed2607ad254cf9`
- Current blob: `06a0960be2050e2a1c27195542ed2607ad254cf9`
- Source: [`native/player/caststore.cpp`](../../native/player/caststore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-caststore-h"></a>
## `native/player/caststore.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `abe42c639579029dfd57f2176489d2e5757970bc`
- Current blob: `abe42c639579029dfd57f2176489d2e5757970bc`
- Source: [`native/player/caststore.h`](../../native/player/caststore.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-downloadstore-cpp"></a>
## `native/player/downloadstore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `8285294f03c6c068d2fd04b088990b1e6816b19f`
- Current blob: `8285294f03c6c068d2fd04b088990b1e6816b19f`
- Source: [`native/player/downloadstore.cpp`](../../native/player/downloadstore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-downloadstore-h"></a>
## `native/player/downloadstore.h`

- Status: **CURRENT**
- Accepted blob: `651c8f0debb5b93d9b3f96c420d34a91799b5d99`
- Current blob: `651c8f0debb5b93d9b3f96c420d34a91799b5d99`
- Source: [`native/player/downloadstore.h`](../../native/player/downloadstore.h)

```text
// DownloadStore — the Theatre lane's video download engine.
// v2 (2026-07-04): bounded job QUEUE with lazy per-job stream resolution — TB2's
// proven gap-episode flow at Harbor's simplicity. A job's durable payload is the
// episode's stream id (tt…:s:e), which never expires: resolution to a concrete
// URL happens only when the job is promoted (needResolve → feedUrl handshake with
// the QML resolver), so retry is always honest. Cap = MAX_ACTIVE_VIDEO.
// Finished files land in the persisted library index (videos/index.json);
// the in-flight queue survives restarts via videos/queue.json.
```

<a id="file-native-player-livestore-cpp"></a>
## `native/player/livestore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `6c7e3fc14aa3892efb5032829d269233a0d22361`
- Current blob: `6c7e3fc14aa3892efb5032829d269233a0d22361`
- Source: [`native/player/livestore.cpp`](../../native/player/livestore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-livestore-h"></a>
## `native/player/livestore.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `0780e44750f711a4c88223162dbfca18acab6e3d`
- Current blob: `0780e44750f711a4c88223162dbfca18acab6e3d`
- Source: [`native/player/livestore.h`](../../native/player/livestore.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-mpvitem-cpp"></a>
## `native/player/mpvitem.cpp`

- Status: **CURRENT**
- Accepted blob: `c84f570052ac97df83e099799ffb72cd29038509`
- Current blob: `c84f570052ac97df83e099799ffb72cd29038509`
- Source: [`native/player/mpvitem.cpp`](../../native/player/mpvitem.cpp)

```text
// MpvItem implementation — lifted 1:1 from KDE mpvqt's video-player example.
```

<a id="file-native-player-mpvitem-h"></a>
## `native/player/mpvitem.h`

- Status: **CURRENT**
- Accepted blob: `48d772f50ca8abb822fc99e26ac6a65d6a3179fa`
- Current blob: `48d772f50ca8abb822fc99e26ac6a65d6a3179fa`
- Source: [`native/player/mpvitem.h`](../../native/player/mpvitem.h)

```text
// MpvItem — the playable mpv surface, a QQuickItem subclass of MpvQt's MpvAbstractItem.
// Lifted 1:1 from KDE mpvqt's video-player example (proven to play real video on our
// Qt 6.11 / MinGW), with one change: QML_ELEMENT is removed. Colosseum loads its QML
// live from disk (no qt_add_qml_module), so the type is registered by hand in main.cpp
//   qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
// and reached from QML with `import Colosseum.Player`.
```

<a id="file-native-player-mpvproperties-h"></a>
## `native/player/mpvproperties.h`

- Status: **CURRENT**
- Accepted blob: `400e2ce738cc41fcab25d28cbf2c58acb36c034f`
- Current blob: `400e2ce738cc41fcab25d28cbf2c58acb36c034f`
- Source: [`native/player/mpvproperties.h`](../../native/player/mpvproperties.h)

```text
// mpv property-name constants, lifted from KDE's mpvqt video-player example.
// Plain C++ helper (no Q_OBJECT / QML macros) — Colosseum only touches these from
// MpvItem.cpp, never from QML, so it needs no moc and no qml registration.
```

<a id="file-native-player-powerstore-cpp"></a>
## `native/player/powerstore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `f9d44db5119a95ca0fed489266247ee1b8be2e5d`
- Current blob: `f9d44db5119a95ca0fed489266247ee1b8be2e5d`
- Source: [`native/player/powerstore.cpp`](../../native/player/powerstore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-powerstore-h"></a>
## `native/player/powerstore.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `d35d72125d8a642e4d71b308f73b00a75bcd5515`
- Current blob: `d35d72125d8a642e4d71b308f73b00a75bcd5515`
- Source: [`native/player/powerstore.h`](../../native/player/powerstore.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-roomstore-cpp"></a>
## `native/player/roomstore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `ff172eda08cd25b36dd5c89985d2c34dbf724acd`
- Current blob: `ff172eda08cd25b36dd5c89985d2c34dbf724acd`
- Source: [`native/player/roomstore.cpp`](../../native/player/roomstore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-roomstore-h"></a>
## `native/player/roomstore.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `6c5f1f95120707b29e5ff5487fffa2eb2b620308`
- Current blob: `6c5f1f95120707b29e5ff5487fffa2eb2b620308`
- Source: [`native/player/roomstore.h`](../../native/player/roomstore.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-seekthumbnailer-cpp"></a>
## `native/player/seekthumbnailer.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `0e79d889613e769f7578ea7eae92696522517d84`
- Current blob: `0e79d889613e769f7578ea7eae92696522517d84`
- Source: [`native/player/seekthumbnailer.cpp`](../../native/player/seekthumbnailer.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-seekthumbnailer-h"></a>
## `native/player/seekthumbnailer.h`

- Status: **CURRENT**
- Accepted blob: `635103fa5163ef6fedbcc7b13006e065e2d3ab3a`
- Current blob: `635103fa5163ef6fedbcc7b13006e065e2d3ab3a`
- Source: [`native/player/seekthumbnailer.h`](../../native/player/seekthumbnailer.h)

```text
// Seek-bar hover thumbnails (F9): one short-lived ffmpeg per hovered 5s bucket,
// latest-wins, LRU-cached as data: URLs. QML paints the tooltip; this owns the
// process transport and cache (QML-paints/C++-decides doctrine).
```

<a id="file-native-player-streamserver-cpp"></a>
## `native/player/streamserver.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `061b8abbc5275a8cba8ce5ec19fb1d6d6aa29f01`
- Current blob: `061b8abbc5275a8cba8ce5ec19fb1d6d6aa29f01`
- Source: [`native/player/streamserver.cpp`](../../native/player/streamserver.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-streamserver-h"></a>
## `native/player/streamserver.h`

- Status: **CURRENT**
- Accepted blob: `3ad759afedc30be111b16e5732f5671703aa80a4`
- Current blob: `3ad759afedc30be111b16e5732f5671703aa80a4`
- Source: [`native/player/streamserver.h`](../../native/player/streamserver.h)

```text
// StreamServer — turns a torrent (infoHash + fileIdx) into a localhost HTTP URL mpv can play.
//
// It does NOT reimplement torrent streaming: it runs Tankoban 2's proven Stremio
// stream-server (`stremio-runtime.exe server.js`) as a child process, the same way TB2
// itself does. The runtime binds http://127.0.0.1:<port>/<infoHash>/<fileIdx> and we
// surface that URL to QML.
//
// Lifecycle: lazy — the 88 MB runtime is only spawned on the FIRST play() call, so a
// session that never watches anything never pays for it. Killed on app exit.
//
// QML contract (exposed as the context property `Stream`):
//   Stream.play(infoHash, fileIdx)      -> eventually emits streamReady(url, infoHash, fileIdx)
//   Stream.ready                         -> bool, true once the runtime's port is known
//   onStreamReady(url, infoHash, idx)    -> hand `url` to MpvItem.loadFile(url)
//   onStreamError(message)               -> show the message; playback won't start
```

<a id="file-native-player-windowmodestore-cpp"></a>
## `native/player/windowmodestore.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `9d08bf633f40e1263371d5dfbc6206b752c84000`
- Current blob: `9d08bf633f40e1263371d5dfbc6206b752c84000`
- Source: [`native/player/windowmodestore.cpp`](../../native/player/windowmodestore.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-windowmodestore-h"></a>
## `native/player/windowmodestore.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `f4336ec7be0cd0ba7823ad2a858ea2d47c849c80`
- Current blob: `f4336ec7be0cd0ba7823ad2a858ea2d47c849c80`
- Source: [`native/player/windowmodestore.h`](../../native/player/windowmodestore.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-windowstatepolicy-cpp"></a>
## `native/player/windowstatepolicy.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `65eedff6d58975e651dc76335093717ea3edf48e`
- Current blob: `65eedff6d58975e651dc76335093717ea3edf48e`
- Source: [`native/player/windowstatepolicy.cpp`](../../native/player/windowstatepolicy.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-player-windowstatepolicy-h"></a>
## `native/player/windowstatepolicy.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `f5e8fe345a17094574953066a33633464e00705c`
- Current blob: `f5e8fe345a17094574953066a33633464e00705c`
- Source: [`native/player/windowstatepolicy.h`](../../native/player/windowstatepolicy.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-qml-playerhotkeys-js"></a>
## `qml/PlayerHotkeys.js`

- Status: **CURRENT**
- Accepted blob: `5129ce0402582f010260bdcbeed76a631fbaf3db`
- Current blob: `5129ce0402582f010260bdcbeed76a631fbaf3db`
- Source: [`qml/PlayerHotkeys.js`](../../qml/PlayerHotkeys.js)

```text
// Pure player hotkey registry (Feature 7). Centralizes shortcut metadata, string/event lookup,
// conflict detection, and grouping for the shortcuts sheet. No QML `Qt` global is used: key
// events are matched by numeric Qt::Key codes (stable ABI) so this library is fully testable
// headless with synthetic events. PlayerPage owns the ACTUAL behavior; this only resolves ids.
```

<a id="file-qml-playerloadingscreen-qml"></a>
## `qml/PlayerLoadingScreen.qml`

- Status: **CURRENT**
- Accepted blob: `e1a8d737932e7770bcba6e361136fcb0978bce0d`
- Current blob: `e1a8d737932e7770bcba6e361136fcb0978bce0d`
- Source: [`qml/PlayerLoadingScreen.qml`](../../qml/PlayerLoadingScreen.qml)

```text
// Stremio-style per-show startup loader. Reference = Stremio's Player (stremio-web): a FULL-BLEED
// backdrop shown CLEARLY (not blurred; only subtle top/bottom gradients) with the show's CINEMETA
// LOGO — the stylized title art — centered as the hero. Text is only a fallback when no logo loads.
// Beneath: the episode line, a status line, and a thin INDETERMINATE bar. Colosseum exposes no
// trustworthy torrent readiness figure, so the bar only sweeps — it NEVER shows a fabricated number.
//
// Owned/fed by PlayerPage: `active` gates everything; PlayerPage flips it off on the truthful
// first-frame advance (or when the resume-choice prompt takes over). Art decode runs ONLY while
// active. Harbor shows the same logo with no backdrop; we keep Stremio's backdrop.
```

<a id="file-qml-playerpage-qml"></a>
## `qml/PlayerPage.qml`

- Status: **CURRENT**
- Accepted blob: `749d47ffc5a085708c8720188d549fe17f339818`
- Current blob: `749d47ffc5a085708c8720188d549fe17f339818`
- Source: [`qml/PlayerPage.qml`](../../qml/PlayerPage.qml)

```text
// PlayerPage - Harbor/TB3-style fullscreen player chrome on top of Colosseum's mpvqt MpvItem.
// Streaming remains behind the Stream.play -> streamReady seam; this file only owns player UI.
```

<a id="file-qml-playertrackprefs-js"></a>
## `qml/PlayerTrackPrefs.js`

- Status: **UNDOCUMENTED**
- Accepted blob: `c41ed4445e0757e3b294382de5ac26efb78d76ea`
- Current blob: `c41ed4445e0757e3b294382de5ac26efb78d76ea`
- Source: [`qml/PlayerTrackPrefs.js`](../../qml/PlayerTrackPrefs.js)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-qml-theatreapi-js"></a>
## `qml/TheatreApi.js`

- Status: **CURRENT**
- Accepted blob: `ede8eb724c1661f97cdd16c6b00a734253c8ef5d`
- Current blob: `ede8eb724c1661f97cdd16c6b00a734253c8ef5d`
- Source: [`qml/TheatreApi.js`](../../qml/TheatreApi.js)

```text
// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Cinemeta is the identity source for movies, series, and anime-shaped series rows.
// Extensions (spec Phase 3): installed catalog extensions add THEIR shelves to the
// tab pages after the house rows, and answer meta asks the house sources can't.
// A .pragma library can't see context properties, so Main.qml pushes the installed
// list in via setExtensions() at boot and on every registry change.
```
