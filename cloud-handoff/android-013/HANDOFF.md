> Publication update: read `PUBLICATION-AUTHORITY.md` first. Earlier preparation-status and permission statements are historical; the current user starting prompt controls the Cloud session.

# Android cloud stabilization: first milestone

TASK: claude-android-cloud-013-20261020

**Goal:** reproduce the preserved native Android candidate in Claude Code Cloud, make its packaging reproducible, diagnose and repair real Media3 progression/seek/surface/teardown defects, and return a reviewable ARM64 APK plus honest cloud-runtime evidence for Codex's later Nokia receiving tests.

**Owner:** Claude Code Cloud with Opus 5.5. Codex retains architecture and accepts/rejects the exact return before integration. This is a substantial end-to-end milestone, with internal recovery checkpoints rather than a succession of tiny approval stops.

**Environment:** an Anthropic-hosted Linux cloud VM attached to the actual GitHub repository and an isolated candidate branch. No access to Laptop 1, Laptop 2, their SSH aliases, BDC, Windows D: paths or the Nokia tablet is assumed or required. Local paths mentioned in supplied historical reports are provenance only. Do not request laptop credentials or attempt to connect to them.

**Tech stack:** existing C++/Qt 6.11.1/QML app, Java/JNI Media3 player, opt-in hardened Nuvio C API v3 engine, NDK r27c, CMake/Ninja and Gradle. Extend the existing native/Media3 architecture. Do not replace it with Node/server.js, WebView playback or a new player/server.

**Task specification:** this handoff, `CLOUD-ENVIRONMENT.md`, `SOURCE-MANIFEST.json`, and `evidence/nuvio-task012-independent-review.md`. Follow current explicit task instructions over historical worker assignments. Historical no-setup constraints on our laptops do not prohibit in-scope installation inside your disposable Cloud VM.

## Read these first, in order

1. This handoff and `CLOUD-ENVIRONMENT.md`; establish actual VM/repository/source identity.
2. `evidence/nuvio-task012-independent-review.md`: current acceptance boundary and remaining blockers.
3. `evidence/nuvio-startup-correction-independent-acceptance.md`: startup/JNI repair already accepted at its identified artifact scope.
4. `evidence/nuvio-task008-independent-acceptance.md`: historical ARM64 packaging foundation, not current Cloud qualification.
5. Applicable repository `AGENTS.md`, then relevant live source/tests. Do not preload project memory/governance or unrelated instructions. No Preflight private instructions, memory/capability material or operating files are supplied or needed.

The prior GLM final report is supplied as an explicitly rejected COMPLETE claim. Its raw screenshots/logs are diagnostic evidence; its image-description prose must not overrule the pixels. Do not repeat old tests merely to reproduce a progress percentage.

Raw Nokia evidence and a separate emulator-smoke ZIP are retained in the full local receiving packet, not the proposed public GitHub source payload. The Cloud source branch includes the independent reviews and synthetic fixture. If raw supplemental files are not present in Cloud, say so and establish fresh observations; do not try to open historical laptop paths. Request a supported private transfer only when a specific missing file materially blocks diagnosis.

## Baseline and ownership

- Upstream app HEAD: `fdd23f09e8b631f5c165e84d969895c99f520a28`, plus the working changes captured in the supplied source manifest. The imported cloud Git commit will be different; preserve both upstream identity and import identity.
- Upstream donor pin: `99a18601fa12858cc8c62576dc72b87edbd81b9a` (`NuvioMedia/nuvio-engine`, historical v0.1.5), plus the supplied hardened working snapshot. `donor-pristine-pin.bundle` and `DONOR-GIT-SHALLOW.txt`, restored together by `restore_donor.py`, provide the exact portable Git pin and authentic annotated tag for preparation tests. This is a shallow source pin, not a complete history; a plain clone of the bundle fails because Git bundles omit the shallow boundary. Never pass off the pristine donor as our hardened working candidate.
- Full source recovery archive: 4644 selected files, SHA256 `920d3e72f4e6d641def87d683ed63e4ae9d82f0bfe66f9190cfe9274543205bf`.
- App and donor source, patches, headers, notices, tests and runtime assets are included. SDK/Qt/NDK/JDK installations, downloaded third-party caches, generated manifests/overlays, APKs, signing keys and Git histories for the full app are not. Regenerate build products using the supplied source and pinned tools.
- Work only on the actual approved isolated Cloud branch. Check source manifest and Git status first; inherited candidate code is not your new delta. No concurrent implementation writer. Preserve immutable import/checkpoint source for comparison.
- A GitHub clone of `master` is not this candidate. Stop implementation if the manifest/import identity does not match; do not reconstruct missing unpublished fixes from prose.

