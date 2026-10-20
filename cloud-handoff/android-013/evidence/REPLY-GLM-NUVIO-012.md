STATUS: COMPLETE
TASK: nuvio-glm-runtime-streaming-012-20261020

# Task 012: correct Android runtime delivery, then isolated private streaming qualification
2026-10-20 IST, GLM 5.3 (ZCode). All four checkpoints (C0-C3) passed on the Nokia T20 with the isolated diagnostic package. One honest limitation: physical audibility of speaker output was not observed (remote session); audio sink/renderer evidence is complete. Parent review and integration remain outstanding; nothing was committed, staged or pushed.

## What was actually wrong and what fixed it
Task011's APK shipped a stale assets/colosseum-runtime/qml/Main.qml (7deeb55e..., 304518 B, no onFixturePlayRequested) because its Gradle staging was copied from android-build, which androiddeployqt never refreshed after the overlay regenerated. Native rebuilds could never deliver the missing QML receiver. This task regenerated the overlay via cached CMake configure, staged the WHOLE generated runtime into a NEW Gradle candidate (android-package-012, task011 staging untouched), and proved the full correspondence chain: source == overlay == Gradle stage == APK member == installed base.apk == materialized+LOADED app-private runtime. With the receiver delivered, the previously-dead transition chain executed on-device: fixturePlayRequested -> openPlayer -> Loader -> PlayerPage.playTorrent -> Stream.play -> admission -> transfer -> Media3 playback.

## Source/overlay/APK/installed/loaded correspondence (C1)
- qml/Main.qml SHA256 5de6d3696c84f37433becb0cde6fa420d3a76d5436516ef8705a6bf6117417d6 at every hop: source, generated overlay, 739/739 runtime members hash-equal to overlay (no missing/extra), APK member, and the materialized bundle qml/Main.qml read back via run-as. Handler present (grep count 1).
- Overlay manifests freshly recomputed and internally consistent: qml-build.manifest file sha 44daafd5d4980a7cff3551e187ea8a9acb6636affbf0693d925eac0d09a80357 (qmlTreeSha256=89dee2b721449a62f7356d5973b7c78c327595f7298aef5672009567e06d6363), runtime-files.manifest file sha c08d80bb698dd79ecdeea75f04ee13f67b1c097599fe3aeb61d934c0f509c20e (bundleSha256=53a9f8f841fe00cb2ca49033952ff5014334266b1ec1fea8dee23124859725a2). CMake configure regeneration was byte-deterministic.
- Runtime drift between overlay and task011 staging was exactly 3 files (Main.qml + both manifests); the whole 739-file tree was re-staged, not a single-file patch. Androiddeployqt-formed staging root files (gradle.properties, res additions, Nuvio010IsolationApplication.java, substituted AndroidManifest v1.1.9) were deliberately retained.
- Content-addressed refresh worked without data wipe: files/runtime/ retains the old task011 bundle 0cfff1cd... beside the new 53a9f8f8... bundle; live QML load warnings reference the new bundle path, then "[boot] Main.qml loaded".

