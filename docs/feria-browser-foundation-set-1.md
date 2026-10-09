# Feria browser foundation, set 1: native lifecycle contract

Status: implemented and verified on Windows against the stock Qt 6.11.1 kit and against the codec-enabled engine overlay. Branch `codex/feria-browser-foundation`. Not merged, not released.

This document is the owner map, the contract, the verification record and the handoff for the sets that follow. It describes what the code does today. It does not restate `docs/feria-browser-engine-contract-v1.md`, which stays the behaviour contract for both engines.

## What this set is

One C++ object, `FeriaBrowserRuntime`, that knows for every open browser activity which engine view serves it, what phase that view is in, and what asynchronous work is outstanding against it. It refuses work and commands for anything that is no longer current, and it decides the order engines leave in. The QML deck stays what it has to be under Qt Quick: the owner of the `WebEngineView`, `WebEngineProfile` and Qt request objects.

Nothing here is a second tab registry. The runtime allocates no activity ids and stores no URL, title or history.

## Base and merge endpoint

The branch is built on `glm/feria-shared-layer-slice-1` at `189388e4`, not on `master`. That branch already holds the C++ navigation/close ledger (`FeriaBrowserOperations`) and routes the Qt close controller and navigation through it. Building on `master` would have produced a second, competing ledger and conflicting edits to the same three QML files.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the accepted GLM slice 1. If that slice is rejected, the commits here are rebased onto `master` carrying only the ledger files they use (`FeriaBrowserOperations.*`, its test, and the Qt-side routing). The WebView2 adapter on the base branch is carried, not changed, by this set.

## Owner map

| Concern | Owner | Kind |
| --- | --- | --- |
| Open activities, active one, activity id, account epoch, profile id, page generation, document generation, host id and generation, close attempt, closed-space records | `SessionStore` (`native/SessionStore.h`) | canonical, unchanged |
| Navigation and close operations, one-use consent requests, exactly-once outcomes, evidence rules | `FeriaBrowserOperations` (`native/feria/`) | transient ledger, from GLM slice 1; one evidence kind added |
| Durable browser state, restore offers, download history and grants | `FeriaAccountStore` | durable, unchanged |
| Storage root, navigation and omnibox policy, window geometry, cookie cleanup | `FeriaBrowserPolicy` | rules; its raw view/profile sets moved into the runtime |
| **Engine instances: view per activity, phase, engine generation, document epoch** | **`FeriaBrowserRuntime`** (`native/feria/FeriaBrowserRuntime.*`) | **transient, new** |
| **Outstanding script, prompt, adoption and download work** | **`FeriaBrowserRuntime`** | **transient, new** |
| **Command door: open, activate, navigate, back, forward, reload, stop, close, recover** | **`FeriaBrowserRuntime`** | **new** |
| **Exit order: work, then views, then profiles** | **`FeriaBrowserRuntime`** with the deck as executor | **new** |
| Qt Quick engine objects, hosting and parking, popup adoption, quiescence sequencing | `FeriaSessionDeck.qml` | engine adapter |
| Qt navigation generations, script closures, renderer-loss handling, `windowCloseRejected` | `FeriaQtWebEngine.qml` | engine adapter |
| Before-unload capture and Qt rejection barriers | `FeriaCloseController.qml` | engine adapter, unchanged |

`FeriaBrowserPolicy` owns the runtime and exposes it as `FeriaBrowserPolicy.runtime`, so it exists wherever a Feria engine view can exist, including the cleaner and popup views that serve no activity. `registerRuntimeView`, `runtimeViewCount` and their profile counterparts keep their names and now read the runtime's table. There is one tracker of live engine objects, not two.

## Identity

Unchanged, and now typed in C++ as `FeriaBrowser::PageKey`: account epoch, profile id (`<root>/ordinary` or `<root>/private`), activity id and page generation. This is the stable binding of one page allocation.

- Navigation, redirects and title changes do not change it. Document generation and the wrapper's own navigation generation move; the key does not.
- Explicit New always allocates a new activity, so two tabs can sit at one URL. Default open keeps open-or-switch: the provider's most recently used space of the same privacy mode.
- The window identity is the existing host id and host generation. A lease that names a host dies when that host placement changes; a lease without host fields survives it.
- Operation identity is the ledger's operation id; a close operation's id is the canonical close attempt.
- Engine generation is new and transient: it counts engine instances for one activity. It moves when the renderer is lost or the view is rebuilt. It is not written to the store.

