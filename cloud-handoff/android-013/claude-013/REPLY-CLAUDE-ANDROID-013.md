# REPLY — claude-android-cloud-013-20261020

**Verdict: PARTIAL.** The Cloud build/package and x86_64-emulator runtime gates have
evidence. ARM64 runtime, Nokia acceptance and audibility are **NOT RUN / PENDING**:
this VM has no KVM and no ARM translation, so the ARM64 APK could not be executed in Cloud.
No integration, PR, CI dispatch or release was performed. This is not Codex acceptance.

## Identity
| | |
|---|---|
| Import | `5e60f3630e2d7f56ae49556c0472c4b5391dcfe2` (upstream `fdd23f09…`), `verify_source.py` PASS 4644/4644 |
| Worker branch | `claude/android-stabilization-013-20261020` (baseline branch untouched) |
| ARM64 APK source | `37fbb576d4444657f442377abbbd1d596078fd71` (later commits change only this directory) |
| Source checkpoint | `c01f801ee5ddd9cc793bbe5ce0be7d0d98e059c5`, `SOURCE-CHECKPOINT.zip` SHA256 `a4babd5d21bff486ac8c93e9649018c77de661ffb15691091c37f9cd811d6aef` (224,201,681 B, 4,720 files) |
| Delta | `checkpoint/SOURCE-DELTA.patch` + `checkpoint/NEW-FILES.json`, restore with `checkpoint.py restore` |

## Final ARM64 diagnostic APK (build/package evidence only)
`colosseum-arm64-v8a-debug.apk`, **253,345,182 bytes**, SHA256
**`8307da040620a0d5013063e347c18943529dab71cd008653ded4385313d3cb1e`**.
Package `org.qtproject.example.colosseum.nuvio013cloud` 1.1.9 (versionCode 1), minSdk 28,
targetSdk 36, `arm64-v8a` only, 16 KiB zipalign OK. **New Cloud debug signer**
`CN=Colosseum task013 Cloud diagnostic debug`, cert SHA-256 `c3395704…741e`. It cannot
update the older Nokia diagnostic package and does not try to.
Engine member == stamp-verified hardened prefix (`736ccdf8…`, pin `99a18601fa12` +
vendored patch, AArch64 ELF). Runtime bundle `30797815…` == generated overlay; correspondence
gate `PACKAGING_GATE_OK tests=6 skipped=0`, OpenSSL pair OK. Backend option and diagnostic
package default OFF/empty; this build enables them explicitly. Delivered via the session
file route (the large-file route Codex verified with the 261 MiB probe).

Separate, **not final**: x86_64 emulator APK SHA256 `9e30c0a0…ef6` (source `d833e45`,
`COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=ON`). Every runtime result below is tied to it.

## Root causes found and fixed (each regression first failed on old code)
1. **Route 404 after idle.** The engine's default 30 s `stream_inactivity_timeout` erased a
   fully buffered route mid-playback. The backend ignored `STREAM_STOPPED` and kept reusing
   the dead URL (seek/reload/resume -> 404). Fix: player-owned routes (timeout 0) plus
   handling stopped routes.
2. **No demand teardown.** The backend never called `remove_torrent`. Close, switch and stale
   chains left torrents live. Fix: `NuvioStreamBackend::stop()` / `Stream.stopDemand()`
   (desktop no-op; minimize never calls it), release on switch/stale/failed, and a reopen
   during removal waits for `TORRENT_REMOVED`.
3. **SIGSEGV on background.** `PowerStore::applyPlatformInhibit` read `result()` of a
   `runOnAndroidMainThread` future that its 1 s deadline had cancelled.
4. **Resume to black/failed.** The background quiesce retires the route, but the foreground
   restore and wake-reconnect reloaded the dead URL. Fix: re-demand the same source and reopen
   at the saved position (its own property; `resetRecoveryWatch()` had cleared it).
5. **Isolated re-admission.** The isolated engine (correctly) refuses saved state. Isolated
   diagnostic sessions now start from empty task-owned roots; ordinary sessions keep resume data.
