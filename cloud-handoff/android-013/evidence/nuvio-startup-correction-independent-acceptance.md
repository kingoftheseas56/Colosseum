# Nokia T20 startup milestone — independent acceptance

Date: 2026-10-20, Asia/Calcutta. Worker: GLM 5.3 Max on ZCode. Receiver: Codex.

**Decision: ACCEPT the JNI repair, eager API-availability proof and bounded Android startup/UI-interaction milestone for the identified diagnostic artifacts. The previous startup-evidence gaps are closed. This is not streaming, native-session, production-integration or release acceptance.**

## Exact return and source/artifact identities

The worker's distinct correction report is `outputs/GLM-NUVIO-STARTUP-EVIDENCE-CORRECTION-REPLY.md`, 10,146 bytes, SHA-256 `76f8f18a13c0906999694a20e146f16d7f0e9ff6a9b6133504a4a6a73ae73926`. Its final marker matches task/path/hash. Earlier task010 and startup reports remain historical; no Preflight helper acknowledgement is claimed for this manual GLM return.

I authenticated Laptop 2 as `desktop-3k414a9\admin`, inspected the live wrapper and its exact diff against the preserved r2 source, and independently verified the following:

- Wrapper `native/player/streamserver_android.cpp`: SHA-256 `4ac7d02fa0aa9b4601876c46a72da90782e586cb9cf8897f1aa1b86066630226`.
- The r2 backup still hashes to `fdb55487fc766e871f1e5331c0c10f658aa04f1120c1249cf087b2baeb0dae36`. The r3 diff adds QDebug, hoists the identical library-path expression, and logs the production backend's actual `available()` result after construction. It does not hard-code success, change the OFF stub or invoke session creation.
- Contract-test source remains `7257970543f7ae21ba1833ebbefaa148842bc42748b497b962877ae967a93a6d`. Donor patch remains `4713fde75a1a6eabdf55e3332834be645faeca44ce75e067958e04c533eaf78a`.
- Actual r3 APK: `D:\NuvioTask008\device010\preflight-run\r3\variant-on-r3\android-build\build\outputs\apk\debug\android-build-debug.apk`, 260,824,479 bytes, SHA-256 `738c273d0be0ca332b426b21390891f54cc39ad62516e71b17f48488bdc5ff2d`.
- Fresh aapt: task package `org.qtproject.example.colosseum.nuvio010isolated`, v1.1.9, ARM64 only. Fresh apksigner verification and zipalign `-c -P 16 4` both exit 0. Certificate SHA-256 is `d54f67da3dbadddf25bfbf5b8bf70461d3ed626bff47437d51e6e2135226c733`.
- Independently streamed/hash-checked APK entries: engine `594cd435ac178b6c93c992941ce9d8b427a37827afc75440ae02835c19f21f11` (unchanged); packaged app library `363f2fdfd7e1319c8e34845c63bb215161b6c02f1772c3c8a563083c730b63af`.

The worker's build and 27-pass/4-skip contract rerun remain supplied results; I did not repeat an unchanged full build or those host tests in this review.

## Supplied device evidence reviewed

Relevant GLM evidence was copied from Laptop 2 to `outputs/review-nuvio-startup-correction-20261020` with SHA-256 checks during transfer and a local transfer manifest. Redundant base64 image copies were omitted; originals, JSON/logs, source backup and available r3 artifact/build receipts were retained. I inspected the per-attempt receipts and six PID-scoped log files. Four r3 ON process logs contain the actual production startup path and `api available: true`; none of those six captured logs contains the selected fatal/JNI/ANR patterns.

The earlier source review established that production `loadEngineApi` resolves the required function table and compares `api_version()` before returning a usable table. Runtime `available()==true` through that actual constructor therefore supports successful eager symbol/API-version validation. Actual native engine/session creation remains a separate lazy step. The previous unsupported inference from library mapping has been withdrawn.

I personally viewed representative worker screenshots showing the post-loading welcome screen, an ON Sign in tap opening the account form with keyboard, BACK dismissing the keyboard while the form remains, and the OFF control showing the same sign-in response. No entered credentials are visible. This closes the requested harmless-interaction gap without pretending BACK returns to welcome or naturally closes the application.