## Instance lifecycle

Phases, per activity: `creating`, `ready`, `close-requested`, `retiring`, `failed`, and `absent` when there is no instance.

| Transition | Cause |
| --- | --- |
| absent to creating | The deck created the entry for a canonical record (`beginInstance`). Refused without a record. |
| creating to ready | A real engine event (load started, or an adopted window's first completion). Object creation alone is not readiness. |
| ready to close-requested and back | Read from `SessionStore`'s close attempt, never copied. Cancellation returns to ready the moment the store clears the attempt. |
| any to failed | Creation failed, the engine is unavailable for this build, the renderer was lost, or the view was destroyed while its record is still open. |
| failed to ready | A document starts in the recovered engine. |
| any to retiring | The canonical record closed, the account changed, the deck retired the entry, or the application is exiting. |
| retiring to absent | The view object was destroyed. With no view there is nothing to drain and the instance ends with its record. |

One activity has at most one live view, and a view serves one activity for its whole life. A replacement view for the same activity starts a new engine generation, and nothing admitted for the old view is valid afterwards.

## Outstanding work

A ticket is admitted against the view or profile the work was started for and redeemed exactly once when its callback arrives.

| Kind | Bound to | Used by |
| --- | --- | --- |
| `script` | view, engine generation, document epoch, page key | every `evaluateJavaScript` in the Qt wrapper |
| `request` | same | permission, screen-share, client-certificate and passkey prompts |
| `adoption` | view, engine generation, page key | a popup placeholder waiting for its first real event |
| `download` | profile object | each download; it outlives the tab that started it |

Redemption fails when the account, page, engine generation or document the ticket was admitted for is no longer current, when the view is retiring, or when the application is exiting. A late callback therefore cannot act on a replacement document, a recovered renderer, another activity or another account.

Scripts already dispatched stay outstanding through retirement until the engine returns them or the renderer is lost; that is the drain barrier the wrapper waits on before it reports the view safe to destroy. Prompts and adoptions are dropped when retirement begins, because they can no longer be answered. A profile is releasable only when no view uses it and no download ticket is held against it.

QML still holds the JavaScript closure for each script callback. Qt Quick gives no other way to keep a callback, and severing it remains the wrapper's job. The decision whether the result may act is native.

## Commands

All shell commands enter through the runtime. `execute(lease, command, arguments)` is for a caller holding a captured identity; `command(spaceId, ...)` reads the current lease first. Results are `{accepted, reason, command, spaceId}` and are also emitted as `commandFinished`.

| Command | Native checks before any engine object is touched | Executed by |
| --- | --- | --- |
| `open` | not exiting, store present, URL allowed | `SessionStore.openBrowser` |
| `activate` | lease current | `SessionStore.switchTo` |
| `navigate` | lease current, not retiring, URL or typed text resolved and allowed by `FeriaBrowserPolicy` | deck, which queues it while the view is still being created |
| `back`, `forward`, `stop` | lease current, phase ready or close-requested | deck, native history only |
| `reload` | lease current, a bound view that is past creating | deck |
| `close` | lease current, not retiring | deck asks the engine to unload; with no view the runtime closes through the ledger with evidence `no-engine-instance` |
| `recover` | lease current, phase failed | deck: reload after renderer loss, rebuild after failed creation |

Refusal reasons: `unknown-command`, `wrong-thread`, `shutting-down`, `unknown-space`, `stale-lease`, `retiring`, `no-engine-instance`, `not-ready`, `not-failed`, `navigation-not-allowed`, `no-adapter`, `adapter-refused`, `close-refused`, `store-refused`, `no-store`.

Presentation moves (detach to a window, maximise, return to embedded) stay with the deck. They are not commands; the runtime only sees their effect on leases.

Commands are privileged. The runtime is reachable from the application's QML context only. Nothing registers it on a web channel, and a page's JavaScript has no object through which to reach it; the real-engine probe checks this from inside a page.

## Events

- `phaseChanged(spaceId, phase, reason)` for every instance transition.
- `commandFinished(result)` for every command, accepted or refused.
- `FeriaBrowserOperations.operationSettled(operationId, outcome)` for every navigation and close terminal.
- `viewCountChanged`, `profileCountChanged`, `shuttingDownChanged`.

## Closing and exit

Before-unload accept and cancel, the Qt rejection barrier and the distinction between user consent and engine acknowledgement are unchanged and still live in `FeriaCloseController.qml` over the ledger. This set adds three things around them.

1. A view is released only when the runtime reports it drained. A timeout in the deck still produces a recoverable failure (`quiescenceFailed`) and never forces destruction.
2. Account change and sign-out destroy a profile only after every view that used it is gone and no download ticket is held against it.
3. Application exit runs `FeriaBrowserRuntime::shutdown()` from `aboutToQuit`: new work is refused, outstanding tickets stop being redeemable, and the deck releases each view's web contents, then queues every entry for deletion ahead of the profiles. Canonical activities are not closed, because they are what gets offered for restore. Before this, exit left engine objects to the QML object tree; the fixture's control run observes that order destroying both profiles while views still exist.

There is no drain at exit, because the event loop is leaving. Instead each Qt view is discarded (`lifecycleState = Discarded`, public API) before its object is deleted. See the engine boundary on script callbacks below for why.

## What the QML adapter must do

For any engine wrapper, present or future:

- call `beginInstance(spaceId)` when it starts building a view and `bindView(spaceId, view)` when the object exists;
- call `markReady(view)` on the first real engine event, `documentChanged(view)` at every document boundary, `rendererLost(view)` when the renderer dies, `beginRetirement(view)` when it starts closing, and `failInstance(spaceId, reason)` when creation fails;
- admit a ticket before dispatching asynchronous work and redeem it in the callback, acting only if redemption succeeds;
- implement `runBrowserCommand(request)` and `shutdownEngines()` on the one object passed to `attach(adapter, store)`.

The Qt path does all of this. The WebView2 wrapper on the base branch gets instance binding and readiness from the deck; it does not yet report document boundaries or renderer loss, and it admits no tickets.

## Engine boundaries

Facts about QtWebEngine 6.11.1 as used here. None is worked around with a JavaScript stub or an invented success signal.

- **Close rejection has no public acknowledgement.** `windowCloseRejected()` is invoked dynamically by `qquickwebengineview.cpp`. It is an undocumented compatibility dependency, kept exactly as it was, and not a supported API. If it is not delivered the close controller fails safe.
- **Script callbacks cannot be cancelled.** A dispatched `runJavaScript` stays outstanding until the engine returns it or the renderer dies. Hence the drain barrier.
- **Destroying a view that still owes script callbacks can crash.** Qt Quick runs a view's pending `runJavaScript` callbacks when its web contents are destroyed. When that happens inside the view's own destructor the process can fault in `Qt6Qml` (access violation `0xc0000005`, frames `Qt6Qml` under `Qt6WebEngineQuick` under `Qt6WebEngineCore` under the view destructor; no Qt symbols were available, so the exact function is inferred, not proven). Observed in 3 of 57 exits of the shutdown journey before the fix (stock engine, once with windows and twice headless), never in normal close, which drains first. The exit path now discards the web contents while the view object is whole, so the callbacks arrive before the destructor. After that change: 0 faults in 110 consecutive exits. This is a mitigation of an engine behaviour, measured, not a proof that the fault cannot occur.
- **Profile lifetime is not enforced by Qt Quick.** Nothing in the public API stops a profile being destroyed under a live view. The order is enforced here.
- **A lost renderer is recovered inside the same `WebEngineView`.** "New engine instance" on renderer loss means a new renderer and a new engine generation in the existing view, not a new view object. A view is rebuilt only after failed creation.
- **Adopted windows depend on their creation view** on stock Qt. The existing guard (child windows block their opener's retirement) is unchanged. The patched-Qt marker `_colosseumAdoptionInputRebound` is absent in both runtimes tested, so that guard was active throughout.
- **Extensions.** The Qt Quick extension experiment's failures (missing API methods, service-worker start failures, inactive DNR rulesets, a `Qt6WebEngineCore` exit exception `0x80000003`) are outside this set. No `chrome.*` API is implemented and C++ services here register no extension bindings. What a later extension layer can use: the activity id as a stable tab identity, the host id as a window identity, `execute` with a lease as the command path, and the three event signals.

## Verification

Run on Windows 11 Pro 10.0.26200, MSVC 2022, 2026-10-09. Everything below was run headless (`QT_QPA_PLATFORM=offscreen`) with loopback fixtures and throwaway profiles. No provider account, personal cookie, DRM stream or production deployment was touched.

### Runtimes

| | Stock kit | Codec-enabled deployment |
| --- | --- | --- |
| Reported by the process | Qt 6.11.1, WebEngine 6.11.1, Chromium 140.0.7339.225, security patch 148.0.7778.96 | Qt 6.11.1, WebEngine 6.11.2, Chromium 140.0.7339.225, security patch 147.0.7727.138 |
| `Qt6Core`, `Qt6Quick`, `Qt6WebEngineQuick` loaded from | `C:\Qt\6.11.1\msvc2022_64\bin` | `D:\ff-stage-codec` |
| `Qt6WebEngineCore.dll` loaded from | `C:\Qt\6.11.1\msvc2022_64\bin` (SHA-256 `02FFF5F4...D340C`) | `D:\ff-stage-codec` (SHA-256 `F85D2F83...571EB`, the overlay manifest's hash) |
| Engine helper | default beside the core DLL | `D:\ff-stage-codec\QtWebEngineProcess.exe` (SHA-256 `342D6A47...7858`) |

The codec runtime is a copy of the known-good `runtime-probe/candidate` deployment with the test executables and the missing test modules added. It must be one complete directory: with only the engine DLL beside the executable and the rest on `PATH`, the sandboxed helper exits with `STATUS_DLL_NOT_FOUND` and no page loads. The fixture prints the versions and module paths above at the start of every run.

### Results

Counts are passed / failed / skipped as printed by Qt Test.

| Suite | What it exercises | Stock | Codec |
| --- | --- | --- | --- |
| `feria_runtime_test` (new, native) | lease, phase, ticket, command, profile-order, thread and exit rules against the real store, ledger and policy; includes a control with the fence switched off | 17 / 0 / 0 | 17 / 0 / 0 |
| `feria_operations_test`, `feria_foundation_policy_test`, `feria_session_ownership_test`, `feria_hosting_state_test`, `feria_hosting_geometry_test`, `feria_account_store_test` | existing native owners | 15, 26, 8, 15, 6 passed, 0 failed; account store exit 0 | same |
| `feria_runtime_probe` (new, real engine) | see the case list below | 15 / 0 / 0 | 15 / 0 / 0 |
| `feria_foundation_probe` | native navigation, history, popups, POST, named targets | 18 / 0 / 1 | 18 / 0 / 1 |
| `feria_session_probe` | retained pages, profile switch, private tabs, freeze, popup adoption, download after tab close | 12 / 0 / 0 | 12 / 0 / 0 |
| `feria_lifecycle_probe` | media pause and resume, script tickets, renderer terminals, cleanup | 21 / 0 / 0 | 21 / 0 / 0 |
| `feria_renderer_probe` | real renderer termination during close and during use | 4 / 0 / 0 | 4 / 0 / 0 |
| `feria_restore_probe` | restore offers, duplicate URLs, failed loads | 8 / 0 / 0 | 8 / 0 / 0 |
| `feria_hosting_probe` | before-unload Stay and Leave, retry, close racing navigation, detach and maximise, dialogs, nested adoption, account retirement with consent pending | 40 / 0 / 1 | 40 / 0 / 1 |
| `feria_download_metadata_probe` | download owner with doubles | 5 / 0 / 0 | 5 / 0 / 0 |
| Journeys (new, whole process) | see below | shutdown 5 of 5, control as expected, retention pass | shutdown 5 of 5, control as expected, retention pass |

Real-engine cases added in `tests/feria_runtime_probe/tst_Runtime.qml`:

- identity across navigation, a server redirect, typed address, Back, Forward and Reload, with a second tab at the same URL that never moves, and default open still reusing the last-used space;
- Stop and refused navigations (`file:`, `javascript:`) leave the page usable;
- park, reactivate, detach to a window and return: same view object, same engine generation, phases never leave `creating, ready`, and the detached window's lease is refused afterwards with `stale-lease`;
- profile switch: every command with the old lease is refused, a script callback from the old account never acts, the new account's lease is its own, and a forged lease carrying the old account epoch is refused;
- a script result that returns after its document was replaced cannot act;
- close racing a navigation: exactly one close terminal (`cancelled`), the space survives, and the retried close succeeds;
- a retiring view is destroyed only after its outstanding script has drained, and admits nothing new meanwhile;
- an activity whose engine cannot be created (`engineId: webview2` in a build without it) reports `failed`, refuses recovery, and can be closed without leaving a canonical record;
- real renderer termination: phase `failed`, work retired, engine generation advanced, lease and page key unchanged, `recover` returns the same view to `ready`;
- a real POST popup is adopted into its own activity with its opener and form body intact;
- a real permission prompt (geolocation on the loopback origin) holds a ticket; replacing the document closes the prompt, invalidates the ticket, and a late Allow grants nothing;
- a page's own JavaScript sees none of `qt`, `QWebChannel`, `FeriaBrowserPolicy`, `Sessions`, `FeriaAccount` or `chrome.webview`;
- a private space shares no cookie or storage with an ordinary one of the same origin, leaves no reopen record, and its profile goes with its last space.

Journeys (`tests/feria_runtime_journey/run_journeys.py`), each one process that ends through the application's exit path:

- **shutdown**, five times: five live pages (two at one URL, one private, one already closed and still draining a script), a running script, a navigation the server has not answered and a download in flight. Pass requires exit code 0, no view or profile left when the event loop returns, and no profile destroyed while a view exists.
- **shutdown control**: the same with the orderly path skipped. It must report engines left alive (exit 3); it then shows both profiles destroyed while views still existed during engine teardown.
- **retention**, two processes over one profile root and one origin: the first sets a cookie and local storage in an ordinary page and in a private page; the second finds the ordinary values, none of the private ones, exactly one restore offer (the ordinary page) and no page allocated before an explicit restore.

### Not passing, and why

- `feria_browser_requests_probe` 25 / 1, `feria_page_tools_probe` 10 / 2 and `feria_browser_menu_probe` 11 / 1 fail the same cases on the unmodified base branch in the same environment (`test_download_old_account_authorization_cannot_record_in_new_account`, `test_compactHeaderIsOptInForProviderRoute`, `test_escapeClosesFindBeforeImmersiveMode`, `test_sections_render_inside_dialog`). They are not caused by this set and were not repaired in it.
- `feria_hosting_probe` is not perfectly stable. On the final code it was 40 / 0 / 1 on both runtimes. In the three headless runs just before the exit-only change it was 40 / 0 / 1 twice on stock and 39 / 1 / 1 once on the codec runtime; the unmodified base gave 39 / 1 / 1 then 40 / 0 / 1. The single failures were different input-timing cases each time (a dropped keystroke, a host geometry compare). The one skip needs the patched-Qt adoption marker.
- Operation attribution in the ledger: a successful Back or Forward is recorded as `back: cancelled (superseded-by-navigation)` followed by `navigate: succeeded`, because the Qt wrapper admits a `navigate` operation on every URL change. Reload is recorded as `reload: succeeded`. Command results and page state are correct; the ledger's label is not. This comes from the base slice and is listed for its owner.

### Not verified

- **The full application.** `colosseum` and `lanista` build from this branch (Release, Ninja, 302 steps, exit 0). Started headless under Lanista in an isolated profile, the application boots, loads `Main.qml` with the new wiring without a QML type or reference error, and reaches Feria and its account manager: 22 steps of `feria_account_native` pass. Step 23 (`Apps tab selected from home`) then fails, and there is no baseline to say whether that is this branch, the scenario or the headless platform, because the existing `master` build did not start under the same conditions (`bridge never became ready`). Lanista's clean-exit check cannot run headless at all: it closes the application through its native window, so every headless session, including a two-step boot-only one, is reported as killed after the graceful timeout. Application-level exit, and browser pages opened through the real shell, are therefore not verified.
- **Windows on screen.** The final record is headless. With real windows, earlier in the same session, the same suites passed except one varying hosting case per run on both this branch and the base. They were not repeated on the final code because the windows interrupt whoever is using the machine.
- **Rebuilding a view after failed creation** (`recreate()`). The native contract is tested; no real-engine case could make a view fail to construct. The engine-unavailable form of failed creation is tested with the real engine.
- **WebView2.** The fixture build has no WebView2 adapter. `tests/feria_webview2_integration` lists the new source file and compiles against the changed sources with the WebView2 SDK (`feria_wv2_integration.exe`, exit 0), but its suite drives real windows and was not run. Nothing here shows whether the WebView2 path still passes.
- **Linux**, the patched-Qt adoption path, live providers, DRM and the Lanista shutdown scenario: not run.

### Reproduce

```powershell
$qt = 'C:\Qt\6.11.1\msvc2022_64'
cmake -S tests/feria_foundation_build -B <build> -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$qt"
cmake --build <build> --config Release
$env:PATH = "$qt\bin;$env:PATH"; $env:QT_QPA_PLATFORM = 'offscreen'
<build>\Release\feria_runtime_test.exe
<build>\Release\feria_foundation_runtime.exe -input tests\feria_runtime_probe
python tests\feria_runtime_journey\run_journeys.py --exe <build>\Release\feria_foundation_runtime.exe --qt-bin "$qt\bin"
# codec-enabled deployment: a complete runtime directory that also holds the executables
python tests\feria_runtime_journey\run_journeys.py --exe <runtime>\feria_foundation_runtime.exe --runtime-dir <runtime>
```

Other probes run the same way with `-input tests\<probe directory>`. For input-driven cases on a real desktop, drop `QT_QPA_PLATFORM` and set `FERIA_FIXTURE_FOREGROUND=1` so a test started from a background shell can take the foreground.

Test-only switches, compiled only into the fixture build (`FERIA_TEST_CONTROLS`): `FERIA_UNSAFE_RUNTIME_FENCE=1` turns the lease and ticket fences off, `FERIA_SKIP_ORDERLY_SHUTDOWN=1` skips the ordered exit.

## Handoff for the later sets

For GLM, and for anyone building the conventional browser interface on the same services.

**Call these, not the deck's internals.** From QML: `FeriaBrowserPolicy.runtime`. From C++: `FeriaBrowserPolicy::runtime()`.

- `open(descriptor, {fresh, navigate, activate})` returns `{accepted, spaceId, reason}`.
- `lease(spaceId)` returns the identity to hold; `leaseCurrent(lease)` checks it.
- `execute(lease, command, arguments)` and `command(spaceId, command, arguments)` for `activate`, `navigate` (`{url}` or `{text, searchEngine}`), `back`, `forward`, `reload`, `stop`, `close`, `recover`.
- `phase(spaceId)`, `instance(spaceId)`, and the signals `phaseChanged`, `commandFinished`, plus the ledger's `operationSettled`.

A second frontend gets its own leases and shares the one deck. `attach` refuses a second adapter on purpose: two engine owners for one store is the thing this set exists to prevent.

**Extend these owners; do not add parallel ones.**

| For | Extend |
| --- | --- |
| history and bookmarks (set 2) | `FeriaAccountStore`, which already owns durable browser state and visit records |
| anything per open tab that must survive navigation | the `SessionStore` record |
| a new operation kind or evidence rule | `FeriaBrowserOperations` |
| a new kind of asynchronous engine work | a `WorkKind` in `FeriaBrowserRuntime`, admitted and redeemed by the adapter |
| a new command | `FeriaBrowser::Command`, its native checks in `execute`, and one branch in the deck's `runBrowserCommand` |
| another engine | the adapter obligations listed above; the WebView2 wrapper still owes document boundaries, renderer loss and tickets |

**Limits you will meet.**

1. The ledger mislabels Back and Forward, as recorded under Verification. Fix it where the Qt wrapper admits operations, not by reading `commandFinished` as a substitute.
2. Close rejection depends on the private `windowCloseRejected()` hook. Do not present it as a public API, and do not replace it with a script result.
3. A view must never be destroyed while it owes script callbacks. Normal close drains; exit discards first. Any new path that deletes a view has to do one or the other.
4. A profile must never be destroyed before its views. Ask `profileReleasable(profile)`.
5. The engine helper needs a complete runtime directory. A lone replacement engine DLL does not work.
6. Tickets only bind engine views and profiles that are registered. Component doubles in tests are unmanaged by design and keep the older local fences.
7. No extension API exists here. The seams for one are the activity id, the host id, leased `execute`, and the three signals. An unresolved engine gap must stay visible as `unsupported`, not be papered over.
