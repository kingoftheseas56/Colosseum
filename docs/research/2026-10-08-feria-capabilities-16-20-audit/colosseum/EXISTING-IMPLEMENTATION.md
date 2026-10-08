# Current Colosseum implementation: Feria categories 16–20

Baseline: `62057bfbdaa2305a05d2d3fb02b870bc9e826638` in `kingoftheseas56/Colosseum`. Audit date: 2026-10-08. Source inspected; product builds, product tests and platform/runtime qualification **not executed**. This report covers current Colosseum only; donor evidence is covered in the three-family comparison.

## Result

| Original category | Current code | Full new-contract position |
| --- | --- | --- |
| 16 | Implemented owned-pause policy; keep it | Bounded DOM coverage; explicit reasons, adapter identity and broader pending-operation blockers need extension |
| 17 | Implemented profile receiver, synchronous acceptance and metadata authorization | Origin/frame provenance and explicit admission/destination policy remain incomplete |
| 18 | Implemented native progress/pause/resume/cancel plus Open controls | Resumability/action diagnostics and verified completed-file Open incomplete |
| 19 | Implemented history, grants, write retry and account retirement | Last-private-space transfer lifetime and typed metadata rejection incomplete |
| 20 | Native attachment downloads and separate Print/PDF implemented | No explicit shared Save page/link/image/media context API found; default engine menu exposure is not disproven |

Do not turn these findings into five rewrites. Preserve the existing canonical owners and extend their boundaries.

## Authority and owner map

