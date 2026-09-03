# Android Media3 PlayerItem Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship one Android `PlayerItem` backend powered by AndroidX Media3/ExoPlayer that preserves Colosseum's existing shared PlayerPage/AudiobookSession contract while desktop remains on MpvItem.

**Architecture:** `AndroidMedia3Item` is the Qt/QML-facing `QQuickItem`; a Java `Media3PlayerHost` owns ExoPlayer on Android's main looper; platform-neutral `AndroidMedia3State` owns generation/state normalization. Video reaches Qt Quick through a caller-owned `SurfaceTexture`/`GL_TEXTURE_EXTERNAL_OES` path, and Media3 subtitle cues are rendered by a thin shared QML overlay.

**Tech Stack:** Qt 6.11 C++17/QML, Qt JNI (`QJniObject`), Android Java, AndroidX Media3 1.11.0, OpenGL ES ExternalOES, CMake/Gradle, Python contract tests, Qt host harnesses, ADB physical-device qualification.

**Spec:** `docs/superpowers/specs/2026-09-03-android-media3-player-design.md`

## Global Constraints

- Desktop playback remains `MpvItem`/MpvQt/libmpv; do not alter its product behavior.
- Android primary playback is Media3/ExoPlayer 1.11.0; no Android libmpv fallback.
- Preserve one QML type identity: `Colosseum.Player 1.0 / PlayerItem`.
- Reuse `qml/PlayerPage.qml` and `qml/AudiobookSession.qml`; no Android player UI fork.
- Android min API 28, compile/target API 36, arm64-v8a first.
- No background playback service, MediaSession notification, PiP, FFmpeg decoder extension, Android WebEngine, Node/Stremio server, or PDF work.
- `content://` is first-class and zero-copy by default.
- A04 v1 transport is dynamic numeric IPv4 loopback: `http://127.0.0.1:<port>/...`; never assume or persist a port.
- Cleartext remains denied globally; only `127.0.0.1` may receive a narrow Android network-security exception.
- Unsupported mpv-only features report `false`; do not manufacture parity.
- Every implementation task starts RED, ends GREEN, and ends with a focused commit.
- Stop after Task 6 if the physical ExternalOES composition gate fails; return evidence to Lead instead of implementing a replacement compositor.
- Final implementation launch remains blocked until Lead has cross-reviewed all four Android Media Runtime plans.

---

### Task 1: Platform-neutral Media3 state core

**Files:**
- Create: `native/player/androidmedia3state.h`
- Create: `native/player/androidmedia3state.cpp`
- Create: `tests/android_media3_state_harness.cpp`
- Modify: `native/CMakeLists.txt` only to register the desktop-buildable state harness.

**Interfaces:**
- Produces: `Colosseum::Player::AndroidMedia3State`, consumed by `AndroidMedia3Item` in Task 3.
- Produces: monotonically increasing `quint64 generation()` and `bool accepts(quint64 generation) const` stale-callback guard.
- Produces: source/timeline/readiness methods shown below; no Android or QSG headers enter this class.

- [ ] **Step 1: Write the failing state harness**

```cpp
AndroidMedia3State state;
const auto g1 = state.beginLoad("https://example.invalid/a.mp4", {{"Referer", "https://a.invalid/"}});
Q_ASSERT(state.accepts(g1));
Q_ASSERT(state.snapshot().decodedWidth == 0);
Q_ASSERT(state.markReady(g1));
state.noteVideoSize(g1, 1920, 1080);
Q_ASSERT(state.snapshot().decodedWidth == 0);
Q_ASSERT(state.markFirstFrame(g1));
Q_ASSERT(state.snapshot().decodedWidth == 1920);
const auto g2 = state.beginLoad("https://example.invalid/a.mp4", {});
Q_ASSERT(g2 > g1 && !state.accepts(g1));
Q_ASSERT(state.snapshot().headers.isEmpty());
Q_ASSERT(state.snapshot().decodedWidth == 0);
```

Cover in the same harness: same-URL reload reset, one-shot ready/end/error flags, stale generations, surface-loss readiness reset, timeline normalization, and capability defaults.

- [ ] **Step 2: Run the harness and verify RED**

Run:
```powershell
cmake --build C:\b\a02-media3-host --target android_media3_state_harness --parallel 1
C:\b\a02-media3-host\android_media3_state_harness.exe
```
Expected: compile/link failure because `AndroidMedia3State` does not exist yet.

- [ ] **Step 3: Implement the minimal state API**

```cpp
struct AndroidMedia3Snapshot {
    QString currentUrl;
    QVariantMap headers;
    double positionSec = 0.0, durationSec = 0.0, bufferedSec = 0.0;
    double bufferingPercent = -1.0;
    int decodedWidth = 0, decodedHeight = 0;
    bool paused = true, muted = false, seeking = false, ready = false;
    int volume = 100;
    double speed = 1.0;
};

class AndroidMedia3State final {
public:
    quint64 beginLoad(const QString &url, const QVariantMap &headers);
    quint64 generation() const;
    bool accepts(quint64 generation) const;
    bool markReady(quint64 generation);
    void noteVideoSize(quint64 generation, int width, int height);
    bool markFirstFrame(quint64 generation);
    void resetVideoSurface();
    bool markEnded(quint64 generation);
    bool markError(quint64 generation);
    const AndroidMedia3Snapshot &snapshot() const;
};
```The state class also owns the fixed Android capability map. It must return `subtitleCueOverlay=true` and the eleven approved mpv-only capabilities as `false`.

- [ ] **Step 4: Run the focused state checks GREEN**

Run:
```powershell
cmake --build C:\b\a02-media3-host --target android_media3_state_harness --parallel 1
C:\b\a02-media3-host\android_media3_state_harness.exe
python tests/test_player_backend_contract.py
```
Expected: state harness prints `ANDROID_MEDIA3_STATE_OK`; existing desktop player contract remains PASS.

- [ ] **Step 5: Commit**

```bash
git add native/player/androidmedia3state.h native/player/androidmedia3state.cpp \
        tests/android_media3_state_harness.cpp native/CMakeLists.txt
git commit -m "test(android): define Media3 player state core"
```

### Task 2: Timeline, seeking, tracks, chapters, and cue normalization

**Files:**
- Modify: `native/player/androidmedia3state.h`
- Modify: `native/player/androidmedia3state.cpp`
- Modify: `tests/android_media3_state_harness.cpp`

**Interfaces:**
- Consumes: Task 1 `AndroidMedia3State` generation ownership.
- Produces: normalized `QVariantList audioTracks()`, `subtitleTracks()`, `chapters()`, `subtitleCues()` and exact seek lifecycle used by Tasks 3 and 6.

- [ ] **Step 1: Extend the harness RED for timeline and seek semantics**

