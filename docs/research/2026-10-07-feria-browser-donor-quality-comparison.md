# Feria: five-donor feature implementation comparison

Date: 2026-10-07. Status: source comparison and implementation-selection recommendations.

Presentation update: the current [spaces contract](2026-10-07-feria-dots-ui-contract-draft.md) and [granular audit/prototype brief](2026-10-07-dots-feria-inspection-brief.md) supersede this report's session-group taskbar and Pop out/Return presentation assumptions. Keep the 52 category numbers stable. Source findings and selected donors below are historical candidates to re-evaluate per batch, not an approved current UI or a mandate to preserve existing backend code.

This is research for the combined Feria browser/shared-taskbar design, not the approved design or an implementation plan. It compares the supplied versions of all five donors against the same capability catalog. A feature being present is not sufficient for selection. There is no requirement to take code from every donor.

## Evidence and limits

The five supplied ZIPs were inventoried, hashed, and extracted for inspection. Browser handlers, supporting native models, profile/window ownership, persistence, and relevant donor test sources were inspected. Searches included the source inventory and Morph's Qt 6 path; Morph's Qt 5-only variants are not candidates for the current build. A search finding no custom implementation means **not found in the supplied application code**, not that Chromium lacks that capability.

The installed Windows Qt 6.11.1 `QtWebEngine/plugins.qmltypes` and view header were inspected for API compatibility. They export `WebEngineProfilePrototype`, `acceptAsNewWindow`, `WebEngineNewWindowRequest.InNewWindow`, and the legacy `WebEngineView.NewViewInWindow`. They do **not** export Nicol's `WebEngineView.NewViewInNewWindow`.

These are source-level selections. The donor applications were not built or run in this comparison. Donor tests were read, not executed. No RAM/CPU benchmark, cross-platform result, live-provider login result, or codec/DRM readiness claim follows from this report. An unrelated project's feature name or tests do not establish that the port works in Colosseum.

Colosseum reference checkout: `b53a22d3e47bb286219973104905333e9ff6f544`. Its live Feria sources were inspected alongside the donors. Existing account/activity changes in the working tree were left untouched.

### Immutable donor inputs

Commit values come from each archive's `SOURCE-RECEIPT.txt`; archive hashes were calculated locally.

| Donor | Supplied source commit | ZIP SHA-256 |
|---|---|---|
| Qt Quick Nano Browser | `qt/qtwebengine` `a7a7106bc839974d9172c19707da15c21a8a7126`, `examples/webenginequick/quicknanobrowser` | `287caaa400da248ef67663a88ec4d7b561f381949ab28f4aeb24718c98d21399` |
| Qt WebEngine Lifecycle Browser | Same Qt commit, `examples/webenginequick/lifecycle` | `82fa1b2393330b28e7baa5ed5087872bae6d0bc793c1cb4a31531de8169b23a1` |
| KDE Angelfish | `KDE/angelfish` `40c6dc69b93f93197cc7b1216224e7e52c491635` | `ab8fdd7b7c702cb00e6b96441d2a83807ccd37c4794fd1d9c8da5ad40d5c542c` |
| Nicol | `arnav-kr/nicol` `abdc8db83989376a06642dd15ec647ba9ce45408` | `6adcd7e71a901a64b7fe0874d4907b9fb6c361f8801e4f9ac953df72f60cc772` |
| Morph | `ubports/development/core/morph-browser` `39175d1346e6da48ac331b6a8f293bbe66a722eb` | `00dd53cb74cba42b76b095d3e12a071e7dee5a4d1431556b09bfc5c670de279c` |

Source locations below are relative to the named ZIP's extracted root. The two Qt archives place the example under `example/`.

Observed notices: the selected Qt example files offer BSD-3-Clause; most inspected Angelfish browser/UI files identify GPL-2.0-or-later, while its native `tabsmodel.cpp` identifies LGPL-2.0-only. Nicol's archive includes AGPL-3.0. Morph application files identify GPL-3.0; its separately bundled Readability assets identify Apache-2.0. These are file/source observations, not a blanket license clearance for copying a whole donor. Preserve the exact source notice and distinguish copying code from using behavior as a reference.

## Selection rules

1. Correctness first: target the requesting session, preserve the original request when required, handle failures, and obey the current Qt API.
2. Ownership and cleanup: stable session identity, explicit profile ownership, window/view lifetime, pending callbacks, and account-switch fences.
3. Privacy and interaction: origin-bound consent, private data exclusion, correct cancellation, and no unsolicited identity/device selection.
4. Resource behavior: avoid duplicate views, unbounded recovery loops, unnecessary loading, or GUI-thread work without a reason.
5. Maintainability: explicit dependencies, separable machinery, useful tests, and comprehensible invariants. Fewer lines are not automatically better.
6. Portability and provenance: identify Qt/KDE/Lomiri/platform dependencies and file-specific license notices. Selection as a behavioral reference is distinct from approval for copying that file.