- Root AGENTS.md is the only AGENTS.md found in the pinned recursive Git tree. Current user scope takes priority over its optional memory instructions. No Preflight materials, user machine, Memory Bridge or unpinned implementation source was used.
- The proposed contract explicitly disclaims implementation proof at line 3. Its canonical-owner and operation names are requirements, not APIs already delivered.
- SessionStore already owns account/profile/space/page/document/host identity. Use its identity, not a new transfer-created browser identity: [native/SessionStore.h:96–107 (setBrowserProfile / browserIdentity allocation)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/SessionStore.h#L96-L107); [native/SessionStore.h:232–250 (openBrowser browserIdentity)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/SessionStore.h#L232-L250).
- One production deck is instantiated under the persistent application root: [qml/Main.qml:4712–4729 (feriaSessionDeck)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/Main.qml#L4712-L4729). Regular/private profiles and one download receiver for each belong to that deck: [qml/feria/FeriaSessionDeck.qml:90–106 (regularDownloads / privateDownloads)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L90-L106); [qml/feria/FeriaSessionDeck.qml:476–486 (profileFactory)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L476-L486).
- Qt native request objects own transfer mechanics; FeriaBrowserDownloads owns the QML live-entry projection; FeriaAccountStore owns admitted persistent history and transient metadata grants. The former is not a second durable store.
- Account switching first retires canonical sessions and clears account storage/grants, then changes browser storage scope: [native/main.cpp:1980–2001 (bindFeriaProfile / storesAboutToChange)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/main.cpp#L1980-L2001). That change invokes quiescence, cancellation and profile retirement: [qml/feria/FeriaSessionDeck.qml:654–667 (policy Connections)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L654-L667); [qml/feria/FeriaSessionDeck.qml:566–603 (startQuiescence / advanceQuiescence)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L566-L603).
- Retained spaces currently instantiate FeriaQtWebEngine directly. The native WebView2 host remains a separate adapter touchpoint; this source inspection does not qualify WebView2 parity.

## 16. Pause on app minimize/world-away; resume only media paused by Colosseum

**Recommendation:** Keep existing Colosseum implementation; adapt explicit identity/reason and capability reporting into existing owners.

**Existing owner:** SessionStore -> retained FeriaSessionDeck entry -> FeriaQtWebEngine presentation command -> volatile document-owned FeriaPlayback ledger.

Implemented:
- Aggregate activitySuspended is derived from world/app display state and main/auxiliary window minimized state, not ordinary inactive-tab visibility.
- Per-element WeakMap captures media object, ownerDocument, currentSrc/src, original play/pause methods; owned flag is set only when Colosseum's pause leaves playing element paused.
- Navigation epoch and presentation serial fence stale presentation commands; source/document/connected/ended checks gate resume; explicit page pause/play and input revoke ownership.
- Optional freeze observes engine recommendedState, recentlyAudible, close blocker and physical pendingJavaScript count.

Limits and acceptance gaps:
- Reason is an aggregate suspended boolean, not a typed per-reason ledger; canonical SessionStore identity is not explicitly carried in presentation script.
- Same-origin/open shadow DOM traversal is bounded; cross-origin frames, later-added frame/shadow roots, closed shadow roots and non-HTML-media players are not universal control.
- play promise rejection is swallowed; successful resume/autoplay is not acknowledged to shared owner.
- Explicit freeze blocker binds closeController.busy, not every permission/licence/request lifetime.

Exact source/test anchors (all at the baseline):
- [qml/feria/FeriaPlayback.js:219–298 (presentationMediaSource / presentationMedia)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaPlayback.js#L219-L298)
- [qml/feria/FeriaQtWebEngine.qml:36–76 (applyPresentationActivity / freezeTimer)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaQtWebEngine.qml#L36-L76)
- [qml/feria/FeriaSessionDeck.qml:803–822 (browserLoader sourceComponent activitySuspended)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L803-L822)
- [qml/Main.qml:4712–4729 (feriaSessionDeck)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/Main.qml#L4712-L4729)
- [native/SessionStore.h:232–250 (openBrowser browserIdentity)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/SessionStore.h#L232-L250)
- [tests/feria_lifecycle_probe/tst_Lifecycle.qml:48–172 (presentation/lifecycle tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_lifecycle_probe/tst_Lifecycle.qml#L48-L172)
- [tests/feria_session_probe/tst_RetainedSessions.qml:82–108 (world-away/inactive tab tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_session_probe/tst_RetainedSessions.qml#L82-L108)

## 17. Download request ownership and admission

**Recommendation:** Keep profile listener, request identity capture and metadata grants; resolve product auto-save/destination policy and extend request provenance without a second store.

**Existing owner:** FeriaSessionDeck profile-scoped FeriaBrowserDownloads; native engine owns request mechanism; FeriaAccountStore owns metadata authorization.

Implemented:
- Exactly one receiver per regular/private shared profile in the production deck; source view session/visit captured during signal, never read from later selection.
- Synchronous receive creates a per-request entry, uses Qt's default/collision-resolved destination unchanged and immediately accepts; inactive/entry-creation failure cancels.
- Private/profile/store identities are captured. The QML boundary-double fallback accepts null-view/unowned requests and omits history; real Qt originless/stale-view getter safety is not established.
- Native grant is UUID, current non-private visit checked at minting, profile path and provider captured; history-write permission is separate from transfer admission.

Limits and acceptance gaps:
- No inspected product ask/download-directory preference or independent user-consent record; default auto-save is current behavior, not by itself a demonstrated defect.
- Originating frame/origin, canonical account/space/page/document identity and native request ID are not captured by receiver; request.url is transfer URL, not source-frame identity.
- Grant binds profile/provider but does not bind transfer ID, source URL, destination/file identity; trusted descriptor path is accepted.
- Multiple transfers are modeled but real multi-space/multi-window same-profile concurrency/one-listener proof remains a fixture gap.
- Pinned Qt Quick view() dereferences adapterClient unconditionally while the profile can assign nullptr. QML null-view fallback is intended behavior covered by a double, not native proof of safe originless/stale-view handling.

Exact source/test anchors (all at the baseline):
- [qml/feria/FeriaSessionDeck.qml:90–106 (regularDownloads / privateDownloads)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L90-L106)
- [qml/feria/FeriaSessionDeck.qml:476–486 (profileFactory)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L476-L486)
- [qml/feria/FeriaBrowserDownloads.qml:34–58 (receive)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L34-L58)
- [native/feria/FeriaAccountStore.cpp:257–315 (authorizeBrowserDownload / cancelBrowserDownload / recordBrowserDownload)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L257-L315)
- [native/feria/FeriaAccountStore.h:76–79 (DownloadAuthorization)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.h#L76-L79)
- [native/feria/FeriaAccountStore.cpp:213–221 (setBrowserPreference)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L213-L221)
- [tests/feria_browser_requests_probe/tst_browser_requests.qml:287–394 (download boundary tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_browser_requests_probe/tst_browser_requests.qml#L287-L394)
- [tests/feria_account_store_test.cpp:490–572 (transient download authorization tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_account_store_test.cpp#L490-L572)

## 18. Download progress/pause/resume/cancel/open

**Recommendation:** Keep native request state/actions; adapt typed capability/action results and verified file Open in the existing owner.

**Existing owner:** Live native WebEngineDownloadRequest projected by FeriaBrowserDownloads.downloadEntry.

Implemented:
- receivedBytes/totalBytes/state/isPaused/isFinished are native-bound; unknown/zero total is indeterminate while active.
- Completed is exact DownloadCompleted; cancelled is separately labeled and releases grant.
- Pause/resume/cancel route to native request; interruption retains metadata grant and is exposed as Resume.
- Native path/name is used; renderer-supplied text is not interpolated into script.

Limits and acceptance gaps:
- All interrupted entries expose Resume without a separate resumability/reason capability or native action outcome.
- Open/Open folder calls Qt.openUrlExternally with path strings; no file existence, current account authorization or completed-file identity validation at action time.
- History Open ignores persisted state and reload validation; metadata strings are not proof a file exists.
- Live entries are not pruned until retirement; cancelled/completed rows may accumulate.
- No inspected real-native interruption, zero-byte/unknown-length, cancel/completion-race, missing/replaced-file or safe Open fixture.
- Source-inferred stale-final-byte-count risk: Qt emits Completed before applying final byte counts; Feria commits on stateChanged and does not refresh a successfully recorded row at isFinishedChanged.

Exact source/test anchors (all at the baseline):
- [qml/feria/FeriaBrowserDownloads.qml:80–145 (downloadEntry / update / action methods)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L80-L145)
- [qml/feria/FeriaBrowserDownloads.qml:174–219 (download presentation and Open)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L174-L219)
- [tests/feria_browser_requests_probe/tst_browser_requests.qml:287–394 (download boundary tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_browser_requests_probe/tst_browser_requests.qml#L287-L394)
- [tests/feria_session_probe/tst_RetainedSessions.qml:231–244 (test_downloadContinuesAfterTabCloses)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_session_probe/tst_RetainedSessions.qml#L231-L244)

## 19. Download persistence and private cleanup

**Recommendation:** Keep FeriaAccountStore history/grants and profile retirement; preserve private profile through valid dependent transfers and distinguish metadata rejection from write failure.

**Existing owner:** FeriaAccountStore persistent browserDownloadHistory; FeriaBrowserDownloads transient entries; FeriaSessionDeck profile lifecycle.

Implemented:
- Accepted ordinary transfer grant survives endSessionVisit/source navigation, while new grants require live visit.
- Private downloads cannot mint ordinary grants; QML private projection omits ordinary history; ordinary rows capped at 100 on commit.
- Successful commit consumes grant once, reentrant commit is blocked, failed atomic QSaveFile write preserves grant/in-memory state for retry.
- Account path switch clears all visits/grants; production account binding then triggers profile quiescence and cancellation.
- No native transfer restart/resume data is serialized; clearing history affects metadata, not saved files.

Limits and acceptance gaps:
- Last private-space retirement waits on views only then cancels private downloads; does not preserve last dependent valid transfer as proposed contract requires.
- Profile teardown calls cancel then destroys entries/profiles without explicit terminal transfer-drain acknowledgement; native cancellation lifetime requires engine qualification.
- Any failed recordBrowserDownload is treated as retryable, including invalidated grant or permanently inadmissible source URL.
- Reload imports browserDownloadHistory without path/state revalidation; active transfer state and current filesystem availability are not reconciled.
- One boundary test's old-account token-clear assertion disagrees with retry-retaining current QML; no tests executed.

Exact source/test anchors (all at the baseline):
- [native/feria/FeriaAccountStore.cpp:257–315 (authorizeBrowserDownload / cancelBrowserDownload / recordBrowserDownload)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L257-L315)
- [native/feria/FeriaAccountStore.cpp:359–377 (commit)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L359-L377)
- [native/feria/FeriaAccountStore.cpp:138–176 (setStoragePath / browserDownloadHistory)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L138-L176)
- [native/feria/FeriaAccountStore.cpp:393–404 (endSessionVisit / visitFor)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L393-L404)
- [qml/feria/FeriaSessionDeck.qml:505–539 (releasePrivateProfile / syncSessions)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L505-L539)
- [qml/feria/FeriaSessionDeck.qml:566–603 (startQuiescence / advanceQuiescence)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L566-L603)
- [native/main.cpp:1980–2001 (bindFeriaProfile / storesAboutToChange)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/main.cpp#L1980-L2001)
- [native/feria/FeriaAccountStore.cpp:28–53 (browserUrl)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L28-L53)
- [tests/feria_download_metadata_probe/tst_Metadata.qml:60–87 (metadata projection/retry/retirement tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_download_metadata_probe/tst_Metadata.qml#L60-L87)
- [tests/feria_account_store_test.cpp:490–572 (transient download authorization tests)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_account_store_test.cpp#L490-L572)
- [tests/feria_session_probe/tst_RetainedSessions.qml:231–244 (test_downloadContinuesAfterTabCloses)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_session_probe/tst_RetainedSessions.qml#L231-L244)
- [tests/feria_browser_requests_probe/tst_browser_requests.qml:339–346 (test_download_old_account_authorization_cannot_record_in_new_account)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_browser_requests_probe/tst_browser_requests.qml#L339-L346)

## 20. Save page/link/image/media through native download pipeline

**Recommendation:** Use supported public engine APIs through existing profile download owner; retain separate Print/PDF semantics. Default engine save exposure is donor-dependent/unqualified here.

**Existing owner:** Current FeriaPageTools handles Find/zoom; FeriaBrowserRequests owns Print/PDF; no custom typed Save context owner found.

Implemented:
- Print/PDF invokes public browser.printToPdf after FileDialog destination and generation/browser/active checks.
- Ordinary native attachment download reaches existing profile pipeline and has a real-engine local fixture.
- Retained view has no custom onContextMenuRequested interception in inspected files, so native default-menu capability must be assessed from engine source rather than assumed absent.

Limits and acceptance gaps:
- No explicit savePage/saveLink/saveImage/saveMedia action, context capability snapshot, source frame/native context token or shared Save operation found in inspected production entry path.
- Print/PDF is a separate native API with path completion signal, not FeriaBrowserDownloads transfer/history pipeline.
- No inspected page-save format, frame/context-staleness, unsafe filename, blob/media or destination-cancel integration fixture.
- No app-controlled WebView2 download/Save adapter found in the bounded host/wrapper; do not infer parity from engine defaults.

Exact source/test anchors (all at the baseline):
- [qml/feria/FeriaBrowserRequests.qml:213–280 (requestPrint / savePrint / FileDialog)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserRequests.qml#L213-L280)
- [qml/feria/PorticoDiscoveryHostView.qml:13–63 (FeriaBrowserMenu / Ctrl+J / Ctrl+P)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/PorticoDiscoveryHostView.qml#L13-L63)
- [tests/feria_browser_requests_probe/tst_browser_requests.qml:227–237 (test_print_destination_is_fenced_after_navigation)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_browser_requests_probe/tst_browser_requests.qml#L227-L237)
- [qml/feria/FeriaBrowserDownloads.qml:34–58 (receive)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L34-L58)
- [qml/feria/FeriaWebView2.qml:1–125 (wrapper)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaWebView2.qml#L1-L125)

## Interpretive details that affect the prototype

### Category 16: preserve the actual reason and identity semantics

The reason presently means “application presentation is suspended,” computed by an OR of world-away/app-hidden, main-window minimized and auxiliary-window minimized. It is **not** ordinary inactive-tab status. Main.qml:3120–3129 closes the app presentation when its browser activity is parked; Main.qml:4722–4723 supplies the displayed state. FeriaSessionDeck.qml:819–821 supplies the minimized checks.

The ledger stores an exact element reference plus its ownerDocument and source string; its containing per-browser document supplies implicit space/frame scope. It also uses local navigationGeneration/presentationSerial. This is meaningful provenance, not a URL-only resume guess. It is nevertheless not the complete canonical identity envelope proposed in the contract, and it does not expose a separate reason set such as world-away/main-minimize/app-minimize.

The traversal limits are explicit: depth five, up to 100 media elements per root, 32 frames per root, and 3,000 nodes per root for open shadow roots. Roots discovered after the transition are not automatically enumerated unless another presentation script runs; play events on already-discovered roots do hold later media. Cross-origin access is deliberately caught/ignored. The ledger runs in the page JavaScript world and hooks element methods; page/provider monkeypatching and captured native methods limit the strength of its guarantees. applyPresentationActivity dispatches evaluateJavaScript without a result callback, so neither the pause result nor eventual resume success reaches an observable operation outcome. Play rejection is swallowed. The autoplay setting is permissive, but that is not proof that every provider/EME/audio graph can be controlled. Freeze is separate from pause and never demonstrates paused playback.

FeriaMediaSession is the existing observation/seek-restoration gate with epoch/serial tickets; it is not the presentation-pause ledger. Do not add a second progress store or confuse saved-position restore with resuming an app-owned pause: [qml/feria/FeriaMediaSession.qml:73–123 (poll / acceptResult)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaMediaSession.qml#L73-L123).

### Category 17: automatic save is a policy, not proof of a bug

The receiver's comment and code deliberately preserve Qt's engine-selected download directory/name, including collision handling, and synchronously accept. An explicit file chooser is absent, but automatic saving to a configured destination can be valid browser policy. This audit therefore records an unresolved product consent/destination requirement, **not** a blanket “every download is unauthorized” finding.

No receiver check of originating user gesture, source frame/origin, destination allowlist or a separate consent token appears. The metadata grant is permission to persist a history record and is minted even when account activity recording is disabled; that does not make it user consent for writing the downloaded file. The inspected account browser preferences are limited to restoreTabs, freezeBackground and searchEngine. Engine-side download admission/default destination behavior must be traced in the Qt donor cell before proposing a deferred chooser that violates Qt request lifetime.

The QML null-view fallback needs a native qualification caveat. At QtWebEngine 6.11.1 revision `eb0793cc4b76e93cf669f586fd68c76019f40ec9`, QQuickWebEngineProfilePrivate sets adapterClient to nullptr when no QML source page is available, while QQuickWebEngineDownloadRequest::view() unconditionally dereferences adapterClient. The getter's documentation promises nullptr, but the inspected body does not implement a null guard. Feria reads request.view before its own `view ? ... : ...` fallback can run. Therefore tests/feria_browser_requests_probe/tst_browser_requests.qml:371–380 proves only the mock-level omit-history intention; it is not proof that a real originless native request reaches that branch safely. This is a source-inferred native-boundary risk, not a demonstrated runtime crash. Capture a valid source once at admission and never re-read the native view after tab retirement; real originless/stale-view handling must be qualified or reported blocked. Exact upstream anchors: [src/webenginequick/api/qquickwebenginedownloadrequest.cpp:23–32](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n23); [src/webenginequick/api/qquickwebengineprofile.cpp:279–290](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n279).

The native grant contains profilePath/providerId/committing only. It does not become a cryptographic file or transfer capability. Its validity may outlive the source Visit intentionally. The legacy recordBrowserDownload fallback with sessionId/visitToken still accepts current non-private visit metadata without a grant; the production receiver uses the one-use grant path. Retain this distinction rather than claiming every invokable write is one-use.

### Category 18: safe Open is the decisive gap

Live-entry Open is gated on DownloadCompleted, but it constructs a file URL from directory/name and delegates directly to Qt.openUrlExternally. Historic Open delegates a persisted descriptor path without checking its state or existence. File URL encoding is present, but encoding is not file authorization. FeriaAccountStore truncates string metadata and validates URLs/byte counts; it does not validate path origin, enforce an approved destination, or verify the completed file exists.

No renderer-to-native arbitrary-path exploit was demonstrated. The finding is the missing safety boundary required by this assignment: action-time lookup of an owner-admitted completed file, current account/private scope, existence and permitted destination; no caller-supplied raw path should be enough. “Open folder” currently opens the containing directory; it is not a native shell “select this file” implementation. A safe Open extension must resolve completed live/private or unowned transfers through the existing transient download owner; only ordinary historical rows resolve through FeriaAccountStore. Do not persist private history or mint an ordinary metadata grant merely to authorize a live private file action.

Cancellation never explicitly sets Completed. Native state is authoritative. The existing code correctly labels cancelled entries, but the meaningful native late-completion/cancel race still needs a real fixture; a boundary double cannot prove engine ordering.

### Final native update ordering: completed metadata can precede final bytes

QtWebEngine 6.11.1 revision `eb0793cc4b76e93cf669f586fd68c76019f40ec9` updates/emits the state at lines 145–147, then receivedBytes at 149–151, totalBytes at 153–155, and isFinished at 158–159/167–173: [src/core/api/qwebenginedownloadrequest.cpp:133–174](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n133). FeriaBrowserDownloads.qml:143 immediately calls update() on stateChanged; lines 111–118 persist a completed record from the current byte properties. If that last native update changes both state and counts, the successful commit sees the previous byte values. The later isFinishedChanged calls update() again, but recorded=true skips the metadata branch. The live bound progress may become correct while the persisted history remains stale.

This is a **source-inferred ordering risk**, not an executed failure. The existing completion double at tests/feria_browser_requests_probe/tst_browser_requests.qml:299–304 sets receivedBytes before Completed, so it masks the native ordering. Add a boundary fixture that emits Completed first, then changed received/total counts, then isFinished, plus a real native download fixture whose terminal update changes state and counts together. Require exactly one successful metadata record with final bytes and exactly one consumed grant, including unknown-to-known/zero-byte totals. Its negative control commits at the first stateChanged and must fail. Settlement must observe the final coherent native snapshot rather than inventing completion or conflating transfer completion with metadata commit.

### Existing canonical local-file/download owners checked

The bounded applicability check followed Feria's direct Qt.openUrlExternally callsites, then the pinned filename inventory and native/main.cpp registration to plausible shared owners. It inspected interfaces and relevant implementations only; it did not survey medium-specific downloader/network/decoder pipelines.

- **LocalDownloads** is the existing unified read model over Tankoban, Biblio and Theatre backbones, explicitly owning neither files nor network. Its invokables route cancel/remove/retry/pause/resume to those backbones and do not expose a general file Open/reveal admission API. No Feria source is registered in this aggregate. Evidence: [native/engine/LocalDownloads.h:3–64](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalDownloads.h#L3-L64); [native/engine/LocalDownloads.cpp:185–211](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalDownloads.cpp#L185-L211).
- **LocalLaunch** is a reusable *media-open* decision boundary: route(path) checks existence, classifies supported comic/book/video families, asks their backend validators and derives Vault identity. It is not a general browser-download external-file opener and does not validate Feria account/private scope or a download grant. routeMap can update Vault identity; openSingle records an accepted item in recent history. Reuse it for explicit in-app supported-media opening only after Feria authorization, while respecting private history policy. Evidence: [native/engine/LocalLaunch.cpp:99–153](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalLaunch.cpp#L99-L153); [native/engine/LocalLaunch.cpp:185–211](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalLaunch.cpp#L185-L211); [native/engine/LocalLaunch.cpp:375–405](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalLaunch.cpp#L375-L405).
- **Theatre DownloadStore** owns a separate media transfer engine. revealDownload() uses its own last-completed/current job path and opens the folder; it has no general admitted-file parameter. downloadedVideos() exposes a missing flag computed from QFile::exists. This existing availability pattern is useful, but replacing browser-profile downloads with this independent network engine would break the assigned ownership boundary. Evidence: [native/player/downloadstore.cpp:409–420](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/player/downloadstore.cpp#L409-L420); [native/player/downloadstore.cpp:643–660](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/player/downloadstore.cpp#L643-L660).
- **DownloadIntentStore** is portable desired-content intent, not a transfer/file capability: its allowlist includes identity/title/series fields and excludes paths/URLs. The feeding projection enumerates Tankoban/Biblio/Theatre. It should not be repurposed as browser transfer-bound metadata authorization. Evidence: [native/account/DownloadIntentStore.cpp:15–31](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/account/DownloadIntentStore.cpp#L15-L31); [native/engine/LocalDownloads.cpp:185–211](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/LocalDownloads.cpp#L185-L211).
- **DownloadFileOps** only supplies removal primitives, and **VaultDownloadsRoot** projects existing media-container backbones into Vault rows. Neither is a safe arbitrary-file Open authority for Feria. Evidence: [native/engine/DownloadFileOps.h:18–39](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/DownloadFileOps.h#L18-L39); [native/engine/VaultDownloadsRoot.h:2–43](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/engine/VaultDownloadsRoot.h#L2-L43).

**Decision:** extend existing FeriaAccountStore transfer-bound admission/metadata and FeriaBrowserPolicy native file validation/Open/reveal, keeping the native browser request owner. A verified completed-file reference must resolve through those existing Feria owners. Do not introduce another registry/history store, silently import private downloads into Vault/recent history, or move transfers to a parallel network downloader. LocalLaunch remains the canonical in-app media-opening route when explicitly chosen and applicable.

### Category 19: file lifetime differs from history lifetime

EndSessionVisit removes visits, not already-minted download grants. An ordinary transfer therefore continues after tab closure; a delayed local HTTP native fixture verifies this intended scenario in test source. History completion grants are transient and not serialized, success consumes them once, and the QSaveFile transaction reserves the grant against synchronous changed-signal replay. Ordinary history is metadata, not a restart-resume mechanism.

Private transfer files are not deleted by private history exclusion. No secure deletion guarantee is established. The current last-private-tab path explicitly cancels active private transfers after views retire and destroys the profile. The contract instead says the profile depends on its last space/download/native request. Preserve ordinary behavior and add a private transfer dependency; do not infer that closing a source tab should cancel an otherwise valid transfer.

A recordBrowserDownload false result currently conflates transient disk failure, revoked/stale grant, invalid provider/bytes and rejected metadata URL. The automatic browserUrl validator accepts only http(s) and rejects credential-bearing/auth URLs. A native blob/data download can complete while its metadata is permanently inadmissible. The UI must distinguish “file saved, history intentionally unavailable/rejected” from retryable disk failure. No claim is made here that such URLs should automatically be persisted.

A boundary fixture at [tests/feria_browser_requests_probe/tst_browser_requests.qml:339–346 (test_download_old_account_authorization_cannot_record_in_new_account)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_browser_requests_probe/tst_browser_requests.qml#L339-L346) clears same-object store grants and expects the QML token to be emptied. Current update() at FeriaBrowserDownloads.qml:113–122 retains it on any failed write. This is a source-inferred inconsistency, **not an executed failure**, and actual production profile retirement takes a stronger path. Preserve tests as evidence of intent and fix/verify this mismatch during the separately authorized prototype.

### Category 20: explicit Save and engine-default behavior are different

The pinned FeriaQtWebEngine, FeriaPageTools, FeriaBrowserRequests, PorticoDiscoveryHostView and FeriaBrowserMenu entry paths contain no custom contextMenuRequested handler or explicit SavePage/DownloadLinkToDisk/DownloadImageToDisk/DownloadMediaToDisk actions. This is a bounded negative finding about the application integration; Qt's default native context menu may still expose supported actions. The Qt source evidence in the three-family comparison resolves that engine-owned path; absent application wiring is not evidence that all Save is missing.

Ctrl+P/menu Print/PDF reaches FeriaBrowserRequests.requestPrint → FileDialog → browser.printToPdf → pdfPrintingFinished. It captures the original browser/generation, invalidates on navigation/visibility changes, and checks its returned path. It is separate from profile downloadRequested and does not create download history. Print-to-PDF does not prove HTML/MHTML save, original frame resource saving, or media extraction.

The WebView2 wrapper has no download/Save signals/commands connected to the existing receiver in the inspected source. FeriaProviderHost::pauseMedia merely pauses top-document video/audio, and FeriaHostItem::resumeWebView restores focus, not playback: [native/feria/provider-host/FeriaProviderHost.cpp:858–867 (pauseMedia)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/provider-host/FeriaProviderHost.cpp#L858-L867); [native/feria/provider-host/FeriaHostItem.cpp:265–296 (returnToQml / resumeWebView)](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/provider-host/FeriaHostItem.cpp#L265-L296). These are adapter parity boundaries requiring separate Windows evidence, not a requirement to embed CEF or claim full WebView2 parity here.

## Existing interfaces versus proposed extensions

### Existing, source-verified

- SessionStore.browserIdentity(spaceId), setBrowserProfile(profile), advanceBrowserDocument(spaceId), setBrowserHost(identity,hostId) supply canonical identity.
- FeriaSessionDeck: sharedProfile/privateProfile; one receiver each; showDownloads(); startQuiescence(forCleanup); releasePrivateProfile().
- FeriaQtWebEngine: activitySuspended, presentationSerial, navigationGeneration, lifecycleBlocked, pendingJavaScript; applyPresentationActivity(); wakePage().
- FeriaPlayback.presentationMedia(suspended, documentEpoch, serial) implements document-owned pause/resume.
- FeriaBrowserDownloads: receive(request), retire(), items/history, openPanel(); entry.update(), togglePause(), cancel(), releaseAuthorization().
- FeriaAccountStore: authorizeBrowserDownload(sessionId,visitToken) → QString; cancelBrowserDownload(downloadToken) → bool; recordBrowserDownload(QVariantMap) → bool; browserDownloadHistory; clearBrowserDownloads().
- FeriaBrowserRequests: requestPrint()/printPage(), savePrint(fileUrl), printTicket/printingPath; native printToPdf completion.

### Proposed, not implemented by this audit

Extend these owners rather than creating a new persistence service:

1. Presentation command: setActivity(capturedCanonicalIdentity, operationId, suspendedReasons), where reasons is a bounded set and the engine adapter retains exact native/document/media provenance. Report inspection-limited/blocked/resume-rejected explicitly. Preserve inactive-tab playback semantics.
2. Download admission snapshot: requestToken, captured canonical identity, engine/profile scope, sourcePageUrl/origin/frame token **when natively available**, native request ID, private flag, initiating action, target URL, suggested filename, destination-policy result. Unknown/unavailable frame or user-gesture evidence must be represented as unknown, not synthesized from selected tab.
3. Keep FeriaBrowserDownloads as the engine shim/native-object owner and FeriaAccountStore as the metadata authority. Strengthen authorizeBrowserDownload with a transfer-bound admitted descriptor (or internal handle) and provide typed record outcomes: saved, retryableWriteFailure, invalidated, rejected. Preserve one-use and write retry behavior.
4. Project native transfer state and allowed actions: progress-known flag, resumable/reason where supported, cancellation requested versus actual terminal, and retained transfer dependencies during source-tab/private/profile retirement.
5. Extend FeriaBrowserPolicy with owner-mediated openDownloadedFile(downloadId) and revealDownloadedFile(downloadId), resolving transfer-bound admission through FeriaAccountStore; resolve identity/path internally and revalidate account/private scope, completion and file availability. Do not accept a raw renderer/caller path as authorization. This does not introduce another history store.
6. Save context: captureSaveContext(identity, nativeContextToken) → supported kinds/formats plus source request/frame capability; saveContext(contextId, kind, destinationDecision) → native download-owned operation. Never scrape/interpolate URLs into JavaScript or build an independent network downloader. Native API/version exclusions remain adapter capabilities.
7. Present these controls in existing app-style Downloads/history and reserved chrome request area. Current modal/download Overlay widgets are a presentation gap against the new contract, not authorization to build a generic Browser Tools drawer.

These names describe semantic proposals only. The donor audit must map each to supported Qt 6.11.1 public API or label unresolved/unsupported; Chromium/CEF internals are not Qt-callable APIs.

## Decisive fixtures for the prototype phase

No fixture below was executed in this audit.

| Fixture | Required observation | Negative control |
| --- | --- | --- |
| Playing + manually-paused media across minimize/world-away | Only owned playback resumes; ordinary inactive tab stays playing | Remove owned flag check and require failure |
| Same-origin iframe removal/replacement while paused, frame navigation, detached/replaced element, same source in different document | No stale frame/element/document resumes; qualify actual frame retirement even though element and ownerDocument are captured | Replace element identity with URL-only lookup or ignore frame retirement |
| Cross-origin/closed shadow/late frame media; autoplay rejected | Honest limited/blocked result, no universal pause/resume claim | Treat uninspected media or rejected play as success |
| Pending close, permission/consent and licence callback | No unsafe freeze and correct request lifetime | Ignore blockers/freeze before native callback retirement |
| Real originless native request and source retirement | Safe source capture or explicit unsupported/blocked result; no later native view dereference | Treat a mock view:null fallback as native safety proof |
| Simultaneous downloads from spaces A/B and an auxiliary window | One profile listener, per-source metadata, selection irrelevant | Retarget completion to active space |
| Unknown length, zero-byte completion, resumable/nonresumable interruption | Accurate progress/state/action availability | Always enable Resume or interpret total zero as done |
| Final update changes state and byte totals together in Qt native signal order | Exactly one history row with final byte counts; one consumed grant | Commit at first stateChanged before byte changes |
| Cancel/completion race on real native request | Exactly one truthful terminal, no cancelled-as-completed record | Locally synthesize Completed after cancel |
| Source-tab close and final-private-tab close while downloading | Transfer remains valid under intended lifetime; private history absent | Retire profile solely because views reach zero |
| Account A → B late completion | A grant cannot write B or reopen A request | Retain grant across setStoragePath |
| Atomic metadata write failure, retry, reentrant replay | Old state survives; same grant retries once; reentrant replay fails | Consume grant before write or remove committing fence |
| Persisted missing/replaced/unauthorized path; cancelled row | No unsafe Open; user sees truthful unavailable state | Open descriptor string without owner validation |
| Context Save page/link/image/media, redirect/frame navigation and destination cancel | Correct native source/context, normal pipeline, no stale retargeting | Save from selected tab or stale context token |

Existing relevant tests are **inspected, not passed**: lifecycle and retained-session runtime probes, boundary-double browser requests/download metadata probes, native account-store tests, Print/PDF request test, page-tools probe (Find/zoom; not Save coverage), and the hosting same-live-document Max test. tests/CMakeLists.txt:27–46 explicitly describes the retained WebEngine fixture as a graphical, explicitly-run target, not an ordinary headless CTest pass.

## Provenance and checks actually performed

- Pinned GitHub connector reads of 49 successful files; exact source cache and SHA-256/Git blob hashes in [MANIFEST.json](../source/colosseum/MANIFEST.json).
- All 49 cached UTF-8 files were rehashed with Git blob framing and matched connector-reported blob SHA. Selected ancestor AGENTS lookups returned 404; the untruncated pinned tree confirms only root AGENTS.md.
- Three additional pinned Qt source cross-checks (origin getter, profile origin binding, terminal-update ordering) were read from the comparison source cache; their SHA-256 hashes match [the Qt manifest](../donors/qtwebengine/source-manifest.json). They are separately identified in findings.json and are not Colosseum baseline files.
- Source reading, bounded textual searches, JSON parsing, cached-blob hash/line-reference validation only. No product test, build, runtime, browser action, implementation change, account action or release.
- [findings.json](findings.json) contains machine-readable status, source symbols, exact line URLs, gaps and proposed negative controls.

