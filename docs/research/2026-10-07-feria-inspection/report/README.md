# Feria browser source and integration inspection

2026-10-07. **Inspection and recommendations only. No production implementation or engine patch.**

## Verdict

Build V1 on the supported Qt WebEngine application interfaces, with a stronger session/window controller. Do not import a whole browser backend. The best replacement is often letting Qt/Chromium own the operation already implemented there, while Colosseum owns identity, policy and presentation.

The existing account store and profile-level download ownership compare well against the supplied donors. Keep them for concrete reasons: checked atomic writes, bounded records, private/credential filtering, account-generation boundaries and one profile download listener. That is not a presumption in favour of existing code. Replace the incorrect session entry/close/restore behavior, remove the DIY navigation-history mechanism once a decisive target-kit reproduction verifies the public replacement, and replace single-active-view presentation dependencies before adding desktop windows.

Two qualifications materially change the earlier comparison:

1. Qt 6.11.1 itself tests reparenting a real WebEngineView into another QQuickWindow. Start with that supported path and stable QML context ownership. Morph provides useful lifetime lessons, but its fake-item test is weaker evidence for WebEngine relocation than Qt's own graphics test.
2. The cookie workaround is not based on an invented limitation: the target Qt source has an empty GetAllCookies callback and a corresponding test FIXME. Do not replace selective sign-out with an unproven loadAllCookies call or broaden it to clearing every provider's cookies.

**V1 is credible without a second embedding engine or a wholesale Chromium fork.** Same-live-page transfer, close cancellation, provider pause/resume and account cleanup across windows still need real component and assembled-app qualification. Source inspection does not establish those runtime outcomes.

## Source authority and provenance