A selection can be an adaptation, a behavior/lifetime reference, the existing Colosseum implementation, or Qt's existing engine/default UI. **No suitable donor** is an acceptable result. Neither a popular browser nor an official example gets a blanket quality pass.

## Common feature matrix

`Default` means the donor leaves the relevant UI to Qt rather than supplying competing application code. `None` means no explicit implementation was found for this capability. `Partial` identifies important missing behavior, legacy coupling, or correctness concerns, not a runtime failure demonstrated in this review. The final column chooses code/patterns; it does not add unapproved features to V1.

### Navigation, sessions, and windows

| # | Capability | Nano | Lifecycle | Angelfish | Nicol | Morph | Selected implementation or reference |
|---|---|---|---|---|---|---|---|
| 1 | Back/forward/reload/stop | Direct Qt actions | Direct Qt actions | Direct Qt actions | Direct Qt actions | Direct Qt actions | Nano's `WebToolBar.qml` action wiring; preserve Colosseum's toolbar and active-page fences. No reason to replace Qt navigation with scripts. |
| 2 | Address parsing and search | `QUrl::fromUserInput`; accepts local files | Same utility | `QUrl::fromUserInput` wrapper | Handwritten prefix/dot heuristic | Native URL classification and search engines | Qt URL parsing plus Colosseum's existing `FeriaBrowserPolicy`; keep explicit search encoding. Reject Nicol's prefix heuristic and importing Nano's unrestricted local-file acceptance into Feria. |
| 3 | Per-page navigation history chooser | Qt history model and offset | None | Separate desktop history chooser | Qt history model and offset | Navigation-history popup | Nano's direct history-model/offset pattern. Nicol adds presentation, not a stronger ownership model. |
| 4 | Independent live pages/session identity | Separate views, tab indexes | Separate views/tab stack | Native index-based tab model | ListModel/Repeater indexes | Native tab model and unique tab identity | Preserve `SessionStore` IDs and `FeriaSessionDeck`. Donor views/actions can attach to them; do not import a second tab registry. |
| 5 | Foreground/background new-page requests | Distinguishes destinations; adopts original request | None | Reopens requested URL | Collapses most destinations into foreground tabs | Distinguishes foreground/background; adopts original request | Nano destination routing plus synchronous original-request adoption already present in Feria. Morph independently supports the request-lifetime rationale. |
| 6 | Real desktop browser windows | Window factory with shared profile | Example is one window | Supplied tab surface; no matching lightweight window factory selected | Factory exists but request enum is wrong | Multiwindow factory, activation and ownership | Nano's Qt-compatible window factory/routing base; Morph for ownership/transfer behavior. Neither donor's complete manager is a drop-in replacement for Colosseum. |
| 7 | Site-requested dialogs/geometry | Dedicated `BrowserDialog`; geometry requests | None | New-tab conversion | New-tab conversion for non-window requests | Dialog destination has a FIXME and becomes a tab | Nano dedicated-dialog pattern, with geometry bounds and explicit close/profile ownership added by the existing Feria host. |
| 8 | Move an existing live page between windows | Creates new views, no equivalent transfer found | None | No equivalent found | No equivalent found | `Reparenter`, `bindExistingTab`, `loadExisting` | **Morph's context/lifetime principle** is the strongest supplied reference. Adapt to Feria's shell-owned context; verify the real WebEngine view survives Pop out/Return. Do not blindly copy the helper. |
| 9 | Close/before-unload and correct requesting page | `RequestClose`; originating view index | Close-action example | Native close model; custom before-unload UI | Keyboard uses `RequestClose`, but page-requested close targets selected index | Custom before-unload; moving avoids destruction | Nano `RequestClose` and originating-page targeting; retain Colosseum's close-to-world rule. Desktop-window and taskbar close routes must respect the same browser handshake. |
| 10 | Reopen a closed page | None | None | No equivalent selected | None | Serialized closed-page stack, excludes private mode | Morph's behavior as reference; retain Feria's existing bounded, private-excluding closed-session stack. Do not import Morph's unlimited stack or neighbor activation. |
| 11 | Session persistence/manual restore | No durable session store | None | Native JSON tabs persistence | None | Multiwindow serialization/restore | Morph's descriptor/restore separation is useful; Colosseum's existing atomic account store remains the implementation owner. Normal startup stays fresh; manual restore remains available. |
| 12 | Reorder/pin/multi-select tabs | TabBar, no full suite | TabBar | Tab model/UI | Custom tab row | Bounds-checked reorder and tab movement | Morph is the strongest supplied reorder reference. A complete pin/multi-select suite was not established. These extras are outside the current approved minimum; no parallel tab strip. |
| 13 | Private profiles and cookie sessions | Persistent/OTR prototypes; toolbar can swap a live view's profile | No separate private workflow | Shared profile bound to private-tabs mode | Profile supplied at window creation; separate OTR prototype | Separate normal/incognito contexts | Keep Feria's account-owned normal profile and separate OTR profile. Borrow Nano/Nicol's profile-at-creation principle, not Nano's live profile toggle or donor-global storage identity. |
| 14 | Freeze/wake/resource lifecycle | No policy | Engine recommended-state timer, freeze/discard controls | Asynchronous view construction, no freeze policy found | Immediately freezes hidden views | Incubation/unload and lazy restore; not equivalent safe freeze | **Qt Lifecycle** for recommended-state-based freeze/wake. Feria's pending-callback and audible-page guards stay. Discarding a page is a different behavior from pausing/freeze and is not silently introduced. |
| 15 | Renderer termination/recovery | Reloads selected view after any termination | None | Load-error UI, no renderer-retry policy found | None found | Per-view monitor, delayed bounded killed-process retry, crash state | **Morph `WebProcessMonitor.qml`** for bounded, per-view recovery principles. Adapt cancellation/generation handling; do not copy Nano's selected-tab reload. |
| 16 | Pause on leaving / resume only auto-paused media | No matching policy | Freeze is not a playback policy | Media context actions, no matching session policy | Freeze is not a playback policy | No matching cross-world policy | No complete donor. Colosseum-specific policy using the existing media observer/owner; do not pretend lifecycle freezing satisfies the locked pause/resume contract. |