```cpp
state.updateTimeline(g, 12'500, 100'000, 25'000, 25.0, true, 90, false, 1.25);
Q_ASSERT(qFuzzyCompare(state.snapshot().positionSec, 12.5));
Q_ASSERT(state.snapshot().bufferingPercent == 25.0);
state.beginSeek(g, 42'000, 1'000);
Q_ASSERT(state.snapshot().seeking);
state.noteSeekDiscontinuity(g, 41'900);
state.notePlayerReady(g, 41'950);
Q_ASSERT(!state.snapshot().seeking);

state.beginSeek(g, 80'000, 2'000);
Q_ASSERT(state.expireSeek(5'001));
Q_ASSERT(!state.snapshot().seeking);
```

Add assertions that stale seek events do nothing and source replacement clears a pending seek.

- [ ] **Step 2: Add RED normalization fixtures for track/chapter/cue data**

```cpp
state.replaceTracks(g, {
    {"a:0:0", "Japanese", "ja", true, false, false},
    {"s:1:0", "English", "en", false, false, false}
});
Q_ASSERT(state.audioTracks().size() == 1);
Q_ASSERT(state.subtitleTracks().size() == 1);

state.replaceChapters(g, {{"Opening", 90.0}, {"Opening", 90.0}, {"Part A", 120.0}});
Q_ASSERT(state.chapters().size() == 2);
```Use plain transport structs in the state layer, not Media3 Java types:

```cpp
struct Media3TrackRow { QString id, title, lang; bool selected, external, forced; };
struct Media3ChapterRow { QString title; double startSec; };
struct Media3CueRow { QString text; double position, line, size; QString alignment; QVariantList spans; };

void updateTimeline(quint64 g, qint64 positionMs, qint64 durationMs, qint64 bufferedMs,
                    double bufferedPercent, bool paused, int volume, bool muted, double speed);
void beginSeek(quint64 g, qint64 targetMs, qint64 nowMs);
void noteSeekDiscontinuity(quint64 g, qint64 acceptedPositionMs);
void notePlayerReady(quint64 g, qint64 positionMs);
bool expireSeek(qint64 nowMs);
void replaceTracks(quint64 g, const QList<Media3TrackRow> &tracks);
void replaceChapters(quint64 g, const QList<Media3ChapterRow> &chapters);
void replaceSubtitleCues(quint64 g, const QList<Media3CueRow> &cues);
```

Seek clears when discontinuity has arrived and the accepted/current position is within 500 ms, or when READY follows the discontinuity; timeout is exactly 3000 ms.

- [ ] **Step 3: Run RED, implement the normalization, rerun GREEN**

Run:
```powershell
cmake --build C:\b\a02-media3-host --target android_media3_state_harness --parallel 1
C:\b\a02-media3-host\android_media3_state_harness.exe
```
Expected before implementation: assertion/compile failure. Expected after: `ANDROID_MEDIA3_STATE_OK`.

- [ ] **Step 4: Commit**

```bash
git add native/player/androidmedia3state.* tests/android_media3_state_harness.cpp
git commit -m "feat(android): normalize Media3 playback state"
```

### Task 3: Java Media3 player host and immutable source graph

**Files:**
- Create: `native/platform/android/src/org/colosseum/player/Media3PlayerHost.java`
- Create: `tests/test_android_media3_player_contract.py`

**Interfaces:**
- Consumes: native handle + source generation from future `AndroidMedia3Item`.
- Produces Java methods: `create(long)`, `load(long,String,Map<String,String>)`, `play`, `pause`, `seekTo`, `setVolume`, `setMuted`, `setSpeed`, `selectTrack`, `addSubtitle`, `setVideoSurface`, `clearVideoSurface`, `setHostLifecycleState`, `release`.
- Produces generation-tagged native callbacks; every callback includes `nativeHandle` and `generation`.

- [ ] **Step 1: Write the static contract test RED**

```python
HOST = (ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java").read_text()
require(HOST, "androidx.media3.exoplayer.ExoPlayer", "Media3 host must own ExoPlayer")
require(HOST, "DefaultHttpDataSource.Factory", "HTTP path must support per-source headers")
require(HOST, "DefaultDataSource.Factory", "content/file/http schemes share one graph")
require(HOST, "DefaultMediaSourceFactory", "Media3 must infer progressive/HLS/DASH")
require(HOST, "setAudioAttributes", "Media3 owns Android audio focus")
require(HOST, "setHandleAudioBecomingNoisy(true)", "headset unplug must pause")
require(HOST, "setEnableDecoderFallback(true)", "device decoder fallback must be enabled")
forbid(HOST, "static ExoPlayer", "do not create a process-global player singleton")
```

Also assert the source build copies incoming headers and never stores them in a static/global request-properties map.

- [ ] **Step 2: Run RED**

Run:
```powershell
python tests/test_android_media3_player_contract.py
```
Expected: failure because `Media3PlayerHost.java` does not exist.

- [ ] **Step 3: Implement the host skeleton and source factory**

```java
public final class Media3PlayerHost implements Player.Listener {
    private final long nativeHandle;
    private final Handler main = new Handler(Looper.getMainLooper());
    private ExoPlayer player;
    private long generation;

    private MediaSource buildSource(String url, Map<String,String> headers) {
        Map<String,String> copy = new HashMap<>(headers);
        DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory();
        http.setDefaultRequestProperties(copy);
        DefaultDataSource.Factory data = new DefaultDataSource.Factory(context, http);
        DefaultMediaSourceFactory media = new DefaultMediaSourceFactory(data);
        return media.createMediaSource(new MediaItem.Builder().setUri(Uri.parse(url)).build());
    }
}
```

Construct ExoPlayer with `DefaultRenderersFactory(context).setEnableDecoderFallback(true)`, `setAudioAttributes(AudioAttributes.DEFAULT, true)`, and `setHandleAudioBecomingNoisy(true)`. All public methods call `main.post(...)` unless already on the main looper.Define these native callback signatures once and keep them stable through the remaining tasks:

```java
private static native void nativeOnReady(long handle, long generation);
private static native void nativeOnEnded(long handle, long generation);
private static native void nativeOnError(long handle, long generation, String code, String message);
private static native void nativeOnTimeline(long handle, long generation,
        long positionMs, long durationMs, long bufferedMs, int bufferedPercent,
        boolean paused, int volume, boolean muted, float speed);
private static native void nativeOnVideoSize(long handle, long generation, int width, int height);
private static native void nativeOnFirstFrame(long handle, long generation);
private static native void nativeOnSeekDiscontinuity(long handle, long generation, long positionMs);
private static native void nativeOnTracks(long handle, long generation, String json);
private static native void nativeOnMetadata(long handle, long generation, String json);
private static native void nativeOnSubtitleCues(long handle, long generation, String json);
```

Use bounded error families `network|http|source|permission|decoder|render|unknown`; Java maps Media3 error codes into those names before crossing JNI.

- [ ] **Step 4: Verify the contract GREEN**

Run: `python tests/test_android_media3_player_contract.py`
Expected: `ANDROID_MEDIA3_PLAYER_CONTRACT_OK`.

- [ ] **Step 5: Commit**

```bash
git add native/platform/android/src/org/colosseum/player/Media3PlayerHost.java \
        tests/test_android_media3_player_contract.py
git commit -m "feat(android): add Media3 Java player host"
```