- Fixed application source: [97d5f18f7abad992f0686acba6219d97e4fe514d](https://github.com/kingoftheseas56/Colosseum/tree/97d5f18f7abad992f0686acba6219d97e4fe514d). No silent rebase or newer-main substitution.
- Foundation commit inspected separately: [b53a22d3e47bb286219973104905333e9ff6f544](https://github.com/kingoftheseas56/Colosseum/commit/b53a22d3e47bb286219973104905333e9ff6f544). Its native changes add session enumeration and account/policy methods; they do not transplant Chromium, Morph or Angelfish native browser engines. Most new browser glue is QML around Qt APIs.
- Read AGENTS.md and MEMORY.md, the pinned donor comparison and draft UI contract. The present brief overrides that draft's prospective implementation dispatch and incumbent-first selection language.
- Attached inspection brief SHA256: e45d23305a09407d5a330b03ccf2bcf123d6062773acf7de19a71f91060a6433.
- Attached illustrative HTML SHA256: 6ed0398879183feb0019cb9829144fff755eadde4fa6bd89c266cce4524c41ea. Inspected its nested source and labels. No browser action in it is production evidence, and it is not approved specification. No visual pixel-parity claim is made from this inspection.
- Application desktop CI explicitly installs Qt **6.11.1** at desktop-ci.yml:75–78 and 319–322. native/CMakeLists.txt:91 requires Quick, Network, Qml, Gui, Sql, WebEngineQuick, WebChannel, WebSockets and Concurrent.
- Exact Qt target: qt/qtwebengine **eb0793cc4b76e93cf669f586fd68c76019f40ec9** (v6.11.1), Chromium fork gitlink **58c11ad487f8a237cf0ac71cc3e818b52db150df**. Root CHROMIUM_VERSION advertises baseline **140.0.7339.264**, security patches through **148.0.7778.96**; the vendored chrome/VERSION reads **140.0.7339.225**. Preserve these different metadata meanings. Neither “Chrome 148” nor a generic Chromium checkout identifies the Qt ABI/integration.

## Read this package

- [CAPABILITY-MAP.md](CAPABILITY-MAP.md): all 52 original categories, scope, current owner, Qt surface, recommended source and frontend/evidence obligations.
- [FRONTEND-INTERFACE.md](FRONTEND-INTERFACE.md): minimal behavior contract for the QML reshaping.
- [EVIDENCE.md](EVIDENCE.md): primary source index, exact commands/results and limits.
- Supporting donor/Qt evidence, when present, is a source-inspection appendix, not an executed donor test receipt.

## Priority findings against the product contract

All references below are to the fixed application pin. These are inspected source findings unless explicitly labelled otherwise.

### 1. Close is not one operation yet

FeriaSessionDeck.qml:97–100 requests WebEngineView.RequestClose; its originating-view windowCloseRequested handler at408 removes that exact session. But Main.qml:2886–2897 tears down and directly calls Sessions.close. Taskbar's close signal reaches that path at4863. The dialog Window in FeriaQtWebEngine.qml:432–435 destroys itself on native close without RequestClose.

**Replace** direct browser record removal on ordinary user close with one exact-session close transaction. RequestClose first; retain record/view until windowCloseRequested authorizes final removal. Native Window.onClosing rejects the initial OS close, then closes on acknowledgement. Cancellation clears the pending transaction without deleting the session. Qt 6.11.1 does not declare a public QML close-rejected signal: its internal dynamic windowCloseRejected callback is not a stable interface. Use an explicitly owned, request-bound before-unload presenter to observe rejection, or qualify another supported cancellation path; do not promise a public signal that does not exist and do not destroy after a timeout. Prevent duplicate requests and reentrant acknowledgements. SessionStore.close's existing no-neighbor-promotion behavior is correct and remains the final operation.

Background close needs an explicit origin-bound dialog policy: FeriaBrowserRequests.qml:248–252 currently rejects native JS/auth dialogs while its view is invisible. Merely routing taskbar close through RequestClose could therefore leave a hidden before-unload page unable to obtain user consent. Activate/present the correct host or use an origin-bound close prompt; never select the current unrelated view. Account-switch/quit forced cleanup is a separate policy from ordinary close and must be identified as such.

### 2. Desktop windows require replacing active-only presentation assumptions

FeriaSessionDeck owns retained entries under the main shell (332–381), not the transient FeriaWorld Loader. This is a useful foundation, but entry visibility is tied to one Sessions.activeId and deck.displayed. PorticoDiscoveryHostView.qml:13–15 resolves deck.activeBrowser, and downloads/toasts also route through the active record. That is insufficient for two simultaneously visible windows.

**Keep canonical SessionStore IDs; replace host lookup with an explicit session-bound controller.** A desktop host takes sessionId and binds that entry. Placement and window identity become observable session state. Selecting an external session raises its exact window rather than reopening an embedded host. A runtime map of IDs to native window handles is an implementation detail, not a competing session registry.

Create the view and its QML context under a lifetime owner that survives either visual host. Reparent its visual item, rebind geometry/focus/overlay parentage and retain QObject/context ownership. Do not serialize URL and recreate the view as “Pop out”; that loses live form/script/navigation state. Qt's real reparent test is the starting mechanism; Morph's context handling is a reference only. Test original-host destruction after move, reverse move, simultaneous windows, dialogs, screen changes and account shutdown before claiming completion.

### 3. Site-request routing preserves the object but loses destination distinctions

Deck.openWindow (102–123) synchronously uses request.openIn on a newly allocated entry, which is the right mechanism for opener/form state. However it collapses every non-dialog destination except background into an embedded foreground session. InNewWindow is not implemented as a taskbar-owned desktop window. The separate dialog path creates raw popup views outside the full session desktop-window machinery. The userInitiated gate also rejects all non-user-initiated requests; this is a popup policy, not evidence every legitimate OAuth/provider flow will work.

**Adapt Qt Nano's destination dispatcher** for foreground/background/window/dialog while keeping the original request within its synchronous handler. Allocate destination view/profile/host in time; reject cleanly if that cannot be done. Do not put this request in a deferred QML consent queue or reopen only requestedUrl. Explicit New uses provider start URL, whereas site requests retain their engine-supplied semantics. These are distinct entry points.

### 4. New, restore and taskbar activation conflict with the brief

- Deck.newTab:88–95 defaults to about:blank; Host Ctrl+T/Ctrl+Shift+N:42–55 and BrowserMenu.newTab:59 explicitly pass about:blank. **Replace** with provider catalogue start-page resolution and app-named commands.
- Deck.considerRestore:151–155 automatically calls restoreTabs when a saved preference is true. Default false is not enough for the locked fresh-start contract. **Remove automatic invocation**, retain previous-session descriptors for explicit restore. Migrate/ignore the old restoreTabs=true preference rather than deleting history or stored descriptors.
- Taskbar.qml:530–537 dispatches switchRequested for one session; Main.qml:4862 always Sessions.switchTo(id). SessionStore.setActive returns immediately for the already-active ID. **Replace browser app activation dispatch** with exact-session toggle/raise behavior. Preserve multiple-session chooser and no-neighbor promotion.

### 5. Pause/resume needs new ownership; freeze is not it

The existing real-WebEngine test test_backgroundMediaAdvancesWithoutWorldVisibility explicitly expects audio time to advance while deck.displayed=false (tst_RetainedSessions.qml:78–89). That is useful evidence of the old policy and directly contradicts this new Feria contract. Do not keep that assertion and pretend the locked pause contract passed, or delete it without recording the intentional product change.

FeriaQtWebEngine's freeze timer (28–45) waits for recommendedState, no audible playback and no pending JS. This is a resource policy and should remain separate. Its top-document pause in prepareForClose is teardown-only. FeriaMediaSession is observation/resume-from-saved-position, not a per-session auto-pause ledger.

Add controlled media suspension keyed by session, page generation, frame/media identity and cause. Record only elements actually paused by Colosseum. Resume only those still eligible, and do not undo a later manual pause/navigation/replacement. Evaluate supported frame script injection and provider connectors; ordinary top-frame querySelectorAll cannot cover inaccessible cross-origin frames, WebAudio or every protected player. Chromium media-session internals are not a public QML pause-all-and-restore-intent API. Return a truthful partial/unsupported capability where the target engine/provider cannot meet it, and qualify real providers separately.

### 6. Replace generic renderer failure with per-view bounded recovery

FeriaQtWebEngine.qml:389 reports completed(url,false) for any renderer termination. There is no distinct per-view retry budget/state. Nano's example reloads currentWebView and can hit a different page. Morph's monitor has bounded timing and useful status distinctions, but its production Browser.qml binds one monitor to currentWebview, not one durable monitor per session.

Adapt the bounded recovery policy into each retained entry, with status/cause, attempt count, last failure and user Retry. Fence delayed recovery to session/account/page generation; cancel on newer navigation, close or profile retirement. Do not reload POST/form flows blindly or treat every termination as the same failure. Expose recovery state to QML; other sessions must remain untouched.

### 7. Navigation workaround should be retired if target-kit tests confirm public APIs

FeriaQtWebEngine.qml:204–280 reconstructs history from history.length, origin-local Navigation API entries and crossOriginAhead, then executes history.back/forward scripts. The comment claims Qt only updates after view-initiated navigation. Target Qt source instead reads Chromium's navigation controller and updates actions at load/commit/finish. That disproves the universal explanation, not every possible signal-ordering bug in the application.

Recommended replacement: public Qt canGoBack/canGoForward, goBack/goForward and navigationHistory models behind the session interface. First replay same-origin, cross-origin, in-page link, pushState/replaceState/hash, JS traversal, redirect, failed load and error-page cases against this exact Qt kit and actual wrapper. If a reproducible Qt signal bug remains, make the smallest version-qualified workaround, preserving native history as authority. Do not maintain a second guessed history stack.

### 8. Cookie cleanup is the exception where target source supports the workaround premise

FeriaBrowserPolicy.cpp:129–192 edits the closed, app-owned cookie database with profile-generation guards, zero-live-view/profile checks, transactions and bounded retries. It still depends on Chromium's private schema and only addresses cookies; separate origin-storage cleanup uses loadHtml/script.

Qt's public cookie API is preferable in principle, but Qt6.11.1's getAllCookies path has an empty callback and its upstream test retains a FIXME. **Do not assert public enumeration is already a usable drop-in.** Compare a public live-cookie observer plus persisted-cookie enumeration probe, an upstream Qt fix/backport, and the existing closed-profile fallback. An upstream callback fix is a Qt integration patch requiring a matching rebuilt engine and regression evidence, not a harmless app-only helper. Until selective completeness is proven, retain the constrained fallback with schema/version checks and explicit failure; never silently replace it with deleteAllCookies in the shared account profile.

### 9. Request/profile owners are useful but their visual coupling must change

The queued request adapter has IDs, generation fencing, explicit screen/certificate choice and masked passkey PINs. These are stronger than the raw supplied examples. Preserve those semantics while splitting presentation from request ownership. active=browser.visible means a transient hide during transfer cancels requests; default dialog/Overlay ownership must move to the current host without changing request origin. Desktop-capture request destruction is particularly sensitive in Qt: always explicitly cancel when dropping an unanswered request.

One download observer per shared profile is correct. Keep Qt's transfer object and native per-account authorization. Regular download lifetime may outlive its originating tab; profile/account retirement deliberately cancels. Delivery messages must identify the original session/host, not sessions.activeId. Downloads history is metadata, not a portable restart-resumable transfer engine. Private download files are still files on disk: private history exclusion does not prove file deletion.

## QtWebEngine and WebView2 are separate integration paths

The retained session deck creates FeriaQtWebEngine directly. PorticoDiscoveryHostView.qml:241–266 preserves WebView2 as a legacy single-host compatibility route; switching engines parks the retained route and opens another host. This is not same-live-page migration between engines, and the Qt multiwindow recommendation does not establish WebView2 retention or removal readiness.

Native FeriaHostItem.cpp:212–254 sizes/shows an HWND-backed host from QQuickWindow geometry/visibility. returnToQml at265–278 calls pauseMedia; resumeWebView at293–295 restores focus only. FeriaProviderHost.cpp:858–866 pauses top-document video/audio with JavaScript. That is useful existing behavior but does not implement the new per-session auto-pause/resume provenance contract or universal cross-frame control.

WebView2 popup code at FeriaProviderHost.cpp:299–430 uses a COM NewWindowRequested deferral, retains args and calls put_NewWindow then Complete. That is a different supported lifetime model from Qt's synchronous stack request. Do not apply Qt's request-lifetime rule to WebView2 or replace its deferral with a queued raw Qt request. Its WindowCloseRequested path hides the owned popup; this is not the requested shared taskbar exact-session close transaction.

The frontend should use one behavior interface with engine-specific capabilities, while each adapter owns its actual transport/window lifetime. Future engine convergence requires platform evidence and implementation authority; this inspection authorizes neither removing WebView2 nor adding CEF as a second embedding stack. Windows COM/WebView2 runtime checks remain NOT RUN.

## Decisions and recommended next implementation scope

1. Establish the session-bound frontend contract and controller, then prove real same-view Pop out/Return with a tiny local fixture before reshaping production QML.
2. Implement the exact-session action family together: new/start-page, activate/minimize, original new-window routing, close handshake, explicit restore and per-view recovery. These operations share lifetime/identity and should not acquire separate registries.
3. Add auto-pause provenance and account/request/download cleanup tests across placement changes. Keep existing canonical account data and accepted browser services; replace specific inferior mechanisms identified above.
4. Replace navigation bookkeeping after the public-API differential probe. Treat selective cookie enumeration as its own decision gate, with the Qt defect explicitly recorded.

These are recommendations for the later assignment, not work performed or a request to begin implementation. No additional product decision is needed to start the controlled window/close prototype. Two decisions may arise from evidence: the promised behavior when a provider cannot expose controllable media, and the treatment of untrusted unsolicited popups. Document exceptions rather than quietly altering the brief. Shared SSO sign-out impact should be surfaced, since current providers share an account profile.

## Qualification required before adoption

Receiving Windows Qt6.11.1 checks are mandatory for a Windows claim. Tests should cover both windows visible, former-host destruction, DPI/screen moves, focused input/IME, native dialogs and accessibility; beforeunload cancel/accept from every origin; POST/opener/window/dialog routing; manual pause versus auto-pause; navigation and account change during pending requests/recovery; download after tab close and private/profile retirement. Full-app taskbar and world-return behavior requires the assembled application, not only isolated QML.

Codecs, Widevine/DRM, hardware authentication, actual provider compatibility and audible playback are separate evidence. No such pass is claimed here.