### Downloads, page tools, and ordinary browser interaction

| # | Capability | Nano | Lifecycle | Angelfish | Nicol | Morph | Selected implementation or reference |
|---|---|---|---|---|---|---|---|
| 17 | Download request ownership | Profile signal attached per window; one pending request slot | None | Profile-level signal, native download manager/model | None found | Native persistent download model and browser integration | Angelfish's **profile-level ownership** is a useful reference; Nano for Qt request APIs. Keep Feria's single handler per profile and native download authorization. Import no donor global download registry. |
| 18 | Download progress/pause/resume/cancel/open | Simple progress/cancel | None | Progress, pause/resume and open actions | None found | Rich download metadata, pause state, cleanup | Angelfish's direct request controls; Morph's private-exclusion/completion behavior. Existing Feria wrapper already combines these with account fences. Guard unknown totals; no raw Nano delegate copy. |
| 19 | Download persistence/private cleanup | None | None | Session manager/model | None | Persistent normal records, private records excluded/pruned | Morph has the strongest explicit supplied private-history reference. Retain `FeriaAccountStore`, whose authorization can survive the originating tab closing while preventing cross-account writes. |
| 20 | Save page/link/image/media | Engine context defaults | Default | Direct `WebAction` menu items | Default | `SavePage` plus scripted PDF/download paths | Use direct Qt `WebAction` wiring, with Angelfish's capability-driven menu structure as reference. Do not take Morph's interpolated download scripts or duplicate a transfer engine. |
| 21 | Print/save PDF | No custom print workflow found | None | Native `printToPdf` and preview | None found | PDF flow plus generated download link | Keep Feria's existing request-bound save-file/`printToPdf` path. Angelfish demonstrates page-size/orientation options, but its fixed preview filename and unchecked success are unsuitable unchanged. |
| 22 | Rich print preview / system printer UI | None | None | Rich PDF preview/settings | None | Save/PDF dialog, not a complete printer stack | Angelfish is the strongest **feature reference**, not a safe drop-in. No complete portable system-print implementation selected from these donors. Rich preview is an additional scope item, not a dependency of current save-PDF support. |
| 23 | Find text, previous/next, match counts | Native `findText`, result counts and find bar | None found | Native find/result counts, Kirigami UI | None found | Native find with boolean result, Lomiri UI | **Nano** native find/results pattern, already adapted in Feria. Keep current page/navigation fences and reset/highlight/focus handling; do not import less informative Morph boolean-only UI. |
| 24 | Zoom in/out/reset | Direct `zoomFactor`, unbounded increments | None found | Qt presentation controls | Same unbounded increments | Bounded zoom, domain/default/fit-to-width machinery | Nano's native engine operation for basic zoom, with Feria's existing bounded steps. Morph has the strongest domain-persistence reference, but its large fit-to-width controller is not necessary for basic zoom. |
| 25 | Page/site fullscreen and Escape | Saves/restores window visibility, accepts page request | None found | Request acceptance and fullscreen signaling | Saves/restores visibility; application-wide Escape | Window/fullscreen controller and test sources | Nano's page-request/previous-window-state contract. Feria's embedding/pop-out owner must supply the actual window behavior and window-scoped Escape. |
| 26 | Context menus/editing/clipboard | Default Qt menu and native edit actions | Default | Rich capability-driven native actions | Default and native edit shortcuts | Rich touch/context override plus DOM scripts | Default Qt interaction for V1 where adequate; Angelfish's direct actions if a missing capability needs presentation. Avoid taking Morph's whole touch/DOM override stack. |
| 27 | HTTP/proxy authentication dialogs | Default | Default | Request-bound custom auth sheet | Default | HTTP dialog; proxy left to default | **Qt defaults** for the current desktop baseline, using the request's originating view. Angelfish is a custom-presentation reference; no reason to replace proven defaults solely to increase copied code. |
| 28 | JavaScript alert/confirm/prompt/before-unload | Default | Default | Custom request-bound sheet | Default | Custom dialog collection; numeric before-unload fallback | **Qt defaults** for V1. Angelfish is the cleaner supplied custom-dialog reference, but prompt defaults, competing requests and navigation/close cancellation require review before porting. |
| 29 | File upload/folder/save and color dialogs | Default | Default | Default for these handlers | Default | Custom mobile pickers; misspelled folder enum | **Qt defaults**. Morph's mobile picker stack is a weaker desktop baseline and contains a source-level folder-mode defect. Validate upload, multi-file, folder and color behavior in Feria. |
| 30 | Built-in PDF viewing | Engine setting exposed | Default | Engine/PDF preview usage | Explicitly enabled | Explicitly disabled in browser wrapper | Qt built-in viewer/default capabilities, preserving Feria policy. No custom PDF engine from a donor; local Biblio ownership remains separate. |
| 31 | Load errors / retry / status / favicon | Engine errors, hovered link/status/icon | Engine defaults | Explicit load/error/retry UI | Engine error pages and favicon | Error/certificate/sad-tab surfaces | Nano engine defaults for ordinary failures; Angelfish's explicit retry and Morph's crashed-page distinction as behavioral references. Do not replace working Qt error pages with donor web UI. |
| 32 | Keyboard/focus/accessibility integration | Broad `StandardKey` actions | Per-visible-page actions | Kirigami shortcuts/focus | Keyboard coverage; application-global Escape | Extensive desktop/mobile focus stack | Nano's standard actions, Lifecycle's active-page gating, and current Colosseum keyboard/controller ownership. Native-window focus must identify the exact session. UI frameworks and accessibility correctness are not interchangeable. |