## What is already accepted, and what is not

Accepted at historical stated scope: hardened donor integrity work, bounded host backend corrections, ARM64 ON/OFF package foundation, JNI startup repair and real Nokia usable-UI/startup checks. Task012 corrected stale QML/runtime delivery and passed independently rerun packaging 4/4 and backend contracts 31/0 skipped. Controlled torrent transfer/payload hash and a separate exact 256-byte HTTP 206 Range probe provide useful progress.

Task012 final APK SHA256 was `1dd3961d96c52c1476ac9ddf2031c178d28648bcfde1a3e6ccd12ed4906e65aa`; actual installed diagnostic APK matched at receiving review. Historical engine member SHA256 was `2cf8de34f2d585aee3f7ad86c763a49671deaf0b05a901bed74c985b906bbc4b`; packaged app member `64f56f812da819d0d7015d1bb6929849428856416770ad34efbb1330655270b8`. These are comparison records, not required hashes for a new Cloud build and not supplied prebuilt artifacts.

Not accepted: sustained changing video frames, correct forward/back seek targets, background/foreground recovery, cleanup/ownership after close, or three clean repeat runs. Some screenshots contradict reported frame markers; three were byte-identical. A claimed resumed image actually shows STARTING STREAM/black, and actual logs report missing EGLSurface/failed make-current/swap. Same PID, opened sockets, initialized render node or an AudioTrack record alone do not establish functioning playback or audible sound. Actual cause remains to be diagnosed.

## Global constraints

- No integration into `master`, merge, release, production rollout, main-branch edits, unrelated changes or GitHub Actions/CI dispatch. No remote push or PR until an exact worker branch/source-publication authority is recorded. Local Cloud-only Git checkpoints are permitted; do not commit generated caches, SDKs, credentials, keys, APKs or personal data.
- This task authorizes setup/build/test dependencies and controlled diagnostic emulator installs inside the Cloud environment. It does not authorize operations on Hemanth's computers or physical devices. Codex will own later Nokia qualification of the returned exact candidate.
- Preserve feature default OFF, accounts behavior, existing Stream/Media3 seam, shared download engine ownership, local-first use, Android TLS verification, hardened binary/stamp checks, licenses and third-party notices. No blanket certificate bypass, broad cleartext permission or minimum-SDK increase to hide a failure.
- Private synthetic fixtures only. Before any torrent demand, prove effective settings/counts/identity in the **actual current native session**: DHT, LSD, UPnP and NAT-PMP disabled; no public/default/bootstrap trackers, no imported peer lists, no inherited live torrents. Disable PEX for fixture runs or prove no external peer expansion; only the local tracker/seeder and expected fixture infohash may participate. Host env variables are not session proof. Keep uploads disabled. No public torrents or external media playback.
- Bind fixture services to task-private loopback/VM networking and explicitly qualify the emulator-to-host route. An emulator's loopback is not the VM host's loopback; historical Nokia/LAN addresses and ADB mappings must not be reused. Observe actual connections and remove only owned mappings/processes afterward.
- Diagnostic-only package identity must be distinct from `org.qtproject.example.colosseum`; use `org.qtproject.example.colosseum.nuvio013cloud`. A new cloud debug signer is expected; report it. It cannot replace the older Nokia diagnostic package with a different signer. Do not request old signing keys or instruct uninstall/data wipe.
- Do not route around platform/security denials. Save the exact operation, returned reason and whether execution began; stop that affected action and continue only genuinely independent permitted work. Do not infer a global ban or stall all unrelated tasks from missing denial details.

## Review focus

1. Stale packaged runtime that is internally consistent but differs from source/generated overlay: reject it before installation.
2. Player position/audio counters advancing while the actual video texture is frozen: require render and visible-frame corroboration.
3. HOME/foreground or surface recreation retaining PID but losing EGL/decoder output: verify actual recovery and absence of stale callbacks.
4. Closing/replacing/backgrounding a request leaking stream demand, routes, work or cross-session publications: inspect before/after ownership and a clean repeat.
5. An x86_64 emulator or host green test being misreported as ARM64/Nokia acceptance: state ABI/API/artifact/scope per result.

