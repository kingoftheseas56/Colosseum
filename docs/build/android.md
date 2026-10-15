# Build and qualify Colosseum on Android

This document defines the first reproducible Android build lane for the existing Qt 6/C++/QML application. Android is a host for Colosseum, not a rewrite.

## Pinned first-APK baseline

Use the Qt 6.11 toolchain family already selected by the desktop project. For the first APK, build one ABI only: `arm64-v8a`.

| Tool / platform | Required baseline |
| --- | --- |
| Qt | 6.11.x Android `android_arm64_v8a` kit plus matching host Qt |
| Android runtime | Android 9 / API 28 minimum |
| Compile + target SDK | API 36 |
| SDK build tools | 36.0.0 |
| NDK | r27c, `27.2.12479018` |
| JDK | 21 or newer |
| CMake | 3.22 or newer; repository policy remains `cmake_minimum_required(3.16)` |
| Ninja | Qt-supported current Ninja; use the Qt-installed copy when practical |
| Gradle / AGP | Qt 6.11 template pair, Gradle 9.3.1 / AGP 9.0.0 |

Run `python3 scripts/android/qualify_toolchain.py` before configuring. The gate is intentionally fail-fast and can also require a connected physical ARM64 device with `--require-device`.

Authoritative Qt references:
- https://doc.qt.io/qt-6/android.html
- https://doc.qt.io/qt-6/android-configure-dev-environment.html
- https://doc.qt.io/qt-6/android-building-projects-from-commandline.html
- https://doc.qt.io/qt-6.11/cmake-supported-cmake-versions.html

## Native dependency boundary

Every native library loaded into the first APK must be built for the same `arm64-v8a` ABI with NDK `27.2.12479018` and a compatible C++ runtime/configuration.

Required before FIRST-APK:
- Qt 6.11.x for `arm64-v8a` and its matching host tools.
- OpenSSL 3 for Android arm64. Colosseum uses OpenSSL directly and through libtorrent; package the same arm64 build with the APK rather than relying on a device copy.
- Boost and libtorrent-rasterbar built for Android arm64 with C++17 and mutually consistent feature macros.
- The Aqueduct native core once W04 lands it. Android must not package or depend on the Stremio Node runtime.
- Colosseum shared C++ sources after W01/W02/W04/W05/W06 platform gates remove desktop-only compile dependencies.

Qt documents `QT_ANDROID_EXTRA_LIBS` / `add_android_openssl_libraries()` for packaging OpenSSL. Libtorrent documents CMake/Ninja builds and requires C++17. Do not mix host libraries into the target prefix.

References:
- https://doc.qt.io/qt-6/android-openssl-support.html
- https://libtorrent.org/building.html

## CMake ownership seam

W08 does not rewrite `native/CMakeLists.txt`; W01 owns that build graph. Android packaging becomes valid only after the application target is created with `qt_add_executable()` on Android so Qt can generate deployment settings and the `apk` / `aab` targets.

The Android branch should set these target properties for the first ABI:

```cmake
QT_ANDROID_ABIS "arm64-v8a"
QT_ANDROID_MIN_SDK_VERSION 28
QT_ANDROID_COMPILE_SDK_VERSION 36
QT_ANDROID_TARGET_SDK_VERSION 36
QT_ANDROID_APP_NAME "Colosseum"
QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
```

A stable `QT_ANDROID_PACKAGE_NAME` and monotonically increasing `QT_ANDROID_VERSION_CODE` must be chosen before distribution. Do not silently turn a temporary development package ID into the permanent product ID.

## Gradle and manifest seam

Keep Qt's Gradle wrapper, Gradle version, AGP version, QtActivity glue, and generated `libs.xml` under Qt ownership. Do not create an independent Android Studio application around Colosseum.

For PRE-APK, the default Qt 6.11 Android template is sufficient. When W02/W07 need Java/Kotlin, services, resources, or TV metadata, copy the template from the installed `android_arm64_v8a/src/android/templates` tree into a repository `android/` package source and connect it with `QT_ANDROID_PACKAGE_SOURCE_DIR`. Do not hand-author `libs.xml`.

The base manifest contract is:
- exported launcher activity using the Qt 6.11 activity/template contract;
- `android.permission.INTERNET` for catalogues, accounts, extensions, and torrent transport;
- hardware acceleration enabled and orientation left unspecified so phone/tablet rotation can be qualified;
- no broad storage permission added by W08; W05 owns MediaStore/SAF requirements;
- no playback foreground service; background playback is out of scope;
- W02 owns foreground-download service/notification permissions and lifecycle integration;
- W07 owns Android TV / Leanback declarations and TV-specific input/features.

API levels should come from Qt CMake target properties rather than a divergent `<uses-sdk>` block.

Reference: https://doc.qt.io/qt-6/deployment-android.html

## Configure and build

After the build-graph/platform blockers below are integrated, configure through the target kit's `qt-cmake`, not host CMake directly:

```bash
export QT_ANDROID_ROOT="$HOME/Qt/6.11.1/android_arm64_v8a"
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
"$QT_ANDROID_ROOT/bin/qt-cmake" -S native -B native/build-android-arm64 -GNinja \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DQT_ANDROID_ABIS=arm64-v8a
cmake --build native/build-android-arm64 --target apk --parallel 1
```