### Permissions, identity, and site capabilities

| # | Capability | Nano | Lifecycle | Angelfish | Nicol | Morph | Selected implementation or reference |
|---|---|---|---|---|---|---|---|
| 33 | Origin-bound site permissions | Modern permission object, origin and grant/deny | Default | Legacy feature/origin permission UI | Default | Legacy feature prompts/domain settings | **Nano's modern permission API**; retain Feria's queued, generation-bound, cancellable presentation and permission reset. Raw Nano's one-property dialog is not enough for concurrent requests. |
| 34 | Screen/window sharing selection | Automatically chooses screen zero | Default | Feature consent, no equivalent modern picker selected | Automatically chooses screen zero | Legacy capture prompts | **No suitable complete donor chooser.** Keep Feria's explicit screen/window selector around Qt's request models. Never copy automatic primary-screen selection merely because it is short. |
| 35 | Client certificate selection | Automatically chooses certificate zero | Default | No explicit selector found | Same automatic selection | No equivalent explicit selector found | **No suitable complete donor chooser.** Keep Feria's explicit choice/select-none/cancel behavior. Handle zero certificates and stale requests. |
| 36 | Passkeys/security-key WebAuthn UX | Complete Qt request-state example | Default | No equivalent modern WebAuth UX found | Default | No equivalent modern WebAuth UX found | **Nano's state-machine/API reference**, not its widget code unchanged. Feria already masks PIN fields and fences/cancels requests. Hardware/provider verification remains outstanding. |
| 37 | TLS certificate errors | Defers and offers override | Default | Defers and offers override/error UI | Rejects errors | Stores error and legacy override logic | Retain Feria's existing rejection/error feedback; Nicol's explicit reject is a simple reference. No donor override UI is automatically imported. TLS certificate trust and client identity certificates are different features. |
| 38 | External protocols / handler registration | Automatically accepts registration request | Default | External URL/context actions | Default | Per-domain custom-scheme consent | Morph has the strongest consent/allowlist **behavior reference**; keep native URL policy and validate the modern Qt handler API. Nano's auto-accept handler is not selected. |
| 39 | Web notification delivery | Permission prompt; no custom native presenter found | Default | Profile notification presenter with `KNotification` | Default | No matching portable presenter selected | Angelfish has the most complete supplied presenter, but KDE delivery is a platform dependency. Adapt through an existing Colosseum/native notification owner or Qt platform behavior; no universal Windows delivery claim. |
| 40 | Cookie/cache clearing and private storage | Cache mode/profile controls | None | Profile/settings-based data | Persistent/OTR setup | Cookie deletion utility, normal/OTR cache clear | Retain Feria's account/profile cleanup and runtime-view/profile drains. Morph is a useful API reference, but its unchecked profile cast is not selected as-is. Clearing both global donor profiles is not account-scoped cleanup. |
| 41 | User agent / desktop-site mode | Engine profile defaults | Defaults | Native UA builder and per-tab desktop/mobile switch | Strips Qt token and appends Nicol token | Domain/default/custom UA machinery | Preserve Qt's platform-correct UA; Morph's per-domain preference behavior is useful if needed. Angelfish hardcodes Linux identifiers, and Nicol's cosmetic UA rewrite has no demonstrated Feria benefit. |
| 42 | Per-site settings/scripts/control exposure | Engine settings menu | Lifecycle settings | Browser settings and app-view context restrictions | Fixed settings | Domain UA/zoom/location/custom-scheme settings | Morph is the strongest per-domain **settings-shape reference**; Colosseum's existing policy/account/preferences and provider AppMode remain owners. Nano supplies individual Qt setting APIs, not a second configuration store. |

