# Feria browser, set 5: native browser services

Status: notification policy implemented; native-services verification extended on Windows. Windows media commands, persistent sign-in cookies and public playback have agent-runtime proof. A signed-in real-provider account and physical keyboard-device check remain unverified. Work is on `codex/feria-release-packaging`, pushed separately from the original discovery branch; not merged or released.

Follow-up (2026-10-11): the owner chose to disable website notifications for now. The QtWebEngine request owner on `codex/feria-release-packaging` denies notification requests without a prompt, revokes saved notification grants when a profile is attached, disables push service, and closes any late notification. Resetting site permissions cannot re-enable notifications; location and other permissions retain their existing behavior. The observations and recommendations below describe the original discovery, before this decision.

Follow-up verification: the complete `feria_interaction_probe` passed on stock QtWebEngine 6.11.1 and the existing codec-enabled 6.11.2 runtime (15 passed, 0 failed each; clean process exit). This includes ordinary/private denial, reload, revocation across views, revocation after reopening the on-disk permission store, and location grant/reload/reset/denial. The two notification service cases passed on stock (4 passed including setup/cleanup): no presentation for a denied request, and a late engine notification is closed without a show event. Five focused request-owner cases passed (7 including setup/cleanup), covering hidden/subframe-origin requests and reset filtering. This is real engine fixture evidence, not a rebuilt/installed application or a real provider check. The full-app interaction script was updated and syntax-checked but not executed; WebView2 compatibility policy was not changed or qualified in this QtWebEngine slice.

Download follow-up: the original broader request-probe failure was fixed separately in `bacf9a40`. The download owner captures the account generation, clears revoked grants on completion/retry and keeps actual save failures retryable. The requests probe passed 28 checks and the metadata probe passed 8 checks on stock and codec runtimes, with clean exits.

WebView2 follow-up (2026-10-11): compatibility mode now installs the same notification-denial policy before allowing either its main view or an owned popup to navigate. The native host denies new notification requests, replaces only saved notification grants with denials, and claims/closes late notification delivery without displaying Windows notification UI. Other permission kinds retain their stored choices. A runtime that cannot install the policy fails initialization; it does not load an unprotected compatibility view. This requires the profile-permission API and notification-delivery API (`ICoreWebView2Profile4` and `ICoreWebView2_24`; the latter was introduced in SDK 1.0.2739.15). API references: [saved permission management](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2profile4?view=webview2-1.0.4191.47), [notification delivery](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2notificationreceivedeventargs?view=webview2-1.0.3912.50).

The focused native test is `tests/feria_webview2_integration/notifications.cpp`, target `feria_wv2_notifications`. It uses the production host, the installed WebView2 runtime, hidden parent windows, loopback pages and disposable profiles. It checks new requests and reload, persisted-grant revocation while preserving the stored location choice, and an owned popup reporting denial to its opener. A separate seeder process creates two grants and exits, explicitly waiting for its browser process to exit; the receiving process verifies both grants are restored before loading the production host. A build against the unchanged host fails the revocation assertion (0 denied, expected 2), proving the test detects the missing policy. Full assembled-app/installed-upgrade qualification and independent injection of a late service-worker notification remain separate checks.

Final result: the focused target built with MSVC and passed 5 checks (3 test cases plus setup/cleanup), 0 failures, process exit 0. The seeder also exited normally. The baseline control exited 1 at the expected saved-grant assertion; the final source was restored and rebuilt before the passing run. WebView2 briefly keeps some disposable profile files locked at test teardown, producing temporary-directory cleanup warnings; no personal profile or provider account was used.

To build this test, configure `tests/feria_webview2_integration` with Qt and the WebView2 SDK prefixes, then build target `feria_wv2_notifications` in Release and run it with the Qt DLL directory on `PATH`. The `FERIA_HIDE_TEST_POPUPS` hook is compiled only in test builds; shipped popup behavior is unchanged.

Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map. Set 4 (`docs/feria-browser-daily-features-set-4.md`) is the base of this branch.

## Native-services acceptance follow-up, 2026-10-12

The focused foundation runtime was rebuilt from this branch with MSVC. The complete `feria_services_probe` passed on stock QtWebEngine 6.11.1 and the existing codec-enabled 6.11.2 overlay: 14 passed, 0 failed, 1 skipped each, process exit 0. The skipped case is the deliberately opt-in public network check below.

`tst_MediaKeys.qml` checks a production browser view's real Windows media session. The native test helper checks the test application's ID and the fixture page's unique title before sending Windows Pause, Play, Next and Previous commands. All four reach the page's registered Media Session action handlers exactly once; the audio pauses and resumes. The helper does not invoke those handlers through page script and does not control another application's session. An unmuted, silent eight-second WAV allows the engine to register its OS session without playing sound through speakers. The earlier muted fixture could not establish this session. No product code or extra media bridge was needed. Physical keyboard keys and the visual Windows overlay were not manually tested; the OS command path was.

The login-cookie case now supports `FERIA_EXPECT_SIGNED_IN=1`, which asserts that the second process starts signed in before any login submission. Two clean launches over one disposable profile and fixed loopback port passed on both runtimes (3 checks per launch). The first process signed in; the second sent no login POST and restored the existing cookie. A fresh-profile control with that expectation fails at the intended assertion rather than silently signing in again. These are real engine cookie checks with dummy fixture credentials, not a third-party account login.

`tst_LiveService.qml` is an opt-in public playback check using `FERIA_SERVICE_URL`. Two final codec-runtime runs against the Blender Foundation's public YouTube video at `https://www.youtube.com/watch?v=aqz-KE-bpKQ` decoded video, advanced its playback clock and paused successfully. Both close checks drained every view/profile and exited 0 (3 passed each). An initial run passed playback but timed out on the 15-second close assertion; no product fix was made for that observation. It remains recorded as an intermittent close concern, rather than being erased by later passing runs.

