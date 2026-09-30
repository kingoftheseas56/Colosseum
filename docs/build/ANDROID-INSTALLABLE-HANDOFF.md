BLOCKED

# Android installable — handoff (2026-09-30)

**Blocking step:** publishing the branch. Everything up to "two signed APKs built locally" is done
and committed on `android/installable` in the cloud session. The push was refused, so no CI run,
emulator smoke, or desktop CI run exists yet.

```
$ git push -u origin android/installable
remote: Claude doesn't have GitHub access to kingoftheseas56/Colosseum for your organization. ...
fatal: unable to access 'https://github.com/kingoftheseas56/Colosseum/': The requested URL returned error: 403
```

Reads (fetch, Actions API) work. Writes need the Claude GitHub App installed on the repo, or a
reconnected GitHub account at https://claude.ai/connect-github. After that, push the branch
(it touches no other branch) and the `android` workflow runs by itself.

## Status by gate (evidence level in brackets)

| Gate | State |
| --- | --- |
| merged | yes: `origin/master` a602b86c + `origin/recon/android-master-2026-09-05` 0877d48c (merge 9dd861b2) [committed locally] |
| deps script works | yes, both ABIs [built locally, ~10 min per ABI on 4 cores] |
| arm64-v8a APK built | yes [built locally] |
| x86_64 APK built | yes [built locally] |
| emulator installs | not run: no KVM in the cloud container, and CI never ran (push blocked) |
| emulator launches | not run |
| alive after 60 s | not run |
| desktop CI green on the branch | not run (push blocked). See "Desktop CI on master is already red" below |

Local APKs (debug build, re-signed with the stable test key below):

| File | SHA-256 |
| --- | --- |
| `colosseum-arm64-v8a-debug.apk` | `9cdd90ee64dff1831c5e3b4666b3cea662a44d98b6e01510be5d7e85b08d3d7a` |
| `colosseum-x86_64-debug.apk` | `af545c96df83c9f3edbee233238c11de05c3a69fe97b42fcb08ff17ad3c04fb2` |

These are the local build's hashes. CI builds are rebuilt from source and will hash differently.

## What was done

### Merge (master is the trunk: recon was merged *into* a branch cut from master)
There were 16 conflicted files. Desktop behaviour was kept everywhere:
- `native/CMakeLists.txt`: master's new trackers/stremio/tankoyomi sources stay in the shared
  target. Windows credential/clipboard, updater, mpv and streamserver sources stay in recon's
  `if(NOT ANDROID)` block.
- `AccountRuntime`: keeps both master's `StremioSyncOptions` constructor and recon's per-platform
  credential store. `loadStremio/saveStremio/clearStremio` became `AccountCredentialStore` virtuals
  that fail closed by default; the Windows store overrides them. The Android store and the
  "unavailable" store fail closed for master's new pending-deletion API.
- `main.cpp`: master's ratings fixture and frame/poster probes plus recon's platform runtime and
  Back handling. `UpdateUserAgent.h` is no longer platform-gated because the catalogue fetch uses
  it on every host.
- QML focus/TV conflicts (KeyboardAction, TopBar, Taskbar, tab bars, CataloguePosterCard,
  ContinueRow, PlayerPage stats) take master's side: FocusRing and the async mpv stats. TV mode
  and player UX are out of scope. `tests/test_android_tv_navigation.py` no longer requires recon's
  `focusFrameWidth` or the Settings/Keyboard Guide taskbar buttons, which master removed.
- The keyboard censuses were regenerated from the merged QML.
- `.github/clang-tidy-allowlist.txt`: the two `main.cpp` leak lines moved. Both sides' line numbers
  were mapped onto the merged file. Unconfirmed until clang-tidy runs, and stale entries don't
  fail the gate.

### Toolchain (all scripts, no machine state)
- `scripts/android/install_toolchain.sh`: Qt 6.11.1 through aqtinstall 3.3.0 (host `linux_gcc_64`,
  `android_arm64_v8a`, `android_x86_64`, plus qtwebsockets and qtimageformats), SDK platform 36,
  build-tools 36.0.0, NDK 27.2.12479018, and a JDK 21 check. It writes `env.sh`.
- `scripts/android/build_deps.sh`: OpenSSL 3.5.9, Boost 1.90.0 (headers plus static filesystem;
  system is header-only) and libtorrent-rasterbar 2.0.14. All static, C++17, API 28, one prefix per
  ABI, checksum-pinned sources, and a stamp so a cached prefix is never rebuilt.
- `scripts/android/build_apk.sh <abi>`: single-ABI debug APK with the SHA-256 written next to it.
- `scripts/android/emulator_smoke.sh`: install, launch, 60 s hold, liveness check,
  logcat/screenshot/tombstones. Fails on `FATAL EXCEPTION`, a native crash signature, or a new
  tombstone.