### Persistence and additional donor capabilities

| # | Capability | Nano | Lifecycle | Angelfish | Nicol | Morph | Selected implementation or reference |
|---|---|---|---|---|---|---|---|
| 43 | Global browser history/bookmarks/search | No durable owner | None | Native asynchronous DB, bounded queries/filter model | No durable owner found | Native history/bookmarks/folders and worker/model layers | Angelfish's bounded/query/model separation is the leaner reference; Morph contributes richer folder/history behavior. Keep `FeriaAccountStore` and credential/private exclusions. Neither donor database replaces canonical Colosseum state. |
| 44 | Configurable search engines / OpenSearch / suggestions | Basic URL parsing | Basic URL parsing | Search-engine configuration | Hardcoded DuckDuckGo | Native OpenSearch parser, suggestions URL/templates and debounce | Existing Feria search mapping for V1. **Morph** is the strongest extensible-engine reference; its suggestion request controller needs status/schema/generation guards. General-browser entry and unsolicited remote suggestions are not added by this review. |
| 45 | Thumbnails / cached previews | No equivalent found | No equivalent found | Snapshot/view readiness hints | No equivalent found | Reduced capture size, single capture reused, async PNG save, cleanup | **Morph** is the strongest V2 browser-preview candidate. Replace URL/global-disk identity with session/account identity and a bounded cache if used. Theatre/native-video capture is a separate unverified adapter. |
| 46 | Full-size hover peek / live preview taskbar | None | None | No equivalent Colosseum behavior found | None | Tab preview/capture, not this full taskbar contract | No complete donor. Deferred to V2; do not instantiate pages/players on hover or confuse cached images with live sessions. |
| 47 | Reader mode / article extraction | None | None | DomDistiller extraction and themed output | None | Readability assets and readerability detection/injection | Angelfish and Morph are competing future references. **No final extraction winner** from source shape alone: article fixtures and DOM/state preservation must decide. This is an additional feature, not an unapproved V1 addition. |
| 48 | Content/ad blocking | None | None | Native Qt interceptor, Rust engine/filter-list machinery | None | Domain navigation permissions, not equivalent adblock engine | **Angelfish** is the strongest full filtering candidate in the supplied set. Threading/shutdown, rule quality, Rust dependency and platform integration need validation. Extra filtering scope is not silently added to V1. |
| 49 | Developer tools / view source | Qt inspected view and setting | None | Per-tab developer view and page-source action | No explicit tools found | View-source and platform debug configuration | Nano inspected-view wiring is the simplest tools reference; Angelfish direct view-source action is useful. Keep diagnostics user-triggered and ensure close ACK/view lifetime. Product controls remain site-specific. |
| 50 | Web-app installation, sharing, mobile gestures | None beyond ordinary browser | None | Web-app creator/manager, sharing, Kirigami/mobile UX | None beyond browser chrome | Lomiri web-app container/content/share/mobile UX | These are additional platform-specific capabilities. Angelfish is the closest site-as-app behavioral reference; neither platform launcher/container is selected for import into Feria's existing site catalog. |
| 51 | Password vault/autofill, translation, spellcheck UI, extension system | No complete independent implementation found | None | No complete matching system selected | None found | No complete portable matching system selected; plugin helper is separate | No new systems are authorized by this comparison. Qt engine/default facilities are not a donor password vault or extension implementation. Extensions and general-browser entry remain deferred. |
| 52 | Proprietary codecs / DRM / Widevine | Depends on Qt build | Depends on Qt build | Depends on Qt build | Depends on Qt build | Platform Widevine helper/installer exists | No donor app layer proves codec/DRM compatibility. Codec-enabled Qt integration is a separate build/runtime prerequisite; Morph's installer is not a portable drop-in or proof of provider DRM support. |

## Evidence for the decisions and rejected code

### E1. Navigation, find, zoom, and shortcut operations

- Nano `example/WebToolBar.qml:43-89`: direct Qt history/back/forward/reload/stop operations. `example/utils.h:21-26` additionally permits existing local files; Feria's narrower native policy must remain in front of this.
- Nano `example/BrowserWindow.qml:111-171,399-414,495-521` and `example/FindBar.qml:11-22,59-67,78-103`: native zoom/find, completion counts, previous/next and keyboard operations. The raw example increments zoom without bounds. Feria already has bounded steps and navigation/session fences in `FeriaPageTools.qml`.
- Nicol `ui/BrowserWindow.qml:772-782`: scheme detection is prefix-based, rather than checking a parsed scheme. `ui/BrowserWindow.qml:176-189` makes Escape an application shortcut, which needs revisiting for multiple windows.
- Morph `src/app/url-utils.cpp:69-113`, `src/app/qml-qt6/ZoomControls.qml:105-115` and its controller: broader URL classification and domain/fit zoom, with considerably more application-specific machinery. Basic zoom should not acquire that dependency tree.