Still unverified: sign-in and restart with an actual provider account (an account/service was requested from the owner and has not been supplied), protected-provider playback, physical media-key hardware, and a rebuilt/installed full application. Public YouTube playback does not qualify every provider, H.264/AAC delivery or Widevine packaging. Password-manager and ad-filtering recommendations in the original discovery below are unchanged.

Reproduce with a freshly built `feria_foundation_runtime` from `tests/feria_foundation_build`:

- Run `-input tests/feria_services_probe` on Windows for the service and OS media-command cases. The Windows target links `windowsapp` for the test helper.
- For cookie restart, use one disposable `FERIA_FIXTURE_ROOT` and one fixed `FERIA_FIXTURE_PORT`; run `FeriaServicesDiscovery::test_01_loginCookieClosingAndReopening` first with `FERIA_EXPECT_SIGNED_IN=0`, then with `1`.
- For public playback, set `FERIA_SERVICE_URL` and run `FeriaLiveService::test_publicPlayback`. When testing a relocated codec overlay, set its plugin, QML, WebEngine resource, locale and process paths explicitly. The test uses a disposable profile, never an existing personal browser profile.

## Base and merge endpoint

Built on `codex/feria-daily-features` at `b246da15`.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the set 4 branch.

## The bar

The plan's scope is password and autofill, ad filtering, notifications, media controls and resource management, with the proof "each service works independently of extensions". The owner's direction for the remaining sets: a functioning browser for a known set of services run like their own apps, not Chrome parity. So each service was judged by what those services need, not by what Chrome has.

## What was observed

Real engine, headless, loopback fixture, throwaway profiles. Probe: `tests/feria_services_probe/tst_ServicesDiscovery.qml` (8 passed 0 failed, by the worker and again by the reviewer). No product code changed. Logs: `C:\b\ff-results\set5\task-0001` and `C:\b\ff-results\set5\review-0001`.

| Service | Today | Verdict |
| --- | --- | --- |
| Staying signed in | A sign-in cookie set by a login form survives closing the tab, and a second process over the same profile starts signed in. Private tabs use an off-the-record profile. | Works. This is what "passwords" has to deliver for services run as apps. |
| Saving and filling passwords | Nothing saves or fills a password. QtWebEngine Quick 6.11 exposes no password manager: no `autoFillPasswords` setting and no `passwordManager` on the profile from QML. | Not built. A password store of our own would mean protected storage, per-site rules and script in sign-in pages. Owner decision; recommendation: do not build. |
| Ad and tracker filtering | Nothing is filtered: a sub-resource from a second host loads. Filtering needs a C++ `QWebEngineUrlRequestInterceptor` on both profiles (no QML setter), so an app rebuild. | Not built. Blocking by host cannot touch ads served from the same hosts as the video, which is how the paid services this app targets serve them, and risks breaking them. Owner decision; recommendation: leave out. |
| Notifications | The permission prompt and its memory work (set 3). After a grant, a page's `new Notification(...)` reaches the app (the profile emits `presentNotification` with title, message and origin; `show()`, `click()` and `close()` drive the page's `onshow`, `onclick`, `onclose`). The product connects nothing to it, so a granted site's notifications are never seen. | The one real gap. Owner decision, see below. |
| Media | The app reads what is playing from the page (title, position, playing state) and restores position; that is what Continue and resume use. It cannot command the page (no play, pause or next from the app) and publishes nothing to Windows. | Good enough for playback inside the service. Hardware media keys and the Windows media overlay could not be checked headless; the engine's own media-key handling was neither switched on nor off by us. |
| Background tabs | A hidden idle tab is frozen after 30 seconds (observed with the delay shortened); a tab playing audio stays active; switching back wakes a frozen tab. Three tabs of one site ran in three renderer processes. No cap on tabs or memory. | Works. |

## Decisions for the owner

1. **Notifications.** Today a site can be allowed to notify and then nothing ever appears. Three ways to make that honest, smallest first:
   - stop offering the permission: refuse notification requests without asking;
   - show a granted site's notification as a small notice inside the browser view only, dropped when the browser is not on screen;
   - show it anywhere in the app.
   Recommendation: the first, unless there is a service whose notifications are wanted. A calm TV-style app has little use for them, and it removes a prompt. The second is a QML-only slice of about a day through the loop if wanted.
2. **Passwords.** Recommendation: do not build a password manager. Services keep the user signed in, which was observed to work.
3. **Ad filtering.** Recommendation: leave out.
4. **Media keys.** Needs one check with a real keyboard and a playing service, on screen: do Play/Pause and Next reach the page? If they do, nothing to build. That check takes a person; it cannot be done headless.

## Original discovery limits (follow-up results above)

- Whether the engine itself ever offers to save a password or suggests typed form values to a real person typing: the probe submitted forms by script.
- Hardware media keys and the Windows media overlay.
- The codec-enabled runtime, the full application, many tabs, memory use over time.
- Any real service: everything here is a loopback fixture. Whether each listed service signs in, stays signed in and plays is the per-service work that follows the sets.

## Reproduce

```powershell
$qt = 'C:\Qt\6.11.1\msvc2022_64'
$env:PATH = "$qt\bin;$env:PATH"; $env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_FORCE_STDERR_LOGGING = '1'
<build>\Release\feria_foundation_runtime.exe -input tests\feria_services_probe
```

The loop mission for this set is `C:\Users\Suprabha\Desktop\Loop-Missions\feria-set5-01945259` (task, reply, review).