### Task 4: Android PlayerItem facade and JNI state bridge

**Files:**
- Create: `native/player/androidmedia3item.h`
- Create: `native/player/androidmedia3item.cpp`
- Modify: `tests/test_android_media3_player_contract.py`

**Interfaces:**
- Consumes: `AndroidMedia3State` from Tasks 1-2 and `Media3PlayerHost` from Task 3.
- Produces: QML-compatible `AndroidMedia3Item : QQuickItem, PlayerBackendContract` used by Lead integration in Task 6.

- [ ] **Step 1: Extend the contract test RED for the full QML surface**

Require the header to contain these properties exactly:

```text
capabilities mediaTitle currentUrl position duration formattedPosition formattedDuration
pause volume mute speed audioTrack subtitleTrack audioTracks subtitleTracks chapters
decodedWidth decodedHeight cacheTime cacheBufferingState coreSeeking
audioDelay subDelay panscan videoZoom videoAspect gifEncoding subtitleCues
```

Require these invokables exactly:

```text
loadSource stopPlayback setHostLifecycleState setAudioFocusState
releaseVideoSurface restoreVideoSurface seekExact seekStep addSubtitle
applyPlaybackProfile refreshAudioOutput playbackStat frameStep frameBackStep
setSubOption setAudioNormalization captureFrame revealCaptureFolder
startGifRecording stopGifRecording abortGifRecording
```

Require `public PlayerBackendContract`, `QQuickItem`, and all existing PlayerPage signals: `fileStarted`, `fileLoaded`, `endFile`, `playbackError`, `videoReconfig`, `decodedDimensionsChanged`, plus property notifications and `subtitleCuesChanged`.

- [ ] **Step 2: Run RED**

Run: `python tests/test_android_media3_player_contract.py`
Expected: missing Android item/header assertions fail.

- [ ] **Step 3: Implement `AndroidMedia3Item` with cached getters**

```cpp
class AndroidMedia3Item final : public QQuickItem, public PlayerBackendContract {
    Q_OBJECT
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    Q_PROPERTY(QVariantList subtitleCues READ subtitleCues NOTIFY subtitleCuesChanged)
public:
    explicit AndroidMedia3Item(QQuickItem *parent = nullptr);
    ~AndroidMedia3Item() override;
    QVariantMap capabilities() const override;
    Q_INVOKABLE void loadSource(const QString &url);
    Q_INVOKABLE void loadSource(const QString &url, const QVariantMap &headers) override;
};
```The item must never hand Java a raw C++ pointer as a lifetime guarantee. Register a monotonically increasing `quint64` callback token in a mutex-protected `QHash<quint64,QPointer<AndroidMedia3Item>>`; JNI callbacks resolve the token and drop it if the QPointer is null.

`loadSource` performs this order:

```cpp
const QVariantMap safe = validatedHeaders(headers); // reject CR/LF name/value pairs
const quint64 g = m_state.beginLoad(url, safe);
setCurrentUrl(QUrl::fromUserInput(url));
emit fileStarted();
callJavaLoad(g, url, safe);
```

Every JNI callback uses `QMetaObject::invokeMethod(item, ..., Qt::QueuedConnection)` before touching state/QML. After queuing, re-check `m_state.accepts(generation)` on the Qt object thread.

Signal mapping is exact:
- first accepted Java READY -> `fileLoaded()` once;
- first accepted ENDED -> `endFile("eof")` once;
- accepted error -> `endFile("error")` first, then `playbackError(code,message)`;
- first frame -> publish decoded dimensions and `decodedDimensionsChanged()`;
- new load/surface loss -> dimensions reset to zero before any new first-frame event.

Compatibility methods for capabilities that are false return neutral values and never mutate Media3. `captureFrame()` returns empty string, `startGifRecording()` returns false, `playbackStat()` returns invalid `QVariant`, and `gifEncoding` remains false.

- [ ] **Step 4: Verify static contract GREEN**

Run:
```powershell
python tests/test_android_media3_player_contract.py
python tests/test_player_backend_contract.py
```
Expected: both PASS; desktop neutral contract remains unchanged.

- [ ] **Step 5: Commit**

```bash
git add native/player/androidmedia3item.h native/player/androidmedia3item.cpp \
        tests/test_android_media3_player_contract.py
git commit -m "feat(android): add Media3 PlayerItem facade"
```

### Task 5: ExternalOES video surface bridge and Qt Quick node

**Files:**
- Create: `native/player/androidmedia3videonode.h`
- Create: `native/player/androidmedia3videonode.cpp`
- Create: `native/platform/android/src/org/colosseum/player/Media3SurfaceBridge.java`
- Modify: `native/player/androidmedia3item.h`
- Modify: `native/player/androidmedia3item.cpp`
- Modify: `tests/test_android_media3_player_contract.py`**Interfaces:**
- Consumes: `Media3PlayerHost.setVideoSurface(Surface)` / `clearVideoSurface(Surface)`.
- Produces: one surface generation per OES texture; frame-available callbacks never carry source state.
- Produces: `AndroidMedia3VideoNode::syncFrame(...)` on the Qt render thread.

- [ ] **Step 1: Extend the contract test RED for ExternalOES ownership**

Require these implementation facts:

```python
require(VIDEO_CPP, "fromNativeExternalOES", "Qt must adopt the OES texture directly")
require(VIDEO_CPP, "GL_TEXTURE_EXTERNAL_OES", "video texture target must be ExternalOES")
require(SURFACE_JAVA, "new SurfaceTexture(textureId)", "Media3 Surface must target Qt's OES texture")
require(SURFACE_JAVA, "new Surface(surfaceTexture)", "ExoPlayer must receive a caller-owned Surface")
require(SURFACE_JAVA, "setOnFrameAvailableListener", "new decoded frames must schedule Qt updates")
require(SURFACE_JAVA, "getTransformMatrix", "SurfaceTexture UV transform must be honored")
```

- [ ] **Step 2: Run RED**

Run: `python tests/test_android_media3_player_contract.py`
Expected: ExternalOES contract assertions fail.

- [ ] **Step 3: Implement the Java surface wrapper**

```java
public final class Media3SurfaceBridge {
    private final SurfaceTexture surfaceTexture;
    private final Surface surface;
    private final long nativeHandle, surfaceGeneration;

    public Media3SurfaceBridge(int textureId, long handle, long generation) {
        nativeHandle = handle;
        surfaceGeneration = generation;
        surfaceTexture = new SurfaceTexture(textureId);
        surface = new Surface(surfaceTexture);
        surfaceTexture.setOnFrameAvailableListener(
            st -> nativeOnFrameAvailable(nativeHandle, surfaceGeneration),
            new Handler(Looper.getMainLooper()));
    }
    public Surface surface() { return surface; }
    public void updateTexImage() { surfaceTexture.updateTexImage(); }
    public void getTransformMatrix(float[] out16) { surfaceTexture.getTransformMatrix(out16); }
}
````release()` must remove the listener, release `Surface` first, then release `SurfaceTexture`. Do not call it until `Media3PlayerHost.clearVideoSurface(surface)` has run on Android's main looper.