## C0 — receive, preserve and qualify Cloud

- [ ] Record actual Cloud OS/CPU/RAM/disk, source branch/import SHA, pristine donor pin, toolchain availability, network responses, emulator/KVM capability and log paths. Never equate configured values with observed ones.
- [ ] For GitHub import, verify `CLOUD-SOURCE-MANIFEST.json` using `verify_source.py --manifest CLOUD-SOURCE-MANIFEST.json --app <repo> --donor <handoff>/source-donor`. For full Windows-byte recovery, use `restore_source.py` in a fresh directory; it verifies `SOURCE-MANIFEST.json`. These are deliberately different representations: unchanged app files use exact upstream Git blobs for Cloud; dirty/new app and donor files retain captured bytes. All Windows-vs-Git differences were independently required to be line-ending-only and are enumerated in the Cloud manifest. Do not weaken either verifier or silently convert bytes before its corresponding baseline check. After verification, executable shell scripts may require deliberate LF conversion in a build copy; record it and preserve import bytes. Use `bash script.sh` or restore executable modes from provenance.
- [ ] Verify a durable source checkpoint round trip before implementation. A local VM commit/archive is only a local checkpoint until Codex/Hemanth can retrieve and verify it. If no authorized output route exists, stop new implementation after safe baseline/environment work and return the precise transport blocker.
- [ ] Install/qualify tools in Cloud only. Required foundation: Qt 6.11.1 Android arm64 kit plus matching Linux host Qt, Android NDK `27.2.12479018`, SDK Platform 36 and Build Tools 36.0.0, JDK21, CMake/Ninja/Python. Start with existing repository scripts; keep all exact downloads/checksums and license handling recorded. No copying a Windows/macOS Qt host kit into Linux or pointing at historical D: paths.
- [ ] Run source contracts with the exact donor available. Use `python3 restore_donor.py <new-working-donor>` for a real Git checkout at the exact pin overlaid with our 149 hardened files, and `--pristine` with a separate new destination for the original pin. The bundle includes the authentic annotated v0.1.5 tag; verify its peeled commit. Set `NUVIO_DONOR_SOURCE=<new-working-donor>` when running `python3 <app>/tests/test_android_nuvio_backend_contract.py`; the source-donor directory alone has no `.git` and would skip important cases. Missing prerequisites/skips must remain skips/blocked, not green tests. Verify the vendored hardening patch against the pristine pin. Preserve the known donor omnibus timing failure separately; reproduce before attributing it to new work.
- [ ] Establish a baseline ARM64 dependency/engine/app build. Reuse `scripts/android/{qualify_toolchain.py,build_deps.sh,build_apk.sh,prepare_nuvio_engine.py,verify_nuvio_hardened_artifact.py}` and `docs/build/android*.md`; adapt only demonstrated portability gaps. The app dependency stack and donor's private pinned dependency stack are intentionally different; do not homogenize their Boost/libtorrent pins.
- [ ] Cloud emulator qualification is separate: API33+ is a useful first target, matching physical T20 API33 when possible. Record supported guest ABI and hardware acceleration honestly. Existing Nuvio artifact/CMake gates are ARM64-specific; an x86_64 app smoke cannot qualify the enabled ARM64 backend. If emulating a matching ABI is infeasible, report the limitation and continue real build/host work. Any narrowly needed x86_64 diagnostic support must remain explicit opt-in, validate ABI and hardening, preserve ARM64 behavior, and have its own regression checks. Never package an ARM64 library as x86_64 or bypass a stamp to get an emulator run.

The donor recovery helper deliberately restores the original shallow boundary, sets `core.autocrlf=true` / `core.filemode=false` only on the NEW hardened working donor, and reconstructs intent-to-add visibility for the existing new integrity test. The archived Windows source bytes and audited Git-generated patch require those clean-filter/index semantics. A raw overlay alone excludes that new test from `git diff HEAD` and produced a patch-drift failure during recovery testing, even though its file was present. Preserve this metadata and the fail-closed validation rather than removing the test from the patch. Preserve raw import bytes and record any shell-script LF build-copy conversion separately.

Checkpoint C0: source/import identity, environment receipt, baseline results/blocked gates, working recovery/download route, and exact next action. Do not spend hours on product changes before this exists.

## C1 — reproducible packaging and trustworthy fixture ground truth