The `apk` target is the FIRST-APK/device format. Use `cmake --build native/build-android-arm64 --target aab --parallel 1` only after the APK lane is green and Play-style distribution packaging matters. Qt runs `androiddeployqt` and Gradle under these CMake targets.

## Low-RAM Linux host

The old touchscreen Linux laptop has 3.7 GiB RAM plus 3.9 GiB swap, CMake 3.28.3 and Ninja 1.11.1. Its CPU/RAM are sufficient for a serialized cross-build, but it currently has only Qt 6.10.3 `gcc_64` and no JDK, Android SDK/NDK, ADB, or Qt Android kit.

After installing the pinned toolchain, keep native compilation and Gradle serialized:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=1
export GRADLE_OPTS='-Dorg.gradle.workers.max=1 -Dorg.gradle.jvmargs=-Xmx512m'
cmake --build native/build-android-arm64 --target apk --parallel 1
```

Do not enable multi-ABI builds on this machine for FIRST-APK. Gradle documents `org.gradle.workers.max` and `org.gradle.jvmargs`; the serialized settings are deliberate memory controls, not performance defaults.

Reference: https://docs.gradle.org/current/userguide/build_environment.html

## Qualification ladder

Treat every stage as a separate gate. Never promote compile evidence into runtime evidence.

1. **Toolchain**: `qualify_toolchain.py` passes with the pinned versions.
2. **Configure**: Android `qt-cmake` completes with no desktop-only Qt/module/library resolution.
3. **Compile**: `colosseum` arm64 native target links with Qt, OpenSSL, Boost/libtorrent, Aqueduct, and shared C++.
4. **Package**: `--target apk` produces an installable debug APK and `androiddeployqt --verbose` shows required Qt/native dependencies bundled.
5. **Install**: `adb install -r <apk>` succeeds on a physical ARM64 phone/tablet.
6. **Launch**: launcher intent reaches the Qt activity, the process remains alive, and Home renders without WebEngine/desktop-runtime errors.
7. **Logcat**: capture startup through first usable frame; reject `FATAL EXCEPTION`, linker errors, missing `.so`, QML import failures, SSL backend failure, or repeated ANR evidence.
8. **Rotate**: physically rotate portrait -> landscape -> portrait; preserve route, selected item, and session state without process death or layout corruption.
9. **Background/return**: send Colosseum to Home, wait at least 30 seconds, return from Recents/launcher, and verify state survives expected Android lifecycle transitions.
10. **Uninstall/reinstall**: uninstall, reinstall the same APK, launch clean, and verify no dependency on stale app-private files.
11. **Waydroid smoke**: useful for launch/input/layout smoke only. Never use it as codec, MediaCodec, DRM, power-management, storage-provider, or final lifecycle authority.
12. **AAB**: only after physical-device APK qualification is green; build and inspect the AAB as a distribution artifact.

Useful device commands once a stable package ID exists:

```bash
adb devices
adb install -r <apk>
adb logcat -c
adb shell monkey -p <package-id> -c android.intent.category.LAUNCHER 1
adb shell pidof <package-id>
adb logcat -d > colosseum-android-logcat.txt
adb shell input keyevent KEYCODE_HOME
adb shell monkey -p <package-id> -c android.intent.category.LAUNCHER 1
adb uninstall <package-id>
adb install <apk>
```

For the final device gate, rerun `qualify_toolchain.py --require-device` so the connected authority is proven to advertise `arm64-v8a`.

## Current blockers at W08 base `58b34865`

These are observed repository facts, not Android guesses:
- `native/CMakeLists.txt` unconditionally requests `Qt6::WebEngineQuick` and `Qt6::WebChannel`; Android WebEngine is explicitly out of product scope.
- the `colosseum` target is created with plain `add_executable()`, so Qt's Android deployment/ABI target machinery is not established;
- MpvQt/libmpv setup falls through the non-Windows path that is documented and coded as Linux-specific;
- `native/main.cpp` unconditionally includes `QtWebEngineQuick` and calls `QtWebEngineQuick::initialize()`;
- Reader2 harness/build wiring still links WebEngine; W06 owns the native Android reader replacement;
- platform/account, Vault, player/Aqueduct and Android lifecycle seams are being handled by W02/W04/W05 and must land before full runtime qualification;
- neither Laptop 1 nor the old Linux touchscreen laptop currently has the pinned Android SDK/NDK/JDK/Qt Android kit installed.

Do not "solve" these from W08 by deleting desktop code. W01 must platform-gate the build graph, W04/W06 must supply their Android paths, and the lead integrates those changes before the first configure gate.

## Delivery order

**PRE-APK**: pin toolchain, install Android kit/SDK/NDK/JDK, build arm64 OpenSSL + Boost/libtorrent prefix, land W01 platform gates, land the minimum W02/W04/W05/W06 compile seams, and make the toolchain/configure gates green.

**FIRST-APK**: build only `arm64-v8a`, package a debug APK with Qt's Gradle template, install on a physical ARM64 device, launch Home, collect logcat, rotate, background/return, and uninstall/reinstall.

**POST-APK**: background-download Android service, MediaStore/SAF depth, Android TV declarations, release signing, AAB, multi-ABI, Play-specific metadata, and broader device matrix.

Waydroid remains smoke-only throughout. A physical ARM64 device is the runtime/codec authority.