- `native/CMakeLists.txt` (Android block only): `QT_ANDROID_ABIS` follows the configured kit's ABI.

Local reproduction:
```bash
scripts/android/install_toolchain.sh && source ~/colosseum-android/env.sh
scripts/android/build_deps.sh              # arm64-v8a x86_64
scripts/android/build_apk.sh x86_64        # -> native/build-android/out/
scripts/android/build_apk.sh arm64-v8a
```

### CI: `.github/workflows/android.yml` (push to `android/installable` only)
- Caches: Qt plus SDK/NDK (keyed on `install_toolchain.sh`), deps prefixes (keyed on
  `build_deps.sh`), and ccache.
- Builds both APKs, uploads the `colosseum-x86_64-debug-apk` and `colosseum-arm64-v8a-debug-apk`
  artifacts (each with a `.sha256` file), and writes the hashes to the job summary.
- `emulator-smoke`: ubuntu-latest with KVM enabled, reactivecircus/android-emulator-runner@v2,
  API 34 google_apis x86_64. Runs `emulator_smoke.sh` and uploads artifact
  `emulator-smoke-x86_64` (`screenshot.png`, `screenshot-25s.png`, `logcat.txt`,
  `logcat-crash.txt`, tombstones).

### Stable test signing
Every CI APK is re-signed with one dedicated **test-only** key (never a release key), so a new
build installs over the previous one.
- Keystore: PKCS12, alias `colosseum-test`, RSA 3072, 25-year validity,
  DN `CN=Colosseum Android TEST ONLY, O=Colosseum, OU=CI test signing (not a release key)`.
  Certificate SHA-256: `75:34:5E:F0:68:94:FD:80:4B:A4:96:7A:F3:F8:65:00:FB:8B:01:03:F7:83:E7:64:BE:BB:C3:39:32:CD:EB:A2`.
- It is **not in the repo**. It was handed to Hemanth outside Git. Add two repository secrets
  (Settings → Secrets and variables → Actions):
  - `ANDROID_TEST_KEYSTORE_B64`: `base64 -w0 colosseum-test.jks`
  - `ANDROID_TEST_KEYSTORE_PASSWORD`: the store/key password (both are the same)
- The workflow decodes it into `$RUNNER_TEMP`, and `build_apk.sh` re-signs with
  `apksigner` (build-tools 36.0.0). Without the secrets, the build emits a warning and keeps Qt's
  per-run debug signature, so builds will not install over each other.
- Locally: `COLOSSEUM_ANDROID_KEYSTORE=... COLOSSEUM_ANDROID_KEYSTORE_PASSWORD=... scripts/android/build_apk.sh <abi>`.
- To regenerate: `keytool -genkeypair -storetype PKCS12 -keystore colosseum-test.jks -alias colosseum-test -keyalg RSA -keysize 3072 -validity 9125`,
  then update both secrets. Devices must uninstall once after a key change.

## Desktop CI on master is already red
`desktop-ci` runs 698–702 on master all fail in about 40 s at `public-trust`
(`scripts/check_public_paths.py`), which skips every desktop build job. The cause is a private
absolute path on line 5 of `docs/superpowers/specs/2026-09-29-discover-sidebar-design.md`. This
branch drops that path (commit 97c1c7b2, docs only) so desktop CI can actually run here.
`check_public_paths.py`, `tests.test_public_path_guard` and `tests.test_linux_runtime_dependency_gate`
pass locally. The Windows/Linux desktop builds have not been run on the merged tree.

## Known gaps (expected, not addressed)
- Qt's TLS plugin (`qopensslbackend`) is packaged, but no `libssl_3.so`/`libcrypto_3.so` is
  shipped, so Qt Network HTTPS will fail at runtime on Android. OpenSSL is linked statically only
  for libtorrent. That should break catalogues, not launch. The fix is shared OpenSSL in
  `build_deps.sh` plus `QT_ANDROID_EXTRA_LIBS`.
- androiddeployqt warns about unresolved `Colosseum.Activity/Bridge/Player` imports (they are
  registered in C++ at runtime) and `QtWebEngine`. On Android that import is used only by the
  lazily loaded `ExtensionsSetupSheet.qml`, which will fail when opened.
- Torrent playback is expected not to work (no Node runtime by design; the native stream server
  isn't finished).

## Next steps once push access exists
1. Push `android/installable`. Watch the `android` run (build, then emulator-smoke) and `desktop-ci`.
2. If the smoke fails, start from `emulator-smoke-x86_64/logcat.txt` (`[boot]` lines,
   `FATAL EXCEPTION`, `Fatal signal`). Stop rule: three distinct fixes for the same crash, then
   BLOCKED with the evidence.
3. When the smoke and desktop CI are green, change the status word to COMPLETE with the run URL
   and artifact names.