### E2. New-window correctness and lifetime

- Nano `example/BrowserWindow.qml:335-350` distinguishes foreground/background tabs/dialogs/windows and passes the original request to the destination view. `example/BrowserManager.qml:19-35` supplies the profile-aware factories.
- Morph `src/app/webbrowser/qml-qt6/BrowserTab.qml:219-225` explicitly creates/adopts the target synchronously because the request cannot simply be retained after its handler. `TabComponent.qml:94-115` differentiates destinations, but has an explicit dialog FIXME.
- Angelfish `lib/contents/ui/WebView.qml:349-357` forwards just `requestedUrl` to `newTab` or a question. That is not an equivalent source pattern for preserving the original opener/form request.
- Nicol `ui/BrowserWindow.qml:326-335` checks `WebEngineView.NewViewInNewWindow`. This constant is absent from the installed Qt 6.11.1 API; the supplied branch is not a reliable new-window router. `openTabWithRequest` at lines 55-68 also falls back to opening the request in the current view when the intended new view is unavailable.
- Nano's window factory and Morph's behavior are candidates; actual Colosseum live-page relocation across `QQuickWindow`s remains unverified.

### E3. Pop out/Return requires more than changing the visual parent

- Morph `src/app/webbrowser/reparenter.cpp:57-92,97-122` makes QML context ownership explicit, alongside QObject and visual-parent ownership.
- Morph `src/app/webbrowser/qml-qt6/Browser.qml:118-129,1445-1466` rebinds window dependencies and treats movement differently from closing. `BrowserTab.qml:102-109` can reuse an existing live view.
- `tests/unittests/qml/tst_Reparenter.qml:65-111` tests surviving destruction of the former container and demonstrates the failure mode of ordinary QML-parent reassignment. It uses fake items; it does **not** prove modern WebEngine cross-window rendering, focus, accessibility, dialogs, or GPU lifetime.
- The helper is not selected verbatim: creation/component failures are not comprehensively guarded, and its context approach must fit Feria's existing shell-owned retained views. Preserve the stable SessionStore ID and shared profile instead of creating a replacement page or a second registry.

### E4. Background lifecycle

- Lifecycle `example/WebTab.qml:152-164` derives transitions/delays from `recommendedState`; explicit actions distinguish Active/Frozen/Discarded at lines 73-94.
- Nicol `ui/BrowserWindow.qml:325` binds every hidden page directly to Frozen. There is no comparable recommended-state or pending-work policy in that path.
- Angelfish `lib/contents/ui/ListWebView.qml:98-113` incubates views asynchronously but keeps its Loader active. Asynchronous construction alone is not a background RAM/CPU policy.
- Morph `BrowserTab.qml:78-99,113-128` supports lazy creation/unload. Its error handling, pending incubator cancellation, destruction/callback behavior and explicit unload consequences require adaptation; it is not interchangeable with freezing an existing page.
- Feria's current safe-freeze timer and callback drain in `FeriaQtWebEngine.qml:26-88` should be extended, not replaced by less complete donor code.

### E5. Recovery belongs to the originating page and must be bounded

- Nano `example/BrowserWindow.qml:375-397` responds to a view's termination by reloading `win.currentWebView`, which can be a different view. It also has no comparable retry bound.
- Morph `src/app/qml-qt6/WebProcessMonitor.qml:27-92` targets one view, delays recovery, allows one killed-process retry within its interval, and records crashed/killed states. `tests/unittests/qml/tst_WebProcessMonitor.qml:45-105` supplies relevant mock scenarios.
- This makes Morph the stronger behavior reference. The adapter must still cancel delayed work when its session closes/navigates or account changes, guard repeated signals, and use the installed Qt status values. A crash and a system-killed renderer are not automatically the same event.

### E6. Closing must not target whichever tab happens to be selected

- Nano `example/BrowserWindow.qml:264-274,321-323` uses `RequestClose` and the originating WebEngine view's index.
- Nicol `ui/BrowserWindow.qml:348` routes every view's `windowCloseRequested` to `closeTab(bar.currentIndex)`. A background page requesting closure can therefore target the selected page. Its compactness does not justify porting this handler.
- Morph avoids destroying a tab while moving it, but its ordinary close logic loads/promotes the remaining current tab and creates an empty tab when needed. Those policies conflict with Colosseum's locked close-to-world/no-neighbor-promotion rule.
- Current Feria `FeriaSessionDeck.closeTab` uses `RequestClose`, but the shell/taskbar `Main.qml.closeSession` removes a session directly. This discrepancy is a current integration gap to resolve in the approved design; donor UI close behavior is not yet consistently applied everywhere.

### E7. Persistence: useful behavior does not justify a second store

