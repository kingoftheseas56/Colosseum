# Task 013 reproducible Cloud build commands

Linux x86_64 (Ubuntu 24.04), JDK 21 on PATH, network to Qt/Google/Maven/GitHub release hosts.
Everything outside the repository lives under `$HOME/colosseum-android` and `/root/c013`.

```bash
# Toolchain (Qt 6.11.1 gcc_64 + android_arm64_v8a + android_x86_64, NDK 27.2.12479018,
# platform 36, build-tools 36.0.0) and qualification
bash scripts/android/install_toolchain.sh
source $HOME/colosseum-android/env.sh
COLOSSEUM_QT_ANDROID_ROOT=$HOME/colosseum-android/Qt/6.11.1/android_arm64_v8a \
  python3 scripts/android/qualify_toolchain.py            # 9/9

# App dependency prefix (OpenSSL 3.5.9, Boost 1.90.0, libtorrent 2.0.14; checksum-pinned)
JOBS=3 bash scripts/android/build_deps.sh arm64-v8a       # ~9.5 min
# (x86_64 only for the separate emulator diagnostic: build_deps.sh x86_64)

# Hardened Nuvio engine from the exact donor pin + vendored patch
python3 cloud-handoff/android-013/restore_donor.py /root/c013/donor-working
python3 scripts/android/prepare_nuvio_engine.py --source /root/c013/donor-working \
  --ndk $ANDROID_NDK_ROOT --abi arm64-v8a --api 28 --prefix /root/c013/nuvio-prefix
python3 scripts/android/verify_nuvio_hardened_artifact.py --prefix /root/c013/nuvio-prefix --abi arm64-v8a

# Final ARM64 diagnostic APK (Cloud debug signer; never a release key).
# build_apk.sh runs the source -> overlay -> APK correspondence gate (skips fail),
# the runtime bundle verifier and the OpenSSL pair check.
export COLOSSEUM_ANDROID_BUILD=/root/c013/build-android JOBS=4 \
  COLOSSEUM_ANDROID_KEYSTORE=/root/c013/keys/c013-cloud-debug.jks \
  COLOSSEUM_ANDROID_KEYSTORE_PASSWORD=<cloud-only> \
  COLOSSEUM_ANDROID_CMAKE_ARGS="-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON \
    -DCOLOSSEUM_NUVIO_ENGINE_PREFIX=/root/c013/nuvio-prefix \
    -DCOLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE=org.qtproject.example.colosseum.nuvio013cloud"
bash scripts/android/build_apk.sh arm64-v8a /root/c013/out/arm64-final

# Separate x86_64 emulator diagnostic (explicit opt-in; not a Nokia artifact)
#   prefix: prepare_nuvio_engine.py ... --abi x86_64 --prefix /root/c013/nuvio-prefix-x86_64
#   cmake args add -DCOLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=ON and use that prefix
bash scripts/android/build_apk.sh x86_64 /root/c013/out/x86_64

# Host checks
NUVIO_DONOR_SOURCE=/root/c013/donor-working python3 tests/test_android_nuvio_backend_contract.py
cmake -S native -B /root/c013/build-host -G Ninja -DCOLOSSEUM_NUVIO_BACKEND_TESTS_ONLY=ON \
  -DCMAKE_PREFIX_PATH=$HOME/colosseum-android/Qt/6.11.1/gcc_64
cmake --build /root/c013/build-host --target tst_nuvio_stream_backend
QT_QPA_PLATFORM=offscreen /root/c013/tst_nuvio_stream_backend   # binary lands in build dir parent
```

Notes: Maven Central intermittently returned HTTP 429 through the egress proxy; the
package step was retried with backoff until dependencies resolved (no source change).

## Fixture services and emulator cycle (Cloud x86_64 evidence only)

```bash
cd cloud-handoff/android-013/claude-013/fixture013
python3 -I fixture013.py track fixture013.torrent 51413 127.0.0.1 51412 tracker.jsonl &
python3 -I fixture013.py seed fixture013.torrent fixture013.mp4 51412 seed.jsonl &
adb reverse tcp:51413 tcp:51413; adb reverse tcp:51412 tcp:51412   # owned; removed afterwards
# push fixture013.torrent to the app's files/nuvio013-fixture.torrent (run-as), then
# results/c013_cycle.sh <out> all  (verify -> play -> seeks -> background/foreground -> close)
# results/eval_rep.py <out>        (derives the per-run JSON)
```
For a physical device the same torrent works unchanged through the same `adb reverse` ports.
