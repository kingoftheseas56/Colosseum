# Source evidence and verification ledger

## Claim labels

**Verified source**: actual pinned source read; conclusions can still be source-derived rather than runtime reproduced. **Reported**: earlier report or supplied metadata not independently reproduced. **Inference**: proposed consequence/design recommendation. **Unknown/NOT RUN/BLOCKED**: no supporting execution claim.

The prior 52-row comparison was an input, not an oracle. Relevant donor errors were independently checked. This inspection did not execute donor tests, compile Colosseum or start a new provider session. No production code was changed. The HTML's buttons are simulations.

## Application source index

The compact aliases in CAPABILITY-MAP.md mean these exact files. Line numbers there refer to the pinned files, not newer branch content.

- **C-Session**: [native/SessionStore.h](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/SessionStore.h)
- **C-Account**: [native/feria/FeriaAccountStore.cpp](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/feria/FeriaAccountStore.cpp)
- **C-Account.h**: [native/feria/FeriaAccountStore.h](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/feria/FeriaAccountStore.h)
- **C-Policy.cpp**: [native/feria/FeriaBrowserPolicy.cpp](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/feria/FeriaBrowserPolicy.cpp)
- **C-Policy**: [native/feria/FeriaBrowserPolicy.h](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/feria/FeriaBrowserPolicy.h)
- **D**: [qml/feria/FeriaSessionDeck.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaSessionDeck.qml)
- **C-Web**: [qml/feria/FeriaQtWebEngine.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaQtWebEngine.qml)
- **C-Requests**: [qml/feria/FeriaBrowserRequests.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaBrowserRequests.qml)
- **C-Downloads**: [qml/feria/FeriaBrowserDownloads.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaBrowserDownloads.qml)
- **C-Tools**: [qml/feria/FeriaPageTools.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaPageTools.qml)
- **Host**: [qml/feria/PorticoDiscoveryHostView.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/PorticoDiscoveryHostView.qml)
- **Menu**: [qml/feria/FeriaBrowserMenu.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaBrowserMenu.qml)
- **Main**: [qml/Main.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/Main.qml)
- **Taskbar**: [qml/Taskbar.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/Taskbar.qml)
- **MediaSession**: [qml/feria/FeriaMediaSession.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaMediaSession.qml)
- **Playback**: [qml/feria/FeriaPlayback.js](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaPlayback.js)
- **WebView2**: [qml/feria/FeriaWebView2.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/qml/feria/FeriaWebView2.qml)
- **Native WebView2 boundary**: [native/feria/provider-host/FeriaHostItem.h](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/native/feria/provider-host/FeriaHostItem.h)

