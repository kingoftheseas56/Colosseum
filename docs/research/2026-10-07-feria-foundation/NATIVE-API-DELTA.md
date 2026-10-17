# Feria categories 1–5: incremental native API findings

Read-only source inspection, 2026-10-07. No implementation, build, browser execution, or target-kit test was performed. This supplements the existing inspection rather than re-auditing it.

## Pinned sources

- Qt WebEngine 6.11.1: `eb0793cc4b76e93cf669f586fd68c76019f40ec9`
- Its Qt Chromium fork: `58c11ad487f8a237cf0ac71cc3e818b52db150df`
- Falkon: existing audit pin `215414413f6338876a5e458927497f05872372f6`
- CEF: `ff57d4eae16d36457895f2de115a71d502e85a08` (retrieved head, commit date 2026-09-29)
- Brave Core: `6b0b85c1ab38567689ff22fc5cdd9ea7b1eb076d` (retrieved head, commit date 2026-10-07)

## Decisive corrections

1. Public QML property is `view.history`, not `view.navigationHistory`. The pinned view header has only `history`; QML documentation and upstream tests agree. `navigationHistory` is not a supported alias discovered on this target.
2. `history.items`, `.backItems`, and `.forwardItems` are QAbstractListModel-derived objects. They are not JavaScript arrays or ListModel objects: do not use `.get(i)` or assume a `.count` property. Bind a ListView/Instantiator to them, consume their count, and use model roles in delegates.
3. Modern destinations are `WebEngineNewWindowRequest.InNewWindow`, `.InNewTab`, `.InNewDialog`, `.InNewBackgroundTab`. The deprecated view enum uses `WebEngineView.NewViewInWindow`, not `NewViewInNewWindow`.
4. `request.openIn(view)` calls `view.acceptAsNewWindow(request)` directly. Both have void results. Neither supplies a success Boolean. Both must run synchronously inside the request signal handler.
5. Prior report's loadFinished-before-action-update explanation must not be presented as a verified QML cause. Quick queues public loadingChanged through QTimer::singleShot(0), while the core caller updates navigation actions synchronously before returning. The implementation does not establish that QML loadingChanged handlers observe old actions.
6. Native toolbar Back and explicit history offset -1 have intentionally different semantics: Chromium toolbar Back/Forward skips history-manipulation entries; explicit history indices/offsets can visit them. JS history.back() is not a reliable behavioral oracle for the toolbar action.

## QML-safe supported interface

Use the public QML module, without private C++ view headers:

- Read `view.canGoBack`, `view.canGoForward`, `view.loading`, `view.loadProgress`, `view.url`, `view.title`
- Invoke `view.goBack()`, `view.goForward()`, `view.reload()`, `view.reloadAndBypassCache()`, `view.stop()`
- Or use `view.action(WebEngineView.Back/Forward/Reload/ReloadAndBypassCache/Stop)` and its `enabled`, `text`, `iconName`, `trigger()` interface
- `WebEngineAction` is not a Qt Quick Controls Action object; bind the needed properties/call trigger instead of assigning it blindly to a Controls action property
- `view.triggerWebAction(enum)` invokes directly; the action object's trigger() checks enabled first
- History: `view.history.items`, `.backItems`, `.forwardItems`; delegate roles `title`, `url`, `icon`, `offset`; then `view.goBackOrForward(offset)`
- Request signal: `onNewWindowRequested: function(request) { ... }`; request fields `destination`, `requestedUrl`, `requestedGeometry`, `userInitiated`; immediately call `request.openIn(targetView)` or `targetView.acceptAsNewWindow(request)`
- There is no public request target-frame-name, HTTP method/body, opener pointer, handled/success flag, or defer/accept-later operation in this inspected QML type

The QML signatures are supported despite implementation class headers ending `_p.h`: consuming the QML API does not require importing those private C++ headers. Calling the private C++ classes directly introduces an unstable ABI dependency.

