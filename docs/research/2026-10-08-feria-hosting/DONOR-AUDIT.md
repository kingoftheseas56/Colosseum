# Feria categories 6–10: donor selection and source boundaries

Scope: only categories 6–10. Baseline `cd4b7f6ce8b895239a1e1e02ecb4f0d377a4d413`. Five supplied research inputs were used as starting evidence: four donor reports and one Codex receiving review. Their old audit-only/52-category stopping rules are superseded by the current packet. This report describes source selection; runtime results belong in the delivery result matrix.

## Verified revisions

- QtWebEngine 6.11.1: `eb0793cc4b76e93cf669f586fd68c76019f40ec9`; tag peeled independently. CHROMIUM_VERSION reports 140.0.7339.264 with security patches through 148.0.7778.96; security backports do not imply feature parity with 148.
- Matching Chromium 140: `56ca847e9ea70a5c56fa3d634361da1002fb284b`, chrome/VERSION independently inspected.
- CEF: `2d4a38cec5a6d4604cc77b92d1a06902b1ead61d`; CHROMIUM_BUILD_COMPATIBILITY declares 156.0.8078.0.
- Brave: `9eb6dab3855ed6ba0950c37f7e3df0bd0fb5ef5a`; package.json declares 155.0.8059.40. No distinctive 6–10 override established that improves matchingQt/Chromium behavior; do not duplicate inherited Chromium work merely to fill a column.

## Selection matrix

| Category | Strongest useful machinery | Rejected alternatives and boundaries |
|---|---|---|
| 6 Canonical desktop windows | Public Qt Window/QQuick item presentation with permanent deck QObject/creation-context ownership. Chromium Browser::ActivateContents resolves supplied WebContents before raising its owner; CEF re-queries current native host after reparent. | No Chrome Browser/TabStripModel or CEF platform-delegate transplantation. Selected page is never a substitute for captured origin. Native window, QObject owner, QML context, visual parent and profile have separate lifetimes. |
| 7 Site dialogs and geometry | Synchronous same-profile Qt newWindowRequested/openIn preserves original WebContents. CEF popup-ID/opener-frame staged creation informs rollback. QScreen availableGeometry and actual host frame margins supply native clamp. | URL replay loses POST/opener semantics. openIn returns void, so no fabricated accepted boolean. Chromium clamp alone does not establish Qt outer-frame/DPI correctness; matching source explicitly carries an inner-vs-outer geometry TODO. |
| 8 Same-page Max/Restore | Same QQuickWebEngineView, stable creation context/QObject owner, explicit disposable visual hosts and saved embedded bounds/focus. Qt scenegraph reparent code reconnects windows; donor graphics test checks actual pixels. | Reload/new adapter is not same live page. Chrome's same-WebContents transfer test does not prove Qt creation-view safety. Adopted-child opener retirement has a proven Qt pointer-lifetime problem, described below. |
| 9 Exact veto-capable close | CEF Pending→None on refusal, native close veto while pending; Chromium cancellation clears queues and weak callbacks; public Qt RequestClose and exact original view windowCloseRequested. Canonical native attempt identity plus pinned Qt rejection acknowledgment hook. | Dialog acceptance is not terminal close. No force-close promotion or timeout-as-success. A JavaScript round trip alone is not source-proven renderer rejection acknowledgment. Dynamic windowCloseRejected is private/undocumented Qt behavior, explicitly version-coupled. |
| 10 App-scoped reopen | Existing SessionStore gets one closed-descriptor writer and two-phase app/profile-filtered reopen. Chromium provides bounded descriptor/pruning and post-restoration removal reasoning; private profiles excluded at boundary. | Chromium RestoreMostRecentEntry is global and assumes context creation success. App scoping and retain-on-failed-runtime creation are new Colosseum glue, not donor-proven behavior. Never resurrect destroyed JS/POST state or import Chrome's session store. |

## Decisive traces and actual assertion bodies

### Qt integration

Pinned source root: https://github.com/qt/qtwebengine/tree/eb0793cc4b76e93cf669f586fd68c76019f40ec9

- `src/core/web_contents_delegate_qt.cpp`: WebContentsCreated580–587 supplies opener view client; adoption224–241/776–788 transfers original adapter, null blocks; SetContentsBounds714–725 subtracts frame margins; CloseContents246–252 may destroy its own delegate; BeforeUnloadFired855–862 routes refusal to originating client.
- `src/core/web_contents_view_qt.cpp`: setFactoryClient37–71 creates delegate; setClient49–59 changes factory/adapter client but does not replace an existing widget delegate.
- `src/webenginequick/api/qquickwebengineview.cpp`: synchronous same-profile adoption951–970/1937–1956; delegate construction379–384; Bind269–292 does not update original m_parentView; bindViewAndDelegateItem1061–1118 fixes visual/focus association only; close765–769 emits public acknowledgment; windowCloseRejected771–777 invokes application method dynamically.
- `src/core/render_widget_host_view_qt_delegate_item.cpp`: mousepress246–257 consults ActiveFocusOnPress, which dereferences original creation view; itemChange338–383 and releaseResources408–445 handle actual QQuickWindow/scenegraph transitions.
- `src/core/web_contents_adapter.cpp`: public lifecycle dispatch2066–2118 refuses Discarded if visible/inspected; discard2217–2267 abortsload, creates hidden kNoRendererProcess WebContents, disconnects WebChannel and destroys original contents/delegate.
- `src/webenginequick/api/qquickwebenginedialogrequests.cpp`: weak controller answer381–405; use dialogAccept()/dialogReject(), not nonexistent generic accept/reject methods.
- Actual donor assertion bodies inspected: newViewRequest null/unhandled/dialog/background cases; JavaScriptDialogs accepted/rejected close after real mouseactivation; graphics reparent green-pixel test118–130; lifecycle discarded JSdisabled/automaticundiscard/DevTools cases; widget close→Discarded. None proves all required Colosseum POST/nested-input/host-retirement cases. Donor tests were not executed by this audit.