### Current tests read, not automatically credited as runtime passes
- [tests/feria_account_store_test.cpp](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_account_store_test.cpp)
- [tests/feria_session_runtime_test.cpp](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_session_runtime_test.cpp)
- [tests/feria_session_probe/tst_RetainedSessions.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_session_probe/tst_RetainedSessions.qml)
- [tests/feria_browser_requests_probe/tst_browser_requests.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_browser_requests_probe/tst_browser_requests.qml)
- [tests/feria_browser_menu_probe/tst_browser_menu.qml](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_browser_menu_probe/tst_browser_menu.qml)
- [tests/feria_tab_restore_test.mjs](https://github.com/kingoftheseas56/Colosseum/blob/97d5f18f7abad992f0686acba6219d97e4fe514d/tests/feria_tab_restore_test.mjs)

Native account tests cover authorization after tab closure, replay/reentrancy, profile changes and metadata-write failure. Request/menu probes use QtObject fake requests/stores: useful branch/lifetime-contract tests, not engine consent or native-dialog proof. The retained session suite uses actual engine fixtures but was only inspected here. Its background-audio expectation explicitly conflicts with the new pause-on-leave requirement. The Node restore test extracts live QML function bodies into a VM with fake stores; its PASS is model evidence only and includes the obsolete optional auto-restore behavior.

## Qt and exact Chromium integration

Q = Qt target eb0793cc4b76e93cf669f586fd68c76019f40ec9, v6.11.1. N = Nano donor and L = Lifecycle donor at a7a7106bc839974d9172c19707da15c21a8a7126. They are distinct revisions.

- [Real reparent test: existing WebEngineView and pixel assertion: tests/auto/quick/qquickwebengineviewgraphics/tst_qquickwebengineviewgraphics.cpp:118–140](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/tests/auto/quick/qquickwebengineviewgraphics/tst_qquickwebengineviewgraphics.cpp#L118-L140)
- [Private C++ header warning: public QML type is not public C++ view ABI: src/webenginequick/api/qquickwebengineview_p.h:8–24](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview_p.h#L8-L24)
- [Scene graph/window reconnection: src/core/render_widget_host_view_qt_delegate_item.cpp:338–379](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/render_widget_host_view_qt_delegate_item.cpp#L338-L379)
- [Window/RHI resource-release constraints: src/core/render_widget_host_view_qt_delegate_item.cpp:408–445](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/render_widget_host_view_qt_delegate_item.cpp#L408-L445)
- [Stack new-window request emitted synchronously: src/webenginequick/api/qquickwebengineview.cpp:744–757](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L744-L757)
- [Synchronous adoption/profile compatibility: src/webenginequick/api/qquickwebengineview.cpp:1938–1955](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L1938-L1955)
- [POST/referrer/initiator handling in Qt delegate: src/core/web_contents_delegate_qt.cpp:99–146](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_delegate_qt.cpp#L99-L146)
- [Chromium opener/frame/storage creation: chromium/content/browser/web_contents/web_contents_impl.cc:5322–5375](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/web_contents/web_contents_impl.cc#L5322-L5375)
- [RequestClose invokes Chromium beforeunload: src/core/web_contents_adapter.cpp:880–883](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L880-L883)
- [QML close-ack and internal dynamic rejected method: src/webenginequick/api/qquickwebengineview.cpp:765–776](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineview.cpp#L765-L776)
- [Live navigation controller getters: src/core/web_contents_adapter.cpp:568–583](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L568-L583)
- [Navigation event/action update ordering: src/core/web_contents_delegate_qt.cpp:351–445](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_delegate_qt.cpp#L351-L445)
- [Upstream QML navigation tests: tests/auto/quick/qmltests/data/tst_navigationHistory.qml:52–175](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/tests/auto/quick/qmltests/data/tst_navigationHistory.qml#L52-L175)
- [Recommended lifecycle policy: src/core/web_contents_adapter.cpp:2125–2167](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L2125-L2167)
- [Freeze/discard implementation; discard replaces WebContents: src/core/web_contents_adapter.cpp:2207–2278](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_contents_adapter.cpp#L2207-L2278)
- [Chromium MediaSession internal suspend/resume contract: chromium/content/public/browser/media_session.h:99–105](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/public/browser/media_session.h#L99-L105)
- [Chromium media-session suspend implementation: chromium/content/browser/media/session/media_session_impl.cc:699–712](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/media/session/media_session_impl.cc#L699-L712)
- [Client-certificate async/shared selection ownership: src/webenginequick/api/qquickwebengineclientcertificateselection.cpp:81–100](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineclientcertificateselection.cpp#L81-L100)
- [Desktop-media shared request and destructor fallback: src/core/api/qwebenginedesktopmediarequest.cpp:109–158](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginedesktopmediarequest.cpp#L109-L158)
- [WebAuth controller owns request object: src/core/authenticator_request_dialog_controller.cpp:39–68](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/authenticator_request_dialog_controller.cpp#L39-L68)
- [Permission validity is not original-prompt validity: src/core/api/qwebenginepermission.cpp:218–252](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginepermission.cpp#L218-L252)
- [Cookie GetAllCookies empty callback: src/core/net/cookie_monster_delegate_qt.cpp:87–90](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/net/cookie_monster_delegate_qt.cpp#L87-L90)
- [Cookie test FIXME and blank-navigation workaround: tests/auto/core/qwebenginecookiestore/tst_qwebenginecookiestore.cpp:119–129](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/tests/auto/core/qwebenginecookiestore/tst_qwebenginecookiestore.cpp#L119-L129)
- [Public cookie operations: src/core/api/qwebenginecookiestore.h:30–50](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginecookiestore.h#L30-L50)
- [HTTP cache completion ordering: src/core/api/qwebengineprofile.cpp:887–901](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebengineprofile.cpp#L887-L901)
- [Extension manager public API: src/core/api/qwebengineextensionmanager.h:24–44](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebengineextensionmanager.h#L24-L44)
- [Extension restrictions and enabled state: src/core/api/qwebengineextensionmanager.cpp:14–88](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebengineextensionmanager.cpp#L14-L88)
- [Spellcheck dictionaries/deployment: src/core/doc/src/qtwebengine-features.qdoc:692–777](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/doc/src/qtwebengine-features.qdoc#L692-L777)
- [Notification API: src/core/api/qwebenginenotification.h:23–52](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/api/qwebenginenotification.h#L23-L52)
- [Push opt-in uses FCM: src/webenginequick/api/qquickwebengineprofile.cpp:992–1008](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/webenginequick/api/qquickwebengineprofile.cpp#L992-L1008)
- [Qt baseline/security metadata: CHROMIUM_VERSION:1–2](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/CHROMIUM_VERSION#L1-L2)
- [Chromium fork version file: chromium/chrome/VERSION:1–4](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/chrome/VERSION#L1-L4)

The reparent source test was inspected, not executed here. It is a narrow intended rendering path, not proof of complete Feria Pop out/Return. The upstream graphics BLACKLIST excludes showHideShow; no global graphics-quality certification is inferred.

Qt emits loadFinished before updating navigation actions in the inspected path. This is a plausible explanation for observing stale action values inside a callback, not a reproduced root cause. The public getters/controller remain a stronger authority than reconstructing history from origin-local JavaScript. Differential runtime testing is the replacement gate.

## Donor source comparisons and corrections

- [N direct navigation/history actions: quicknanobrowser/WebToolBar.qml:43–89](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/WebToolBar.qml#L43-L89)
- [N destination routing: quicknanobrowser/BrowserWindow.qml:335–350](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/BrowserWindow.qml#L335-L350)
- [N unsafe selected-view crash reload: quicknanobrowser/BrowserWindow.qml:378–397](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/BrowserWindow.qml#L378-L397)
- [N close/native window handshake: quicknanobrowser/BrowserWindow.qml:544–551](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/BrowserWindow.qml#L544-L551)
- [N profile/window factory: quicknanobrowser/BrowserManager.qml:19–35](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/BrowserManager.qml#L19-L35)
- [N permission object UI: quicknanobrowser/PermissionDialog.qml:15–69](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/PermissionDialog.qml#L15-L69)
- [N WebAuth state mapping: quicknanobrowser/WebAuthDialog.qml:30–77](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser/WebAuthDialog.qml#L30-L77)
- [L recommended-state timer: lifecycle/WebTab.qml:152–164](https://github.com/qt/qtwebengine/blob/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/lifecycle/WebTab.qml#L152-L164)
- [M context/QObject/visual parent handling: src/app/webbrowser/reparenter.cpp:57–122](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/reparenter.cpp#L57-L122)
- [M actual whole-tab transfer: src/app/webbrowser/qml-qt6/TabsBar.qml:97–110](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/qml-qt6/TabsBar.qml#L97-L110)
- [M window-dependent property rebinding: src/app/webbrowser/qml-qt6/Browser.qml:118–129](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/qml-qt6/Browser.qml#L118-L129)
- [M movement still enters closed history: src/app/webbrowser/qml-qt6/Browser.qml:1445–1473](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/qml-qt6/Browser.qml#L1445-L1473)
- [M monitor resets on view identity, not same-view navigation: src/app/qml-qt6/WebProcessMonitor.qml:46–92](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/qml-qt6/WebProcessMonitor.qml#L46-L92)
- [M production monitor is selected-view bound: src/app/webbrowser/qml-qt6/Browser.qml:582–585](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/qml-qt6/Browser.qml#L582-L585)
- [M reparent test uses fake item containers: tests/unittests/qml/tst_Reparenter.qml:65–111](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/tests/unittests/qml/tst_Reparenter.qml#L65-L111)
- [M OpenSearch parsing: src/app/webbrowser/searchengine.cpp:86–151](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/searchengine.cpp#L86-L151)
- [M preview async save/cleanup: src/app/webbrowser/qml-qt6/PreviewManager.qml:74–105](https://gitlab.com/ubports/development/core/morph-browser/-/blob/39175d1346e6da48ac331b6a8f293bbe66a722eb/src/app/webbrowser/qml-qt6/PreviewManager.qml#L74-L105)
- [A profile download ownership: lib/angelfishwebprofile.cpp:23–34](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/angelfishwebprofile.cpp#L23-L34)
- [A single-slot download question: lib/contents/ui/ListWebView.qml:29–38](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/contents/ui/ListWebView.qml#L29-L38)
- [A permission request lacks view in question: lib/contents/ui/WebView.qml:395–399](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/contents/ui/WebView.qml#L395-L399)
- [A permission answer targets currentWebView: lib/contents/ui/PermissionQuestion.qml:49–63](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/contents/ui/PermissionQuestion.qml#L49-L63)
- [A persistence reset/write errors: lib/tabsmodel.cpp:161–235](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/tabsmodel.cpp#L161-L235)
- [A bounded20 reopen exists: lib/tabsmodel.cpp:341–368](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/tabsmodel.cpp#L341-L368)
- [A history test actually removes bookmark: autotests/dbmanagertest.cpp:73–79](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/autotests/dbmanagertest.cpp#L73-L79)
- [A hardcoded Linux UA: lib/useragent.cpp:24–34](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/useragent.cpp#L24-L34)
- [A print preview fixed filename/success handling: lib/contents/ui/WebView.qml:444–463](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/lib/contents/ui/WebView.qml#L444-L463)
- [A adblock native/Rust ownership: src/adblockurlinterceptor.cpp:47–136](https://github.com/KDE/angelfish/blob/40c6dc69b93f93197cc7b1216224e7e52c491635/src/adblockurlinterceptor.cpp#L47-L136)
- [Nicol routing/freezing/automatic selection/close defects: ui/BrowserWindow.qml:325–348](https://github.com/arnav-kr/nicol/blob/abdc8db83989376a06642dd15ec647ba9ce45408/ui/BrowserWindow.qml#L325-L348)
- [Nicol unavailable new view fallback resolves current index: ui/BrowserWindow.qml:55–68](https://github.com/arnav-kr/nicol/blob/abdc8db83989376a06642dd15ec647ba9ce45408/ui/BrowserWindow.qml#L55-L68)
- [F same-tab transfer disconnects source signals: src/lib/tabwidget/tabwidget.cpp:693–730](https://github.com/KDE/falkon/blob/215414413f6338876a5e458927497f05872372f6/src/lib/tabwidget/tabwidget.cpp#L693-L730)
- [F detach/attach updates window/UI associations: src/lib/webtab/webtab.cpp:314–365](https://github.com/KDE/falkon/blob/215414413f6338876a5e458927497f05872372f6/src/lib/webtab/webtab.cpp#L314-L365)

### What the independent donor check changes

- Morph is not a proven per-session recovery implementation: the component is target-aware, but the app binds one instance to currentWebview. Switching resets the budget; background views are not monitored by that connection. It does cancel timers on view identity change; do not falsely claim it has no cancellation.
- Morph's actual move transfers a BrowserTab and rebinds its window dependencies. loadExisting exists but no caller was found; it is not the demonstrated production transfer path. Its close history is populated before the moving flag skips destruction, so moving can be recorded as closing.
- Morph's reparent tests are fake Items in one window. However its Qt6 CMake preprocessing exists, so old-looking imports alone do not establish its tests cannot run.
- Angelfish has a bounded20 reopen stack. The earlier table should acknowledge it. Its permission UI's currentWebView targeting and single-slot download question are weaker than Feria's request-bound queue.
- Nicol's new-index itemAt fallback generally yields null, rather than a reliable existing view. Its selected-index close and automatic screen/certificate choice remain unsuitable.
- Falkon adds explicit detach/attach cleanup evidence. It is a Widgets/WebEngine/Falkon object graph, not a QQuick drop-in.

### Dependency cones and notices

| Candidate | Actual dependencies / assumptions | Use decision and notices |
|---|---|---|
| Qt Nano/Lifecycle | Qt Quick/Controls/Layout/Window/WebEngine; examples' profile/window glue | Direct supported operations and behavior patterns. Selected files BSD-3-Clause; retain notices if copied |
| Morph Reparenter | Public Qt Core/Qml/Quick context/component/item APIs | Invariant useful; check failed component creation/casts/property writes. Selected helper/app/test files GPL-3.0 notices |
| Morph full browser | Morph.Web/project-private modules, Lomiri Components/Popups/Content/SystemInfo, Qt5Compat, Linux/AppArmor/libpsl-oriented build | Do not import shell. Project-private modules are not synonymous with Qt private headers. Selected Readability asset licensing is separate |
| Angelfish | Qt WebEngine, KDE i18n/notifications; full app ECM/KF6/Kirigami/FutureSQL/QCoro; Qt>=6.9 build requests QmlPrivate; optional Corrosion/Rust adblock | Profile/request patterns useful; full global models/persistence not replacement winners. Profile/download/permission GPL-2.0-or-later; tabsmodel/test LGPL-2.0-only |
| Nicol | Qt Quick/WebEngineQuick, minimum6.8 | Small dependency cone does not outweigh behavior defects. Root AGPL-3.0 notice |
| Falkon | Qt Widgets/WebEngine + WebTab/TabWidget/BrowserWindow/location-bar associations | Adapt cleanup behavior, not classes into QML. Inspected files GPL-3.0-or-later |
| Chromium media/contents | Exact Qt Chromium fork, content::WebContents, base, Mojo, media-session and Qt adapter internals | Engine integration patch/rebuild if actually required. Not callable via a public QML object; no wholesale Chromium/Chrome shell recommendation |

These observations identify source notices and porting costs, not blanket legal clearance. No donor source was copied into product code in this inspection. Firefox, Brave and CEF application trees were not comprehensively inspected; no claim of a winner over them is made. The wider pool was addressed through exact Chromium integration and a focused Falkon comparison. Optional adblock/reader candidates retain explicit unresolved quality/testing gates.

### Source access

Pinned GitHub application and primary donor files were accessible through the GitHub connector. Existing own-cloud donor extracts supplied surrounding files and SOURCE-RECEIPT revisions. Seven key Morph files were independently fetched from pinned public GitLab raw URLs and matched the extracts after CRLF normalization. GitLab connector authentication and a web fetch failed, but permitted public raw retrieval succeeded; that is not a remaining Morph source blocker. No user-computer fallback was used.

## Commands and actual results

Own sandbox: Linux x86_64, glibc2.41. Preserved PySide6/Qt Core reports6.11.1. No dependencies installed. No security/proxy changes.

| Check | Command / result | Evidence classification |
|---|---|---|
| Pinned source reads | GitHub read-only raw/commit/tree requests; local line-numbered inspection using nl/sed/grep | PASS source inspection; no app behavior pass |
| Attachment identities | sha256sum inspection brief and HTML | PASS, values in README |
| Existing restore model test | cd source && node tests/feria_tab_restore_test.mjs; exit0 | PASS model/source-extracted logic only; includes old automatic-restore policy |
| Qt availability | PYTHONPATH=../qml-preview-setup/runtime python3 import probe; QtCore qVersion()=6.11.1 | PASS Core version lookup |
| Real WebEngine probe readiness | import PySide6.QtWebEngineCore / QtWebEngineQuick | BLOCKED: ModuleNotFoundError for both in this preserved runtime. Stub .pyi files are not loadable modules |
| HTML inspection | Python stdlib HTMLParser extracts nested iframe source and visible labels | PASS source inspection; no screenshot/render claim |
| bs4 convenience parse | Python import bs4 | Failed: ModuleNotFoundError; used standard-library parser, installed nothing |
| Full app / Lanista / pop-out / POST / close cancellation | Not executed | NOT RUN: no matching compiled candidate supplied/established for source97d5…; available PySide runtime lacks WebEngine module. Do not generalize this to all cloud desktop capability |
| Donor unit/graphics/runtime suites | Read selected tests only | NOT RUN here; upstream test source is not our test receipt |
| Windows / hardware keys / codecs / DRM / live providers | No execution | NOT RUN, separate receiving qualification |

The availability script catches import errors and exits normally so it can report both modules; an exit0 of that diagnostic is not WebEngine availability. Historical Linux browser success or another artifact's Qt version was not promoted into a result for this pin.

## Reproducible next probes

1. Obtain a matching Qt6.11.1 WebEngine component runner or candidate. Record executable checksum, OS/architecture, Qt version and Chromium version from runtime APIs separately from declared source metadata. No new engine build required merely to investigate public app APIs.
2. Run Qt's same-view reparent fixture extended with JS counter, input field, navigation stack, repeated transfer and old-host destruction. Compare actual rendered pixels/focus/dialog behavior and stable session/profile/view identity.
3. Use two loopback origins and server-observed POST/opener fixtures; test all requested destinations, noopener and policy rejection without overriding browser security.
4. Drive close from taskbar, menu, page and OS window; accept/cancel beforeunload, including background requests and repeat close. Do not auto-destroy on timeout.
5. Differential native navigation test: live getter, action.enabled, history model and load-signal/event-loop ordering across links/SPA/error/cross-origin before deleting heuristic.
6. Reopened persistent-profile cookie test: existing host/domain/path/HttpOnly/Secure/partitioned cookies; native request proof of selective deletion and unrelated-cookie retention. Do not count cache clearing as sign-out.
7. Provider-independent media fixture including manual pause, autoplay, iframe and later navigation. Then identify provider-specific controllability gaps honestly.

No production implementation, merges, PRs, releases or adoption occurred. Report/evidence commits only; exact commit identifiers are in the delivery entry. No Codex, Work, BDC or user-machine access was used. Read-only source research was parallelized; no coding was delegated.
