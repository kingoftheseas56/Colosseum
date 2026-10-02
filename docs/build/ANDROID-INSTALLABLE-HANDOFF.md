BLOCKED

# Android Checkpoint A — 2026-10-02

Phase 1 is incomplete. Do not merge this branch or start Phase 2 on this evidence.
The Nokia T20 is not connected; Hemanth explicitly said to continue without it.

## Saved changes

All work stays on `android/installable`; master and the primary checkout are untouched.

- `1001a9df`: saved the ten original launch repairs, packaged Qt TLS libraries, and added packaged-QML compilation to APK verification.
- `7fac8537`: corrected the player pause handler's activityTracker reference; linked Qt Concurrent into the Vault tests that compile MalCatalog.
- `78a55bd7`: limited mouse Back and right-click context handlers to mouse/touchpad. Touch previously opened a world and immediately invoked Back.
- `01fe7626`: explicit Android OpenSSL paths, full build diagnostics, and dependency caching before application compilation.
- `a5d49e2f`, `c1bd188e`: completed missing desktop test-target dependency lists and added the standalone A-01 composition probe.
- `ec79b2c1`: link OpenSSL with Qt's `_3.so` names directly; reject invalid ELF load-segment alignment in the final APK.

## Phase 1 evidence

| Requirement | State and remaining gap |
| --- | --- |
| Save repairs | Committed and pushed with explicit pathspecs. |
| World navigation | Focused Qt touch tests pass. Repaired ARM64 runtime reaches Tankoban and Theatre instead of returning Home; the diagnostic x86_64 runtime also reaches Biblio. Screenshots show each world's content. Emulator rendering artifacts limit visual qualification. |
| HTTPS | Qt loads both TLS libraries. Sign-in with synthetic invalid credentials reaches the production HTTPS service and displays its rejection. Successful sign-in/sync was not tested. Catalogue data appears, but fresh-fetch proof remains incomplete. |
| Video | Pause-handler regression passes. Diagnostic x86_64 runtime plays a local 60-second H.264/AAC MP4 with visible 640x360 frames and an audio track. Pause holds at 25.094 seconds; the forward button seeks to 35.094 while paused; resume reaches 37.397 with pause=false. Audible sound and direct-URL playback remain unverified. |
| EPUB | Production renderer not implemented; mandatory A-01 real-hardware composition gate is open. |
| Manga / CBZ | End-to-end chapter and download-then-read checks remain incomplete. A complete three-page CBZ fixture was pushed to Downloads, but tapping it in the Android picker did not return to the reader. This is not a passing CBZ test; its cause is not yet isolated. |
| Nokia tablet | Not tested; user instructed continuation without the tablet. |

Focused checks pass: nine Android build-graph tests; shell touch versus mouse Back (four Qt Quick passes); KeyboardAction touch versus context click (four passes); player pause/resume activity regression. All 282 packaged QML components compile. The ELF check rejects the crashing CI APK and accepts the corrected local ARM64 runtime.

## A-01 gate

Approved `A-01-DESIGN.md`, section 4: “Full EPUB bridge work starts only after this mechanism passes on required real hardware.” This is the specific reason the full Foliate bridge is not claimed as implemented.

`tools/android-reader-probe` builds separately for both ABIs. The x86_64 emulator demonstrates a real WebView in a foreign QWindow, an overlapping QML child window receiving touch, uncovered WebView scrolling, text entry and IME, view recreation, and background/foreground return. Logs include:

```
reader-probe foreign window true
reader-probe overlay tap 1
reader-probe document "Reader composition probe" progress 100
```

Selection, rotation, repeated lifecycle stress, and physical-device qualification remain open. This is not a production reader or fallback architecture. AndroidPaper still reports that the Android ebook renderer is unavailable.

## Build evidence

[Android run 37011845498](https://github.com/kingoftheseas56/Colosseum/actions/runs/37011845498) built both ABIs, then correctly failed its emulator startup gate:

```
UnsatisfiedLinkError: dlopen failed: cannot find "9_CRL_get_nextUpdate"
from verneed[0] in DT_NEEDED list ... libcrypto_3.so
```

After post-link renaming and llvm-strip, the library's final PT_LOAD had file offset `0x5afe20`, virtual address `0x5c0000`, and alignment `0x4000`. These are not congruent; Android maps the wrong bytes. Section-header inspection alone missed this. The new packaging check catches it. Do not distribute that run's uncorrected APKs.

Replacement [Android run 37015687738](https://github.com/kingoftheseas56/Colosseum/actions/runs/37015687738), source commit `ec79b2c1`, is **green**: both ABI builds, packaged-QML/ELF checks, and the API 34 x86_64 install/launch/60-second smoke passed. This is startup verification, not full functional qualification.

[Desktop run 37015687500](https://github.com/kingoftheseas56/Colosseum/actions/runs/37015687500) now builds Linux successfully, but seven test executables fail: `background_work_coordinator_harness`, `account_attachment_runtime`, `account_core`, `account_attachment_coordinator`, `core_sync_adapters`, `keyboard_key_events`, and `tracker_lifecycle`. Examples include `profileReady.count()` actual 0 versus expected 1, Stremio pending count 2 versus expected 0, and `client.available()` false. No baseline comparison establishes whether these failures predate this branch. Windows was still running at the last inspection. The desktop merge gate is red.

Automatic approval review rejected starting the local HTTP fixture server with "blocked by policy" and supplied no further reason. It was not retried; the direct-URL media check remains open.

## Local artifacts

Artifacts are in Downloads, outside Git:

- **Review APK: `colosseum-arm64-v8a-checkpoint-a-ci.apk`**, downloaded unchanged from green Android run 37015687738, source `ec79b2c1`. SHA-256: `6458330297c564253a32ab8d53719c5b67e4701c84dccd0bd0d1b59221ac6123`. Hash matches CI; APK signature, 16 KiB ZIP alignment and runtime-bundle/ELF verification also pass locally. ARM hardware was not tested. This supersedes the earlier locally repacked `colosseum-arm64-v8a-checkpoint-a.apk` for review.
- Corresponding x86_64 CI APK SHA-256: `8f8576d6b771dfc2e2e643f62ce4c43fd2deaa0476348551d81d2b14acd499d8`; downloaded hash matches CI and runtime-bundle verification passes locally. Its startup was verified in CI. Local detailed playback evidence used the earlier diagnostic build, not this exact APK.
- `colosseum-reader-composition-probe-arm64.apk`: separate Task Zero app; built, not tested on ARM hardware.
- `colosseum-checkpoint-a-evidence.zip`: selected runtime screenshots, probe logs, desktop failure log, and this report. Images document emulator rendering limitations as well as successful interactions.

The local API 36 x86_64 emulator uses ARM translation. Host GPU, SwiftShader, and ANGLE runs showed rendering artifacts and System UI ANRs. Audio output was disabled; audible sound is unverified. A temporary x86_64 diagnostic APK repairs only the CI ELF file alignment; it is separate from untouched CI artifacts.

CI test-signing secrets were absent in the observed run. CI kept its per-run debug signature, which differs from the local debug key. No credentials or keystores were committed. Release signing remains out of scope.

The Android native stream server is still an unavailable placeholder. Node was not packaged. Torrent streaming and all Phase 2 UI decisions remain untouched.