- [ ] **Step 4: Implement the render-thread node**

```cpp
class AndroidMedia3VideoNode final : public QSGGeometryNode {
public:
    AndroidMedia3VideoNode(QQuickWindow *window, GLuint textureId, const QSize &size);
    ~AndroidMedia3VideoNode() override;
    void syncFrame(const QJniObject &surfaceBridge, const QRectF &targetRect,
                   const QSize &videoSize, bool framePending);
private:
    std::unique_ptr<QSGTexture> m_texture;
    QSGGeometry m_geometry{QSGGeometry::defaultAttributes_TexturedPoint2D(), 4};
    QSGTextureMaterial m_material;
};
```

Create the wrapper only on the scene-graph render thread:

```cpp
m_texture.reset(QNativeInterface::QSGOpenGLTexture::fromNativeExternalOES(
    textureId, window, size));
m_material.setTexture(m_texture.get());
setGeometry(&m_geometry);
setMaterial(&m_material);
```

When `framePending` is true, call Java `updateTexImage()` on this render thread, obtain the 4x4 transform matrix, transform the four `(u,v)` corners, and update `QSGGeometry::TexturedPoint2D` vertices. Compute an aspect-fit `targetRect` from the latest Media3 video size and the QQuickItem bounds.

- [ ] **Step 5: Wire `AndroidMedia3Item::updatePaintNode` and release order**

The item allocates the GL texture with the active scene-graph context, constructs one `Media3SurfaceBridge`, passes `surface()` to `Media3PlayerHost`, and assigns a monotonically increasing `surfaceGeneration`. `nativeOnFrameAvailable` only sets an atomic pending-frame flag and queues `update()`.

`releaseVideoSurface()` asks Java to clear the exact current Surface on the main looper. Only the Java completion callback authorizes release of the Java bridge and GL/OES resources. A late completion/frame callback with an old `surfaceGeneration` is ignored.

- [ ] **Step 6: Run the ExternalOES static checks GREEN**

Run: `python tests/test_android_media3_player_contract.py`
Expected: `ANDROID_MEDIA3_PLAYER_CONTRACT_OK`.

- [ ] **Step 7: Commit**

```bash
git add native/player/androidmedia3videonode.* native/player/androidmedia3item.* \
        native/platform/android/src/org/colosseum/player/Media3SurfaceBridge.java \
        tests/test_android_media3_player_contract.py
git commit -m "feat(android): compose Media3 video through Qt Quick"
```

### Task 6: Lead-owned Android build wiring and first physical composition gate

**Ownership:** Lead integration task. Do not let an isolated A02 worker independently rewrite broad shared bootstrap/build files.

**Files:**
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`
- Create from the matching Qt template, then edit: `native/platform/android/build.gradle`
- Modify: `tests/test_android_media3_player_contract.py`

**Interfaces:**
- Consumes: Tasks 3-5 Android files.
- Produces: Android `PlayerItem -> AndroidMedia3Item`; desktop remains `PlayerItem -> MpvItem`.
- Produces: Gradle Media3 1.11.0 dependency closure and OpenGL Qt Quick backend.

- [ ] **Step 1: Add RED build/registration assertions**

```python
require(MAIN, 'qmlRegisterType<AndroidMedia3Item>("Colosseum.Player", 1, 0, "PlayerItem")',
        "Android must register the Media3 backend under the neutral type")
require(CMAKE, "player/androidmedia3item.cpp", "Android build must compile the player facade")
require(GRADLE, 'androidx.media3:media3-exoplayer:1.11.0', "Media3 core must be pinned")
require(GRADLE, 'androidx.media3:media3-exoplayer-hls:1.11.0', "HLS module must be pinned")
require(GRADLE, 'androidx.media3:media3-exoplayer-dash:1.11.0', "DASH module must be pinned")
forbid(GRADLE, "media3-session", "background/session product behavior is not approved")
```

- [ ] **Step 2: Wire Android-only C++ sources and neutral registration**

Under `if(ANDROID)`, add `androidmedia3state.cpp`, `androidmedia3item.cpp`, and `androidmedia3videonode.cpp`; do not move `mpvitem.cpp` out of its existing `NOT ANDROID` block.

In `main.cpp`, after the other Android-foundation lead gates are applied, use:

```cpp
#if defined(Q_OS_ANDROID)
#include "player/androidmedia3item.h"
#else
#include "player/mpvitem.h"
#endif
...
#if defined(Q_OS_ANDROID)
qmlRegisterType<AndroidMedia3Item>("Colosseum.Player", 1, 0, "PlayerItem");
#else
qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "PlayerItem");
qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
#endif
```

Keep `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)` before `QGuiApplication` on Android. Do not re-enable Player 2, MpvQt, or WebEngine to make Android configure pass.

- [ ] **Step 3: Create the Gradle overlay from the exact Qt 6.11.1 template**

On the provisioned Android host:

```powershell
$qtAndroid = 'C:\Qt\6.11.1\android_arm64_v8a'
Copy-Item "$qtAndroid\src\android\templates\build.gradle" `
          'native\platform\android\build.gradle'
```

Preserve the template's Qt/AGP machinery. In its existing `dependencies {}` block add only:

```gradle
implementation "androidx.media3:media3-exoplayer:1.11.0"
implementation "androidx.media3:media3-exoplayer-hls:1.11.0"
implementation "androidx.media3:media3-exoplayer-dash:1.11.0"
```

Do not copy or replace Qt's Gradle wrapper; Qt 6.11's pinned Gradle/AGP pair remains authoritative.

- [ ] **Step 4: Run host/static gates before packaging**

Run:
```powershell
python tests/test_android_media3_player_contract.py
python tests/test_player_backend_contract.py
python scripts/android/qualify_toolchain.py --qt-root C:\Qt\6.11.1\android_arm64_v8a `
  --host-qt-root C:\Qt\6.11.1\msvc2022_64 --sdk-root $env:ANDROID_SDK_ROOT `
  --ndk-root "$env:ANDROID_SDK_ROOT\ndk\27.2.12479018" --require-device
```
Expected: contract tests PASS and toolchain/device gate PASS. A missing Android kit/device is a blocker, not a reason to weaken the gate.

- [ ] **Step 5: Configure/build/install the physical-gate APK**

Use the W08-pinned Qt/NDK configuration, serialized:

```powershell
C:\Qt\6.11.1\android_arm64_v8a\bin\qt-cmake.bat -S native -B native\build-android-arm64 -GNinja `
  -DANDROID_SDK_ROOT=$env:ANDROID_SDK_ROOT `
  -DANDROID_NDK_ROOT="$env:ANDROID_SDK_ROOT\ndk\27.2.12479018" `
  -DQT_ANDROID_ABIS=arm64-v8a