## Exact identities
- FINAL APK (after the one-line diagnostic log below): D:\NuvioTask008\streaming-glm012-20261020\android-package-012\build\outputs\apk\debug\android-package-012-debug.apk, 260,897,707 B, SHA256 1dd3961d96c52c1476ac9ddf2031c178d28648bcfde1a3e6ccd12ed4906e65aa. Laptop1 copy C:\nuvio-streaming-fixture-20261020\resume012\android-package-012b-debug.apk equal. Installed base.apk rehashed equal on device.
- Intermediate candidate (before the diagnostic log, same runtime delivery): SHA256 750dc9319f8189e2ce84efff29d5865857ebaba9f5e4dbd2ab80cb058f32d135; run A used it.
- Packed engine libnuvio_engine.so: 139,307,488 B, 2cf8de34f2d585aee3f7ad86c763a49671deaf0b05a901bed74c985b906bbc4b (unchanged from task011 pin, both candidates). Packed stripped app lib: 43845448 B / 64f56f812da819d0d7015d1bb6929849428856416770ad34efbb1330655270b8 final (pre-log candidate carried task011's 43861e65.../42843528). Native ABI inventory arm64-v8a only; 1012 ZIP members.
- Signer: apksigner verify --print-certs exit 0, SHA-256 d54f67da3dbadddf25bfbf5b8bf70461d3ed626bff47437d51e6e2135226c733 (authorized diagnostic signer). Badging: org.qtproject.example.colosseum.nuvio010isolated v1.1.9, minSdk 28, target 36.
- Device: Nokia T20 physical serial 4000TA13971B0500637 (ro.serialno match) via 192.168.29.37:34061, API 33, arm64-v8a. Diagnostic lastUpdateTime 2026-10-20 19:19:15 (in-place install -r; never uninstalled). Original org.qtproject.example.colosseum v1.1.7 firstInstall=lastUpdateTime=2026-10-08 01:40:39 unchanged before/during/after.

## Task-only source delta and inherited dirty separation
Laptop2 app D:\NuvioTask008\repos\app-standalone detached HEAD fdd23f09e8b631f5c165e84d969895c99f520a28; donor branch codex/nuvio-android-hardening-20261018 HEAD 99a18601fa12858cc8c62576dc72b87edbd81b9a — both unchanged, donor untouched by 012. Inherited dirty/untracked work (task010/011 integration edits) preserved unmodified; all 9 task011 after-hashes re-verified equal at start (no concurrent writer). Task012 changed exactly:
1. native/player/streamserver_android.cpp 2c2bf46752a15668b8c4d10ba321ec20fbac038329aedc81eb8843f16f31ff11 -> c0d25234682b1b93ce861aa4a902ba04578ce7e9556be627dffe0dcc6cec12ea: one task-scoped correction — the streamReady forwarding became a lambda that (only when m_diagnosticTask) logs the route URL, because the donor route target embeds an unguessable random 32-byte token (libtorrent_stream_bridge.cpp register_stream) and no existing log/property exposes the URL; the Range206-on-same-URL gate is unprovable without it. Signal semantics unchanged; production packages log nothing.
2. NEW tests/test_android_runtime_packaging.py (863aa3d9d9a67dc21214bb004b7736722ab9e9522e80673822724ca3fb926b17): packaging regression that catches the exact 011 drift class (overlay==source, recomputed tree/bundle hashes match manifests, candidate APK/staging member-for-member == overlay; skips cleanly without a build tree). Positive: 4/4 OK on final APK. Negative control against task011's APK: FAIL "packaged runtime member drift" as designed.
3. streamserver_android.cpp.pre012.bak (backup of the pre-edit bytes, untracked).
Donor patch/regenerate not affected (donor tree untouched; contract test's vendored-patch-apply case passes).

## Gate matrix
| Gate | Verdict | Key evidence |
| --- | --- | --- |
| Isolation before every demand | PASS x4 | Fresh-process verify PASS each run (sessions 0xb4000078c96e8dc0 / c976e020 / c970cfa0 / c95f82b0): isolation_pass=true, torrent_count=0, DHT/LSD/UPnP/NAT-PMP false, empty bootstrap, PEX 0, live DHT inactive, 127.0.0.1 listener, libtorrent 2.0.12.0 |
| Admission fail-closed | PASS + negative control | PASS only on the verified same-session identity; post-demand readback same session torrent_count=1; stale-trigger replays on unverified fresh sessions DENIED twice (19:35:12, 19:39:31) |
| Owned tracker/seed | PASS | Tracker announce accepted from 127.0.0.1, UA "Nuvio Engine/0.1.5"; seed handshake from reverse-mapped engine peer, 2,874,417 B served, final not_interested; host probe byte-exact pieces 0/80/160 |
| Verified Android piece transfer | PASS | On-device payload 2,628,657 B, SHA256 77c5d5876f5c2f1b69ef2e3e1286df47262cf55fd6391e8c5bf081a55058b02a == fixture, from fresh empty state; resume file under infohash |
| HTTP route identity | PASS | streamReady URL logged per run (e.g. http://127.0.0.1:38467/stream/054c615e...e6a); /proc/net/tcp during playback: LISTEN + ESTABLISHED 127.0.0.1 pair on that exact port, app uid |
| Range 206 | PASS | Labeled probe on the same URL during active playback: 206, Content-Range bytes 1000000-1000255/2628657, Accept-Ranges bytes, 256 B == fixture bytes exact |
| Media3 first frame + 2 changing markers | PASS | OES render node ready; fixture timecode/frame-code markers: 00:08 F063, 00:10 F078, whole-frame pixel diffs; screenshots + band crops on Laptop1 |
| Forward + backward seek | PASS | 00:28 F219 after 85%-bar tap; 00:18 F138 after -10s (exact -10s); seek recreated AudioTrack |
| Audio renderer/sink | PASS / audibility UNVERIFIED | AudioTrack 22050 Hz adds for playback PID, speaker routing, Media3 AudioFocus; separate AVC decoder path; physical audibility not observed (remote) |
| Background/resume | PASS (run D) | Same PID across HOME + re-front; resumed visible frame 00:22 F166. One earlier background (run C) was killed by OS memory pressure; restart replay was DENIED by the gate |
| Real in-app stop | PASS | Run A close-arrow: player closed, process alive, library UI; force-stop used only as final containment |
| Clean repeat, fresh state, unchanged APK | PASS | Runs B/C/D full chains on final APK with fresh owned torrent state each |
| Regressions | PASS | Contract 31 executed / 0 skipped with NUVIO_DONOR_SOURCE supplied (exit 0); packaging 4/4; historical donor omnibus timing failure not rerun, preserved |

## Commands, exits, failures
- ssh laptop2 whoami: desktop-3k414a9\admin, exit 0 (before consequential work).
- cmake 4.4.3 configure: exit 0 (51.4s). Native rebuild after the log patch: exit 0. Gradle offline assembleDebug: exit 0 twice (012, 012b). apksigner/aapt: exit 0.
- FAILURES (honest record): scp exit 255 (unsupported) -> PowerShell stdout binary stream used (2m33s, hash-equal). First streamed adb install failed with empty device reason twice; root causes found: MSYS path mangling of /data/... push targets plus a 0.4 MB/s wireless link that silently dropped one 261 MB payload; fixed with 16x16MiB chunked verified transfer (per-part sha256). First chunk assembly used shell-glob order -> total hash mismatch caught -> numeric-order reassembly -> exact. A `Task ';' not found` Gradle failure came from nested cmd quoting before any build; fixed by PowerShell-invoked gradlew. Run B probe port-parse bug -> probe skipped that run; run C probe passed. Run B backward-seek tap missed (timecode continued 00:28->00:30); run C used the -10s button: 00:18 F138 exact. Run C background process kill (memory pressure) documented above; run D passed with survival.
- Chunked install pm install -r: Success (18:48:32 for 750dc931..., 19:19:15 for final 1dd3961d...).

## Evidence paths
- Laptop2 D:\NuvioTask008\streaming-glm012-20261020\: RECEIPT-C0/C1/C2/C3.md, android-package-012\ (Gradle candidate + final APK), apk-identity-012.json / apk-identity-012b.json, overlay-vs-staging011.json, cmake-configure-012.log, app-build-012.log, apk-gradle-012(b).log, app-contract-012b.log, packaging-test-012b.log, compare_*/verify_*/patch_* scripts.
- Laptop1 C:\nuvio-streaming-fixture-20261020\resume012\: receipts copies, gate/run logs (gate-verify/play, runA/B/C/D logs + logcat), seed-log-012.jsonl (871 piece events), tracker-log-012.jsonl, payload-sha + final-payload-sha, range-probe + runC RANGE_PROBE lines, runC-procnettcp-early.txt, runC-audio*, all screenshots (runA/B/C/D *.png + band-*.png), install/chunked-install logs, cleanup-012.txt, both APK copies.
- Task011 evidence (streaming-preflight-resume-20261020, resume011, preserved-baseline) untouched; REPLY-011 hash re-verified b0c9117888020c2a8a14b39f2415b8027c358f835cd86be72bbf22c20b9915d4 before start.

## Final containment
Owned seed PID 1564 and tracker PID 35872 (cmdline-verified) stopped; ports free. Reverse tcp:51412/51413 removed (list empty). Task-owned /data/local/tmp files removed; task011 leftovers untouched. Diagnostic package force-stopped as containment after natural-stop evidence; PID absent. Original app and its data untouched throughout. No commit/stage/push/CI/release/integration; no toolchain installs; no unrelated process kills; no public networks; no Feria surfaces touched.

States: AUTHORED one diagnostic-gated log line + one regression test; COMPILED (native + Gradle offline); PACKAGED/VERIFIED (member-level); DEVICE-INSTALLED (in-place, hash-equal base.apk); DEVICE-VERIFIED runtime delivery, isolation, admission, transfer, HTTP route + Range, Media3 frames/markers, seeks, audio sink, background/resume, natural stop, clean repeat. NOT PARENT-REVIEWED, NOT INTEGRATED. Physical audibility unverified (remote session). Parent acceptance is a separate act.
