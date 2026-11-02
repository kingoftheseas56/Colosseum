# C0 — qualify the cloud environment before Phase 1

C0 succeeds when this cloud session can run the actual Beta Android-selected QML graph and focused fixtures on matching host Qt. An Android APK is a separate, optional gate. Do not spend the remaining credits rebuilding an entire Android environment before establishing useful host evidence.

Prepared from clean Beta `501b23edba022a82666bd25e64f4f02ad8bb9a9f` on 2026-11-03. This file contains instructions for the Claude Code cloud Linux worker; none were executed on Laptop 1. The audit is evidence, not authorization for every finding. Parent supplies the accepted Phase 1 scope and source attachment/checkout.

## Bound the session

- Use the current cloud task and its actual checkout. Stale task ID `013` is not evidence that the new task, source, setup or cache is ready. Record current task identifier if exposed.
- The selected environment has Full network access and shell limits `BASH_DEFAULT_TIMEOUT_MS=3600000` / `BASH_MAX_TIMEOUT_MS=7200000`. These are ceilings, not a reason to run hour-long setup commands.
- The lightweight apt setup is reported to return zero even after a timeout. Verify tool files, versions, shared-library resolution and one real test; a green setup status alone does not qualify anything.
- Spend at most 5 minutes on inventory, 15 minutes on one host provisioning attempt and 10 minutes on host checks. Stop C0 environment work at 30 minutes. Allow one targeted dependency correction, at most 5 minutes, within that budget. Preserve the exact error and continue source/fake-fixture work if the host remains unavailable. Do not restart equivalent downloads or switch Qt versions repeatedly.
- Optional APK work is allowed only within START.md's bounded, resource-checked incremental gate after host qualification and a pushed checkpoint. C0 may report `HOST_PASS / APK_BLOCKED` and proceed with authorized host/source changes.
- Work only in this cloud checkout and a session-owned cache/artifact directory. Never refer to Laptop 1 D: caches as cloud inputs, delete system toolchains to gain space, read restricted Preflight operating material, or fetch unrelated project history. Do not upload credentials or local debug/release signing keys.

## 1. Inventory and provenance — no downloads yet

From the actual cloud repository root:

```bash
set -euo pipefail
export C0_ROOT="${TMPDIR:-/tmp}/colosseum-c0-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$C0_ROOT/logs" "$C0_ROOT/tmp"
export TMPDIR="$C0_ROOT/tmp"
{
  date -u; pwd; uname -a; cat /etc/os-release
  git rev-parse HEAD; git status --short
  nproc; free -h; df -h . "$C0_ROOT"
  for tool in python3 node java cmake ninja g++ pkg-config git curl unzip timeout; do
    command -v "$tool" || true
  done
  python3 --version; node --version || true; java -version || true
  cmake --version || true; ninja --version || true
  printf 'Existing configured roots:\n'
  printf '%s\n' "${COLOSSEUM_QT_HOST_ROOT:-unset}" "${ANDROID_SDK_ROOT:-unset}" "${ANDROID_NDK_ROOT:-unset}"
} 2>&1 | tee "$C0_ROOT/logs/inventory.log"
```

Verify that audited application commit `501b23edba022a82666bd25e64f4f02ad8bb9a9f` is an ancestor of the cloud handoff HEAD, and that initial product source matches it. The new handoff commit adds task inputs only; HEAD must therefore NOT be required to equal the old application commit. Record the actual starting/working branch and HEAD. Preserve intended task dirt; never reset/clean it. Record B0 visual reconciliation and later product changes as distinct commits.

CI uses Ubuntu 24.04, Python 3.12 and Temurin JDK 21. A different Linux image must be recorded rather than assumed equivalent. No explicit Node version is pinned in the Android workflow: record the actual version and demonstrate the `.mjs` tests work; do not label a chosen Node version a repository requirement. Python 3.12 is the tested CI baseline, not proof every lower version is incompatible.

