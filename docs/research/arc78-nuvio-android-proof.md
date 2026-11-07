# Arc 78 Android Nuvio host proof

Source basis: `00572b54dc00b52298a278167b4b7b81e7822f72`.
Candidate branch: `dots/arc78-nuvio-android-proof`. No production merge.

## Recommendation

Use a private, bound Android `QtService` process, loading the existing native
QuickJS-NG host as an APK shared library. Four independent `:nuvio0` through
`:nuvio3` services preserve the desktop resolver's maximum parallelism. Each job
gets a fresh process, Java VM/Qt Android context, and output pipe. A service crash
or hard deadline does not execute in the activity process. This is crash
isolation, not a separate-UID security sandbox: the service intentionally retains
the app's network permission and existing Android credential backend.

### Alternatives under the same limits

| Option | Crash isolation | Android integration | Decision |
| --- | --- | --- | --- |
| APK-packaged native executable | Yes, as a separate process | Requires a real installed executable path; Android 10+ forbids executing binaries copied into writable app data. Uncompressed APK library paths are not ordinary executable paths. A raw exec also lacks the existing Android JNI/Keystore context, requiring a replacement bootstrap or credential/network broker. | Potentially small for a self-contained C host, but not the smallest supported adaptation of this Qt/JNI host. Not runtime-qualified by this proof. |
| Private `:remote` QtService | Yes | Android loads APK code with the VM and Qt context. Binder transfers only bounded arguments and a pipe FD; large scripts/results do not become Binder transactions. Process-death handling and explicit cancellation own the lifecycle. | Selected. Reuses the existing engine, network implementation, and exact TMDB authorization. |
| QuickJS thread in the activity | No | Avoids IPC and process startup. QuickJS interruption can stop JS loops; it cannot contain a native SIGABRT/SIGSEGV or safely stop arbitrary hung native code. | Rejected for the requested crash-isolation requirement. No need to ship an app-crashing control variant. |

The comparison is architectural. Only the selected service is an implementation
candidate. No measurements of the unimplemented alternatives are claimed.

References: [Android service processes](https://developer.android.com/guide/topics/manifest/service-element),
[Android 10 execution restrictions](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission),
[Qt Android services](https://doc.qt.io/qt-6/android-services.html),
[Binder transaction limits](https://developer.android.com/reference/android/os/TransactionTooLargeException),
[QuickJS memory and interruption controls](https://bellard.org/quickjs/quickjs.html#Memory-handling).

## Preserved host contract

The same `WorkerMain.cpp` runs in the service. The Android-only entry obtains a
bounded argument array and FD over an unexported Binder, then calls the common
host routine. Desktop still starts the same executable and uses stdin/stdout
process semantics. Android constructs `QAndroidService`, which releases the
QtService startup handshake before Java delivers the job Binder; plain
`QCoreApplication` would leave the Java service waiting. This uses Qt 6.11.1's
private Core API and must be checked when upgrading Qt. The normal Android build
does not include fault hooks or the proof UI. `COLOSSEUM_NUVIO_ANDROID=OFF` removes
both service Java and manifest
entries for the size baseline.

Unchanged limits include:

- 4 MiB script, 512 KiB output, at most 64 stream results.
- 32 MiB QuickJS heap and 512 KiB QuickJS stack.
- 20-second plugin deadline; 22-second parent process watchdog.
- 2 MiB HTTP body, 80 total requests, 8 in flight.
- 128 active timers and 4096 total timer registrations.
- Existing public-address DNS validation, IP pinning, TLS verification,
  redirect/header handling, and HTTPS direct-stream filtering.

`NuvioTmdbCredentials.cpp`, `NuvioTmdbCredentials.h`, `NuvioNetwork.cpp`,
`NuvioWorkerProtocol.h`, and `host-bootstrap.js` are unchanged from the source
basis. JS sees only `__COLOSSEUM_TMDB_KEY__`. The actual credential is loaded and
substituted natively only for the existing exact TMDB HTTPS endpoint contract.
No new key is supplied through arguments, JS, test fixtures, logs, or Git.

## Evidence and limits

The Android CI lane builds arm64-v8a and x86_64 normal APKs, separate disabled
baselines, and an explicitly instrumented x86_64 emulator APK. The size report
compares normal enabled/disabled APKs from the same commit and toolchain; proof
instrumentation is excluded. See the Dots-Colosseum delivery record for the exact
candidate commit, run URLs, APK sizes, screenshots, logs, and pass/fail outcomes.

The controlled public HTTPS Nuvio fixture queries real Internet Archive metadata
for *Night of the Living Dead (1968)* and returns a direct public-domain URL.
The separately disclosed HTTPS Stremio fixture traverses the unchanged Stremio
implementation. These fixtures prove transport/protocol integration, not the
availability or compatibility of every third-party scraper or provider.

Fault stages intentionally supply synthetic plugin code. A proof-only native
abort hook must produce both a `native_abort_requested` worker log and matching
SIGABRT crash evidence. The infinite-loop plugin uses the unchanged deadlines.
The driver requires the same application PID, preserved Stremio registry/rows,
and a fresh successful Stremio query after the faults. A physical device is
rejected by the driver. No personal phone data is installed over.

The Java lifecycle regression uses Android stubs to exercise queueing, process
retirement, pending cancellation, fast-crash PID retention, and four independent
slots. It is not Android SDK or emulator evidence. Existing legacy build-graph
text assertions have ten identical failures on the pristine source basis because
they assume a former monolithic CMake layout; they were not changed for this task.

Two existing CI prerequisites needed narrow repairs before building the pinned
source: the CMake source-model test must ignore non-application library targets,
and Android's TopBar must declare the DLNA intent already wired by shared hosts.
The Android bar still has no DLNA button. The toolchain also needs Qt Quick 3D for
the already-staged Dexter scene. Both size-comparison variants use these same
prerequisites; their size delta isolates Nuvio rather than these repairs.

Real-device follow-up remains necessary for OnePlus arm64 runtime, OEM background
process/battery policy, memory pressure, the real credential store, TLS/network
behavior on that device, and startup latency. An arm64 build alone does not prove
arm64 runtime. API 34 emulator coverage does not establish newer Android/16 KiB
page-size compatibility or background behavior on other devices.