QtDeclarative creation-context source `a02bed441965ee1f18f856352c7d5ee5ba35d795`, `src/qml/qml/qqmlcomponent.cpp205–227`, establishes that Component declaration context must outlive created objects even if createObject is supplied another parent.

### Matching Chromium and CEF

Pinned Chromium root: https://github.com/chromium/chromium/tree/56ca847e9ea70a5c56fa3d634361da1002fb284b

- `chrome/browser/ui/browser.cc`: exact source activation2194–2203, CloseContents2211–2215, BeforeUnloadFired2287–2297.
- `chrome/browser/ui/unload_controller.cc`: BeforeUnloadFired128–169, CancelWindowClose278–295, renderer loss301–308, detached content349–363, weak callback invalidation and pending dispatch365 onward.
- `chrome/browser/unload_browsertest.cc`: cancellation345–360 waits for renderer title `cancelled` before retry; doubleclose cancellation471–504 asserts oneabort/zerosuccess, waits for acknowledgment and then retries; doubleaccept444–466 asserts one terminalsuccess. These are stronger than CEF's delayed forced cleanup. Two similarly named browser tests are DISABLED and are not counted as active evidence.
- `content/browser/web_contents/web_contents_impl.cc`: staged creation5233–5249/5433–5487 preserves storage partition and POST, uses weakpointer across embeddercall; original ShowCreatedWindow5580–5659 moves ownership once; AdjustWindowRectForDisplay553–579 clamps workarea with permissionboundary; source warns about inner/outer inaccuracies.
- `chrome/browser/ui/tabs/tab_strip_model_unittest.cc793–830`: preserves exact model/WebContents during transfer, but detached host association and opener metadata are not automatically preserved.
- `components/sessions/core/tab_restore_service_helper.cc`: global most-recent438–445; restore594–638/770–783 removes by stableID after callbacks; RestoreTab1000–1069 assumes contextcreation; prune839 onward. Tests WindowRestore454–506 and PruneEntries1165–1250 assert bounds/appname/order. Test named DontPersistPostData does not assert actual POSTbyte absence and is not overclaimed.

Pinned CEF root: https://github.com/chromiumembedded/cef/tree/2d4a38cec5a6d4604cc77b92d1a06902b1ead61d

- `libcef/browser/alloy/alloy_browser_host_impl.cc`: CloseBrowser/TryCloseBrowser266–316 gate duplicates and veto pending native close; BeforeUnloadFired950–961 resets pending on refusal; CloseContents879–925 and teardown514–546 separate approval/nativehost/destruction.
- `libcef/browser/browser_info_manager.cc`: PendingPopup destructor53–58 abortsonce; CanCreateWindow112–259 carries openerframe+popupID and late identityresolution; CreateWindowResult261–283 distinguishes abort from valid no-opener result.
- `tests/ceftests/life_span_unittest.cc`: refusalassertions include noDoClose/noOnBeforeClose/noUnload, but delayed cleanup is not normal rejectthenretry proof.

## Qt creation-view blocker and proposed exception

An adopted child's widget delegate retains the opener QQuickView pointer after public openIn. Ordinary visual reparenting does not fix this. Deleting that opener while child input continues can dereference a null QPointer. No supported public delegate-rebind route was found. Private pointer mutation and forced child reload are rejected.

A public Discarded origin shell can destroy the original opener document/WebContents while retaining only the QML view/context needed by existing child delegates. It must never reactivate through visibility, URL, DevTools or commands. A discarded middle child releases its own ancestor dependency immediately, avoiding unnecessary ancestor chains. Hard admission count/depth bounds plus finalleaf teardown can bound retained objects, but there is no finite duration bound if a child remains open indefinitely. User ruling on this exception was requested before implementation. Until that ruling and real runtime proof, opener-retirement acceptance is unresolved.

Required runtime proof for any approved shell route: lifecycle=Discarded, no original DOM media/loading/heartbeat, child real mouse/key input, nested release, finalleaf destroys all shells, cap rejects without orphan entry, no stale callback reactivation. Renderer PID may remain shared by another page, so PID exit cannot be promised as document-release proof.

## Compatibility and verification contract

Only behavioral patterns, public Qt API wiring and newly authored Colosseum glue are selected; no GPL donor test code is copied into production. Exact Qt 6.11.1 dynamic rejection callback is an explicitly private compatibility dependency, covered by real repeat-rejection tests. Missing callback must fail diagnostically, never authorize close. Replace it with a public terminal rejection API when available and verified.

Source coverage is distinct from native/model tests, actual QtWebEngine graphical component tests, full-app runtime and Windows qualification. Native additions require the matching newly compiled runtime. Multimonitor/DPI/GPU loss and full-app cases unavailable on the Linux runner stay NOT RUN with receiving instructions. The known baseline retained-download failure is not hidden or relabeledgreen.