Worker records show three r3 ON cold launches plus a later interaction launch. Some timing fields were malformed by clock-domain mistakes; they are not used as precise duration proof in this acceptance. The separate OFF post-UI receipt has marker at 02:39:25.435 IST and aliveness/focus checks at 02:40:45.061 IST. Existing unchanged OFF launch/control evidence remains valid at its stated artifact scope.

## Fresh parent physical-device receiving check

I used Laptop 1's existing ADB to revalidate the same connected Nokia T20 at `192.168.29.37:34061`: model Nokia T20, device RDD, API 33, arm64-v8a and matching device serial. No APK installation or source modification was performed by Codex.

The reproducible script and raw result are in `outputs/review-nuvio-startup-correction-20261020/receiving_startup_check.py` and `parent-startup-receipt.json`.

Fresh successful check, approximately 02:53:48–02:55:16 IST:

- Installed diagnostic `base.apk` SHA-256 directly measured on-device: `738c273d0be0ca332b426b21390891f54cc39ad62516e71b17f48488bdc5ff2d`, matching the independently reviewed r3 APK.
- Actual cold launch, PID **31383**, ADB WaitTime 832 ms.
- Process log: discovery environment set before Qt; real production library path and **`api available: true`** at 02:53:54.956 IST.
- Usable-UI marker at 02:54:05.255 IST. The parent starts a monotonic timer only after observing this marker, then records seven aliveness samples through **66.844 seconds after observation**. No mixing device wall-clock and host monotonic values.
- Same PID remained alive, diagnostic activity was the actual current foreground at the final check, screen was awake/unlocked, and the collected PID log contained no selected JNI/fatal/ANR patterns.
- App-private `files/nuvio/tls/android-ca.pem`: 192,859 bytes, mode 0600.
- I personally inspected the final parent screenshot: fully rendered Colosseum welcome UI, no loading/lock/notification overlay. Image SHA-256 `0c7ab4832ae27aea37955280b76904da068cf756e60151de3ac9f937d0c9a44b`.
- Original `org.qtproject.example.colosseum` package stayed v1.1.7 with identical recorded first-install/update timestamps before and after. Parent commands targeting it were read-only metadata queries; its private data was not inspected. No mutating command targeted the original package.
- Parent cleanup was an explicit force-stop of the task-owned diagnostic package. It does not establish natural shutdown.

The first parent attempt also recorded a valid eager API result and 66.406 seconds of process aliveness, but its focus observer queried `dumpsys window windows`, which omitted the current-focus field. That harness failure is preserved as `parent-startup-receipt-focus-observer-failure.json`; it is not evidence the app lost focus. I corrected the observer to the full `dumpsys window` output and repeated the bounded check. The successful receipt has runner exit 0 and result PASS. Failed observations were not erased or relabeled as successful runs.

## Quality notes and remaining boundaries

- GLM initially used the receipt hash as the expected APK hash and continued after that false identity row. The final report discloses this, and parent artifact/on-device checks resolve the actual identity. Future harnesses must derive the expected APK hash from the artifact entry, validate it before consequential actions, and stop on an unresolved identity mismatch. A false row cannot be ignored merely because later testing appears successful.
- Worker timing mistakes and final-cycle short observations are disclosed. The fresh parent monotonic test independently closes the important post-usable-UI stability gap; invalid elapsed-time fields remain invalid historical evidence.
- `universeHallLayer` reference errors and TopBar binding-loop warnings remain observed shell warnings shared by ON/OFF. BACK retaining the account form is also recorded. These are separate application findings; no unrelated repairs were made.
- Effective native-session DHT/LSD/UPnP/NAT-PMP settings, actual engine create/destroy, TLS consumption, loopback stream delivery, real Media3 frame/audio, Range/seeks, playback lifecycle/cleanup, negative fixtures, natural app exit and production packaging remain unqualified.
- The next bounded streaming task must first prove effective discovery isolation in the actual session before introducing a private local fixture, then demonstrate the real application/player route. Idle player labels or the earlier mpv log are not Media3 proof.

**Startup review is complete and accepted at the identified diagnostic artifact scope. No streaming task was executed or dispatched, no reminder restarted, and no staging/commit/push/merge/CI/release/integration occurred.**