- Angelfish `lib/tabsmodel.cpp:161-194` calls `beginResetModel()` and returns early on a missing input file before `endResetModel()`. The write path at lines 206-235 uses direct `QFile`, continues after an open failure, does not verify the write result, and returns true. This is not the strongest persistence implementation.
- Morph `src/app/webbrowser/qml-qt6/morph-browser.qml:371-447` separates serialization, restore and private-window exclusion. `src/app/session-storage.cpp:78-90` writes a temporary file then renames it, but does not check write/rename success. The Windows replacement behavior must not be assumed equivalent to atomic `QSaveFile`.
- Colosseum's `FeriaAccountStore.cpp` already owns atomic writes, credential/private filtering, caps, bookmarks/history/preferences and manual-restore descriptors. Keep that machinery; adapt missing descriptor behavior through it.
- Fresh normal startup is locked. Neither Angelfish's automatic tab load nor Morph's default `restoreSession: true` overrides that decision.

### E8. Profile and download ownership

- Angelfish `lib/angelfishwebprofile.cpp:23-34` hooks downloads at the profile owner, which is a better ownership boundary than per-window handlers. Its global manager is not account-scoped and is not selected wholesale.
- Nano `example/BrowserManager.qml:19-25` connects `profile.downloadRequested` to each new window. A shared profile can consequently have multiple window handlers. `BrowserWindow.qml:490-492` retains just one pending dialog request.
- Nano `DownloadView.qml:41-44` divides received bytes by total bytes without handling zero/unknown totals; parallel requests and lifetime are not robustly modeled by its two parallel arrays.
- Morph `src/app/downloads-model.cpp:225-243,246-279,369-410` explicitly separates private records from database writes and supplies completion/pause/prune behavior. Its own synchronous database/model layers remain competing owners, not replacements for FeriaAccountStore.
- Feria already has one download adapter per profile, request-state controls, unknown-total handling, cleanup and native transient authorization that fences the destination account. Do not regress those protections while porting controls.

### E9. Permissions and identity selection

- Nano `example/PermissionDialog.qml:15-24,32-69` uses the modern origin/permission object, but it has a single mutable request property. Queuing, validity, cancellation and page-generation binding must remain in Feria's wrapper.
- Both Nano `BrowserWindow.qml:370-373,399-400` and Nicol `BrowserWindow.qml:338-346` automatically choose the first screen/certificate. Neither is a suitable implementation of an explicit user selection flow.
- Nano `example/WebAuthDialog.qml:30-77,156-201` supplies the richest modern WebAuth state mapping. Its PIN widgets at lines 110-129 do not mask input; its account-model population at lines 161-165 needs checking against the delegate's role usage at lines 137-148. The API/state mapping wins, not the raw dialog unchanged.
- Feria's `FeriaBrowserRequests.qml` already provides masked PINs, explicit screen/window and certificate choices, plain origin labels, a queue, generation checks and cancellation. Preserve these; hardware-backed auth remains runtime-unverified in this review.
- Legacy `grantFeaturePermission` UI in Angelfish/Morph is a weaker match than Nano's current API for this build. It can inform presentation but should not replace the current engine permission owner.

### E10. Dialogs, save and print

- Qt defaults avoid importing custom mobile dialogs solely to replicate existing desktop behavior. They still need real Feria acceptance checks for uploads, folder selection, auth cancellation and before-unload.
- Morph `src/app/qml-qt6/WebViewImpl.qml:131-154` contains `FilealogRequest.FileModeUploadFolder`, rather than the declared request type. Its custom folder branch is not a quality winner.
- Angelfish `lib/contents/ui/JavaScriptDialogSheet.qml:19-45,78-92` has clear type-based UI but resets prompt input instead of using the supplied default and can reject through more than one close path. Treat it as a presentation reference, not an automatic replacement.
- Angelfish `WebView.qml:444-463` uses a shared `print-preview.pdf` and ignores the completion success when assigning the preview URL. `PrintPreview.qml:190-192` derives the save path directly from page title. Concurrent windows, failed writes and title/path safety require correction.
- Morph `Browser.qml:899-917` interpolates URL/filename values into JavaScript for downloads/PDF. The filename sanitization omits single quotes despite inserting the filename inside a single-quoted JavaScript string. Prefer native save/print/download actions instead of importing this script construction.
- Retain Feria's request-bound native PDF output/chooser and completion reporting. A richer print preview or system-printer stack is not supplied by importing a file unchanged.

### E11. History/bookmarks and donor tests

- Angelfish `lib/bookmarkshistorymodel.cpp:16,96-155` provides bounded parameterized queries, filtering and asynchronous model update. It is a useful model/query reference, although stale asynchronous results need fencing if the filter/account changes while a query runs.
- Morph provides more elaborate folders, history filtering and worker layers. That breadth may be useful later; importing another SQLite owner now would violate canonical Colosseum ownership.
- Angelfish `autotests/dbmanagertest.cpp:73-79` labels a test `testRemoveFromHistory` but calls `removeBookmark`. Test-file presence alone is therefore not adequate proof of the intended behavior.
- Angelfish's tab-model tests exercise row/index/private behavior. Morph's request, restore, preview/privacy and reparent tests supply useful scenarios, but some are mocks/legacy module imports and must be adapted and rerun rather than counted as Colosseum verification.

