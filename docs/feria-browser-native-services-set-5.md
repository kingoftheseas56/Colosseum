# Feria browser, set 5: native browser services

Status: DISCOVERY COMPLETE, nothing built. Each of the five services was observed in the real engine; four need no work for the browser's purpose and one waits on an owner decision. Branch `codex/feria-native-services`. Pushed, not merged.

Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map. Set 4 (`docs/feria-browser-daily-features-set-4.md`) is the base of this branch.

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

## Not verified

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
