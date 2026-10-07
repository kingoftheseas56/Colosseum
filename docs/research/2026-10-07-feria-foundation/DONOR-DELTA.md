# Categories 1–5: incremental comparison and selection

Baseline: Colosseum `2916fe85812643c2e0ae45b0aec132cb6eb4e507` (master resolved before edits).
Reuse: [first audit, 01cc5a48](https://github.com/kingoftheseas56/Colosseum/tree/01cc5a48dfd9b69176380d8749d576ab1d35a661/docs/research/2026-10-07-feria-inspection/report).
This does not reopen categories 6–52. The four central implementation files (SessionStore, BrowserPolicy.cpp, SessionDeck and QtWebEngine wrapper) match the earlier inspected snapshot byte-for-byte. The material drift is the written app/spaces/sidebar contract. Prior proven source findings remain evidence; source claims are not executed donor tests.

## Sources reused

| Abbreviation | Exact repository/revision | Path prefix |
|---|---|---|
| Nano | [qt/qtwebengine a7a7106](https://github.com/qt/qtwebengine/tree/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/quicknanobrowser) | examples/webenginequick/quicknanobrowser/example |
| Lifecycle | [same Qt revision](https://github.com/qt/qtwebengine/tree/a7a7106bc839974d9172c19707da15c21a8a7126/examples/webenginequick/lifecycle) | examples/webenginequick/lifecycle/example |
| Angelfish | [KDE/angelfish 40c6dc6](https://github.com/KDE/angelfish/tree/40c6dc69b93f93197cc7b1216224e7e52c491635) | repository root |
| Nicol | [arnav-kr/nicol abdc8db](https://github.com/arnav-kr/nicol/tree/abdc8db83989376a06642dd15ec647ba9ce45408) | repository root |
| Morph | [morph-browser 39175d1](https://gitlab.com/ubports/development/core/morph-browser/-/tree/39175d1346e6da48ac331b6a8f293bbe66a722eb) | repository root |

## 1. Navigation actions

Already known: all five use native engine navigation. Colosseum instead reconstructs cross-origin history using page JavaScript and drives history.back/forward. The previous audit correctly questioned that reconstruction but did not reproduce its alleged target-Qt cause.

| Donor | Relevant implementation | Decision |
|---|---|---|
| Nano | WebToolBar.qml:64–82, native getters and goBack/goForward/reload/stop | Reuse supported API shape; replace ambient currentWebView targeting |
| Lifecycle | WebTab.qml:35–65, per-view native WebEngineAction enabled/trigger | Strongest compact per-view action ownership reference |
| Angelfish | src/qml/desktop/Desktop.qml:69–127; lib/contents/ui/WebView.qml:439–441 | Native actions useful; do not copy extra loadingActive state or ambient current view |
| Nicol | ui/BrowserWindow.qml:131–155,660–666,745–754 | No additional advantage; reload false branch lacks null guard |
| Morph | src/app/webbrowser/qml-qt6/NavigationBar.qml:102–127,150–160,190–200 | Do not import old manual onUrlChanged workaround without reproduction |

Newly established: Qt Quick queues loadingChanged, unlike the earlier report's blanket callback-order explanation. Native toolbar Back intentionally skips certain unactivated script-created entries; history offset traversal does not. See NATIVE-API-DELTA.md for exact Qt/Chromium lines. A mismatch with JavaScript history.back is not evidence of an engine defect.

Implemented: FeriaNavigation per retained view, native availability/getters and operations; compatibility pageBack/pageForward facade; exact-ID deck commands. Removed the wrapper's crossOriginAhead bookkeeping, injected navigation-history script and delayed refresh timer. No native engine modification.

## 2. Address parsing/search

Already known: public Qt parsing is reusable; donor filesystem/custom-scheme permissions are too broad. Existing FeriaBrowserPolicy is the canonical policy, not a reason to preserve its errors.

| Donor | Relevant implementation | Decision |
|---|---|---|
| Nano | utils.h:21–26; WebToolBar.qml:141–145 | Qt fromUserInput useful; reject QFileInfo/fromLocalFile shortcut |
| Lifecycle | utils.h:21–26; WebTab.qml:128–136 | Same parser; no search implementation |
| Angelfish | lib/urlutils.cpp:20–22; NavigationEntrySheet.qml:83–88; desktop/Desktop.qml:154–179 | Raw search concatenation unsafe for query metacharacters; regex/ambient field coupling not selected |
| Nicol | ui/BrowserWindow.qml:772–788 | Whole-term escaping useful; prefix-without-colon and broad schemes not selected |
| Morph | src/app/url-utils.cpp:69–113; AddressBar.qml:462–479 | Broad scenario coverage useful; local-file/custom-scheme policy and per-UTF16-unit escaping not selected |

Newly established by a real QtCore 6.11.1 probe: QUrlQuery preserves a literal + in the encoded query. Form-style search endpoints can interpret it as a space. Encode the whole query value using QUrl::toPercentEncoding; native tests include literal +, &, # and supplementary Unicode. Reject control characters, oversized input, typed credentials and absolute local paths. Recognize host:port before explicit-scheme classification; native tests cover localhost, IPv6, uppercase schemes and IDN. Keep existing supported search engines and remote navigation allowlist. No remote keystroke suggestions or filesystem inspection.

Implemented in existing FeriaBrowserPolicy.cpp plus deck.navigateInput(id,text,engine). Parsing does not change the provider/app identity on redirects or external origins.

## 3. Per-page history/offsets

Already known: Qt provides a per-view native history model, distinct from FeriaAccountStore's global account History.

| Donor | Relevant implementation | Decision |
|---|---|---|
| Nano | WebToolBar.qml:37–60 | Select native history.items roles and goBackOrForward(model.offset) |
| Lifecycle | WebTab.qml actions only | No history chooser implementation; engine defaults underneath |
| Angelfish | mobile HistorySheet.qml:36–54 versus desktop/TabHistory.qml:18–45 | Mobile offset path useful; desktop URL assignment is not traversal and has unqualified URL role |
| Nicol | BrowserWindow.qml:668–700 | Native model/offset useful; Back-only menu access misses forward-only state |
| Morph | NavigationBar.qml:60–72,114,160; NavHistoryPopup.qml:26,34,89–92 | Model/signal separation useful; navigationHistory alias is not public Qt 6.11.1 view.history |

Newly established: models are QAbstractListModel, with no JS get()/array count API. Back offsets are -row-1, forward +row+1. Instantiator counts provide bounds. Chooser selection must carry a revision because still-in-range offsets can become stale. Qt model resets include same-document commits and some metadata updates.

Implemented: native historyItems, back/forward counts, historyRevision; historyOffset(id,offset,revision) rejects stale tokens, noninteger/zero/out-of-bounds offsets and retired IDs. Reference host renders history in the docked sidebar, never over the website. Native Back availability remains independent from backCount because Chromium's skip policy differs.

## 4. Independent pages/canonical identity

Already known: canonical SessionStore UUIDs and shell-owned retained views are stronger fits than a second donor tab registry. Both are actually used by the prototype.

| Donor | Relevant implementation | Decision |
|---|---|---|
| Nano | BrowserWindow.qml:180–188,250–283; BrowserManager.qml:12–16 | Independent views/profiles useful; child/button indexes are not canonical identity |
| Lifecycle | WebTabStack.qml:18–50; WebTab.qml:146–164 | Retained objects useful; discarding destroys live JS state despite retaining the wrapper |
| Angelfish | lib/tabsmodel.cpp:282–295; ListWebView.qml:54–119 | Row model/asynchronous Loaders add no UUID ownership; shared current-page UA/reload coupling undesirable |
| Nicol | BrowserWindow.qml:33–49,87–89,293–325 | Index model and unconditional hidden freeze weaker than existing ownership |
| Morph | BrowserTab.qml:28–43,78–120; tabs-model.cpp:129–195; Browser.qml:91–129 | Strong identity/runtime separation reference; object-string/date IDs, unload and separate persistence not imported |

Implemented: browserSpaces(provider), selectedBrowser(provider), browserApps() and activateBrowserApp(provider) extend SessionStore. They are projections of existing records/MRU, not stored duplicates. App entries expose selectedSpaceId; skins and names do not change IDs. Reference host uses app-only bottom entries and app-local sidebar rows. Production Main/Taskbar visual integration is deliberately unchanged. App-wide media suspension remains later work; activating/minimizing an app in this navigation host is not proof of media policy.

## 5. Original foreground/background requests

Already known: original requests must be adopted synchronously into a fresh view with the source profile. URL replay loses POST/opener context. Existing synchronous deck adoption is retained and tightened.

| Donor | Relevant implementation | Decision |
|---|---|---|
| Nano | BrowserWindow.qml:335–350; BrowserManager factories | All four destinations; correct original-object concept, but preloads URL in foreground and uses selected rather than emitting profile |
| Lifecycle | WebBrowser.qml:112–116 | Ordinary new tab only; no request adoption handler |
| Angelfish | WebView.qml:349–357; NewTabQuestion.qml:14–22 | URL-only replay/global foreground preference unsuitable |
| Nicol | BrowserWindow.qml:55–69,326–336 | Original request present; wrong enum, background collapse and current-view fallback unsuitable |
| Morph | TabComponent.qml:94–118; BrowserTab.qml:219–225; Browser.qml:1398–1419 | Strongest synchronous no-initial-URL adoption reference; null handling and dialog FIXME need correction |

Corrections: pinned Nano uses target.acceptAsNewWindow(request), which is equivalent to request.openIn(target) in 6.11.1; both return void. InNewBackgroundTab is modern request destination spelling. Original renderer requests may retain WebContents/POST/opener; context-menu requests can legitimately be URL-only. Preserve what the engine supplies, not invented payload. Named targets/noopener are engine concerns, not an application URL/name registry.

Implemented: exact source entry/profile/storage-root fence, synchronous adoption, no existing-view fallback, background creation does not select, explicit provider-start newSpace separate from engine request adoption. Tab/background go to retained spaces; window/dialog retain the existing popup host, with empty requested URL treated as about:blank. This classifies and exercises existing routes; it does not settle the open final site-window/dialog presentation contract or implement live cross-window transfer/close transactions.

## Wider candidates and dependencies

NATIVE-API-DELTA.md records narrowly inspected Qt 6.11.1, its Chromium fork, Falkon, Brave and CEF source and exact pins. Falkon reinforces synchronous destination/profile ownership; CEF exposes richer lifecycle callbacks but requires a different embedding; Brave wrappers depend on Chromium's browser layer and container/Tor policy. None provides a smaller portable implementation than supported Qt APIs plus Colosseum ownership. No whole-browser superiority or Chrome parity is claimed.

No GPL/AGPL donor source is copied. Morph/Falkon/Angelfish/Nicol inform behavioral selection and tests. New navigation glue is a behavioral adaptation/API wiring of Qt Nano/Lifecycle; retain the existing NanoBrowser-LICENSE.txt. Nano/Lifecycle inspected files offer BSD-3-Clause or Qt commercial licensing. Angelfish files differ (GPL-2+, LGPL-2-only/LGPL-2+); Nicol is AGPLv3; selected Morph/Falkon files are GPL3(+); Brave files MPL2; CEF headers BSD-style. These observations are not blanket license clearance. No KDE, Lomiri, Rust adblock, CEF, Brave or Chromium-private ABI dependency is added.

Dependencies: existing Qt Core/Gui/Qml/Quick/WebEngineQuick/Sql; tests additionally QuickTest/Test/Network. No engine build, new account/history registry, backend service or provider account is required.
