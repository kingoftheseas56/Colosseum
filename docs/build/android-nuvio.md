# Opt-in Android Nuvio torrent playback backend (candidate slice)

Status: reviewable first vertical slice, default OFF, not production-accepted.
This page documents the exact opt-in build, verification and current
limitations for the `COLOSSEUM_ANDROID_NUVIO_BACKEND` candidate.

## What it is

An opt-in Android-only torrent playback backend behind the existing
`Stream` (StreamServer) / Media3 seam:

- `native/player/nuvioengineapi.{h,cpp}` — function-table mirror of the
  pinned Nuvio C API (version 3), resolved at runtime from a private,
  independently built and hardened `libnuvio_engine.so`.
- `native/player/nuviostreambackend.{h,cpp}` — the testable async owner:
  lazy engine creation on explicit `play()`/`prefetch()` demand, worker
  thread for all blocking engine work, request generations that suppress
  stale completions and retire stale routes, loopback-URL validation, honest
  stats mapping.
- `native/player/streamserver_android.cpp` — delegation from the existing
  seam; with the option OFF the unavailable stub is preserved unchanged.

The shared download TorrentEngine is untouched. Upload is Disabled. Cache
budgets are explicit. Android system TLS roots are exported to a private PEM
(`NuvioAndroidTrust.java`) before the native engine is created; certificate
verification is never disabled. The network_security_config stays
loopback-cleartext-only.

## Requirements

- Qualified local Android kit: Qt `android_arm64_v8a`, NDK (r27c line), and
  the Colosseum Android dependency prefix from `scripts/android/build_deps.sh`.
- The pinned Nuvio donor source (branch
  `codex/nuvio-android-hardening-20261018`, tag `v0.1.5`,
  commit `99a18601fa12858cc8c62576dc72b87edbd81b9a`) — pin and provenance in
  `native/third_party/nuvio-engine/SOURCE_MANIFEST.md`.

## Prepare the hardened engine

```
python scripts/android/prepare_nuvio_engine.py \
    --source <nuvio-engine-donor-checkout> \
    --ndk <ANDROID_NDK_HOME> \
    --abi arm64-v8a --api 28 \
    --prefix <nuvio-prefix>
```

The script verifies the donor is the pristine `v0.1.5` pin, applies the
reviewed hardening patch from
`native/third_party/nuvio-engine/patches/0001-android-hardening.patch`
(verified-only piece delivery + Android LSD/UPnP/NAT-PMP disabled + the
loopback integrity regression), builds `libnuvio_engine.so` and stamps the
prefix with `.colosseum-nuvio-hardened-stamp`. An artifact without the stamp
can never pass the CMake readiness gate.

## Configure and build (arm64, feature ON)

Add to your usual Colosseum Android configure (see `docs/build/android.md`):

```
-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON \
-DCOLOSSEUM_NUVIO_ENGINE_PREFIX=<nuvio-prefix>
```

Fail-closed behavior: with the option ON, configure fails loudly when the
prefix, the library, or the hardened stamp is missing. The engine `.so` is
packaged via `QT_ANDROID_EXTRA_LIBS` and loaded at runtime from the app's
`nativeLibraryDir`; it is never linked into `libcolosseum`.

With the option OFF (default) the build is unchanged: the Android Stream
stub stays unavailable and desktop/Windows behavior is untouched.

## Verification

Focused host test (no engine library, no network — a scripted C provider
drives the production adapter):

```
cmake -S native -B native/build-nuvio-host -G Ninja ^
  -DCOLOSSEUM_NUVIO_BACKEND_TESTS_ONLY=ON ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build native/build-nuvio-host --target tst_nuvio_stream_backend
ctest --test-dir native/build-nuvio-host -R nuvio_stream_backend --output-on-failure
```

Source-layout contracts: `python tests/test_android_nuvio_backend_contract.py`.

Donor regression (loopback-only corrupt-piece integrity test):

```
cmake -S <nuvio-engine> -B <nuvio-engine>/bh -G Ninja ^
  -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON -DNUVIO_ENGINE_BUILD_TESTS=ON
cmake --build <nuvio-engine>/bh --target nuvio_engine_stream_integrity
ctest --test-dir <nuvio-engine>/bh -R stream_integrity --output-on-failure
```

## Final-source qualification boundaries (2026-10-18)

The host backend now keeps the engine and thread in independently owned
worker state; QObject destruction fences queued publications and transfers
the joinable worker to the process-lifetime reaper without waiting for
in-flight create, poll, or destroy. Backgrounding synchronously advances
its lifecycle epoch, and deferred playback retains its original generation.
The GUI and worker still require an Android runtime test for real Media3
Range delivery and lifecycle.

Preparation is intentionally fail-closed. Use `--verify-only` with the
real pinned donor after exporting the exact final git diff as the vendored
patch. It materializes the patch on a pristine pinned archive, compares
all changed source files, and rejects tag/pin/drift errors. The artifact
package gate additionally needs an actual donor build output matching
the packaged library, 16 KiB-aligned AArch64 ELF load segments, genuine
dynamic C API exports, and matching provenance. A self-consistent stamp or
synthetic ELF header is not an accepted artifact. None of this substitutes
for device JNI or Media3 verification.

As of the 2026-10-18 review, the pinned libtorrent source's 12 dependency
patches are not reproducibly preparable from pristine: isolated apply
checks rejected eight of twelve, and the CMake sentinel for the first
patch does not match its actual inserted declaration. A prepatched
developer build tree is **not** a substitute. The Android ON build and
package acceptance remain blocked until a separate authorized change
repairs and verifies the donor dependency patchset/CMake preparation.
Do not manually preapply it or bypass the sentinel gate.

Host and donor verification evidence, including passing and failing tests,
is in the bounded Preflight Nuvio review receipt for task
`nuvio-finish-001-20261018`. Android ARM64 prerequisites include an
installed NDK; a Qt ARM64 kit alone does not satisfy this gate.

## Limitations and unverified gates

- No device/emulator install or launch has been performed in this slice:
  on-device JNI/package/runtime behavior is unverified.
- No live torrent/tracker/DHT traffic has been exercised; internet peer
  transport is preserved but unqualified.
- `streamReady`/`fetchReady` publish an engine-validated loopback URL only;
  they do not claim decoded playback.
- Engine destruction may spend seconds collecting resume saves; no fixed
  shutdown bound is claimed.
- No foreground-service or background-playback promise: the backend quiesces
  on background and reconstructs on the next explicit foreground demand.
- Licensing: upstream GPL-3.0 notices preserved verbatim; internal prototype
  authorization is not shipping/licensing approval, and dynamic linking does
  not remove GPL obligations.
