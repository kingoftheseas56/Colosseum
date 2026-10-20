# Independent Android candidate receiving review

Verdict: **READY FOR CONTROLLED DEVICE TESTING. NOT ACCEPTED FOR INTEGRATION OR RELEASE.**

Task nuvio-android-setup-package-008-20261019. Accepted manual return for build/packaging scope: REPLY-008-LAPTOP2-REVIEW.md, SHA256 7ad4aadc18b87ec9ea2f7dfd616a7699ad852c1457d7f737434202ee0c244124. Exact marker task/path/hash matches. The prior finalized Laptop 1 needs-input reply remains preserved. No helper Send/closure receipt or review-closed operation is asserted for this manual route.

## Independent checks completed

Parent authenticated Laptop 2 through the existing SSH alias, confirming desktop-3k414a9/admin, then inspected actual source and APK bytes under D:/NuvioTask008. No device discovery, installation or execution occurred.

| Check | Parent result |
|---|---|
| Current source identity | All 44 manifest files match SHA256 and size; both Git HEADs are pinned; both diff --check HEAD checks pass. |
| Exported donor patch | Actual preparation --verify-only passes again against a pristine extraction of the pinned donor and compares the hardening source. Current patch identity is 4713fde75a1a6eabdf55e3332834be645faeca44ce75e067958e04c533eaf78a, superseding task003's old patch identity for this replica. |
| Hardened engine verifier | Actual verifier rerun with current source and installed prefix: exit 0. |
| OFF APK | SHA256 e7a8d2c244c11a78d0fb1d624ae04d10e72d7ad3e725dbbe5bba953e08f87932; 87 ARM64 shared libraries; no Nuvio engine. |
| ON APK | SHA256 e3bf96d29d032af1850688030ced907e84101681418b15a0757df68d47c38a8f; 88 ARM64 shared libraries; actual engine ZIP-member SHA matches hardened prefix. |
| Engine identity | APK member and installed engine SHA256 594cd435ac178b6c93c992941ce9d8b427a37827afc75440ae02835c19f21f11. |
| Packaged native ELF | Independently parsed every native library in both APKs: ELF64 little-endian AArch64 ET_DYN, 16 KiB-compatible LOAD alignment. Correct sole ABI and no duplicate ZIP names. |
| Dynamic C API | Actual packaged engine has the 23 intended C exports, including all 20 symbols requested by the loader. API version 3 is the pinned source/stamp contract; executing api_version() on Android remains untested. |
| Engine dynamic references | Independently resolved 404 strong undefined engine references by name against actual packaged dependencies and Android API28 system stubs: none unresolved. Dependency graph includes libc++_shared, libc, libdl, libm. This does not execute relocations or prove linker-namespace/version behavior on a device. |
| Signatures | Fresh apksigner verify --verbose: both exit 0, one v2 debug signer. This is local-debug signing, not release approval. |
| ZIP alignment | Fresh zipalign -c -P 16 -v 4: both exit 0. Native members are compressed; this is not proof of uncompressed in-place loading. ELF alignment was independently checked. |
| Android runtime bundle | Actual repository APK runtime-bundle validator: both exit 0; 400 packaged QML files. Independently observed trust-helper and Media3 strings in packaged DEX. This is package presence, not execution. |
| Fail-closed controls | Reran actual verifier against five existing disposable prefixes: missing engine, modified binary, wrong ELF machine, wrong patch and stale commit all reject with exit 1 and the expected failure reason. Production prefix/source was not mutated. |
| Backend contracts | Fresh execution of actual tests/test_android_nuvio_backend_contract.py against actual donor: 30 cases, zero skips, exit 0. |

Independent audit implementation: work/review-task008/independent_audit.py. Full JSON copied to work/review-task008/independent-audit.json; logs remain at D:/NuvioTask008/codex-review/. The parent audit checked packaged binary bytes directly, not only worker-generated JSON.

The first audit attempt incorrectly applied shipping-library alignment rules to NDK system symbol stubs; corrected the audit to check their symbols without imposing runtime LOAD alignment. APK/library alignment requirements were not relaxed. Earlier review-script transport failures and that diagnostic assertion are not product failures.

## Source review and risk assessment

Reviewed current donor CMake delta, app Gradle delta, JNI wrapper, trust helper, native API loader and artifact verification paths. Compared changed-source identities with the accepted baseline.