Sources:
- [QML properties](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L61-L74)
- [QML callable methods](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L451-L456), [navigation slots](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L489-L504)
- [Action object properties](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineaction_p.h#L33-L58), [enabled guard](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineaction.cpp#L129-L140)
- [Public request fields/enums](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginenewwindowrequest.h#L24-L47), [QML enum documentation](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginenewwindowrequest.cpp#L54-L67)
- [Deprecated destination spelling](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L162-L168)
- [QML request.openIn delegation](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebenginenewwindowrequest.cpp#L23-L40)

## Navigation state, offsets, notification timing

### Engine authority and skip behavior

View canGoBack/canGoForward directly read WebContentsAdapter, which reads Chromium NavigationController.CanGoBack/CanGoForward. Their QML notifications are connected to the respective action enabledChanged signals. Back/Forward action availability uses the same adapter; Stop availability is loading, Reload/ReloadAndBypassCache availability is !loading.

Chromium GetIndexForGoBack/GetIndexForGoForward scan to the next entry whose should_skip_on_back_forward_ui is false. CanGoBack/Forward and GoBack/Forward use these functions. Qt view.goBack()/goForward() call the matching native methods. In contrast, Qt view.goBackOrForward(offset) computes current+offset and calls GoToIndex after bounds validation. Qt's C++ QWebEngineHistory::canGoBack() uses CanGoToOffset(-1), which is an index-range check; do not substitute it for view.canGoBack.

This is especially relevant to the existing Feria reconstruction, which drives JS history.back()/forward(). A difference on redirect/pushState cases is not automatically a native failure: test the intended toolbar semantics and explicit history-selector semantics separately.

### Model rules

- all-items row i: absolute index i, offset i-currentIndex; includes current offset 0
- back-items row i: absolute index currentIndex-1-i, offset -i-1; nearest first
- forward-items row i: absolute index currentIndex+i+1, offset i+1; nearest first
- roles: display/title, toolTip/url, offset, icon
- goBackOrForward ignores an out-of-range resulting index; it does not promise a navigation-success result
- A still-in-range offset can point to the wrong item after intervening navigation. Fence the chooser to exact view/session and a model generation; invalidate on model reset or navigation/move/destruction rather than treating saved offsets as persistent identity
- Use ListView.count or Instantiator.count to display model size. Back-model count must not determine toolbar Back enabled state because of the skip distinction
- This model is per-live-view traversal; it is not the account browser-history database

### Timing

Quick loadStarted sets isLoading=true, resets history, and schedules loadingChanged(info) at the next event-loop turn. Quick loadFinished sets isLoading=false, resets history, and similarly schedules loadingChanged(info). Core invokes updateNavigationActions immediately after the corresponding client callback. Committed navigation resets history before updateNavigationActions. Same-document navigations are excluded from load-start handling but can pass the commit path; do not depend solely on loadingChanged for SPA history.

loadProgressChanged also queues its public notification. URL/title notifications have queued/coalesced paths. History resets can also occur for icon changes. These are separately delivered observations, not one atomic event snapshot; bind state directly and use request/loading event status for the event it represents. No target runtime root cause for the existing disabled-button comment was reproduced.

Sources:
- [View getters](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1515-L1543)
- [Notification connection](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L971-L983)
- [Action state rules](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1120-L1171)
- [Adapter reads](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L568-L583), [native traversal](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L892-L927)
- [Chromium skip vs offsets](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/renderer_host/navigation_controller_impl.cc#L1175-L1246)
- [History model ordering/offsets/roles](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginehistory.cpp#L130-L240)
- [History QML properties vs ordinary C++ methods](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginehistory.h#L84-L116)
- [Explicit offset bounds](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1872-L1882)
- [Public QML history docs](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/doc/src/navigation_history.qdoc#L12-L70)
- [Upstream ListView/count tests, inspected only](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/tests/auto/quick/qmltests/data/tst_navigationHistory.qml#L13-L130)
- [Queued Quick loading notifications](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L685-L708)
- [Core load/commit/action ordering](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_delegate_qt.cpp#L351-L445)
- [Queued URL/title changes](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_delegate_qt.cpp#L174-L195)
- [Icon model reset/progress notifications](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L620-L642)

## Original request adoption: exact guarantees and hazards

The request is a stack object in QQuickWebEngineViewPrivate::adoptNewWindow. The signal is emitted synchronously; after handlers return, Qt checks isRequestHandled and otherwise returns nullptr to the engine. Storing a JS reference does not extend its native lifetime. An asynchronous Loader, Qt.callLater, Promise, UI-confirmation callback, or queued controller call cannot safely adopt the original request later. Create a correctly profiled target synchronously, adopt once, then perform unrelated asynchronous work if needed.

The target loses any preexisting adapter/page/history on adoption. Use a fresh view, never an existing user session as scratch space. An existing initialized adapter associated with another profile is rejected by adoptWebContents. Set target.profile to the exact source/session profile during creation; changing a live profile creates another adapter and reloads the URL, so profile switching is not state-preserving migration.

Renderer-created popup WebContents carries Chromium's frame name, opener IDs/opener-suppression semantics, browsing instance, storage namespace, and initial hidden disposition. Qt's OpenURLFromTab path also propagates initiator, referrer, redirects, extra headers and POST data. Preserve the actual request rather than loading requestedUrl yourself; URL replay is not a substitute. Preserve engine noopener/COOP behavior as well: preserving an original request does not mean every popup will have an opener.

Important exception: Qt QML context-menu OpenLinkInNewWindow/OpenLinkInNewTab constructs a URL-only request with nullptr adapter. In that branch acceptAsNewWindow intentionally calls setUrl. OpenLinkInNewTab emits InNewBackgroundTab. Therefore do not claim all newWindowRequested instances carry POST/opener data. The guarantee is to retain whatever original semantics Qt supplied, not invent missing payload. The OpenLinkInNewBackgroundTab WebAction enum is marked unsupported in QML; it is not the mechanism to assume for this routing.

The foreground/background destination is a host policy instruction. Chromium initially hides a NEW_BACKGROUND_TAB; the app must keep its previous selected session and focus, even though it synchronously creates/adopts a background target. A target's URL may initially be about:blank/empty or navigate later, so URL change alone is not reliable adoption confirmation. The public void methods expose no acceptance acknowledgement: enforce preconditions, track target lifetime, and use real engine fixtures/diagnostics to verify adoption. If factory/profile/policy fails, abandon/clean up any newly created empty session and leave the request unhandled; do not replay it as a GET fallback.

Qt does not expose target frame names in this request object. Named-target reuse and opener semantics belong to the engine; do not build an app-wide URL/name registry from request.requestedUrl. Test repeated named window.open, target-form POST, noopener, opener destruction, nested popup, and background focus behavior with engine fixtures.

Sources:
- [Stack request and handled check](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L744-L757)
- [One-use/synchronous validation](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1938-L1955)
- [Profile check and target replacement](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L951-L968)
- [Live-profile change replaces adapter](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1287-L1335)
- [POST/referrer/initiator propagation](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_delegate_qt.cpp#L99-L146)
- [Chromium background/frame/opener/storage construction](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/web_contents/web_contents_impl.cc#L5283-L5393)
- [QML context-menu URL-only/background requests](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L2041-L2057)
- [Unsupported QML background action enum](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L219-L226)

## Narrow wider-source comparison and dependency/licensing consequences

### Falkon

Existing pin, new focused read of src/lib/webengine/webpage.cpp:520–584. Falkon rejects blocked automatic popups, creates the target page synchronously, routes all four destinations, and opens the original request into the target. It distinguishes selected vs unselected tab creation and records child-tab relations. Its selected-tab focus workaround uses a guarded QPointer and checks that the tab remains current before a delayed focus attempt. That is a useful anti-stale-focus invariant, not a recommendation to copy a 100 ms timer.

Dependency cone is Widgets QWebEnginePage/WebPage, BrowserWindow/TabWidget, plugin policy, popup widgets, and global application settings. It adds no native capability missing from Qt Quick for categories 1–5. Adapt policy/invariants rather than copying this graph. Inspected file carries GPL-3.0-or-later notice.

[Falkon request routing](https://github.com/KDE/falkon/blob/215414413f6338876a5e458927497f05872372f6/src/lib/webengine/webpage.cpp#L520-L584), [file license](https://github.com/KDE/falkon/blob/215414413f6338876a5e458927497f05872372f6/src/lib/webengine/webpage.cpp#L1-L17)

### CEF

At the new CEF pin, include/cef_load_handler.h:56–67 offers one OnLoadingStateChange(browser,isLoading,canGoBack,canGoForward) callback, with explicit ordering relative to per-frame load callbacks. That is an engine-API contrast to Qt's independently notified properties, not a Qt QML signal to call.

include/cef_life_span_handler.h:56–133 exposes more popup metadata than Qt QML: browser/frame source, popup_id, target_url, target_frame_name, disposition, user_gesture, popupFeatures, creation settings, plus OnAfterCreated/OnBeforePopupAborted cleanup contracts. Those pending-resource cleanup boundaries are useful design guidance. Qt does not thereby gain these callbacks/IDs, and copying CEF types would require a separate CEF embedding, process/client integration and window-host implementation. The headers carry a BSD-style three-clause notice; Chromium/transitive components retain separate obligations. No CEF implementation or runtime was audited.

[CEF load callback](https://github.com/chromiumembedded/cef/blob/ff57d4eae16d36457895f2de115a71d502e85a08/include/cef_load_handler.h#L56-L67), [CEF popup interface and cleanup](https://github.com/chromiumembedded/cef/blob/ff57d4eae16d36457895f2de115a71d502e85a08/include/cef_life_span_handler.h#L56-L133), [CEF file license](https://github.com/chromiumembedded/cef/blob/ff57d4eae16d36457895f2de115a71d502e85a08/include/cef_life_span_handler.h#L1-L28)

### Brave

At the new Brave pin, chromium_src/chrome/browser/ui/navigator/browser_navigator.cc:31–40 optionally changes NEW_POPUP to NEW_FOREGROUND_TAB behind a feature flag; :43–62 handles container storage-partition inheritance from source_site_instance, then :67 includes upstream Chromium browser navigation code. This is a Chromium browser-layer policy override, not an independent embeddable QML navigation controller. chromium_src/chrome/browser/ui/browser_commands.cc:21–38 adds a Tor-specific reload hook then includes Chromium browser commands. Neither yields a smaller or better-fitting Qt categories-1–5 implementation than Qt's supported APIs. The relevant files carry MPL-2.0 notices and depend on Chromium NavigateParams, BrowserWindowInterface, profiles, feature/build flags, and Brave container/Tor components. No broad Brave audit or quality superiority claim is supported.

[Brave navigation policy](https://github.com/brave/brave-core/blob/6b0b85c1ab38567689ff22fc5cdd9ea7b1eb076d/chromium_src/chrome/browser/ui/navigator/browser_navigator.cc#L1-L67), [Brave reload override](https://github.com/brave/brave-core/blob/6b0b85c1ab38567689ff22fc5cdd9ea7b1eb076d/chromium_src/chrome/browser/ui/browser_commands.cc#L1-L38)

These are source-license observations and dependency estimates, not legal clearance. No donor code was copied into product code. Using existing public Qt APIs adds no donor-code dependency; actual Qt distribution obligations still follow the application's applicable Qt license.

## Minimal decisive runtime gates for the next implementation

- Bind native navigation state; compare in-page click, cross-origin Back/Forward, same-document pushState/replaceState, redirect skipping, error page, stopped load and reload
- Explicit history chooser: nearest-first roles, -1/-2/+1/+2, current entry, no URL replay, stale chooser after reset, source session close/move, bounds no-op
- Original requests: window.open opener reference and script continuity; POST target body; named target reuse; noopener; exact normal/private profile; one-shot/double-adoption; async adoption intentionally rejected
- Background route: current selected session/focus unchanged; target loads; later activation works; no delayed focus steals
- Failed factory/policy/profile adoption: no ghost session or GET fallback; target/source destruction safely handled

All runtime gates above remain NOT RUN in this research task.