Practical C0 admission policy, not measured download sizes: reserve at least 4 GiB free and 2 GiB available memory for host-only provisioning/probes. Before even considering a fresh Android native lane, require roughly 20 GiB free and 4 GiB available memory, a known cache or measured feasible download, plus a separate time budget. Record actual available resources and downloaded/cache sizes; these thresholds do not guarantee completion. Do not add swap or redesign the cloud image during C0.

## 2. Qualify only the Linux host kit first

Prefer an existing exact Qt **6.11.1** `gcc_64` host installation. Identify it from the configured environment/cache; verify `qmake -query QT_VERSION`, `bin/qmltestrunner`, QtTest/Quick/Controls imports, and `lib/pkgconfig/Qt6Quick.pc`. A directory named 6.11.1 is insufficient. Do not substitute distro Qt 6.4/6.8 or the desktop WebEngine build.

The Android workflow needs these runtime libraries: `libegl1 libgl1 libxkbcommon0 libfontconfig1 libx11-6 libdbus-1-3`, plus GLib (`libglib2.0-0t64` on its Ubuntu 24.04 image). The host graph probe additionally compiles a small C++ program with **g++ and pkg-config Qt6Quick**; Python and qmlcachegen alone cannot run that probe. Build essentials, CMake, Ninja, Git, curl, unzip, patch, binutils and Python venv/pip must actually be present. Test the current image before apt. If a dependency is missing, use the existing lightweight setup/Ubuntu CI package list for one bounded correction; select the GLib package for the actual distribution. Do not install desktop mpv, a full QtWebEngine build, or Android tools for host fixtures.

If exact host Qt is absent, use the existing desktop CI aqt source pin and Android workflow's small host module set. This is a host-only slice of the current CI installation, not a new toolchain:

```bash
export COLOSSEUM_ANDROID_TOOLCHAIN="$C0_ROOT/toolchain"
python3 -m venv "$COLOSSEUM_ANDROID_TOOLCHAIN/venv"
source "$COLOSSEUM_ANDROID_TOOLCHAIN/venv/bin/activate"
timeout --kill-after=15s 180s python -m pip install \
  'git+https://github.com/miurahr/aqtinstall.git@8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b'
timeout --kill-after=15s 600s aqt install-qt linux desktop 6.11.1 linux_gcc_64 \
  -O "$COLOSSEUM_ANDROID_TOOLCHAIN/Qt" -m qtwebsockets qtimageformats
export COLOSSEUM_QT_HOST_ROOT="$COLOSSEUM_ANDROID_TOOLCHAIN/Qt/6.11.1/gcc_64"
```

Record pip/aqt exit status and complete logs using `set -o pipefail` if adding `tee`. Network availability does not prove the Qt archive or Git pin is reachable. One metadata/download failure gets at most one targeted retry; do not build Qt from source or use an unpinned mirror. Record bytes/cache progress and stop at the host time cap.

Do **not** call `install_toolchain.sh` unconditionally for this step: it installs both Android kits and SDK/NDK by default, and requires JDK even when only host checks are needed. It uses aqtinstall 3.3.0 when `aqt` is absent; the pinned aqt source above comes from current desktop/QML CI. Record which route was used.

After selecting the verified host kit:

```bash
export QT_HOST="$COLOSSEUM_QT_HOST_ROOT"
export PATH="$QT_HOST/bin:$PATH"
export LD_LIBRARY_PATH="$QT_HOST/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$QT_HOST/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
"$QT_HOST/bin/qmake" -query QT_VERSION
"$QT_HOST/bin/qmltestrunner" -help > "$C0_ROOT/logs/qmltestrunner-help.log" 2>&1
ldd "$QT_HOST/lib/libQt6Gui.so.6" "$QT_HOST/plugins/platforms/libqoffscreen.so" \
  | tee "$C0_ROOT/logs/host-ldd.log"
if grep -q 'not found' "$C0_ROOT/logs/host-ldd.log"; then exit 2; fi
pkg-config --modversion Qt6Quick
pkg-config --variable=prefix Qt6Quick
pkg-config --cflags --libs Qt6Quick > "$C0_ROOT/logs/host-pkg-config.log"
```

