# Task 013 progress record (interim, not the final reply)

Task `claude-android-cloud-013-20261020`. Import `5e60f3630e2d7f56ae49556c0472c4b5391dcfe2`
(upstream `fdd23f09…`). Worker branch `claude/android-stabilization-013-20261020`.
Updated at each checkpoint so a session interruption loses no findings.

## C0 — complete
- VM: Ubuntu 24.04, 4 vCPU Xeon 2.1 GHz, 15 GiB RAM, no swap, ~30 GB disk allowance,
  **no KVM** (no vmx/svm). Receipt: `C0-ENVIRONMENT.json`.
- `verify_source.py` with `CLOUD-SOURCE-MANIFEST.json`: PASS 4644/4644.
- Source route: worker-branch push + fresh-clone round trip verified.
- APK route: 273,678,336-byte probe sent via the session file route; Codex verified
  SHA256 `9af0893d…a7ac321` after download.
- Toolchain (repo `install_toolchain.sh`): Qt 6.11.1 gcc_64 + android_arm64_v8a +
  android_x86_64, NDK 27.2.12479018, platform 36, build-tools 36.0.0, JDK 21.0.12.
  `qualify_toolchain.py` 9/9 after a Linux fix (host `qmlimportscanner` is in `libexec/`).
- Donor: `restore_donor.py` working + pristine OK (tag peels to pin). Backend contract
  first run 30/1: `prepare_nuvio_engine.py` archived the "pristine" pin from the working
  donor whose `core.autocrlf=true` produced CRLF bytes. Fixed (export with
  `core.autocrlf=false`): 31 executed / 0 skipped / 0 failed. Pristine-as-source still fails closed.

## C1 — complete
- Supplied `fixture.mp4` is unsuitable: one `stss` sync sample (frame 251, 31.375 s);
  ffmpeg cannot seek to 10 s. Replaced by deterministic `fixture013/fixture013.mp4`
  (generated twice, identical SHA256 `cd109e11…b1af`; 60 s, 640x360, 12 fps, IDR every
  1 s, moov first, burned-in T/F/W markers + moving bar, AAC tone per second).
- Private torrent `fixture013.torrent` SHA256 `0a739d06…a9e0`, infohash
  `7f13e4dfe42445a675cccb6fda1e7549babad2a8`, 104 x 16 KiB pieces, announce
  `http://127.0.0.1:51413/announce` (the hardened engine admits only this tracker).
  Device reaches tracker/seeder through owned `adb reverse` mappings, so the same torrent
  serves emulator and Nokia.
- `fixture013.py` (ported tracker/seeder/probe): piece hashes verified before serving,
  LAN allow-list removed, malformed/foreign/out-of-bounds demand closes the connection.
  Probe PASS incl. foreign-infohash and out-of-bounds negative controls.
- Packaging: `build_apk.sh` now runs `test_android_runtime_packaging.py` against the real
  build tree and APK with skips as failures, incl. a new source -> overlay rule check.
  Negative controls: stale self-consistent APK passes the old verifier (rc 0) and fails the
  gate (rc 1); stale overlay copy fails (`changed qml/TopBar.qml`).

## C2 — in progress (Cloud x86_64 emulator evidence only)
Emulator: API 33 google_apis x86_64, software TCG (no KVM), no ARM translation
(`abilist=x86_64`) -> the ARM64 APK cannot run in this VM. Runtime evidence uses an
explicit x86_64 diagnostic build (`COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=ON`).
Playback runs ~0.16x real time under TCG.

Defects found from runtime evidence and fixed (regression first failed on old code):
1. Route 404 after idle: engine 30 s `stream_inactivity_timeout` erased a fully buffered
   route mid-playback; backend ignored `STREAM_STOPPED` and reused the dead URL.
2. No demand teardown: backend never called `remove_torrent`; close/switch left torrents
   live. Added `NuvioStreamBackend::stop()` / `Stream.stopDemand()`, release on switch,
   stale and failed chains, reopen waits for `TORRENT_REMOVED`.
3. SIGSEGV on background: `PowerStore::applyPlatformInhibit` read `result()` of a
   cancelled `runOnAndroidMainThread` future (1 s deadline).
4. Resume black/failed: background quiesce retires the route; restore re-prepared the dead
   URL and wake-reconnect reloaded it. PlayerPage now re-demands and reopens at the saved
   position; diagnostic package proves isolation on the new session before re-admission.
5. Isolated sessions refused re-admission over their own saved resume state; isolated
   (diagnostic-only) sessions now start from empty task-owned roots.
6. Restore position was cleared by `resetRecoveryWatch()`; kept in its own property.

Host `tst_nuvio_stream_backend`: 33 passed / 0 failed / 0 skipped (baseline 26/0/0 after a
host-build include fix).

Runs (logs/screenshots retained for the evidence bundle, not committed):
- run4: isolation PASS, admission PASS, 1,691,605 bytes served once, frames T=1.333 ->
  9.083 -> 23.500 (matching F markers), seeks to W=10 and exactly 9.000.
- run5: crash fixed (same PID across background); seek back Range open at 252,388,
  1,439,217 bytes to EOF.
- run6: INVALID — guest `system_server` watchdog death (DeadSystemException), environment.
- run7: background/foreground same PID; re-demand: roots reset, new session verify +
  admission PASS, new route, full refetch; close: route retired, torrent released.
  Restore position bug (6) found here; one AAC decoder watchdog timeout after resume
  (`previous call to queue exceeded timeout`), environment-suspected, not reproduced.
- rep1-3 (APK `9e30c0a0…`, source `d833e45`, x86_64 API 33, fixture infohash
  `7f13e4df…`): all 3 completed, 0 timeouts, 0 fatal, 0 HTTP 404, 0 playback errors, PID
  stable across background and close, isolation PASS on distinct sessions
  (`0x70948ea8e2d0`, `0x70948e998e70`, `0x70948e9a29b0`), re-demand after foreground,
  2 x 1,691,605 bytes served, 0 pieces after close, uploaded=0, torrent released on close.
  rep1 frames: 13.583 -> 31.250, seek fwd 51.083 (W=10), seek back exactly 9.000, prebg
  18.333 -> fg 27.166 -> 39.083 (position restored). rep2/rep3: the app's own
  "Resume from 0:40/0:38" prompt (progress kept from the previous rep) paused start at
  00.000, so start progression and the back-seek target frame were not captured; fwd seek
  W=10 (50.083/50.583), back-seek Range open at byte 252,388 then W=02 playback,
  post-resume progression 25.166 -> 35.916 / 26.166 -> 37.333.
  Summaries: `results/rep*-eval.json`; raw evidence sent via the file route.
- Cleanup: owned `adb reverse` mappings removed, no owned fixture processes, emulator stopped
  (`results/final-ownership.txt`). Installed APK == built APK (`9e30c0a0…`).

## Known baseline failure (not task 013)
`tests/test_android_media3_item_contract.py` fails identically on the import:
expects `qmlRegisterType<AndroidMedia3Item>` in `native/main.cpp` (unchanged).

## Next
ARM64 final rebuild (running) + package verification -> checkpoint + restore
drill -> evidence bundle -> REPLY-CLAUDE-ANDROID-013.md + .done.