cmake --build native\build-android-arm64 --target apk --parallel 1
$apk = Get-ChildItem native\build-android-arm64 -Recurse -Filter '*.apk' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$apk) { throw 'Android APK not produced' }
adb install -r "$apk"
```

Expected: Java compilation resolves all three Media3 modules; native link has no MpvQt/libmpv dependency; app launches without QML import/linker errors.

- [ ] **Step 6: Execute the ExternalOES composition gate on the physical ARM64 device**

Through the existing production `PlayerPage`, start a known-working direct HTTPS H.264/AAC source. Keep ordinary Colosseum QML chrome visible over the video and verify all four observations:

```text
1. player.fileLoaded becomes true through the normal load route.
2. player.decodedWidth > 0 and player.decodedHeight > 0 after onRenderedFirstFrame.
3. Pause/menu/loading QML remains visibly above the decoded video with correct z-order.
4. portrait -> landscape -> portrait destroys/restores the surface and produces a new first-frame event without black video.
```

Capture `adb logcat`, one screenshot with QML chrome over active video, and the exact device/API/build SHA in the implementation result.If any of those four composition observations fails, STOP A02 implementation. Record whether the failure is texture creation, Surface attachment, frame callback, UV transform, QSG composition, z-order, rotation/surface recreation, or another constraint. Return that evidence to Lead; do not implement SurfaceView, copied-frame rendering, libmpv, or another fallback without a new architecture decision.

- [ ] **Step 7: Commit only after the physical composition gate is GREEN**

```bash
git add native/CMakeLists.txt native/main.cpp native/platform/android/build.gradle \
        tests/test_android_media3_player_contract.py
git commit -m "build(android): wire Media3 PlayerItem backend"
```

### Task 7: Track selection, chapters, external subtitles, and Qt cue overlay

**Files:**
- Modify: `native/platform/android/src/org/colosseum/player/Media3PlayerHost.java`
- Modify: `native/player/androidmedia3item.h`
- Modify: `native/player/androidmedia3item.cpp`
- Create: `qml/Media3SubtitleOverlay.qml`
- Modify: `tests/android_media3_state_harness.cpp`
- Modify: `tests/test_android_media3_player_contract.py`

**Interfaces:**
- Consumes: Task 2 normalized track/chapter/cue shapes.
- Produces: exact QML track rows, chapter rows, `subtitleCues`, and `addSubtitle(url,title,lang,select)` source revision.

- [ ] **Step 1: Add RED tests for opaque track IDs and chapter normalization**

In the state harness assert:

```cpp
Q_ASSERT(audio.at(0).toMap().value("id").toString() == "a:0:0");
Q_ASSERT(subs.at(0).toMap().value("id").toString() == "s:1:0");
Q_ASSERT(chapters.at(0).toMap().value("title").toString() == "Chapter");
Q_ASSERT(chapters.at(0).toMap().value("startSec").toDouble() >= 0.0);
```

The Java contract test must require `TrackSelectionOverride`, `MediaItem.SubtitleConfiguration`, Media3 `Chapter`, and cue-listener code.

- [ ] **Step 2: Implement Media3 track enumeration and exact selection**

On each `onTracksChanged(Tracks tracks)`, rebuild a generation-local `Map<String, TrackTarget>`. For every group/index with audio or text type, emit one JSON row with the exact keys `id,title,lang,selected,external,forced,codec,default,hearingImpaired`. Use `a:<groupIndex>:<trackIndex>` and `s:<groupIndex>:<trackIndex>` only as ephemeral UI IDs.

Selection must use the current Media3 group, never persist a `TrackGroup` across sources:

```java
TrackTarget t = trackTargets.get(id);
TrackSelectionParameters.Builder b = player.getTrackSelectionParameters().buildUpon();
b.setTrackTypeDisabled(t.trackType, false);
b.clearOverridesOfType(t.trackType);
b.addOverride(new TrackSelectionOverride(t.mediaTrackGroup, t.trackIndex));
player.setTrackSelectionParameters(b.build());
```

For subtitle-off, clear text overrides and set `C.TRACK_TYPE_TEXT` disabled. A new source rebuilds the map and discards every prior `TrackTarget`.

- [ ] **Step 3: Implement Media3 chapter metadata normalization**

In `onMetadata(Metadata metadata)`, collect `androidx.media3.extractor.metadata.Chapter` entries for the active generation. Ignore `isHidden()`. Convert each period-relative `startTimeMs` to current-window milliseconds by subtracting `Timeline.Window.positionInFirstPeriodUs / 1000`, clamp to zero, use `"Chapter"` for blank names, sort by start, and deduplicate equal `(start,title)` pairs before `nativeOnMetadata(...)`.

- [ ] **Step 4: Implement active cue transport with `CueGroup`**

Override the current listener API:

```java
@Override public void onCues(CueGroup cueGroup) {
    JSONArray rows = new JSONArray();
    for (Cue cue : cueGroup.cues) {
        rows.put(CueJson.encode(cue));
    }
    nativeOnSubtitleCues(nativeHandle, generation, rows.toString());
}
```

`CueJson.encode` must preserve plain text plus line/position anchors, alignment, cue size, text-size hints, window/background color and supported spans (foreground/background color, bold, italic, underline, relative/absolute size). Unsupported Android span classes are ignored, not serialized as fake fidelity.

- [ ] **Step 5: Implement external subtitle source revisions without changing Colosseum source identity**

Keep an immutable Java `SourceDescriptor` for the active source: URL, copied HTTP headers, external subtitle configurations, and generation. `addSubtitle(url,title,lang,select)` appends a `MediaItem.SubtitleConfiguration`, rebuilds only the Media3 media source for that same generation, and preserves current position/play intent/audio selection.

```java
MediaItem.SubtitleConfiguration cfg = new MediaItem.SubtitleConfiguration.Builder(Uri.parse(url))
    .setMimeType(mimeForSubtitleUrl(url))
    .setLanguage(emptyToNull(lang))
    .setLabel(emptyToNull(title))
    .build();
long pos = player.getCurrentPosition();
boolean play = player.getPlayWhenReady();
sourceDescriptor = sourceDescriptor.withSubtitle(cfg);
player.setMediaSource(buildSource(sourceDescriptor), pos);
player.prepare();
player.setPlayWhenReady(play);
```

Map `.ass`/`.ssa` to `MimeTypes.TEXT_SSA`, `.srt` to `MimeTypes.APPLICATION_SUBRIP`, `.vtt` to `MimeTypes.TEXT_VTT`, and otherwise omit MIME so Media3/provider inference remains possible. Do not increment the native source generation, emit `fileStarted`, or change `currentUrl` for this revision.

- [ ] **Step 6: Create the Qt subtitle overlay component**

```qml
import QtQuick