The pkg-config prefix and Qt6Quick version must match this Qt host, not a distro Qt accidentally found first. If qmltestrunner or its QtTest imports are absent, record the missing host component; do not claim a kit passed because qmake exists. Missing offscreen plugins/shared libraries are environment failures, not QML regressions.

## 3. Run existing smallest checks, then the real staged graph

All commands below are cloud instructions, not completed results. Use one log per check, preserve real pipeline statuses and timeout code 124; a killed/partial run is unverified. No full desktop build is needed.

```bash
set -o pipefail
timeout --kill-after=10s 90s python3 tests/test_android_cache_paths.py \
  2>&1 | tee "$C0_ROOT/logs/cache-paths.log"
timeout --kill-after=10s 90s python3 tests/test_android_toolchain_qualification.py \
  2>&1 | tee "$C0_ROOT/logs/toolchain-unit.log"
timeout --kill-after=10s 120s python3 tests/test_android_runtime_bundle.py \
  2>&1 | tee "$C0_ROOT/logs/runtime-staging.log"
timeout --kill-after=10s 90s node tests/android_reader_boot_test.mjs \
  2>&1 | tee "$C0_ROOT/logs/reader-boot.log"
timeout --kill-after=10s 90s node tests/android_reader_selection_touch_test.mjs \
  2>&1 | tee "$C0_ROOT/logs/reader-selection.log"
timeout --kill-after=10s 90s node tests/android_reader_gesture_cancel_test.mjs \
  2>&1 | tee "$C0_ROOT/logs/reader-cancel.log"
timeout --kill-after=10s 90s "$QT_HOST/bin/qmltestrunner" \
  -input tests/qml/tst_account_choice_accessibility.qml \
  2>&1 | tee "$C0_ROOT/logs/qml-accessibility.log"
timeout --kill-after=15s 300s python3 tests/android_qml_preflight.py --qt-root "$QT_HOST" \
  2>&1 | tee "$C0_ROOT/logs/android-qml-preflight.log"
```

`test_android_runtime_bundle.py` invokes real staging CMake without an Android/Qt target toolchain; check that it did not skip for missing CMake. `android_qml_preflight.py` stages the actual `native/cmake/android.cmake` filtered tree, then uses the real Qt engine/Android selectors and literal loader roots. Its C++ probe mocks only PlayerItem, SeekThumbnailer and ActivityPlaybackTracker native registrations; a pass is QML compile evidence, not a native app launch or first usable frame. Do not treat a standalone qmlcachegen parse as equivalent to this graph check.

After the first QML fixture passes, run only the relevant existing protections needed for parent-approved Phase 1 work, one input at a time with the same environment and 90-second cap:

- `tests/qml/tst_android_shared_action_touch.qml` and `tst_android_shared_poster_touch.qml` for shared input ownership.
- `tests/qml/tst_android_beta_reader_touch.qml` for reader overlay/IME seams.
- `tests/qml/tst_player_more_touch.qml`, `tst_android_subtitle_viewport.qml`, `tst_player_seek_touch.qml` for existing player popup and cancellation protections.

The audit's PF-02 Sources budget, PF-05 actual speed popup, WT-02 account forms, WT-04 native font notification and BR-READER-TARGETS narrow/reparented cases need additional actual-owner fixtures if selected. Existing neighboring tests do not establish these findings fixed. Alpha-only fixture names in gap-register.csv are donor evidence, not tests that necessarily exist in Beta. Preserve desktop geometry and logical-coordinate policy; do not reinstate the old Android DPR division.

## 4. Optional APK gate — stop at missing prerequisites

Current live scripts, rather than historical prose, set the supported lane:

| Item | Current repository contract |
| --- | --- |
| Qt install | 6.11.1 matching host + `android_arm64_v8a`; scripts also install x86_64 |
| Android minimum | API 28 / Android 9 |
| Compile / target SDK | API 36; build tools 36.0.0 |
| NDK | r27c, exactly 27.2.12479018 |
| JDK | 21+; CI uses Temurin 21 |
| CMake / Ninja | qualification gate CMake >=3.22; current supported Ninja, plus C/C++ compiler |
| Gradle / AGP | preserve Qt 6.11 template/wrapper; docs identify 9.3.1 / 9.0.0, inspect installed template before claiming it matches |
| Android native dependencies | OpenSSL 3.5.9, Boost 1.90.0, libtorrent-rasterbar 2.0.14, API28/C++17, script checksum pins |
| Product ABI | arm64-v8a; x86_64 is a separate diagnostic lane |

`docs/build/android.md` contains historical W08 blockers and old device instructions. Do not repeat them as current failures or uninstall/clear application data to satisfy that old checklist. Actual CMake/scripts and parent-authorized physical-device qualification take precedence.

Before downloading SDK/NDK or building deps, report existing kits/prefix stamps, free disk/RAM, required uncached downloads and realistic remaining budget. Generic CI has a 150-minute build-job limit and persistent caches; an uncached cloud task cannot assume equivalent resources. No download sizes or completion time were measured for this cloud environment. C0 does not start a multi-hour cold native build.

The existing commands for an attempt admitted under START.md are:

```bash
# Prefer already provisioned exact caches. Otherwise run the installer only within
# a separately accepted budget; its default is BOTH Android ABIs, not host-only.
bash scripts/android/prepare_cache_paths.sh
# timeout --kill-after=30s <accepted-seconds>s bash scripts/android/install_toolchain.sh
source "$COLOSSEUM_ANDROID_TOOLCHAIN/env.sh"
python3 scripts/android/qualify_toolchain.py \
  --qt-root "$COLOSSEUM_ANDROID_TOOLCHAIN/Qt/6.11.1/android_arm64_v8a" \
  --host-qt-root "$COLOSSEUM_QT_HOST_ROOT" \
  --sdk-root "$ANDROID_SDK_ROOT" --ndk-root "$ANDROID_NDK_ROOT"
# Only if dependency prefix absent AND its cost has been accepted:
# JOBS=1 timeout --kill-after=30s <accepted-seconds>s bash scripts/android/build_deps.sh arm64-v8a
```

Do not pass `--require-device` in cloud C0. WT-14 is a known **host discovery gap**: `qualify_toolchain.py` currently searches only `bin/qmlimportscanner[.exe]`, while a Linux kit may have `libexec/qmlimportscanner`. Inspect both and record the real location. A bin-only gate failure with a working libexec scanner is not proof Qt is absent. Do not fake a pass with a symlink, change the gate during C0, or import Alpha's whole qualification script. START.md conditionally authorizes a bounded discovery fix plus bin/libexec fixture tests when the actual cloud kit establishes this mismatch. Real missing SDK/NDK/JDK failures remain failures.

### Playback engine availability is a separate prerequisite

The repository vendors Nuvio headers, manifest and hardening patch, **not** its compiled engine/donor checkout. `native/cmake/NuvioBackend.cmake` defaults `COLOSSEUM_ANDROID_NUVIO_BACKEND=OFF`; the generic Android workflow/build command does not enable it. A default APK therefore does not reproduce the playback-capable local Beta artifact.

For a matching product APK, parent must supply an authorized cloud-accessible donor checkout/prepared artifact with source provenance. Required donor baseline is `99a18601fa12858cc8c62576dc72b87edbd81b9a` (`v0.1.5`), API version 3, with vendored `0001-android-hardening.patch` and all twelve pinned libtorrent patches. A mutable branch/tag or unpatched release AAR is insufficient. Do not assume Laptop 1 D: libraries or private credentials are available in cloud, fetch them opportunistically, or silently disable the backend to claim equivalent APK success.

When the prepared cloud prefix is present, verify it with:

```bash
python3 scripts/android/verify_nuvio_hardened_artifact.py \
  --prefix "$COLOSSEUM_NUVIO_ENGINE_PREFIX" --abi arm64-v8a
```