### E12. Advanced features and platform coupling

- Morph `src/app/webbrowser/searchengine.cpp:86-151` is the strongest supplied configurable OpenSearch implementation reference. `SearchSuggestions.qml:35-87` has abort/debounce and parse failure handling, but no explicit HTTP-status/schema/generation safeguards. Do not enable remote keystroke suggestions merely because the component exists.
- Morph `src/app/qml-qt6/ZoomControls.qml` and `src/app/domain-settings-model.cpp` offer the richest domain preference shape. Its fit-to-width controller is much larger than basic engine zoom; adapt only behavior needed by the approved site controls.
- Angelfish `lib/useragent.cpp:24-34` hardcodes Linux/Plasma platform strings. Nicol `main.cpp:80-84` removes the Qt token and appends its own branding. Neither is a platform-correct improvement demonstrated for Feria.
- Morph `BrowserTab.qml:45-51,159-205` scales captures and reuses one capture for preview/disk output. `PreviewManager.qml:74-105` moves image save work out of the UI path and prunes files. These are strong V2 references, but its size is relative to the window and its disk identity is URL/global-cache based; Colosseum needs explicit budgets and account/session/privacy fences.
- Angelfish's adblock interceptor uses a native Qt request boundary and a Rust filtering engine; initialization/reset/shutdown threading must be inspected before reuse. Domain blocking in Morph is not an equivalent adblock engine.
- Angelfish DomDistiller and Morph Readability are alternative article-extraction implementations. Their source shapes do not establish extraction quality on Colosseum's sites. An article fixture comparison is the evidence needed before declaring a winner; no extraction port is authorized by the current V1 scope.
- Qt examples have the lightest UI dependency set. Angelfish brings KDE/Kirigami/KF/QCoro and optional Rust machinery. Morph has Lomiri, platform/account/content-hub pieces and Qt 5 migration residue even in its Qt 6 path. These are porting costs, not grounds to dismiss a stronger algorithm or lifetime invariant.

## Selected reusable code and genuine gaps

The selection is deliberately mixed:

- **Qt Nano:** direct navigation/history/edit actions; modern new-page/new-window routing; popup geometry; native find/result wiring; fullscreen request/window-state contract; modern permission and WebAuth request APIs; inspected-view diagnostics. Adapt ownership and consent rather than copying the whole application/window manager.
- **Qt Lifecycle:** recommended-state transition/timer logic for safe background-page freeze/wake. Preserve Colosseum's additional callback/account/session guards.
- **Morph:** context survival and move-without-close invariants; per-view bounded killed-renderer recovery; serialized normal/private state separation and reopen behavior. Its preview, OpenSearch and per-domain setting machinery provide concrete later candidates. Select these independently of the Lomiri application shell.
- **Angelfish:** profile-level download ownership, direct download/media/context actions, bounded history/bookmark query/model structure, and the richest supplied printing/filtering/notification feature references. Do not import its flawed tab persistence or hardcoded platform UA.
- **Nicol:** useful comparison for compact normal/private window setup, but no unique V1 implementation wins over the inspected alternatives. Wrong destination enum, selected-tab close targeting, unconditional freezing and automatic identity/device selection rule out a wholesale port.

Necessary Colosseum-specific work remains: shared taskbar/session identity and exact-window focus; provider start-page selection; native Pop out/Return ownership; centralized close handshake; pause/resume policy and cross-frame media limitations; account/private cleanup; explicit device/certificate choices; and site-specific control exposure. First extend the current owners. Any new mechanism needs a named gap rather than the assumption that all functionality must be written again.

The browser engine remains QtWebEngine. These donor application layers do not replace codec build/integration, prove arbitrary DRM support, or establish WebView2 removal readiness.

## Scope preserved

V1 prioritizes functional Feria browser sessions and desktop windows within the shared Colosseum taskbar. Each site remains its own app. New explicit sessions start at the site's start page; linked destinations preserve their requested URL. Closing affects only the selected session and reveals the world underneath when appropriate. Leaving pauses media; restoration resumes only media Colosseum automatically paused. Normal startup is fresh; manual restore remains available.

V2 carries cached thumbnails/hover peek/live previews/preview playback controls and generic reader/player desktop pop-outs. Extensions and a general-browser entry remain deferred. Additional donor features such as reader mode, richer printing, adblock, search suggestions, mobile sharing or web-app installation are compared here without silently adding them to the approved V1 scope.

## Verification state

- Archive identity/hashes and source receipts: checked.
- Feature inventory, selected handlers/supporting models and relevant donor test sources: inspected.
- Key Qt 6.11.1 window-request/profile APIs and Nicol's missing enum: checked against the installed kit.
- Existing Colosseum owners and material differences from the locked design: inspected.
- Donor standalone builds/tests, actual moved WebEngine view, multiwindow login/popups, native dialog/permission flows, hardware auth, Windows/Linux performance, codec/DRM behavior: **not verified by this comparison**.
- No product code was changed by this research. Final port selections remain subject to the approved design and meaningful runtime checks of the adapted implementation.