- Android libc++ experimental feature opt-in is PRIVATE on the donor target and conditional on ANDROID. It does not become a public app compile requirement. Existing C++20 jthread/stop_token code is retained behind the POD/function C API. Static resolution against the actual packaged C++ runtime passes. Android cancellation/destruction still needs runtime stress testing.
- Android shared builds now use the existing Linux export-map/hidden-static-symbol/defined-symbol/RELRO policy. Actual packaged export inspection confirms the intended limited C API surface. Test-only seam is not enabled in the production Android configuration.
- Qt's typed context is copied into a scoped QJniObject before the instance call. It is used synchronously to obtain ApplicationInfo.nativeLibraryDir; no new retained raw JNI reference or cross-thread Java-object ownership was introduced. Actual native compilation occurred in the worker build; JNI execution remains a device gate.
- Gradle keepDebugSymbols targets only libnuvio_engine.so. It preserves the stamped engine bytes instead of weakening the provenance verifier to tolerate arbitrary modifications. Other libraries keep normal stripping behavior. This creates a large debug artifact but is acceptable for diagnostic testing.
- The existing Java helper exports AndroidCAStore into app-private PEM before engine creation; export failure leaves the backend unavailable. No certificate-verification bypass was added. Actual CA enumeration, file access and TLS handshake remain unexecuted Android checks.
- Default feature-OFF composition remains intact. The accepted host ownership, generation/epoch fencing and verified-piece delivery source remains unchanged by this packaging slice.

No new blocking source defect was found for entering a controlled device test. No product source fix was made during this receiving review.

## Disclosed gaps

This review did not independently rebuild both APKs from scratch. Their worker configure/compile/Gradle evidence was reviewed; the resulting source manifests, exact artifacts, ELF and APK checks, source preparation, verifier and contracts were independently rechecked. This is not a clean-machine reproducibility claim.

Task003's 24 functional Qt cases plus init/cleanup, seven donor integrity cases and C API smoke remain previously accepted host evidence, not new Android-runtime executions. They were not repeated here because the delivery/lifecycle implementation did not change. Known donor omnibus incomplete-request shutdown timing failure (96/97) remains disclosed and must be included in later cancellation/teardown qualification. Three older baseline app contract failures remain separate.

Debug ON APK is about 265 MB and the unstripped engine about 139 MB. Before device installation, check target free space and protect existing app data/package identity; the current development package identity is unchanged. Size, stale host-only documentation and release packaging efficiency should be addressed before a shipping decision, not hidden as completed work. GPL/third-party distribution review remains unresolved.

## Next test contract, pending user authorization

No device commands or installation are authorized by this review. The next bounded device task should identify the selected ARM64 device/API, installed package/signature/data state and a safe test-data policy before changing it. Use these exact hash-pinned APKs and controlled fixture content; preserve OFF as the comparison baseline.

1. Cold-start OFF and ON. Confirm library loading, trust PEM creation and honest unavailable/error states. Verify warmUp does not create torrent networking and an explicit demand creates the engine lazily.
2. Drive the actual existing Stream/Media3 path with controlled, hash-known media. Verify initial decode/audio, loopback HTTP Range/seek behavior and correct file/source selection.
3. Exercise fast source/file switches, prefetch during playback, cancellation, corrupt/unverified fixture data, and unavailable-trust/engine conditions using specifically authorized test fixtures or fault builds.
4. Background/foreground and stop/restart repeatedly, including in-flight preparation/destruction. Inspect stale callbacks, GUI stalls, native/Media3 resource cleanup and engine re-creation.
5. Record device logs, exact content/APK hashes, observed timings, failures and repeatability. No integration, production release, broad live-torrent activity or claim of TLS/playback/lifecycle acceptance without the corresponding observation.

## Dots review status

Main branch feature/colosseum-server-1.0 remains at f32f84c963cec05e0b0c253ac5829f410dfe7854 at this review's remote-head check. Parent has inspected its actual ServerComposition/standalone host/CMake and read CLEAN-150-HLS and continuation receipts for the architecture comparison. That is source-and-supplied-evidence review only. Parent has NOT independently rebuilt/rerun the 150 tests, reproduced its HLS decode journey, reviewed every later ancillary delta or accepted it for integration. Do not describe Dots' entire branch as independently accepted.

Receiving decision: accept task008's build/packaging slice and advance the exact candidate to a separately authorized controlled device test. Preserve both isolated source replicas; no synchronization to the original app branch, merge, commit, installation or integration was performed.