`prepare_nuvio_engine.py --verify-only` validates the pinned donor/patch and materializes a temporary pristine proof; it does not build the Android engine or establish artifact readiness. A new engine build has its own dependency downloads and budget and is outside C0's default. Preserve strict artifact verification.

Only after all gates, scope and budget pass, the existing product-capable build route is:

```bash
export JOBS=1 CMAKE_BUILD_PARALLEL_LEVEL=1
export GRADLE_OPTS='-Dorg.gradle.workers.max=1 -Dorg.gradle.parallel=false -Dorg.gradle.jvmargs=-Xmx1536m'
export COLOSSEUM_ANDROID_CMAKE_ARGS="-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON -DCOLOSSEUM_NUVIO_ENGINE_PREFIX=$COLOSSEUM_NUVIO_ENGINE_PREFIX"
# Prefix paths must contain no whitespace: build_apk.sh word-splits these extra args.
# timeout --kill-after=30s <accepted-build-seconds>s bash scripts/android/build_apk.sh arm64-v8a "$C0_ROOT/apk-out"
```

Use existing script outputs/verifiers: SHA256, runtime bundle + matching qmlcachegen, OpenSSL pair. Also inspect final signature (`apksigner verify --print-certs`), package/version/ABI, engine provenance and 16 KiB alignment (`zipalign -c -P 16 -v 4`). A fresh cloud debug signature is not the installed phone's signer; it must not be presented as an update-compatible artifact. Do not transfer keys or install anything as part of C0. Source-only/host success remains useful when native engine, SDK, memory or time prevents an APK.

## C0 delivery and stop condition

Return a compact `C0-RESULT.json` or markdown receipt with actual commit/diff identity, Linux image/CPU/RAM/disk, tool versions and resolved Qt/SDK/NDK paths, dependency/Qt acquisition logs, commands + exit codes + durations, skipped/time-limited checks, graph summary/mocks, and any missing native engine/artifact/signing prerequisite. Classify independently: `SOURCE_CHECKS`, `HOST_QML`, `ANDROID_TOOLCHAIN`, `ANDROID_APK`; use PASS/FAIL/BLOCKED/NOT_ATTEMPTED with exact evidence.

After useful host qualification or the time limit, stop environment work and return to parent-approved Phase 1 implementation. Do not turn C0 into a full desktop build, emulator infrastructure project, source migration, release or publication. Cloud fixtures/compile/APK packaging do not qualify OnePlus 11R or Nokia T20 touch, native font callbacks, real IME, DRM, protected playback, audio routes, or device lifecycle. Those remain explicitly separate acceptance gates.

## Evidence used for this preparation

- `.github/workflows/android.yml`: Ubuntu24.04/Python3.12/JDK21, cache/setup graph, host dependency check, staged QML preflight, manual two-ABI APK lane.
- `.github/workflows/desktop-ci.yml` and `code-quality.yml`: Qt6.11.1 and pinned aqt source commit.
- `scripts/android/install_toolchain.sh`, `qualify_toolchain.py`, `build_deps.sh`, `build_apk.sh`, `prepare_cache_paths.sh`: actual current pins/defaults/commands.
- `tests/android_qml_preflight.py`, `android_qml_engine_compile.py`, `test_android_runtime_bundle.py`: real CMake staging, Linux g++/pkg-config probe and mock boundary.
- `native/cmake/android.cmake`, `NuvioBackend.cmake`, `native/third_party/nuvio-engine/SOURCE_MANIFEST.md`, `scripts/android/prepare_nuvio_engine.py`: filtered asset graph and separate hardened-engine prerequisite.
- `D:/Colosseum-Android-Alpha-Beta-Audit-20261102/ROADMAP.md` and `gap-register.csv`: 54 audit findings, bounded mechanism units, host/device separation; especially PF-01/PF-02/PF-05, BR-READER-TARGETS, WT-02/WT-04/WT-12/WT-14.