Primary files: `native/cmake/android.cmake`, `scripts/android/build_apk.sh`, `native/platform/android/build.gradle`, `native/platform/android/AndroidBootstrap.cpp`, `native/bootstrap/StartupLayout.cpp`, `tests/test_android_runtime_packaging.py`, `tests/verify_android_apk_runtime_bundle.py`, affected Android packaging tests.

- [ ] Preserve baseline failures. Make the normal candidate build invoke package correspondence checks for every generated runtime member/manifests, not a manual one-file QML copy. A self-consistent stale bundle must fail against the intended source/generated overlay. Packaging tests that skip because no overlay exists do not qualify an APK.
- [ ] Demonstrate a stale/corrupt disposable-package negative control and final positive check. Verify final APK ABI, ELF/dependencies/alignment, signer/package, engine provenance, whole QML/runtime materialization identity and OpenSSL runtime pair. Rebuild OFF control when touched composition warrants it; preserve default OFF.
- [ ] Inspect supplied synthetic MP4 independently with Cloud ffprobe/ffmpeg. Decode ground-truth frames at known timestamps; establish actual duration, monotonic visible time/frame markers and audio content. The existing file may be faulty: do not force old screenshots to fit a claim. If unsuitable, generate a deterministic licensed synthetic clip and private torrent with known hashes, recording changed fixture identity and expected frame windows.
- [ ] Audit/port the supplied Python tracker/seeder/probe to the VM. They are historical fixtures, not trusted production infrastructure: validate the 20-byte peer ID/68-byte handshake, infohash, piece-length/hash/bounds and exact payload; reject malformed/foreign demand. Keep original scripts/evidence untouched. Prove matching seed metadata and pieces before introducing player demand.

Checkpoint C1: reproducible package commands, exact artifacts/manifests, negative control, verified fixture and service protocol evidence, restorable source delta.

## C2 — fix the demonstrated native/Media3 path, then prove Cloud behavior

Primary files, inspect before choosing edits:
- `native/player/androidmedia3item.{cpp,h}`, `androidmedia3state.{cpp,h}`, `androidmedia3videonode.{cpp,h}`: Qt item, state, render thread/surface ownership.
- `native/platform/android/src/org/colosseum/player/Media3PlayerHost.java` and `Media3SurfaceBridge.java`: Java player and Android surface lifecycle/JNI.
- `native/player/nuviostreambackend.{cpp,h}`, `nuvioengineapi.{cpp,h}`, `streamserver_android.cpp`, `streamserver.h`: request generations, engine/demand ownership, route and lifecycle seam.
- Donor `src/{core,http,torrent,cache,scheduler}` and `include/nuvio_engine`: only if correlated evidence locates a donor defect. Keep vendored patch/preparation/stamp correspondence valid after donor changes.
- `qml/Main.qml`: only the existing player composition or narrow diagnostic fixture trigger if actually needed. No broader UI redesign, new browser/player or unrelated product work.
- Existing tests: `tests/auto/player/tst_nuvio_stream_backend.cpp`, `tests/test_android_nuvio_backend_contract.py`, relevant `tests/test_android_media3_*.py`, `tests/android_media3_state_harness.cpp`, donor integrity/lifecycle tests.

- [ ] First prove actual session discovery isolation and identity before seed/player demand. Log the session/request/fixture identifiers needed to correlate all observations; do not expose tokens or whole URLs containing secrets.
- [ ] Play through the real Colosseum Stream -> loopback HTTP -> Media3 -> Qt-rendered surface path. Correlate actual Media3 HTTP request/Range/response metadata with rendered frames and the session. A separate curl/HTTP probe must be labeled separate.
- [ ] Require at least three time-separated, demonstrably changing video frames tied to increasing player position/render callbacks. Preserve full original captures or a continuous screen recording; overlays or generative image descriptions do not prove video motion.
- [ ] Exercise actual forward/back seek controls against distinct known target windows; capture position, decoded/rendered frame markers and relevant HTTP requests. Do not hardcode historical screenshot readings or equate a control tap with a successful seek.
- [ ] Reproduce background/foreground and surface destroy/recreate. Diagnose the actual EGL/Surface/decoder/request ownership fault before repair; don't suppress error logging or treat PID survival as playback recovery. Show valid frames and audio path after return. Distinguish process replacement and its supported cause from normal surface recovery.
- [ ] Prove natural player close/stop and replacement retire demand/routes/work appropriately. Use ownership counters or actual session/route state, reject stale completions, and demonstrate no unexpected continued fixture transfer. An idle shared engine is not automatically a leak; state intended owner/lifetime.
- [ ] Add regressions for demonstrated fixes and meaningful negative controls: stale packaged bytes; cancelled/stale request generation; missing/corrupt fixture/piece or unavailable seed; lifecycle recovery; no background/close orphan demand. Preserve first failures and final runs. Do not rewrite tests merely to match current behavior.
- [ ] Three clean supported playback/seek/lifecycle/stop repetitions on a pinned Cloud candidate if the environment can execute it. Record ABI/API, APK/PID/session/fixture identities, observed failures and counts for each. If Cloud cannot execute the backend, return built/host-tested status and the exact runtime blocker rather than COMPLETE. Continue achievable implementation and package work without substituting a fake provider for Android runtime proof.
- [ ] Audio proof must match evidence: AudioTrack allocation, decoded audio, captured audible PCM and physical listening are different. Cloud audio may be unavailable; report that limitation and leave Nokia audibility pending.