Item {
    id: overlay
    required property var player
    Repeater {
        model: overlay.player ? overlay.player.subtitleCues : []
        delegate: Text {
            required property var modelData
            text: modelData.text || ""
            textFormat: Text.RichText
            color: modelData.color || "white"
            horizontalAlignment: modelData.alignment === "start" ? Text.AlignLeft
                               : modelData.alignment === "end" ? Text.AlignRight : Text.AlignHCenter
            x: Math.max(0, Math.min(overlay.width - width,
                (modelData.position >= 0 ? modelData.position : 0.5) * overlay.width - width / 2))
            y: Math.max(0, Math.min(overlay.height - height,
                (modelData.line >= 0 ? modelData.line : 0.90) * overlay.height - height))
        }
    }
}
```

Before passing styled text into QML, C++ converts the normalized safe style runs into escaped rich-text spans; subtitle text itself is HTML-escaped first. Do not evaluate subtitle text as QML/JS/HTML input.

- [ ] **Step 7: Run focused normalization and contract gates GREEN**

Run:
```powershell
cmake --build C:\b\a02-media3-host --target android_media3_state_harness --parallel 1
C:\b\a02-media3-host\android_media3_state_harness.exe
python tests/test_android_media3_player_contract.py
```
Expected: `ANDROID_MEDIA3_STATE_OK` and `ANDROID_MEDIA3_PLAYER_CONTRACT_OK`.

- [ ] **Step 8: Commit**

```bash
git add native/platform/android/src/org/colosseum/player/Media3PlayerHost.java \
        native/player/androidmedia3item.* qml/Media3SubtitleOverlay.qml \
        tests/android_media3_state_harness.cpp tests/test_android_media3_player_contract.py
git commit -m "feat(android): expose Media3 tracks chapters and subtitles"
```

### Task 8: Foreground lifecycle and audio-interruption policy

**Files:**
- Modify: `native/player/androidmedia3state.h`
- Modify: `native/player/androidmedia3state.cpp`
- Modify: `native/player/androidmedia3item.cpp`
- Modify: `native/platform/android/src/org/colosseum/player/Media3PlayerHost.java`
- Modify: `tests/android_media3_state_harness.cpp`
- Modify: `tests/test_android_media3_player_contract.py`

**Interfaces:**
- Consumes: W02 lifecycle strings `active|inactive|hidden|suspended` through `PlayerBackendContract::setHostLifecycleState`.
- Produces: `HostPlaybackAction` decisions `None|Pause|Resume` with source/user-pause generation guards.
- Keeps Media3 as the sole normal Android audio-focus owner.

- [ ] **Step 1: Add RED host-lifecycle arbitration tests**

```cpp
state.noteUserPlay(g);
Q_ASSERT(state.applyHostLifecycle(g, "hidden") == HostPlaybackAction::Pause);
Q_ASSERT(state.applyHostLifecycle(g, "active") == HostPlaybackAction::Resume);
state.noteUserPause(g);
Q_ASSERT(state.applyHostLifecycle(g, "active") == HostPlaybackAction::None);
state.beginLoad("https://example.invalid/b.mp4", {});
Q_ASSERT(state.applyHostLifecycle(g, "active") == HostPlaybackAction::None);
Q_ASSERT(state.applyHostLifecycle(state.generation(), "inactive") == HostPlaybackAction::None);
```

Add cases for terminal audio-focus loss, stop, and source replacement preventing host auto-resume.

- [ ] **Step 2: Implement deterministic host-induced pause ownership**

Add:

```cpp
enum class HostPlaybackAction { None, Pause, Resume };
void noteUserPlay(quint64 generation);
void noteUserPause(quint64 generation);
void noteStopped(quint64 generation);
void noteTerminalAudioFocusLoss(quint64 generation);
HostPlaybackAction applyHostLifecycle(quint64 generation, const QString &state);
```

`hidden`/`suspended` return `Pause` only when the current generation was playing. Record that the pause is host-owned. `active` returns `Resume` only for that exact host-owned pause and only if no later user pause, stop, source generation, or terminal focus loss invalidated it. `inactive` always returns `None`.

`AndroidMedia3Item::setPause()` records an explicit user pause/play before commanding Java. `setHostLifecycleState()` asks the state core for an action and commands Java directly so a host pause is not mislabeled as a user pause.

- [ ] **Step 3: Keep Media3 as the sole normal audio-focus owner**

The Java host keeps `setAudioAttributes(..., true)` and `setHandleAudioBecomingNoisy(true)`. Do not call `AudioManager.requestAudioFocus()` or create an `OnAudioFocusChangeListener` in A02/W02.

`setAudioFocusState("loss")` is only a host/test override: record terminal focus loss and pause. `"loss_transient"` may pause without terminal ownership; `"gain"` never auto-resumes a user-paused item. Normal production focus transitions remain Media3-managed.

Extend the static test:

```python
forbid(HOST, "requestAudioFocus(", "A02 must not create a second audio-focus owner")
forbid(HOST, "OnAudioFocusChangeListener", "Media3 must own normal focus callbacks")
require(HOST, "setHandleAudioBecomingNoisy(true)", "noisy output must remain enabled")
```

- [ ] **Step 4: Run RED -> GREEN checks**

Run:
```powershell
cmake --build C:\b\a02-media3-host --target android_media3_state_harness --parallel 1
C:\b\a02-media3-host\android_media3_state_harness.exe
python tests/test_android_media3_player_contract.py
```
Expected after implementation: both PASS.

- [ ] **Step 5: Commit**

```bash
git add native/player/androidmedia3state.* native/player/androidmedia3item.cpp \
        native/platform/android/src/org/colosseum/player/Media3PlayerHost.java \
        tests/android_media3_state_harness.cpp tests/test_android_media3_player_contract.py
git commit -m "feat(android): enforce player lifecycle policy"
```

### Task 9: Lead-owned shared QML lifecycle, subtitle overlay, and capability truth

**Ownership:** Lead integration task. These are the deliberately small shared-file edits A02 publishes to Lead.

**Files:**
- Modify: `qml/PlayerPage.qml`
- Modify: `qml/AudiobookSession.qml`
- Modify: `qml/SubtitleMenu.qml`
- Modify: `tests/test_android_media3_player_contract.py`
- Modify: `tests/test_player_backend_contract.py` only if a neutral host-lifecycle assertion belongs in the existing desktop contract.

**Interfaces:**
- Consumes: neutral `PlayerBackendContract` host methods already implemented by desktop as no-ops and Android as real behavior.
- Consumes: Android capability `subtitleCueOverlay=true` and `subtitleStyling=false`.
- Produces: no Android-specific QML branch; shared QML drives the neutral backend contract.

- [ ] **Step 1: Add RED shared-QML contract assertions**

Require:

```python
require(PLAYER_PAGE, "PlatformRuntime.applicationState", "video player must forward host lifecycle")
require(PLAYER_PAGE, "PlatformRuntime.surfaceAvailable", "video player must forward surface lifecycle")
require(AUDIOBOOK, "PlatformRuntime.applicationState", "audiobook must forward host lifecycle")
require(PLAYER_PAGE, 'supportsPlayerCapability("subtitleCueOverlay")', "Media3 cue overlay must be capability-gated")
require(SUBTITLE_MENU, "property bool styleEnabled", "subtitle style UI must have explicit capability input")
require(PLAYER_PAGE, 'styleEnabled: root.supportsPlayerCapability("subtitleStyling")',
        "unsupported subtitle styling must not be exposed")