6. **Packaging.** A stale but self-consistent APK passed the old verifier. `build_apk.sh` now
   runs the source -> overlay -> APK gate with skips as failures.
7. **Fixture.** The supplied `fixture.mp4` has a single `stss` sync sample at 31.375 s
   (ffmpeg cannot seek to 10 s). It is replaced by a deterministic generated clip with 1 s
   GOPs and burned-in T/F/W markers.
8. **Portability.** pristine-pin export CRLF (contract 30/1 -> 31/0), Linux host
   `qmlimportscanner` path, host test include, a hardcoded `nuvio010isolated` id replaced by
   an explicit configure value, ABI-aware engine gate (x86_64 only by opt-in).

## Cloud x86_64 emulator results (API 33, software TCG ~0.16x real time; not ARM64, not Nokia)
Isolation was proven on each live session before demand: DHT/LSD/UPnP/NAT-PMP false,
`max_pex_peers=0`, no bootstrap, `torrent_count=0`, loopback listen. Private tracker/seeder
only, `uploaded=0` throughout.

| Run (APK 9e30c0a0, src d833e45) | Session | Play | Fwd seek | Back seek | Bg/fg | Close |
|---|---|---|---|---|---|---|
| rep1 | 0x70948ea8e2d0 | PASS 13.583->31.250 | PASS 51.083 W=10 | PASS 9.000, Range @252388 | PASS same PID, 18.333 -> 27.166 -> 39.083 | PASS released, 0 pieces after |
| rep2 | 0x70948e998e70 | NOT CAPTURED at start (app's own "Resume from 0:40" prompt); progression shown later 14.583->35.916 | PASS 50.083 W=10 | PARTIAL: frame hidden by prompt; Range @252388, then W=02 | PASS same PID, re-demand, 25.166->35.916 | PASS |
| rep3 | 0x70948e9a29b0 | as rep2 (prompt 0:38); 15.083->37.333 | PASS 50.583 W=10 | PARTIAL as rep2 | PASS | PASS |

All 3: 0 timeouts, 0 fatal, 0 HTTP 404, 0 playback errors, exactly 2 x 1,691,605 bytes
served (initial fetch + fresh-session re-demand). Owned `adb reverse` mappings and
fixture processes were removed afterwards; installed APK == built APK.

So there is **one fully clean repetition (rep1)**. rep2/rep3 complete every lifecycle/seek/close
step, but their start-progression and back-seek target-frame captures were blocked by the
app's resume prompt (progress remembered from the previous rep). These are reported as-is,
not re-run.

## Defects / observations retained, not fixed (scope frozen)
- After foreground return, `StreamServer.play` re-demand runs twice per run (one more from the
  PlayerPage recovery path). It is harmless (same session, chain reuse) but unexplained.
- run7: one `c2.android.aac.decoder` "previous call to queue exceeded timeout" -> codec error
  after resume. Environment-suspected (guest starvation), not reproduced in rep1-3.
- First latched frames can draw stretched for ~250 ms until the queued video size arrives.
- `tests/test_android_media3_item_contract.py` fails identically at the import (pre-existing).
- run6 ENVIRONMENT-BLOCKED: guest `system_server` watchdog death; excluded.

## Pending for Codex / Nokia
ARM64 runtime of `8307da04…` (install as a new package; new signer). Fixture play, forward
and back seeks, background/foreground, close teardown on the T20 via the same torrent and
`adb reverse` ports. AudioTrack/decoded/audible audio (Cloud audio was `-no-audio`: **no
audibility evidence**).

## Files
`ARTIFACTS.json`, `TEST-RESULTS.json`, `BUILD-COMMANDS.md`, `PROGRESS.md`, `checkpoint/`,
`results/` (per-run JSON, gate outputs, harness scripts), `fixture013/`, `checkpoint.py`.
Raw screenshots/logs: `c013-evidence-interim-runs2-7.zip` (`f64857da…`) and
`c013-evidence-rep1-3.zip` (`34b3e3db…`), delivered via the session file route, not Git.