Checkpoint C2: source changes, reproduced/fixed root causes, tests and raw captures with a per-run evidence table. This is Cloud evidence, not Nokia acceptance.

## C3 — receiving-ready delivery and recovery drill

- [ ] Rebuild/package the final candidate after the last source change. Hash actual packaged engine/app/runtime members; verify the installed emulator APK equals the final returned APK when an emulator was used. No receipts referring to an earlier build.
- [ ] Produce a full source checkpoint plus a minimal explicit delta against the imported baseline, including untracked authored files and necessary fixtures/build scripts. Include all new source even if ignored by Git; exclude SDK/cache/output/profile/key material. A patch alone is insufficient when it omits new files.
- [ ] In a fresh directory restore the checkpoint, verify hashes and rerun the smallest decisive host checks. Record actual restoration evidence. Successful archive creation or Git commit creation alone is not recovery proof.
- [ ] Record task-owned fixture processes/ports/ADB mappings before/after and clean up only what this task owns. Keep logs, failed attempts and checkpoints.
- [ ] Return `REPLY-CLAUDE-ANDROID-013.md`, `REPLY-CLAUDE-ANDROID-013.md.done`, `SOURCE-CHECKPOINT.zip` with SHA256/restore instructions, `SOURCE-DELTA.patch` plus new-file manifest, `ARTIFACTS.json`, `TEST-RESULTS.json`, raw logs/screenshots/video, exact build/setup commands and final ARM64 diagnostic APK (and separately labeled emulator artifact if different). If platform size limits require multiple artifacts, provide one manifest linking every part and a tested reconstruction script.
- [ ] Marker JSON: task ID, status, exact reply filename, reply SHA256, source/import/checkpoint identity, artifact manifest hash and UTC completion time. Write reply and marker last after final artifact verification. Do not claim Codex acceptance or integration.

## Recovery cadence and stop rules

Keep C0/C1/C2/C3 checkpoints and refresh after meaningful source changes, before risky toolchain/lifecycle work, and before context/quota exhaustion. For extended execution, roughly 30 minutes of unrecoverable new implementation is the upper bound, not a requirement to pause for approval. A checkpoint contains source hash manifest, import/parent ID, owned delta, commands/tool versions, pass/fail/skip counts, artifact hashes, exact outstanding blocker and next action. Preserve useful work on partial return.

If remote checkpoint push is authorized, use only the explicitly approved worker branch, verify its remote SHA, and never force-push or update main. Otherwise use the already-qualified download route. Record local checkpoint and durable external checkpoint as different states. Do not claim backup based on an unverified link or an archive still inside the same VM.

## Receiving verdict

Use `CLOUD-MILESTONE-READY` only when required cloud build/package and supported runtime gates have evidence; list Nokia gates explicitly PENDING. Use `PARTIAL` or `NEEDS-INPUT` when prerequisites or real runtime coverage remain absent. Do not fabricate a broad 2.0.0/2.0.1 completion claim: this assignment is the Android streaming/package/lifecycle milestone, not the whole release roadmap.

Codex will review the exact source delta, artifact and raw evidence, rerun decisive checks, then arrange controlled Nokia qualification on the returned artifact. No integration happens before that receiving decision. Do not wait for Hemanth to manually perform routine Cloud tests you can execute yourself.