forbid(PLAYER_PAGE, "Qt.platform.os === \"android\"", "shared PlayerPage must not fork on Android")
forbid(AUDIOBOOK, "Qt.platform.os === \"android\"", "shared audiobook session must not fork on Android")
```

- [ ] **Step 2: Add neutral lifecycle forwarding to `PlayerPage.qml`**

Inside the existing `PlayerItem`, extend `Component.onCompleted` after `applyPlaybackProfile()`:

```qml
if (typeof PlatformRuntime !== "undefined" && PlatformRuntime) {
    mpv.setHostLifecycleState(PlatformRuntime.applicationState)
    if (PlatformRuntime.surfaceAvailable) mpv.restoreVideoSurface()
    else mpv.releaseVideoSurface()
}
```

Add a sibling `Connections`:

```qml
Connections {
    target: typeof PlatformRuntime !== "undefined" ? PlatformRuntime : null
    function onApplicationStateChanged() {
        mpv.setHostLifecycleState(PlatformRuntime.applicationState)
    }
    function onSurfaceAvailableChanged() {
        if (PlatformRuntime.surfaceAvailable) mpv.restoreVideoSurface()
        else mpv.releaseVideoSurface()
    }
}
```

- [ ] **Step 3: Add neutral lifecycle forwarding to `AudiobookSession.qml`**

The hidden audiobook `PlayerItem` needs application-state forwarding but no video-surface calls:

```qml
Connections {
    target: typeof PlatformRuntime !== "undefined" ? PlatformRuntime : null
    function onApplicationStateChanged() {
        mpv.setHostLifecycleState(PlatformRuntime.applicationState)
    }
}
```

On the audiobook `PlayerItem` completion path, call `setHostLifecycleState` once with the current value so an item created while already hidden/suspended cannot begin playback under stale foreground assumptions.

- [ ] **Step 4: Mount the Media3 cue overlay only when the backend advertises it**

Immediately after the `PlayerItem` sibling in `PlayerPage.qml`:

```qml
Loader {
    anchors.fill: mpv
    z: 1
    active: root.supportsPlayerCapability("subtitleCueOverlay")
    sourceComponent: Component {
        Media3SubtitleOverlay {
            anchors.fill: parent
            player: mpv
        }
    }
}
```

The desktop MpvItem never exposes `subtitleCues`, because the Loader stays inactive when the capability is absent/false.

- [ ] **Step 5: Hide the mpv-only subtitle appearance affordance when unsupported**

Add to `SubtitleMenu.qml`:

```qml
property bool styleEnabled: true
```

Change `styleButton` to `visible: menu.styleEnabled`, and make the status text's right anchor choose `styleButton.visible ? styleButton.left : closeButton.left` so layout stays correct when the button disappears.

In `PlayerPage.qml` set:

```qml
styleEnabled: root.supportsPlayerCapability("subtitleStyling")
onStyleRequested: {
    if (!root.supportsPlayerCapability("subtitleStyling")) return
    subStyleBar.open = !subStyleBar.open
    root.wakeChrome()
}
```

- [ ] **Step 6: Run shared-QML and desktop regression gates GREEN**

Run:
```powershell
python tests/test_android_media3_player_contract.py
python tests/test_player_backend_contract.py
C:\Qt\6.11.1\msvc2022_64\bin\qmlformat.exe --check qml\Media3SubtitleOverlay.qml
```
Expected: both Python contracts PASS and the new overlay parses/formats cleanly. Do not reformat unrelated large QML files.

- [ ] **Step 7: Commit**

```bash
git add qml/PlayerPage.qml qml/AudiobookSession.qml qml/SubtitleMenu.qml \
        tests/test_android_media3_player_contract.py tests/test_player_backend_contract.py
git commit -m "feat(player): wire neutral Android host lifecycle"
```

### Task 10: Cross-lane source/security integration and final qualification

**Ownership:** Lead integration + qualification task. Do not start it until A03 and A04 result packets are published and Lead has approved the four-lane package.

**Files:**
- Modify or create from the Qt 6.11.1 template: `native/platform/android/AndroidManifest.xml`
- Create: `native/platform/android/res/xml/network_security_config.xml`
- Modify: `tests/test_android_media3_player_contract.py`
- Modify: `THIRD_PARTY_NOTICES.md`
- Produce runtime evidence outside source control under the Android Media Runtime room/result packet.

**Interfaces:**
- Consumes A03: `VaultContentRef.uri` is the current permission-valid `content://` location; `vaultId` remains durable identity outside A02.
- Consumes A04: dynamic `http://127.0.0.1:<port>/...` URL with truthful range/reconnect semantics; URL is ephemeral and the port is never persisted.
- Produces final evidence that direct HTTPS, A03 content URIs, and A04 loopback all converge on `PlayerItem.loadSource` without storage/server knowledge inside A02.

- [ ] **Step 1: Enforce the cross-lane packet gate before touching packaging**

Read:
```text
C:\Users\Suprabha\Desktop\AgentRooms\2026-09-03-colosseum-android-media-runtime\A-03-RESULT.md
C:\Users\Suprabha\Desktop\AgentRooms\2026-09-03-colosseum-android-media-runtime\A-04-RESULT.md
C:\Users\Suprabha\Desktop\AgentRooms\2026-09-03-colosseum-android-media-runtime\LEAD-APPROVAL.md
```

Stop if A03 requires a filesystem-copy path for ordinary video/audio, or if A04 requires a fixed/non-loopback URL, player-visible torrent identity, or a transport other than ordinary HTTP byte ranges. Do not adapt A02 by absorbing Vault/server ownership.

- [ ] **Step 2: Add RED network-security assertions**

```python
SECURITY = (ROOT / "native/platform/android/res/xml/network_security_config.xml").read_text()
MANIFEST = (ROOT / "native/platform/android/AndroidManifest.xml").read_text()
require(MANIFEST, 'android:networkSecurityConfig="@xml/network_security_config"',
        "Android app must opt into the narrow network policy")
require(SECURITY, '<base-config cleartextTrafficPermitted="false"',
        "remote cleartext must fail closed")
require(SECURITY, '<domain includeSubdomains="false">127.0.0.1</domain>',
        "only numeric IPv4 loopback is allowed for A04")
forbid(MANIFEST, 'android:usesCleartextTraffic="true"',
       "manifest must not globally allow cleartext")
```

Run: `python tests/test_android_media3_player_contract.py`
Expected: RED until manifest/security files are integrated.

- [ ] **Step 3: Integrate the narrow Android network policy without overwriting other lanes**

If Lead has already produced the unified Android manifest, edit that file. Otherwise copy the exact Qt 6.11.1 template first:

```powershell
$qtAndroid = 'C:\Qt\6.11.1\android_arm64_v8a'
if (!(Test-Path 'native\platform\android\AndroidManifest.xml')) {
    Copy-Item "$qtAndroid\src\android\templates\AndroidManifest.xml" `
              'native\platform\android\AndroidManifest.xml'
}
New-Item -ItemType Directory -Force 'native\platform\android\res\xml' | Out-Null
```

Add `android:networkSecurityConfig="@xml/network_security_config"` to the existing `<application>` element while preserving A01/A03/A04/TV manifest additions.Create exactly:

```xml
<?xml version="1.0" encoding="utf-8"?>
<network-security-config>
    <base-config cleartextTrafficPermitted="false" />
    <domain-config cleartextTrafficPermitted="true">
        <domain includeSubdomains="false">127.0.0.1</domain>
    </domain-config>
</network-security-config>
```

Do not add `localhost`, wildcard domains, RFC1918 ranges, or a permissive base config unless a later approved A04 contract explicitly requires them.

- [ ] **Step 4: Record Media3 license notice**

Add a concise AndroidX Media3 entry to `THIRD_PARTY_NOTICES.md` naming AndroidX Media3/ExoPlayer, version `1.11.0`, Apache License 2.0, and the upstream project/license reference. Do not copy unrelated Android dependency notices into source by hand; release packaging should retain Gradle dependency metadata separately.

- [ ] **Step 5: Run final host/static/desktop regression gates**

Configure a fresh serialized desktop proof tree:

```powershell
C:\Qt\Tools\CMake_64\bin\cmake.exe -S native -B C:\b\a02-media3-final -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\Ninja\ninja.exe -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64 `
  -DLIBTORRENT_ROOT=C:\tools\libtorrent-2.0-msvc -DBOOST_ROOT=C:\tools\boost-1.84.0 `
  -DOPENSSL_MSVC_ROOT=C:\tools\openssl-msvc `
  -DMPVQT_PREFIX=C:\tools\mpvqt-feasibility\mpvqt-msvc-install `
  -DBUILD_TESTING=ON -DCOLOSSEUM_BUILD_PLAYER2=OFF
C:\Qt\Tools\CMake_64\bin\cmake.exe --build C:\b\a02-media3-final `
  --target colosseum android_media3_state_harness -- -j1
```Then run:

```powershell
C:\b\a02-media3-final\android_media3_state_harness.exe
python tests/test_android_media3_player_contract.py
python tests/test_player_backend_contract.py
git diff --check
```

Expected: state harness + both contracts PASS, full desktop `colosseum.exe` links with MpvQt, and diff check is clean.

- [ ] **Step 6: Rebuild/install the final integrated Android APK**

```powershell
C:\Qt\6.11.1\android_arm64_v8a\bin\qt-cmake.bat -S native -B native\build-android-arm64 -GNinja `
  -DANDROID_SDK_ROOT=$env:ANDROID_SDK_ROOT `
  -DANDROID_NDK_ROOT="$env:ANDROID_SDK_ROOT\ndk\27.2.12479018" `
  -DQT_ANDROID_ABIS=arm64-v8a
cmake --build native\build-android-arm64 --target apk --parallel 1
$apk = Get-ChildItem native\build-android-arm64 -Recurse -Filter '*.apk' |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$apk) { throw 'Android APK not produced' }
adb install -r "$apk"
```

Expected: install succeeds on the W08-qualified physical ARM64 device; logcat contains no missing `.so`, Media3 class-resolution, QML import, or fatal Java exception errors.

- [ ] **Step 7: Qualify direct progressive, HLS, and DASH through the production QML route**

Use these clear public regression assets:

```text
Progressive: https://storage.googleapis.com/exoplayer-test-media-0/BigBuckBunny_320x180.mp4
HLS:         https://storage.googleapis.com/shaka-demo-assets/angel-one-hls/hls.m3u8
DASH:        https://storage.googleapis.com/shaka-demo-assets/angel-one/dash.mpd
```

For each: open via normal PlayerPage source routing, require `fileLoaded`, position advancement, pause/resume, at least one seek, and no `playbackError`. For HLS/DASH also verify the exposed audio/subtitle track lists are non-stale after source replacement and that a track selection changes the selected row.

- [ ] **Step 8: Prove provider-header isolation on wire**

Run a controlled HTTP media fixture on the development host that logs request headers and serves a known-valid MP4, expose it to the device as numeric loopback with `adb reverse`, and feed it through the existing production direct-candidate route (`playStreamAt` -> `loadDirectStreamUrl` -> `PlayerItem.loadSource(url, headers)`).

First candidate headers:
```json
{"Referer":"https://fixture.example/title","Origin":"https://fixture.example"}
```
Second candidate: same fixture URL, empty headers.

Acceptance: the first request log contains exactly the supplied Referer/Origin, segment/reconnect requests for that source retain them, and the second load contains neither header. A same-URL reload with empty headers counts as the strongest leak test.

- [ ] **Step 9: Prove A03 and A04 source convergence**

A03 journey: resolve one Vault video and one audiobook to `VaultContentRef`; pass `ref.uri` unchanged into the existing PlayerItem source route. Require `content://` in `currentUrl`, zero permanent source duplication, video decoded dimensions/position advancement, and audiobook seek/speed/pause/resume. Reboot the process and repeat where A03 promises persisted access.

A04 journey: request one fresh Colosseum Server URL, assert it is numeric IPv4 loopback with a non-assumed runtime port, play it, seek forward and backward, and inspect A04 server evidence for valid byte-range reconnects. Restart the server, obtain a newly published URL, and prove A02 uses the replacement rather than persisting the previous port.

Also verify `NetworkSecurityPolicy`/runtime behavior permits cleartext to `127.0.0.1` while a controlled non-loopback HTTP destination is denied.

- [ ] **Step 10: Run the remaining physical acceptance matrix**

Record pass/fail evidence for: multi-audio selection, embedded subtitles, online SRT, representative ASS positioning/style, rotation, background 30s/return, wired/Bluetooth noisy-output pause, transient/terminal audio-focus behavior, unsupported-codec bounded error, and stop/back resource release with no orphan audio.

For every video success, require `decodedWidth > 0 && decodedHeight > 0`; `fileLoaded` alone is never a video-pass criterion. For ASS, record unsupported karaoke/vector/clipping/animated effects honestly instead of calling them supported.

- [ ] **Step 11: Commit the final packaging/security/notices change after all gates are GREEN**

```bash
git add native/platform/android/AndroidManifest.xml \
        native/platform/android/res/xml/network_security_config.xml \
        tests/test_android_media3_player_contract.py THIRD_PARTY_NOTICES.md
git commit -m "build(android): seal Media3 playback integration"
```

---

## Implementation Completion Gate

A02 implementation is complete only when Tasks 1-10 are committed, the Task 6 ExternalOES gate is GREEN on physical ARM64 hardware, the Task 10 cross-lane packet gate is satisfied, desktop MpvItem still builds/tests, and every claimed Android success has physical decoded-frame/audio evidence. A passing APK build by itself is not completion.

If any required A03/A04/Lead packet is absent or contradicts this plan, status is **BLOCKED AT CROSS-LANE SEAL**, not implementation-complete.
