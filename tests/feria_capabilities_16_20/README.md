# Feria 16–20 native verification fixture

This builds the actual C++ policy/account owners and real Qt Quick WebEngine adapter. It does not use PySide or substitute a simulated download object for integration results. Focused modeled tests elsewhere remain separately labeled.

Requirements: Qt 6.11.1 Linux SDK, WebEngine/WebChannel/Positioning modules, CMake >=3.24, Ninja, C++20 compiler, X11/Xvfb and normal Qt xcb/OpenGL runtime dependencies. Known successful route: GitHub Actions Ubuntu24.04, Xvfb1280x800x24, Mesa llvmpipe. No sandbox-disabling flag is part of this fixture.

Build and test from repository root:

```sh
cmake -S tests/feria_capabilities_16_20 -B capability-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build capability-build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir capability-build --output-on-failure
FERIA_SESSION_EVIDENCE_DIR="$PWD/evidence/captures" xvfb-run -a -s '-screen 0 1280x800x24' capability-build/feria_capabilities_runtime -input tests/feria_capabilities_16_20/qml
```

`QtWebEngineQuick::initialize()` runs before GUI application construction. Each process uses a temporary synthetic profile. Exact Qt/WebEngine/Chromium/security-patch accessor versions are emitted before tests.

## HTTP fixture contract

- `FixtureUrl` and `Fixture.baseUrl`: ephemeral127.0.0.1 HTTP origin.
- `Fixture.downloadRoot` and `downloadsDirectory`: private temporary downloads directory.
- `/download?name=fixture.bin&gate=token`: deterministic1MiB attachment. Server writes exactly64KiB then holds until `Fixture.release("token")`. A test must observe actual received bytes and active native state before exercising pause, close, or retirement.
- `/gated-download?gate=token`: holds response headers and body until release, for the strictly live-view document provenance reproducer.
- `Fixture.interrupt("token")`: closes only fixture sockets associated with that gate. A later genuine native resume request supports and records HTTP Range. No synthetic state transition is injected into Qt.
- `unknown=1`: omits Content-Length, finishing only when connection closes. `bytes=0`: real zero-length attachment.
- `requestLog()`: actual HTTP method/URL/path/gate/Range/start/status/bytes written. `requestCount(key)`: matching gate or route path count.
- `checksum(path)`/`expectedChecksum(key)`, `pathSize(path)`, `fileExists(path)`, `readText(path)`: verify bytes written by the actual native download; file access stays under the fixture temporary root. For zero-length payload checks, call expectedChecksum after its keyed request.
- `/media`: audio IDs playing/manual and video ID video, with valid local WAV and generated VP8/Vorbis WebM; no autoplay.
- `/media-frame` and `/media-frame-replacement`: same-origin document fixtures with frameAudio. A localhost hostname creates the deliberately uninspectable cross-origin comparison.
- `/save-context`: linkA/linkB/image/audio/canvas/javascriptLink. `/save-page`: FERIA_SAVE_PAGE_MARKER. `/image.png` and `/media.wav` are actual binary resources.

All query variants route by parsed path. No external provider, credentials, personal profile, DRM, hardware audio, or Windows behavior is qualified by these fixtures.

## Generated media provenance

The32KiB media.webm is generated entirely from FFmpeg lavfi testsrc and sine fixtures, using installed libvpx and libvorbis encoders:

```sh
ffmpeg -f lavfi -i 'testsrc=size=160x90:rate=12:duration=3' -f lavfi -i 'sine=frequency=440:sample_rate=8000:duration=3' -c:v libvpx -deadline good -cpu-used 4 -b:v 100k -c:a libvorbis -b:a 24k media.webm
```

SHA-256:2040aaefe3922332e5dcb93f540fe9b612b8495f8cb2053d49fdbc8695a7381d.

## Evidence limits

Download source attribution through the pinned Quick raw-backed request.view getter is excluded. A deliberately supplied source-envelope test remains a model of that boundary and is labeled accordingly. Originless/creation-view lifetime probes denied in earlier work are not executed. Automatic Page Save correlation is not inferred from URL, active selection, or event order.

The unchanged legacy retained-download test is run separately on both exact baseline and candidate. Its fixed-delay failure stays visible. The deterministic gated regression is additional evidence and does not weaken or replace the old assertion.
